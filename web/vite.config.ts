import { fileURLToPath } from 'node:url';
import { defineConfig } from 'vitest/config';

// `@engine` is the module Emscripten writes into src/wasm/ (the `wasm` CMake
// preset); src/wasm-module.d.ts gives it types.
export default defineConfig({
  // Relative asset paths, so the built page works from any folder, including
  // GitHub Pages under /catan-analysis-engine/.
  base: './',
  resolve: {
    alias: { '@engine': fileURLToPath(new URL('./src/wasm/catan_engine.js', import.meta.url)) },
  },
  worker: { format: 'es' },
  test: { environment: 'node' },
});
