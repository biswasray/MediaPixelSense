import { useEffect, useState } from 'react';
import { api } from '../api.js';

function Dot({ ok, label, title }) {
  return (
    <span className={`status ${ok ? 'ok' : 'bad'}`} title={title}>
      <span className="dot" /> {label}
    </span>
  );
}

export default function StatusBar() {
  const [health, setHealth] = useState(null);

  useEffect(() => {
    let alive = true;
    const load = () => api.health().then((h) => alive && setHealth(h)).catch(() => alive && setHealth(false));
    load();
    const timer = setInterval(load, 15000);
    return () => {
      alive = false;
      clearInterval(timer);
    };
  }, []);

  if (health === null) return <div className="statusbar">Connecting…</div>;
  if (health === false) return <div className="statusbar"><Dot ok={false} label="Server offline" /></div>;

  const { engine, llm } = health;
  const llmOk = llm.reachable && llm.installed;
  const llmTitle = !llm.reachable
    ? 'Ollama is not running. Start it with: ollama serve'
    : !llm.installed
      ? `Model missing. Run: ollama pull ${llm.model}`
      : 'Ready';

  return (
    <div className="statusbar">
      <Dot ok={engine.ok} label={engine.ok ? `Engine · ${engine.operations} ops` : 'Engine missing'} title={engine.error ?? 'Ready'} />
      <Dot ok={llmOk} label={llm.model} title={llmTitle} />
    </div>
  );
}
