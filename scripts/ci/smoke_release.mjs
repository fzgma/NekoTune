// Run the installed desktop app with isolated data and no build-tool search paths.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import fs from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';

const stage = path.resolve(process.argv[2]);
const nativeWayland = process.argv.includes('--wayland');
const wallpaperRuntime = JSON.parse(await fs.readFile(path.join(stage, 'wallpaper-runtime.json'), 'utf8'));
if (wallpaperRuntime.backend === 'wayland') {
  for (const file of ['plugins/wayland-shell-integration/libxdg-shell.so', 'qml/QtQuick/Effects/qmldir'])
    await fs.access(path.join(stage, file));
}
if (nativeWayland) assert.equal(wallpaperRuntime.backend, 'wayland');
async function checkDistribution(backendOnly, inputMethod = '', standalone = false) {
  const label = standalone ? 'Standalone frontend + external backend'
    : backendOnly ? 'Backend' : `Desktop${inputMethod ? ' (' + inputMethod + ')' : ''}`;
  const inputPlugin = inputMethod ? await fs.realpath(path.join(stage, 'plugins/platforminputcontexts',
    `lib${inputMethod === 'fcitx' ? 'fcitx5' : 'ibus'}platforminputcontextplugin.so`)) : '';
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-release-'));
  const socketName = process.platform === 'win32'
    ? 'nekotune-release-' + randomUUID() : path.join(root, 'backend.sock');
  const endpoint = process.platform === 'win32' ? '\\\\.\\pipe\\' + socketName : socketName;
  const environment = { ...process.env };
  for (const key of ['LD_LIBRARY_PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH',
    'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_DIR', 'QTDIR']) delete environment[key];
  for (const key of Object.keys(environment)) {
    if (key.toLowerCase() === 'path') delete environment[key];
  }
  Object.assign(environment, {
    NEKOTUNE_HOME: root, NEKOTUNE_SOCKET: socketName,
    NEKOTUNE_DB_PATH: path.join(root, 'player.sqlite3'),
    XDG_CONFIG_HOME: path.join(root, 'config'), XDG_DATA_HOME: path.join(root, 'data'),
    XDG_CACHE_HOME: path.join(root, 'cache'),
    QT_QPA_PLATFORM: process.platform === 'win32' ? 'windows' : nativeWayland && (!backendOnly || standalone) ? 'wayland' : 'offscreen',
    QT_QUICK_BACKEND: 'software',
    QT_FORCE_STDERR_LOGGING: '1',
    PATH: process.platform === 'win32'
      ? `${stage}/bin;${process.env.SystemRoot}/System32;${process.env.SystemRoot}`
      : '/usr/bin:/bin',
  });
  if (inputMethod) {
    environment.QT_IM_MODULE = inputMethod;
    environment.QT_DEBUG_PLUGINS = '1';
    delete environment.QT_IM_MODULES;
  }
  if (backendOnly && process.platform !== 'win32') environment.LD_LIBRARY_PATH = path.join(stage, 'lib');
  const executable = backendOnly
    ? path.join(stage, 'bin', process.platform === 'win32' ? 'nekotune-backend.exe' : 'nekotune-backend')
    : process.platform === 'win32' ? path.join(stage, 'bin', 'nekotune.exe') : path.join(stage, 'NekoTune');
  environment.NEKOTUNE_SAFE_MODE = '1';
  const testWallpaper = nativeWayland && (!backendOnly || standalone);
  if (testWallpaper) {
    await fs.mkdir(path.join(root, 'config'), { recursive: true });
    await fs.writeFile(path.join(root, 'config/settings.json'), JSON.stringify({
      wallpaper: { enabled: true, autoRestore: true },
    }));
    environment.NEKOTUNE_SAFE_MODE = '0';
    delete environment.QT_QUICK_BACKEND;
  }
  const child = spawn(executable, backendOnly || testWallpaper ? [] : ['--safe-mode'], { env: environment, windowsHide: true });
  let output = '', exited = false, exitStatus = 'running';
  const trace = [];
  const diagnostics = () => `${label}: ${exitStatus}\nIPC: ${trace.join('\n')}\n${output}`;
  child.stdout.on('data', bytes => { output += bytes; });
  child.stderr.on('data', bytes => { output += bytes; });
  child.on('exit', (code, signal) => { exited = true; exitStatus = `exit=${code}, signal=${signal}`; });
  child.on('error', error => { output += error.stack; exited = true; exitStatus = 'spawn failed'; });
  let frontend, frontendExited = false;
  if (standalone) {
    frontend = spawn(path.join(stage, 'bin', process.platform === 'win32' ? 'nekotune-qt-qml.exe' : 'nekotune-qt-qml'),
      testWallpaper ? [] : ['--safe-mode'], { env: environment, windowsHide: true });
    frontend.stdout.on('data', bytes => { output += bytes; });
    frontend.stderr.on('data', bytes => { output += bytes; });
    frontend.on('exit', () => { frontendExited = true; });
    frontend.on('error', error => { output += error.stack; frontendExited = true; });
  }
  let socket;
  try {
    const deadline = Date.now() + 45_000;
    while (Date.now() < deadline) {
      assert.ok(!exited, `Process exited during startup:\n${diagnostics()}`);
      socket = net.createConnection(endpoint);
      if (await new Promise(resolve => {
        socket.once('connect', () => resolve(true));
        socket.once('error', () => resolve(false));
      })) break;
      socket.destroy();
      await new Promise(resolve => setTimeout(resolve, 150));
    }
    assert.ok(socket && !socket.destroyed, `IPC startup timed out:\n${diagnostics()}`);
    trace.push('pipe connected');
    socket.on('close', () => trace.push('pipe closed'));
    socket.on('error', error => trace.push(`pipe error: ${error.message}`));
    let buffer = '', id = 0;
    const pending = new Map();
    const rejectPending = reason => {
      for (const { reject } of pending.values()) reject(new Error(`${reason}\n${diagnostics()}`));
      pending.clear();
    };
    child.on('close', () => rejectPending('Process closed during IPC request'));
    socket.on('close', () => rejectPending('IPC disconnected during request'));
    socket.on('data', bytes => {
      buffer += bytes;
      while (buffer.includes('\n')) {
        const newline = buffer.indexOf('\n');
        const message = JSON.parse(buffer.slice(0, newline));
        trace.push(`received: ${JSON.stringify(message)}`);
        if (trace.length > 20) trace.shift();
        buffer = buffer.slice(newline + 1);
        const request = pending.get(message.id);
        if (request) { pending.delete(message.id); request.resolve(message); }
      }
    });
    async function call(method, params = {}) {
      assert.ok(!exited && !socket.destroyed, diagnostics());
      const requestId = ++id;
      const reply = new Promise((resolve, reject) => pending.set(requestId, { resolve, reject }));
      trace.push(`send: ${requestId} ${method}`);
      socket.write(JSON.stringify({ id: requestId, method, params }) + '\n');
      let timer;
      try {
        const response = await Promise.race([reply, new Promise((_, reject) => {
          timer = setTimeout(() => reject(new Error(`IPC timeout: ${method}\n${diagnostics()}`)), 5000);
        })]);
        assert.equal(response.status, 'ok', JSON.stringify(response));
        return response.data;
      } finally { clearTimeout(timer); pending.delete(requestId); }
    }
    assert.ok(await call('player.status'));
    let extensions;
    while (Date.now() < deadline) {
      extensions = await call('extensions.list');
      if (extensions.catalogueReady) break;
      await new Promise(resolve => setTimeout(resolve, 150));
    }
    assert.equal(extensions?.catalogueReady, true, `Bundled Node supervisor failed:\n${diagnostics()}`);
    const selection = await call('extensions.select', { slot: 'theme', contribution: 'builtin/ocean' });
    assert.equal(selection.selections.theme, 'builtin/ocean');
    const themePath = path.join(root, 'ocean.nktheme.zip');
    await call('extensions.export_theme', { contribution: 'builtin/ocean', path: themePath });
    const archive = await fs.readFile(themePath);
    assert.equal(archive.readUInt32LE(0), 0x04034b50);
    const imported = await call('extensions.install', { path: themePath, themeOnly: true });
    assert.equal(imported.extension.type, 'theme');
    assert.equal(imported.extension.hasCode, false);
    await call('extensions.select', { slot: 'theme', contribution: '' });
    await new Promise(resolve => setTimeout(resolve, 1500));
    assert.ok(!exited, `Process exited after IPC startup:\n${diagnostics()}`);
    assert.doesNotMatch(output, /QQmlApplicationEngine failed|module "[^"]+" is not installed/);
    assert.doesNotMatch(output, /TypeError:|ReferenceError:|Binding loop detected/);
    if (inputMethod) {
      assert.ok(output.includes(`"${inputPlugin}" loaded library`),
        `Packaged ${inputMethod} input context was not loaded:\n${diagnostics()}`);
    }
    if (testWallpaper) assert.match(output, /NekoTune wallpaper mapped on/, diagnostics());
    if (standalone) {
      assert.ok(!frontendExited, `Standalone frontend exited early:\n${diagnostics()}`);
      frontend.kill();
      await Promise.race([
        new Promise(resolve => frontend.once('exit', resolve)),
        new Promise((_, reject) => setTimeout(() => reject(new Error('Standalone frontend did not exit')), 5000)),
      ]);
      // The external backend and its IPC remain alive after frontend removal.
      assert.ok(await call('player.status'));
    }
    console.log(`Installed ${label}, SQLite, IPC, bundled Node and theme import/export passed.`);
  } finally {
    if (frontend && !frontendExited) frontend.kill('SIGKILL');
    socket?.destroy();
    child.kill();
    if (!exited) await Promise.race([
      new Promise(resolve => child.once('exit', resolve)),
      new Promise(resolve => setTimeout(resolve, 5000)),
    ]);
    if (!exited) child.kill('SIGKILL');
    await fs.rm(root, { recursive: true, force: true, maxRetries: 20, retryDelay: 150 });
  }
}

const failures = [];
const checks = process.platform === 'linux' ? [[true, ''], [false, 'fcitx'], [false, 'ibus'], [true, '', true]]
  : [[true, ''], [false, ''], [true, '', true]];
for (const [backendOnly, inputMethod, standalone] of checks) {
  try { await checkDistribution(backendOnly, inputMethod, standalone); }
  catch (error) { console.error(error); failures.push(error); }
}
if (failures.length) throw new AggregateError(failures, 'Installed distribution validation failed');
