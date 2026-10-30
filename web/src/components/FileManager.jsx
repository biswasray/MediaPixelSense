import { useRef, useState } from 'react';
import { api, formatBytes, mediaUrl } from '../api.js';

const FILTERS = [
  ['all', 'All'],
  ['image', 'Images'],
  ['video', 'Videos'],
  ['audio', 'Audio'],
];

function Thumb({ file }) {
  if (file.kind === 'image') return <img className="thumb" src={mediaUrl(file)} alt="" loading="lazy" />;
  if (file.kind === 'video') {
    return <video className="thumb" src={`${mediaUrl(file)}#t=0.5`} preload="metadata" muted playsInline />;
  }
  return <div className="thumb thumb-audio">♪</div>;
}

export default function FileManager({ files, selected, onSelect, onChanged, onRenamed, onError }) {
  const [filter, setFilter] = useState('all');
  const [query, setQuery] = useState('');
  const [dragging, setDragging] = useState(false);
  const [uploading, setUploading] = useState(false);
  const [renaming, setRenaming] = useState(null);
  const inputRef = useRef(null);

  const visible = files.filter(
    (f) => (filter === 'all' || f.kind === filter) && f.name.toLowerCase().includes(query.toLowerCase()),
  );

  async function upload(list) {
    if (!list?.length) return;
    setUploading(true);
    try {
      const { saved, skipped } = await api.upload(list);
      await onChanged();
      if (saved.length) onSelect(saved[0]);
      if (skipped.length) onError(`Unsupported file type: ${skipped.join(', ')}`);
    } catch (err) {
      onError(err.message);
    } finally {
      setUploading(false);
    }
  }

  async function remove(file) {
    if (!confirm(`Delete "${file.name}"? This cannot be undone.`)) return;
    try {
      await api.remove(file.name);
      if (selected === file.name) onSelect(file.parent ?? null);
      await onChanged();
    } catch (err) {
      onError(err.message);
    }
  }

  async function commitRename(file, value) {
    setRenaming(null);
    const ext = file.name.slice(file.name.lastIndexOf('.'));
    const newName = value.trim().endsWith(ext) ? value.trim() : `${value.trim()}${ext}`;
    if (!value.trim() || newName === file.name) return;
    try {
      const { name } = await api.rename(file.name, newName);
      onRenamed(file.name, name);
    } catch (err) {
      onError(err.message);
    }
  }

  return (
    <aside
      className={`panel files ${dragging ? 'dragging' : ''}`}
      onDragOver={(e) => {
        e.preventDefault();
        setDragging(true);
      }}
      onDragLeave={(e) => e.currentTarget.contains(e.relatedTarget) || setDragging(false)}
      onDrop={(e) => {
        e.preventDefault();
        setDragging(false);
        upload(e.dataTransfer.files);
      }}
    >
      <div className="panel-head">
        <h2>Files</h2>
        <button className="btn primary" onClick={() => inputRef.current.click()} disabled={uploading}>
          {uploading ? 'Uploading…' : '+ Import'}
        </button>
        <input
          ref={inputRef}
          type="file"
          multiple
          accept="image/*,video/*,audio/*"
          hidden
          onChange={(e) => {
            upload(e.target.files);
            e.target.value = '';
          }}
        />
      </div>

      <input className="search" placeholder="Search files" value={query} onChange={(e) => setQuery(e.target.value)} />
      <div className="tabs">
        {FILTERS.map(([key, label]) => (
          <button key={key} className={filter === key ? 'active' : ''} onClick={() => setFilter(key)}>
            {label}
          </button>
        ))}
      </div>

      <ul className="file-list">
        {visible.map((file) => (
          <li
            key={file.name}
            className={file.name === selected ? 'selected' : ''}
            onClick={() => onSelect(file.name)}
            onDoubleClick={() => setRenaming(file.name)}
          >
            <Thumb file={file} />
            <div className="file-meta">
              {renaming === file.name ? (
                <input
                  className="rename"
                  autoFocus
                  defaultValue={file.name.slice(0, file.name.lastIndexOf('.'))}
                  onClick={(e) => e.stopPropagation()}
                  onBlur={(e) => commitRename(file, e.target.value)}
                  onKeyDown={(e) => {
                    if (e.key === 'Enter') e.target.blur();
                    if (e.key === 'Escape') setRenaming(null);
                  }}
                />
              ) : (
                <span className="file-name" title={file.name}>{file.name}</span>
              )}
              <span className="file-sub">
                <span className={`badge ${file.kind}`}>{file.kind}</span>
                {formatBytes(file.size)}
                {file.parent && <span className="derived" title={`Edited from ${file.parent}`}>↳ edit</span>}
              </span>
            </div>
            <div className="file-actions" onClick={(e) => e.stopPropagation()}>
              <button title="Rename" onClick={() => setRenaming(file.name)}>✎</button>
              <button title="Delete" onClick={() => remove(file)}>🗑</button>
            </div>
          </li>
        ))}
        {!visible.length && (
          <li className="empty">
            {files.length ? 'No matching files.' : 'Drop images or videos here, or click Import.'}
          </li>
        )}
      </ul>
    </aside>
  );
}
