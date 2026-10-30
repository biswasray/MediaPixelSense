import { useEffect, useState } from 'react';
import { api, describeParams, formatBytes, formatDuration, mediaUrl } from '../api.js';

function Media({ file }) {
  const src = mediaUrl(file);
  if (file.kind === 'image') return <img className="media" src={src} alt={file.name} />;
  if (file.kind === 'video') return <video className="media" src={src} controls playsInline key={src} />;
  return <audio className="media-audio" src={src} controls key={src} />;
}

export default function Preview({ file, files, onSelect }) {
  const [info, setInfo] = useState(null);
  const [showBefore, setShowBefore] = useState(false);

  useEffect(() => {
    setInfo(null);
    setShowBefore(false);
    if (!file) return;
    let alive = true;
    api.info(file.name).then((r) => alive && setInfo(r.info)).catch(() => {});
    return () => {
      alive = false;
    };
  }, [file?.name, file?.modified]);

  if (!file) {
    return (
      <section className="panel preview">
        <div className="preview-empty">
          <div className="big-icon">▣</div>
          <p>Select or import a file to preview it.</p>
        </div>
      </section>
    );
  }

  const parent = file.parent ? files.find((f) => f.name === file.parent) : null;
  const shown = showBefore && parent ? parent : file;

  return (
    <section className="panel preview">
      <div className="panel-head">
        <h2 title={file.name}>{shown.name}</h2>
        <div className="head-actions">
          {parent && (
            <button className={`btn ${showBefore ? 'primary' : ''}`} onClick={() => setShowBefore((v) => !v)}>
              {showBefore ? 'Showing before' : 'Compare before'}
            </button>
          )}
          <a className="btn" href={mediaUrl(shown)} download={shown.name}>Download</a>
        </div>
      </div>

      <div className="stage">
        <Media file={shown} />
      </div>

      <div className="details">
        {info && (
          <>
            {info.width > 0 && <span>{info.width} × {info.height}</span>}
            {info.duration > 0 && <span>{formatDuration(info.duration)}</span>}
            {info.kind === 'video' && <span>{info.hasAudio ? 'with audio' : 'no audio'}</span>}
          </>
        )}
        <span>{formatBytes(file.size)}</span>
      </div>

      {file.parent && (
        <div className="lineage">
          <span>
            Edited from{' '}
            {parent ? (
              <button className="link" onClick={() => onSelect(parent.name)}>{parent.name}</button>
            ) : (
              <em>{file.parent} (deleted)</em>
            )}
          </span>
          <div className="chips">
            {file.operations?.map((op, i) => (
              <span className="chip" key={i} title={describeParams(op.params)}>
                {op.name}
                {Object.keys(op.params).length > 0 && <small> {describeParams(op.params)}</small>}
              </span>
            ))}
          </div>
        </div>
      )}
    </section>
  );
}
