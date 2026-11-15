// Entry point: mounts the page and starts compiling the WebAssembly module
// right away, so the first replay or benchmark finds it ready.

import { createRoot } from 'react-dom/client';
import { App } from './App';
import { loadEngine } from './engine';
import './style.css';

createRoot(document.getElementById('root')!).render(<App />);
void loadEngine().catch((e) => console.error('Cannot load the engine:', e));
