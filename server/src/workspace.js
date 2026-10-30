// The workspace is a flat folder of media files plus a metadata file that
// records how each edited file was derived (its parent and the operations).

import fs from 'node:fs/promises';
import path from 'node:path';
import { config } from './config.js';

const META_FILE = path.join(config.workspace, '.pixelsense.json');
export const TMP_DIR = path.join(config.workspace, '.tmp');

const KINDS = {
  image: ['png', 'jpg', 'jpeg', 'webp', 'bmp', 'tif', 'tiff'],
  video: ['mp4', 'mov', 'mkv', 'webm', 'avi', 'm4v', 'gif'],
  audio: ['mp3', 'wav', 'aac', 'm4a', 'ogg', 'flac'],
};

export function extOf(name) {
  return path.extname(name).slice(1).toLowerCase();
}

export function kindOf(name) {
  const ext = extOf(name);
  return Object.keys(KINDS).find((k) => KINDS[k].includes(ext)) ?? null;
}

export async function init() {
  await fs.mkdir(TMP_DIR, { recursive: true });
}

// Maps a client-supplied file name to a path inside the workspace, rejecting
// anything that would escape it.
export function resolveFile(name) {
  if (typeof name !== 'string' || !name || name !== path.basename(name) || name.startsWith('.')) {
    throw Object.assign(new Error('Invalid file name'), { status: 400 });
  }
  return path.join(config.workspace, name);
}

export function sanitizeName(name) {
  const base = path.basename(name).replace(/[^\w.\- ]+/g, '_').replace(/^\.+/, '').trim();
  return base || 'file';
}

async function readMeta() {
  try {
    return JSON.parse(await fs.readFile(META_FILE, 'utf8'));
  } catch {
    return { files: {} };
  }
}

async function writeMeta(meta) {
  await fs.writeFile(META_FILE, JSON.stringify(meta, null, 2));
}

export async function listFiles() {
  const meta = await readMeta();
  const entries = await fs.readdir(config.workspace, { withFileTypes: true });
  const files = [];
  for (const entry of entries) {
    if (!entry.isFile() || entry.name.startsWith('.')) continue;
    const kind = kindOf(entry.name);
    if (!kind) continue;
    const stat = await fs.stat(path.join(config.workspace, entry.name));
    files.push({
      name: entry.name,
      kind,
      size: stat.size,
      modified: stat.mtimeMs,
      ...(meta.files[entry.name] ?? {}),
    });
  }
  return files.sort((a, b) => b.modified - a.modified);
}

export async function fileMeta(name) {
  return (await readMeta()).files[name] ?? null;
}

// Picks an unused name like "holiday_edit3.mp4", based on the original stem.
export async function nextEditName(sourceName, ext) {
  const stem = path.parse(sourceName).name.replace(/_edit\d+$/, '');
  const existing = new Set(await fs.readdir(config.workspace));
  for (let n = 1; ; n++) {
    const candidate = `${stem}_edit${n}.${ext}`;
    if (!existing.has(candidate)) return candidate;
  }
}

export async function uniqueName(name) {
  const existing = new Set(await fs.readdir(config.workspace));
  if (!existing.has(name)) return name;
  const { name: stem, ext } = path.parse(name);
  for (let n = 2; ; n++) {
    const candidate = `${stem} (${n})${ext}`;
    if (!existing.has(candidate)) return candidate;
  }
}

export async function recordEdit(name, parent, operations) {
  const meta = await readMeta();
  meta.files[name] = { parent, operations, createdAt: Date.now() };
  await writeMeta(meta);
}

export async function removeFile(name) {
  await fs.unlink(resolveFile(name));
  const meta = await readMeta();
  delete meta.files[name];
  for (const entry of Object.values(meta.files)) {
    if (entry.parent === name) entry.parent = null;
  }
  await writeMeta(meta);
}

export async function renameFile(name, newName) {
  const target = sanitizeName(newName);
  if (extOf(target) !== extOf(name)) {
    throw Object.assign(new Error('Renaming cannot change the file extension'), { status: 400 });
  }
  try {
    await fs.access(resolveFile(target));
    throw Object.assign(new Error(`"${target}" already exists`), { status: 409 });
  } catch (err) {
    if (err.status) throw err;
  }
  await fs.rename(resolveFile(name), resolveFile(target));
  const meta = await readMeta();
  if (meta.files[name]) {
    meta.files[target] = meta.files[name];
    delete meta.files[name];
  }
  for (const entry of Object.values(meta.files)) {
    if (entry.parent === name) entry.parent = target;
  }
  await writeMeta(meta);
  return target;
}
