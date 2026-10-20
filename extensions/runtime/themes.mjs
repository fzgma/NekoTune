import fs from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import path from 'node:path';
import { randomUUID } from 'node:crypto';
import { pipeline } from 'node:stream/promises';
import yazl from 'yazl';
import catalog from '../../shared/themes/catalog.json' with { type: 'json' };

export { catalog };
export const themeLimits = { archive: 20 * 1024 * 1024, unpacked: 64 * 1024 * 1024, pixels: 16_000_000 };
const object = (value) => value !== null && typeof value === 'object' && !Array.isArray(value);
const modes = ['light', 'dark'];
const pageIds = new Set([
  'home',
  'library',
  'queue',
  'lyrics',
  'settings',
  'extensions',
  'music_sources',
  'lyrics_debug',
]);

export function validateTokens(tokens = {}, legacy = false) {
  if (!object(tokens)) throw new Error('Theme tokens must be an object');
  for (const [key, value] of Object.entries(tokens)) {
    const rule = catalog.tokens[key];
    if (!rule) {
      if (legacy) continue;
      throw new Error(`Unknown theme token: ${key}`);
    }
    const valid =
      rule.type === 'color'
        ? typeof value === 'string' && /^#(?:[\da-f]{6}|[\da-f]{8})$/i.test(value)
        : rule.type === 'string'
          ? typeof value === 'string' && value.length > 0 && value.length <= 200
          : rule.type === 'bool'
            ? typeof value === 'boolean'
            : typeof value === 'number' &&
              Number.isFinite(value) &&
              value >= rule.min &&
              value <= rule.max &&
              (rule.type !== 'int' || Number.isInteger(value));
    if (!valid) throw new Error(`Invalid theme token: ${key}`);
  }
}

export async function themeFile(directory, relative) {
  if (
    typeof relative !== 'string' ||
    !relative ||
    relative.includes('\\') ||
    relative.includes(':') ||
    path.isAbsolute(relative) ||
    relative.split('/').some((part) => part === '..' || !part)
  )
    throw new Error('Theme resource must be a contained relative path');
  const root = await fs.realpath(directory);
  let target = root;
  for (const part of relative.split('/')) {
    target = path.join(target, part);
    if ((await fs.lstat(target)).isSymbolicLink()) throw new Error('Theme links are not allowed');
  }
  if (!(await fs.stat(target)).isFile()) throw new Error('Theme resource is not a file');
  return target;
}

function imagePixels(data) {
  if (
    data.length >= 33 &&
    data.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])) &&
    data.readUInt32BE(8) === 13 &&
    data.toString('ascii', 12, 16) === 'IHDR'
  )
    return data.readUInt32BE(16) * data.readUInt32BE(20);
  if (data.length > 4 && data.readUInt16BE(0) === 0xffd8) {
    let offset = 2;
    while (offset + 4 <= data.length) {
      if (data[offset++] !== 0xff) break;
      while (data[offset] === 0xff) offset++;
      const marker = data[offset++];
      if (marker === 0xd9 || marker === 0xda) break;
      if (marker === 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
      if (offset + 2 > data.length) break;
      const length = data.readUInt16BE(offset);
      if (length < 2 || offset + length > data.length) break;
      if ([0xc0, 0xc1, 0xc2].includes(marker) && length >= 8)
        return data.readUInt16BE(offset + 3) * data.readUInt16BE(offset + 5);
      offset += length;
    }
  }
  throw new Error('Theme image must be a valid PNG or JPEG');
}

async function checkImage(directory, relative) {
  const file = await themeFile(directory, relative);
  if (!/\.(png|jpe?g)$/i.test(relative) || (await fs.stat(file)).size > themeLimits.unpacked)
    throw new Error('Invalid or oversized theme image');
  const pixels = imagePixels(await fs.readFile(file));
  if (!pixels || pixels > themeLimits.pixels) throw new Error('Theme image pixel limit exceeded');
  return pixels;
}

export async function validateTheme(theme, directory, { strict = false } = {}) {
  if (!object(theme)) throw new Error('Invalid theme descriptor');
  const modern = theme.schemaVersion !== undefined;
  if (
    (strict || modern) &&
    (theme.schemaVersion !== 1 ||
      !object(theme.variants) ||
      modes.some((mode) => !object(theme.variants[mode])))
  )
    throw new Error('Theme schemaVersion 1 requires light and dark variants');
  validateTokens(theme.tokens, !modern);
  const images = new Set();
  for (const mode of modes) {
    const variant = theme.variants?.[mode];
    if (!variant) continue;
    validateTokens(variant.tokens);
    if (variant.background !== undefined) {
      const bg = variant.background;
      if (!object(bg) || typeof bg.source !== 'string') throw new Error('Invalid theme background');
      for (const key of ['opacity', 'overlayOpacity', 'blur'])
        if (
          bg[key] !== undefined &&
          (typeof bg[key] !== 'number' || !Number.isFinite(bg[key]) || bg[key] < 0 || bg[key] > 1)
        )
          throw new Error(`Invalid background ${key}`);
      if (bg.overlayColor !== undefined && !/^#(?:[\da-f]{6}|[\da-f]{8})$/i.test(bg.overlayColor))
        throw new Error('Invalid background overlayColor');
      images.add(bg.source);
    }
  }
  if (theme.ui !== undefined) {
    if (!modern || !object(theme.ui)) throw new Error('QML themes require schemaVersion 1');
    for (const key of Object.keys(theme.ui))
      if (!['background', 'shell', 'bottomPlayer', 'wallpaper', 'pages'].includes(key))
        throw new Error(`Unknown theme UI slot: ${key}`);
    if (theme.ui.pages !== undefined && !object(theme.ui.pages)) throw new Error('Invalid theme pages');
    for (const id of Object.keys(theme.ui.pages ?? {}))
      if (!pageIds.has(id)) throw new Error(`Unknown theme page: ${id}`);
    for (const source of [
      ...['background', 'shell', 'bottomPlayer', 'wallpaper'].flatMap((key) =>
        theme.ui[key] === undefined ? [] : [theme.ui[key]],
      ),
      ...Object.values(theme.ui.pages ?? {}),
    ]) {
      if (typeof source !== 'string' || !source.endsWith('.qml'))
        throw new Error('Theme UI source must be QML');
      await themeFile(directory, source);
    }
  }
  let pixels = 0;
  for (const file of images) pixels += await checkImage(directory, file);
  if (pixels > themeLimits.pixels) throw new Error('Theme image pixel limit exceeded');
}

export async function inspectThemePackage(directory, manifest) {
  if (
    manifest.main ||
    manifest.nativeModules ||
    Object.keys(manifest.dependencies ?? {}).length ||
    !manifest.contributes?.themes?.length ||
    Object.keys(manifest.contributes).some((key) => key !== 'themes')
  )
    throw new Error('Theme packages may only contribute themes, without a backend or native modules');
  let size = 0,
    pixels = 0,
    hasCode = false;
  const files = [];
  async function walk(folder, prefix = '') {
    for (const entry of await fs.readdir(folder, { withFileTypes: true })) {
      const relative = prefix + entry.name;
      if (entry.isSymbolicLink()) throw new Error('Theme links are not allowed');
      if (entry.isDirectory()) {
        await walk(path.join(folder, entry.name), relative + '/');
        continue;
      }
      if (
        !entry.isFile() ||
        !(
          relative === 'extension.json' ||
          /\.(qml|js|mjs|json|png|jpe?g)$/i.test(relative) ||
          entry.name === 'qmldir'
        )
      )
        throw new Error(`Unsupported theme file: ${relative}`);
      size += (await fs.stat(path.join(folder, entry.name))).size;
      files.push(relative);
      if (size > themeLimits.unpacked || files.length > 2048)
        throw new Error('Theme package size limit exceeded');
      if (/\.(qml|js|mjs)$/i.test(relative) || entry.name === 'qmldir') hasCode = true;
      if (/\.(png|jpe?g)$/i.test(relative)) pixels += await checkImage(directory, relative);
      if (pixels > themeLimits.pixels) throw new Error('Theme image pixel limit exceeded');
    }
  }
  await walk(directory);
  return { files, hasCode };
}

export function resolveTokens(theme, mode) {
  const base = Object.fromEntries(Object.entries(catalog.tokens).map(([key, rule]) => [key, rule.default]));
  if (mode === 'light') Object.assign(base, catalog.light);
  if (!theme.schemaVersion && mode === 'light') return base;
  for (const values of [theme.tokens ?? {}, theme.variants?.[mode]?.tokens ?? {}])
    for (const [key, value] of Object.entries(values)) if (catalog.tokens[key]) base[key] = value;
  if (
    !Object.hasOwn(theme.tokens ?? {}, 'bgCard') &&
    !Object.hasOwn(theme.variants?.[mode]?.tokens ?? {}, 'bgCard')
  )
    base.bgCard = base.bgSurface;
  return base;
}

export async function exportTheme({ theme, entry, destination }) {
  if (typeof destination !== 'string' || !destination.endsWith('.zip'))
    throw new Error('Theme export requires a ZIP path');
  const full = entry?.manifest.type === 'theme';
  const zip = new yazl.ZipFile();
  if (full) {
    const { files } = await inspectThemePackage(entry.directory, entry.manifest);
    for (const relative of files) zip.addFile(await themeFile(entry.directory, relative), relative);
  } else {
    const assets = new Map();
    let resourceBytes = 0;
    const exported = {
      id: 'theme',
      title: theme.title,
      author: theme.author ?? entry?.manifest.author,
      schemaVersion: 1,
      variants: {},
    };
    for (const mode of modes) {
      exported.variants[mode] = { tokens: resolveTokens(theme, mode) };
      const background = theme.variants?.[mode]?.background;
      if (background && entry) {
        const file = await themeFile(entry.directory, background.source);
        let source = assets.get(file);
        if (!source) {
          source = `assets/${mode}${path.extname(background.source)}`;
          resourceBytes += (await fs.stat(file)).size;
          if (resourceBytes > themeLimits.unpacked) throw new Error('Theme package size limit exceeded');
          assets.set(file, source);
          zip.addFile(file, source);
        }
        exported.variants[mode].background = { ...background, source };
      }
    }
    const manifest = {
      id: `theme.export-${randomUUID()}`,
      name: typeof theme.title === 'string' ? theme.title : (theme.title?.en ?? 'Exported theme'),
      version: '1.0.0',
      apiVersion: 1,
      type: 'theme',
      contributes: { themes: [exported] },
    };
    const manifestBytes = Buffer.from(JSON.stringify(manifest, null, 2));
    if (resourceBytes + manifestBytes.length > themeLimits.unpacked)
      throw new Error('Theme package size limit exceeded');
    zip.addBuffer(manifestBytes, 'extension.json');
  }
  const temporary = `${destination}.${randomUUID()}.tmp`;
  try {
    const writing = pipeline(zip.outputStream, createWriteStream(temporary, { flags: 'wx', mode: 0o600 }));
    zip.end();
    await writing;
    if ((await fs.stat(temporary)).size > themeLimits.archive)
      throw new Error('Theme archive exceeds 20 MiB');
    await fs.rename(temporary, destination);
    return { path: destination, includesQml: Boolean(full && theme.ui) };
  } finally {
    await fs.rm(temporary, { force: true });
  }
}
