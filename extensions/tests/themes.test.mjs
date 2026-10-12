import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { catalog, validateTheme, resolveTokens, exportTheme } from '../runtime/themes.mjs';
import { readManifest, unpack } from '../runtime/packages.mjs';

async function directory(t) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'nekotune-theme-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  return root;
}
function descriptor() {
  return { id: 'default', title: 'Example', schemaVersion: 1, variants: { light: {}, dark: {} } };
}
async function manifestAt(root, theme, extra = {}) {
  await fs.writeFile(
    path.join(root, 'extension.json'),
    JSON.stringify({
      id: 'test.theme',
      name: 'Theme',
      version: '1.0.0',
      apiVersion: 1,
      type: 'theme',
      contributes: { themes: [theme] },
      ...extra,
    }),
  );
  return await readManifest(root);
}
test('built-ins resolve complete dual palettes and preserve the original dark defaults', async () => {
  assert.equal(catalog.themes.length, 3);
  for (const theme of catalog.themes) {
    await validateTheme(theme, '.', { strict: true });
    for (const mode of ['light', 'dark']) {
      const tokens = resolveTokens(theme, mode);
      assert.deepEqual(Object.keys(tokens).sort(), Object.keys(catalog.tokens).sort());
      assert.notEqual(tokens.textPrimary, tokens.bgCanvas);
    }
  }
  assert.equal(resolveTokens(catalog.themes[0], 'dark').bgCanvas, '#0E0D14');
  assert.equal(resolveTokens({ tokens: { bgCanvas: '#112233' } }, 'light').bgCanvas, catalog.light.bgCanvas);
  const theme = descriptor();
  theme.tokens = { radiusMd: 20, accentPrimary: '#123456' };
  theme.variants.light.tokens = { accentPrimary: '#654321' };
  assert.equal(resolveTokens(theme, 'light').radiusMd, 20);
  assert.equal(resolveTokens(theme, 'light').accentPrimary, '#654321');
});
test('invalid schemas, variables, paths, scripts and native files are rejected', async (t) => {
  const root = await directory(t);
  for (const tokens of [
    { unknown: 1 },
    { radiusMd: -1 },
    { durationFast: Infinity },
    { fontBody: 2.2 },
    { bgCanvas: 'oops' },
  ]) {
    const theme = descriptor();
    theme.tokens = tokens;
    await assert.rejects(validateTheme(theme, root, { strict: true }));
  }
  await assert.rejects(validateTheme({ ...descriptor(), variants: { dark: {} } }, root), /light and dark/);
  await fs.writeFile(path.join(root, 'Test.qml'), 'import QtQuick\nItem {}');
  for (const source of ['../Test.qml', '/Test.qml', 'file:///Test.qml', 'bad\\Test.qml'])
    await assert.rejects(validateTheme({ ...descriptor(), ui: { shell: source } }, root));
  await assert.rejects(manifestAt(root, descriptor(), { main: 'Test.qml' }), /backend/);
  await fs.writeFile(path.join(root, 'module.so'), 'native');
  await assert.rejects(manifestAt(root, descriptor()), /Unsupported theme file/);
  await fs.rm(path.join(root, 'module.so'));
  if (process.platform !== 'win32') {
    await fs.symlink(path.join(root, 'Test.qml'), path.join(root, 'Link.qml'));
    await assert.rejects(manifestAt(root, descriptor()), /links/);
  }
});
test('image dimensions and aggregate package budget are checked before installation', async (t) => {
  const root = await directory(t);
  const header = Buffer.alloc(33);
  Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]).copy(header);
  header.writeUInt32BE(13, 8);
  header.write('IHDR', 12);
  header.writeUInt32BE(5000, 16);
  header.writeUInt32BE(5000, 20);
  await fs.writeFile(path.join(root, 'big.png'), header);
  const theme = descriptor();
  theme.variants.dark.background = { source: 'big.png' };
  await assert.rejects(manifestAt(root, theme), /pixel limit/);
  await fs.rm(path.join(root, 'big.png'));
  const file = await fs.open(path.join(root, 'large.json'), 'w');
  await file.truncate(64 * 1024 * 1024 + 1);
  await file.close();
  await assert.rejects(manifestAt(root, descriptor()), /size limit/);
});
test('full QML packages and palette-only exports survive a portable round trip', async (t) => {
  const root = await directory(t),
    source = path.join(root, 'source');
  await fs.mkdir(source);
  await fs.writeFile(path.join(source, 'Background.qml'), 'import QtQuick\nItem { objectName: "retained" }');
  const theme = { ...descriptor(), ui: { background: 'Background.qml' } };
  const manifest = await manifestAt(source, theme);
  assert.equal(manifest._themeHasCode, true);
  const zip = path.join(root, 'full.nktheme.zip');
  await exportTheme({ theme, entry: { manifest, directory: source }, destination: zip });
  const full = path.join(root, 'full');
  await unpack(zip, full, true);
  assert.deepEqual((await readManifest(full)).contributes.themes[0].ui, theme.ui);
  assert.match(await fs.readFile(path.join(full, 'Background.qml'), 'utf8'), /retained/);
  const paletteZip = path.join(root, 'palette.nktheme.zip');
  await exportTheme({
    theme,
    entry: { manifest: { ...manifest, type: undefined }, directory: source },
    destination: paletteZip,
  });
  const palette = path.join(root, 'palette');
  await unpack(paletteZip, palette, true);
  const exported = await readManifest(palette);
  assert.equal(exported._themeHasCode, false);
  assert.equal(exported.contributes.themes[0].ui, undefined);
  assert.deepEqual(resolveTokens(exported.contributes.themes[0], 'light'), resolveTokens(theme, 'light'));
});

test('palette exports reuse a shared image without doubling its pixel budget', async (t) => {
  const root = await directory(t);
  const source = path.join(root, 'source');
  await fs.mkdir(source);
  const header = Buffer.alloc(33);
  Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]).copy(header);
  header.writeUInt32BE(13, 8);
  header.write('IHDR', 12);
  header.writeUInt32BE(3000, 16);
  header.writeUInt32BE(3000, 20);
  await fs.writeFile(path.join(source, 'shared.png'), header);
  const theme = descriptor();
  theme.variants.light.background = { source: 'shared.png', overlayOpacity: 0.7 };
  theme.variants.dark.background = { source: './shared.png', overlayOpacity: 0.8 };
  const zip = path.join(root, 'shared.nktheme.zip');
  await exportTheme({ theme, entry: { manifest: {}, directory: source }, destination: zip });
  const output = path.join(root, 'output');
  await unpack(zip, output, true);
  const exported = (await readManifest(output)).contributes.themes[0];
  assert.equal(exported.variants.light.background.source, exported.variants.dark.background.source);
  assert.equal(exported.variants.light.background.overlayOpacity, 0.7);
  assert.equal(exported.variants.dark.background.overlayOpacity, 0.8);
  assert.equal((await fs.readdir(path.join(output, 'assets'))).length, 1);
});

test('built-in text remains legible on ordinary and selected surfaces', () => {
  const luminance = (hex) =>
    hex
      .slice(-6)
      .match(/../g)
      .map((value) => parseInt(value, 16) / 255)
      .map((value) => (value <= 0.04045 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4))
      .reduce((sum, value, index) => sum + value * [0.2126, 0.7152, 0.0722][index], 0);
  const contrast = (a, b) =>
    (Math.max(luminance(a), luminance(b)) + 0.05) / (Math.min(luminance(a), luminance(b)) + 0.05);
  for (const theme of catalog.themes)
    for (const mode of ['dark', 'light']) {
      const tokens = resolveTokens(theme, mode);
      for (const text of ['textPrimary', 'textSecondary', 'textMuted'])
        for (const surface of ['bgCanvas', 'bgSurface', 'bgRaised', 'bgHover', 'bgSelected'])
          assert.ok(
            contrast(tokens[text], tokens[surface]) >= 4.5,
            `${theme.id}/${mode}: ${text} on ${surface}`,
          );
      assert.ok(contrast(tokens.textOnAccent, tokens.accentPrimary) >= 4.5);
    }
});
