import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// In development the UI runs on Vite and forwards API/media calls to the server.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/api': 'http://127.0.0.1:8787',
      '/media': 'http://127.0.0.1:8787',
    },
  },
});
