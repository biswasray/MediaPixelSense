import { useEffect, useRef, useState } from 'react';
import { api, describeParams } from '../api.js';

const SUGGESTIONS = {
  image: ['Make it black and white', 'Crop to a centered square', 'Brighten it a little and add more contrast', 'Add the caption "Hello" at the bottom'],
  video: ['Keep only the first 5 seconds', 'Make it 2x faster', 'Fade in and out', 'Turn it into a GIF'],
  audio: [],
  none: ['What can you do?'],
};

let nextId = 1;

function Message({ msg, onOpen }) {
  if (msg.role === 'user') return <div className="msg user">{msg.content}</div>;
  return (
    <div className={`msg assistant ${msg.error ? 'has-error' : ''}`}>
      {msg.content && <p>{msg.content}</p>}
      {msg.steps?.length > 0 && (
        <ol className="steps">
          {msg.steps.map((s, i) => (
            <li key={i}>
              <code>{s.name}</code>
              {Object.keys(s.params).length > 0 && <span className="params">{describeParams(s.params)}</span>}
              {s.warnings?.map((w) => <div className="warn" key={w}>⚠ {w}</div>)}
            </li>
          ))}
        </ol>
      )}
      {msg.notes?.map((n) => <div className="warn" key={n}>⚠ {n}</div>)}
      {msg.error && <div className="error">✕ {msg.error}</div>}
      {msg.output && (
        <button className="link result" onClick={() => onOpen(msg.output)}>
          Saved as {msg.output}
        </button>
      )}
    </div>
  );
}

export default function ChatPanel({ file, onEdited }) {
  const [messages, setMessages] = useState([
    {
      id: 0,
      role: 'assistant',
      content: 'Hi! Select a file and tell me how to edit it, e.g. "crop to a square and make it brighter". Each edit is saved as a new file, so your original is never changed.',
    },
  ]);
  const [input, setInput] = useState('');
  const [busy, setBusy] = useState(false);
  const listRef = useRef(null);

  useEffect(() => {
    listRef.current?.scrollTo({ top: listRef.current.scrollHeight, behavior: 'smooth' });
  }, [messages, busy]);

  async function send(text) {
    const message = text.trim();
    if (!message || busy) return;
    setInput('');
    const history = messages.filter((m) => m.id !== 0).map((m) => ({ role: m.role, content: m.content }));
    setMessages((m) => [...m, { id: nextId++, role: 'user', content: message }]);
    setBusy(true);
    try {
      const res = await api.chat(file?.name ?? null, message, history);
      setMessages((m) => [...m, { id: nextId++, role: 'assistant', ...res, content: res.reply }]);
      if (res.output) onEdited(res.output);
    } catch (err) {
      setMessages((m) => [...m, { id: nextId++, role: 'assistant', content: '', error: err.message }]);
    } finally {
      setBusy(false);
    }
  }

  const suggestions = SUGGESTIONS[file?.kind ?? 'none'];

  return (
    <aside className="panel chat">
      <div className="panel-head">
        <h2>Edit with AI</h2>
        <span className="context" title={file?.name}>{file ? `Editing: ${file.name}` : 'No file selected'}</span>
      </div>

      <div className="messages" ref={listRef}>
        {messages.map((msg) => <Message key={msg.id} msg={msg} onOpen={onEdited} />)}
        {busy && (
          <div className="msg assistant typing">
            <span /><span /><span />
          </div>
        )}
      </div>

      {suggestions.length > 0 && (
        <div className="suggestions">
          {suggestions.map((s) => (
            <button key={s} onClick={() => send(s)} disabled={busy}>{s}</button>
          ))}
        </div>
      )}

      <form
        className="composer"
        onSubmit={(e) => {
          e.preventDefault();
          send(input);
        }}
      >
        <textarea
          rows={2}
          placeholder={file ? 'Describe an edit…' : 'Select a file to start editing'}
          value={input}
          onChange={(e) => setInput(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter' && !e.shiftKey) {
              e.preventDefault();
              send(input);
            }
          }}
        />
        <button className="btn primary" disabled={busy || !input.trim()}>Send</button>
      </form>
    </aside>
  );
}
