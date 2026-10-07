import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { AudioProxy } from '../runtime/audio.mjs';

test('audio proxy carries authentication/Range, refreshes expired addresses and revokes streams', async (t) => {
  const payload = Buffer.from('0123456789');
  let resolves = 0;
  const server = http.createServer((request, response) => {
    if (request.headers.authorization !== 'Bearer fixture') {
      response.writeHead(401).end();
      return;
    }
    const range = /^bytes=(\d+)-(\d+)$/.exec(request.headers.range ?? '');
    const start = range ? Number(range[1]) : 0,
      end = range ? Number(range[2]) : payload.length - 1;
    response.writeHead(range ? 206 : 200, {
      'Content-Type': 'audio/wav',
      'Content-Length': end - start + 1,
      'Accept-Ranges': 'bytes',
      ...(range ? { 'Content-Range': `bytes ${start}-${end}/${payload.length}` } : {}),
    });
    response.end(payload.subarray(start, end + 1));
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const proxy = new AudioProxy(async () => {
    resolves++;
    return {
      url: `http://127.0.0.1:${server.address().port}/audio.wav`,
      headers: { Authorization: 'Bearer fixture' },
      expiresAt: resolves === 1 ? Date.now() - 1 : Date.now() + 60_000,
      extension: 'wav',
    };
  });
  await proxy.start();
  t.after(() => {
    proxy.close();
    server.closeAllConnections();
    server.close();
  });
  const opened = await proxy.open('test.source/music', 'one');
  assert.equal(opened.url.includes('fixture'), false);
  const response = await fetch(opened.url, { headers: { Range: 'bytes=2-5' } });
  assert.equal(response.status, 206);
  assert.equal(response.headers.get('content-range'), 'bytes 2-5/10');
  assert.equal(await response.text(), '2345');
  assert.equal(resolves, 2);
  const folder = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-download-'));
  t.after(() => fs.rm(folder, { recursive: true, force: true }));
  const downloaded = await proxy.download('test.source/music', 'one', path.join(folder, 'audio'));
  assert.equal(await fs.readFile(downloaded.path, 'utf8'), '0123456789');
  proxy.revoke('test.source');
  assert.equal((await fetch(opened.url)).status, 404);
});
test('source download limits and cancellation leave no published partial audio', async (t) => {
  const folder = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-download-limits-'));
  t.after(() => fs.rm(folder, { recursive: true, force: true }));
  const timers = new Set();
  const server = http.createServer((request, response) => {
    response.writeHead(200, { 'Content-Type': 'audio/wav' });
    response.write(Buffer.alloc(32));
    const timer = setInterval(() => response.write(Buffer.alloc(32)), 10);
    timers.add(timer);
    response.on('close', () => {
      clearInterval(timer);
      timers.delete(timer);
    });
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  let limit = 64;
  const proxy = new AudioProxy(async () => ({
    url: `http://127.0.0.1:${server.address().port}/audio.wav`,
    maxBytes: limit,
    contentTypes: ['audio/wav'],
  }));
  await proxy.start();
  t.after(() => {
    proxy.close();
    server.closeAllConnections();
    server.close();
    for (const timer of timers) clearInterval(timer);
  });
  await assert.rejects(proxy.download('test.source/music', 'one', path.join(folder, 'limited')), /limit/);
  assert.deepEqual(await fs.readdir(folder), []);
  limit = 1024 * 1024;
  const downloading = proxy.download('test.source/music', 'one', path.join(folder, 'cancelled'), 'task', () =>
    proxy.cancelDownload('task'),
  );
  await assert.rejects(downloading);
  assert.deepEqual(await fs.readdir(folder), []);
});

test('generic binary downloads use audio signatures and reject error pages', async (t) => {
  const folder = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-audio-types-'));
  t.after(() => fs.rm(folder, { recursive: true, force: true }));
  let payload, contentType;
  const server = http.createServer((_request, response) => {
    response.writeHead(200, contentType ? { 'Content-Type': contentType } : {});
    response.write(payload.subarray(0, 1));
    setImmediate(() => response.end(payload.subarray(1)));
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const proxy = new AudioProxy(async () => ({
    url: `http://127.0.0.1:${server.address().port}/audio`,
    extension: 'mp3',
    contentTypes: ['audio/mpeg', 'audio/flac', 'audio/wav', 'audio/ogg'],
    maxBytes: 1024,
  }));
  await proxy.start();
  t.after(() => {
    proxy.close();
    server.closeAllConnections();
    server.close();
  });
  let index = 0;
  for (const [type, audio, extension] of [
    ['application/octet-stream', Buffer.from('ID3fixture'), 'mp3'],
    ['binary/octet-stream', Buffer.from([0xff, 0xfb, 0x90, 0x64]), 'mp3'],
    ['', Buffer.from('fLaCfixture'), 'flac'],
    ['application/octet-stream', Buffer.from('RIFF1234WAVEfixture'), 'wav'],
    ['application/octet-stream', Buffer.from('OggSfixture'), 'ogg'],
    ['Audio/X-FLAC; charset=binary', Buffer.from('fLaCfixture'), 'flac'],
  ]) {
    contentType = type;
    payload = audio;
    const base = path.join(folder, `audio-${index++}`);
    const downloaded = await proxy.download('test.source/music', 'one', base);
    assert.equal(downloaded.path, `${base}.${extension}`);
    assert.deepEqual(await fs.readFile(downloaded.path), audio);
  }
  const published = (await fs.readdir(folder)).sort();
  for (const [type, body] of [
    ['text/html', '<html>error</html>'],
    ['application/json', '{"error":"denied"}'],
    ['application/octet-stream', '{"error":"denied"}'],
    ['', '<html>error</html>'],
    ['application/octet-stream', 'x'.repeat(100)],
  ]) {
    contentType = type;
    payload = Buffer.from(body);
    await assert.rejects(
      proxy.download('test.source/music', 'one', path.join(folder, 'invalid')),
      /Response is not a supported audio type/,
    );
    assert.deepEqual((await fs.readdir(folder)).sort(), published);
    assert.equal(proxy.streams.size, 0);
    assert.equal(proxy.downloads.size, 0);
  }
});
