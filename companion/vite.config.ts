import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  server: {
    // Never let the browser cache dev modules. Without this, a browser
    // that cached a source module (e.g. TrackLane.tsx) keeps revalidating
    // it as "unchanged" and a normal reload silently serves the stale
    // copy — the "new UI in code, old UI in the browser" desync (2026-07).
    headers: {
      'Cache-Control': 'no-store',
    },
  },
})
