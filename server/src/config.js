import path from 'node:path';
import { fileURLToPath } from 'node:url';

const projectRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const exe = process.platform === 'win32' ? 'PixelSenseEngine.exe' : 'PixelSenseEngine';

export const config = {
  port: Number(process.env.PORT ?? 8787),
  projectRoot,
  workspace: path.resolve(process.env.PSE_WORKSPACE ?? path.join(projectRoot, 'workspace')),
  engine: process.env.PSE_ENGINE ?? path.join(projectRoot, 'out', 'build', 'x64-release', 'PixelSenseEngine', exe),
  webDist: path.join(projectRoot, 'web', 'dist'),
  ollamaUrl: (process.env.OLLAMA_URL ?? 'http://127.0.0.1:11434').replace(/\/$/, ''),
  ollamaModel: process.env.OLLAMA_MODEL ?? 'qwen2.5:1.5b',
};
