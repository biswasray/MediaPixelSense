// Thin wrapper around the PixelSenseEngine CLI. The engine owns the list of
// operations and their validation; this module just runs it and parses JSON.

import { execFile } from 'node:child_process';
import { config } from './config.js';

function runEngine(args) {
  return new Promise((resolve, reject) => {
    execFile(config.engine, args, { windowsHide: true, maxBuffer: 16 * 1024 * 1024 }, (err, stdout) => {
      const text = stdout?.toString().trim();
      if (text) {
        try {
          return resolve(JSON.parse(text));
        } catch {
          // fall through to the error below
        }
      }
      if (err?.code === 'ENOENT') {
        return reject(new Error(`PixelSenseEngine not found at ${config.engine}. Run build-engine.bat first.`));
      }
      reject(new Error(err?.message ?? `Unexpected engine output: ${text}`));
    });
  });
}

let opsCache = null;

export async function listOperations() {
  opsCache ??= await runEngine(['ops']);
  return opsCache;
}

export async function probe(file) {
  const result = await runEngine(['probe', file]);
  if (!result.ok) throw new Error(result.error);
  return result.info;
}

// Extension the output file should have, from the operation's "output" kind.
export function outputExtension(op, inputExt, params) {
  switch (op.output) {
    case 'image': return 'png';
    case 'audio': return 'mp3';
    case 'gif': return 'gif';
    case 'format': return String(params.format ?? inputExt).toLowerCase();
    default: return inputExt;
  }
}

// Resolves with the engine's result object ({ ok, error?, warnings, info }).
export async function apply(opName, input, output, params = {}) {
  const pairs = Object.entries(params)
    .filter(([, v]) => v !== null && v !== undefined && v !== '')
    .map(([k, v]) => `${k}=${v}`);
  return runEngine(['apply', opName, input, output, ...pairs]);
}
