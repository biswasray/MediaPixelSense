async function request(url, options = {}) {
  const res = await fetch(url, options);
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error ?? `Request failed (${res.status})`);
  return data;
}

const json = (method, body) => ({
  method,
  headers: { 'content-type': 'application/json' },
  body: JSON.stringify(body),
});

const enc = encodeURIComponent;

export const api = {
  health: () => request('/api/health'),
  files: () => request('/api/files'),
  info: (name) => request(`/api/files/${enc(name)}/info`),
  upload(fileList) {
    const form = new FormData();
    for (const f of fileList) form.append('files', f);
    return request('/api/files', { method: 'POST', body: form });
  },
  rename: (name, newName) => request(`/api/files/${enc(name)}`, json('PATCH', { name: newName })),
  remove: (name) => request(`/api/files/${enc(name)}`, { method: 'DELETE' }),
  chat: (file, message, history) => request('/api/chat', json('POST', { file, message, history })),
};

export function mediaUrl(file) {
  return `/media/${enc(file.name)}?v=${Math.round(file.modified)}`;
}

export function formatBytes(n) {
  if (n < 1024) return `${n} B`;
  const units = ['KB', 'MB', 'GB'];
  let i = -1;
  do {
    n /= 1024;
    i++;
  } while (n >= 1024 && i < units.length - 1);
  return `${n.toFixed(n < 10 ? 1 : 0)} ${units[i]}`;
}

export function formatDuration(s) {
  const m = Math.floor(s / 60);
  const sec = (s % 60).toFixed(1).padStart(4, '0');
  return `${m}:${sec}`;
}

export function describeParams(params) {
  return Object.entries(params ?? {})
    .map(([k, v]) => `${k}=${v}`)
    .join(', ');
}
