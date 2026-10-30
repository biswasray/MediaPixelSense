import express from 'express';
import multer from 'multer';
import fs from 'node:fs/promises';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { randomUUID } from 'node:crypto';

import { config } from './config.js';
import * as engine from './engine.js';
import * as ws from './workspace.js';
import { planEdits, modelStatus } from './llm.js';

await ws.init();

const app = express();
app.use(express.json({ limit: '1mb' }));

const asyncRoute = (fn) => (req, res, next) => fn(req, res, next).catch(next);

// Runs operations in sequence, chaining each output into the next input.
// Intermediate results live in .tmp; only the final file joins the workspace.
async function runPipeline(sourceName, operations) {
  const ops = await engine.listOperations();
  const byName = new Map(ops.map((o) => [o.name, o]));
  const steps = [];
  const temps = [];
  let current = ws.resolveFile(sourceName);

  try {
    for (const step of operations) {
      const op = byName.get(step.name);
      if (!op) throw new Error(`Unknown operation "${step.name}"`);
      const ext = engine.outputExtension(op, ws.extOf(current), step.params);
      const tmp = path.join(ws.TMP_DIR, `${randomUUID()}.${ext}`);
      temps.push(tmp);

      const result = await engine.apply(step.name, current, tmp, step.params);
      if (!result.ok) {
        return { steps, error: `${step.name} failed: ${result.error}` };
      }
      steps.push({ name: step.name, params: step.params, warnings: result.warnings ?? [] });
      current = tmp;
    }

    if (!steps.length) return { steps };
    const outputName = await ws.nextEditName(sourceName, ws.extOf(current));
    await fs.rename(current, ws.resolveFile(outputName));
    await ws.recordEdit(outputName, sourceName, steps.map(({ name, params }) => ({ name, params })));
    return { steps, output: outputName };
  } finally {
    await Promise.all(temps.map((t) => fs.rm(t, { force: true })));
  }
}

// --- Status ----------------------------------------------------------------

app.get('/api/health', asyncRoute(async (_req, res) => {
  let engineStatus;
  try {
    engineStatus = { ok: true, operations: (await engine.listOperations()).length };
  } catch (err) {
    engineStatus = { ok: false, error: err.message };
  }
  res.json({ engine: engineStatus, llm: await modelStatus(), workspace: config.workspace });
}));

app.get('/api/operations', asyncRoute(async (_req, res) => {
  res.json(await engine.listOperations());
}));

// --- File manager ----------------------------------------------------------

app.get('/api/files', asyncRoute(async (_req, res) => {
  res.json(await ws.listFiles());
}));

app.get('/api/files/:name/info', asyncRoute(async (req, res) => {
  const info = await engine.probe(ws.resolveFile(req.params.name));
  res.json({ name: req.params.name, info, meta: await ws.fileMeta(req.params.name) });
}));

const upload = multer({
  storage: multer.diskStorage({
    destination: ws.TMP_DIR,
    filename: (_req, _file, cb) => cb(null, randomUUID()),
  }),
  limits: { fileSize: 4 * 1024 ** 3 },
});

app.post('/api/files', upload.array('files'), asyncRoute(async (req, res) => {
  const saved = [];
  const skipped = [];
  for (const file of req.files ?? []) {
    // multer decodes header file names as latin1
    const original = ws.sanitizeName(Buffer.from(file.originalname, 'latin1').toString('utf8'));
    if (!ws.kindOf(original)) {
      skipped.push(original);
      await fs.rm(file.path, { force: true });
      continue;
    }
    const name = await ws.uniqueName(original);
    await fs.rename(file.path, ws.resolveFile(name));
    saved.push(name);
  }
  res.json({ saved, skipped });
}));

app.patch('/api/files/:name', asyncRoute(async (req, res) => {
  const name = await ws.renameFile(req.params.name, String(req.body?.name ?? ''));
  res.json({ name });
}));

app.delete('/api/files/:name', asyncRoute(async (req, res) => {
  await ws.removeFile(req.params.name);
  res.json({ ok: true });
}));

app.use('/media', express.static(config.workspace, { dotfiles: 'deny', etag: true, maxAge: 0 }));

// --- Editing ---------------------------------------------------------------

// Direct execution, bypassing the LLM: { file, operations: [{ name, params }] }
app.post('/api/apply', asyncRoute(async (req, res) => {
  const { file, operations } = req.body ?? {};
  ws.resolveFile(file);
  if (!Array.isArray(operations) || !operations.length) {
    return res.status(400).json({ error: 'operations must be a non-empty array' });
  }
  res.json(await runPipeline(file, operations.map((o) => ({ name: o.name, params: o.params ?? {} }))));
}));

// { file?, message, history? } -> { reply, steps, output?, error? }
app.post('/api/chat', asyncRoute(async (req, res) => {
  const { file, message, history = [] } = req.body ?? {};
  if (!message || typeof message !== 'string') return res.status(400).json({ error: 'message is required' });

  let selected = null;
  if (file) selected = { name: file, info: await engine.probe(ws.resolveFile(file)) };

  const ops = await engine.listOperations();
  const plan = await planEdits(ops, selected, Array.isArray(history) ? history : [], message);
  const notes = plan.rejected.map((n) => `Skipped unknown operation "${n}".`);

  if (!plan.operations.length) {
    return res.json({ reply: plan.reply, planned: [], steps: [], notes });
  }
  if (!selected) {
    return res.json({ reply: 'Please select a file in the File Manager first, then tell me what to change.', planned: plan.operations, steps: [], notes });
  }

  const result = await runPipeline(selected.name, plan.operations);
  res.json({ reply: plan.reply, planned: plan.operations, notes, ...result });
}));

// --- Web UI (production build) ----------------------------------------------

if (existsSync(config.webDist)) {
  app.use(express.static(config.webDist));
  app.get(/^\/(?!api|media).*/, (_req, res) => res.sendFile(path.join(config.webDist, 'index.html')));
}

app.use((err, _req, res, _next) => {
  const status = err.status ?? (err.code === 'ENOENT' ? 404 : 500);
  if (status >= 500) console.error(err);
  res.status(status).json({ error: err.code === 'ENOENT' ? 'File not found' : err.message });
});

app.listen(config.port, '127.0.0.1', () => {
  console.log(`PixelSense server on http://127.0.0.1:${config.port}`);
  console.log(`  workspace: ${config.workspace}`);
  console.log(`  engine:    ${config.engine}`);
  console.log(`  model:     ${config.ollamaModel} @ ${config.ollamaUrl}`);
});
