import { useCallback, useEffect, useState } from 'react';
import { api } from './api.js';
import FileManager from './components/FileManager.jsx';
import Preview from './components/Preview.jsx';
import ChatPanel from './components/ChatPanel.jsx';
import StatusBar from './components/StatusBar.jsx';

export default function App() {
  const [files, setFiles] = useState([]);
  const [selectedName, setSelectedName] = useState(null);
  const [error, setError] = useState(null);

  const refresh = useCallback(async () => {
    try {
      const list = await api.files();
      setFiles(list);
      return list;
    } catch (err) {
      setError(err.message);
      return [];
    }
  }, []);

  useEffect(() => {
    refresh().then((list) => setSelectedName((cur) => cur ?? list[0]?.name ?? null));
  }, [refresh]);

  const selected = files.find((f) => f.name === selectedName) ?? null;

  // After an edit, jump to the result so the next instruction builds on it.
  const handleEdited = useCallback(
    async (outputName) => {
      await refresh();
      setSelectedName(outputName);
    },
    [refresh],
  );

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <span className="logo">◆</span> MediaPixelSense
        </div>
        <StatusBar />
      </header>
      {error && (
        <div className="banner" onClick={() => setError(null)}>
          {error} <span className="dismiss">dismiss</span>
        </div>
      )}
      <main className="workspace">
        <FileManager
          files={files}
          selected={selectedName}
          onSelect={setSelectedName}
          onChanged={refresh}
          onRenamed={(oldName, newName) => {
            refresh();
            if (selectedName === oldName) setSelectedName(newName);
          }}
          onError={setError}
        />
        <Preview file={selected} files={files} onSelect={setSelectedName} />
        <ChatPanel file={selected} onEdited={handleEdited} />
      </main>
    </div>
  );
}
