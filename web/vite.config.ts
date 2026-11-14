import { fileURLToPath } from 'node:url';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vitest/config';

// The board is catan-project's own Board component, imported from the
// checkout next to this repo (as scripts/check.sh already expects for the
// difference test), so the viewer and the game can never look different.
const platform = fileURLToPath(new URL('../../catan-project/frontend/src', import.meta.url));

export default defineConfig({
  plugins: [react()],
  // Relative asset paths, so the built page works from any folder, including
  // GitHub Pages under /catan-analysis-engine/.
  base: './',
  resolve: {
    alias: {
      // The module Emscripten writes (the `wasm` CMake preset); typed in src/wasm-module.d.ts.
      '@engine': fileURLToPath(new URL('./src/wasm/catan_engine.js', import.meta.url)),
      '@platform': platform,
    },
    // catan-project's files would otherwise find React in catan-project's
    // node_modules: two copies of React on one page break hooks.
    dedupe: ['react', 'react-dom'],
  },
  // The dev server only serves files under these folders.
  server: { fs: { allow: ['.', platform] } },
  worker: { format: 'es' },
  test: { environment: 'node' },
});
