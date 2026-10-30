// Turns a natural-language edit request into a list of engine operations
// using a local Qwen model served by Ollama. The model is constrained to a
// JSON schema whose operation names come from the engine itself.

import { config } from './config.js';

function describeParam(p) {
  const range = p.choices ? p.choices.join('|') : p.min !== undefined ? `${p.min}..${p.max}` : '';
  const bits = [p.type === 'enum' ? 'one of' : p.type, range, p.required ? 'required' : p.default !== undefined ? `default ${p.default}` : 'optional']
    .filter(Boolean)
    .join(' ');
  return `${p.name} (${bits}): ${p.description}`;
}

function describeOps(ops) {
  return ops
    .map((op) => {
      const params = op.params.length ? `\n    ${op.params.map(describeParam).join('\n    ')}` : '';
      return `- ${op.name}: ${op.description}${params}`;
    })
    .join('\n');
}

function describeFile(file) {
  if (!file) return 'No file is selected. Tell the user to pick a file in the File Manager before editing.';
  const { name, info } = file;
  const size = info.width ? `${info.width}x${info.height}px` : '';
  const timing = info.kind === 'video' ? `, ${info.duration.toFixed(2)}s long, ${info.hasAudio ? 'with' : 'without'} audio` : '';
  return `The selected file is "${name}": a ${info.kind}, ${size}${timing}.`;
}

function systemPrompt(ops, file) {
  return `You are PixelSense, an assistant inside a photo and video editor.
You edit the selected file by choosing operations from this list. Use only these names and parameters:
${describeOps(ops)}

${describeFile(file)}

Reply with JSON: {"reply": "<one short friendly sentence>", "operations": [{"name": "<operation>", "params": {...}}]}
Rules:
- Operations run in order; each one works on the result of the previous one.
- Use exact pixel and second values computed from the file details above.
- Leave out parameters the user did not ask about; defaults are applied.
- If the request is a question, unclear, or impossible with these operations, return "operations": [] and explain in "reply".`;
}

// Few-shot turns. Small models follow the format far more reliably with them.
function examples(kind) {
  const shots = [
    ['make it black and white and rotate it left', { reply: 'Converted to black and white and rotated 90° counter-clockwise.', operations: [{ name: 'grayscale', params: {} }, { name: 'rotate', params: { degrees: -90 } }] }],
    ['what can you do?', { reply: 'I can resize, crop, rotate, adjust colors, add captions and more. Just describe the edit.', operations: [] }],
  ];
  if (kind === 'video') {
    shots.push(['keep 2s to 5s and make it twice as fast', { reply: 'Trimmed to 2–5s and doubled the speed.', operations: [{ name: 'trim', params: { start: 2, end: 5 } }, { name: 'speed', params: { factor: 2 } }] }]);
  } else {
    shots.push(['brighter, and add the caption "Summer 2024" at the top', { reply: 'Brightened the image and added your caption at the top.', operations: [{ name: 'brightness', params: { amount: 0.15 } }, { name: 'text', params: { text: 'Summer 2024', position: 'top' } }] }]);
  }
  return shots.flatMap(([user, answer]) => [
    { role: 'user', content: user },
    { role: 'assistant', content: JSON.stringify(answer) },
  ]);
}

function responseSchema(ops) {
  return {
    type: 'object',
    properties: {
      reply: { type: 'string' },
      operations: {
        type: 'array',
        items: {
          type: 'object',
          properties: {
            name: { type: 'string', enum: ops.map((o) => o.name) },
            params: { type: 'object' },
          },
          required: ['name', 'params'],
        },
      },
    },
    required: ['reply', 'operations'],
  };
}

// Coerces whatever the model produced into [{ name, params }] with scalar
// parameter values. Unknown operations are reported back, not executed.
function normalizePlan(raw, ops) {
  const known = new Map(ops.map((o) => [o.name, o]));
  const operations = [];
  const rejected = [];
  for (const item of Array.isArray(raw?.operations) ? raw.operations : []) {
    const name = String(item?.name ?? item?.op ?? item?.operation ?? '').toLowerCase().trim();
    if (!known.has(name)) {
      if (name) rejected.push(name);
      continue;
    }
    const params = {};
    for (const [k, v] of Object.entries(item.params ?? {})) {
      if (['string', 'number', 'boolean'].includes(typeof v)) params[k] = String(v);
    }
    operations.push({ name, params });
  }
  return { reply: typeof raw?.reply === 'string' ? raw.reply : '', operations, rejected };
}

async function ollama(pathname, body) {
  let res;
  try {
    res = await fetch(`${config.ollamaUrl}${pathname}`, {
      method: body ? 'POST' : 'GET',
      headers: { 'content-type': 'application/json' },
      body: body ? JSON.stringify(body) : undefined,
    });
  } catch {
    throw new Error(`Cannot reach Ollama at ${config.ollamaUrl}. Is it running? (ollama serve)`);
  }
  if (!res.ok) {
    const detail = await res.text();
    if (res.status === 404 && detail.includes('not found')) {
      throw new Error(`Model "${config.ollamaModel}" is not installed. Run: ollama pull ${config.ollamaModel}`);
    }
    throw new Error(`Ollama error ${res.status}: ${detail}`);
  }
  return res.json();
}

export async function modelStatus() {
  try {
    const tags = await ollama('/api/tags');
    const names = (tags.models ?? []).map((m) => m.name);
    const wanted = config.ollamaModel.includes(':') ? config.ollamaModel : `${config.ollamaModel}:latest`;
    return { reachable: true, model: config.ollamaModel, installed: names.includes(wanted) };
  } catch (err) {
    return { reachable: false, model: config.ollamaModel, installed: false, error: err.message };
  }
}

/**
 * @param allOps   operation list from the engine
 * @param file     { name, info } of the selected file, or null
 * @param history  previous [{ role, content }] turns (plain text)
 * @param message  the user's new message
 */
export async function planEdits(allOps, file, history, message) {
  const kind = file?.info.kind;
  const ops = allOps.filter((op) => kind !== 'image' || op.target !== 'video');

  const messages = [
    { role: 'system', content: systemPrompt(ops, file) },
    ...examples(kind),
    ...history.slice(-8).map((m) => ({ role: m.role === 'assistant' ? 'assistant' : 'user', content: String(m.content) })),
    { role: 'user', content: message },
  ];

  const body = {
    model: config.ollamaModel,
    messages,
    format: responseSchema(ops),
    stream: false,
    options: { temperature: 0.1, num_ctx: 8192 },
  };
  if (/qwen3/i.test(config.ollamaModel)) body.think = false;

  const result = await ollama('/api/chat', body);
  const content = result.message?.content ?? '';
  let parsed;
  try {
    parsed = JSON.parse(content);
  } catch {
    return { reply: content || 'Sorry, I could not understand that.', operations: [], rejected: [] };
  }
  return normalizePlan(parsed, ops);
}
