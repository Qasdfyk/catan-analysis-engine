// A game record handed over by the page that opened the viewer (step 13a):
// the game ends on catan-project, the player clicks "Review this game", and
// the viewer opens with that game, no file to download and upload.
//
// The two pages are on different origins (localhost and GitHub Pages), so
// they talk by window.postMessage, which works across origins between a page
// and the window it opened. The record never goes over the network or into a
// URL. The protocol, in full (docs/web.md):
//
//   game page:  window.open(viewer + "#/replay?from=opener")
//   viewer   -> opener: {type: "catan-review:ready", version: 1}
//   opener   -> viewer: {type: "catan-review:record", version: 1, record, label?}
//                    or {type: "catan-review:error", version: 1, message}
//
// The viewer speaks first because a message sent to a window that has not
// loaded yet is lost: the game page waits for "ready" before it sends. The
// viewer takes records only from its own opener, but from any origin, so a
// browser extension or another site can use the same door later. A record
// is only data to look at; the engine checks it like any file.

export const READY = 'catan-review:ready';
export const RECORD = 'catan-review:record';
export const ERROR = 'catan-review:error';
export const VERSION = 1;

export type Handoff =
  | { type: typeof RECORD; record: string | ArrayBuffer; label?: string }
  | { type: typeof ERROR; message: string };

/** The parts of `window` the handoff uses, so tests can pass a fake one. */
export interface HandoffWindow {
  opener: { postMessage(message: unknown, targetOrigin: string): void } | null;
  addEventListener(type: 'message', listener: (e: MessageEvent) => void): void;
  removeEventListener(type: 'message', listener: (e: MessageEvent) => void): void;
}

/** "#/replay?from=opener": the viewer was opened to receive a record. */
export function expectsHandoff(hash: string): boolean {
  const query = hash.split('?')[1] ?? '';
  return new URLSearchParams(query).get('from') === 'opener';
}

/** A message of the protocol, or null for anything else (other scripts post messages too). */
export function parseHandoff(data: unknown): Handoff | null {
  if (typeof data !== 'object' || data === null) return null;
  const d = data as Record<string, unknown>;
  if (d.version !== VERSION) return null;
  if (d.type === RECORD && (typeof d.record === 'string' || d.record instanceof ArrayBuffer)) {
    return { type: RECORD, record: d.record, label: typeof d.label === 'string' ? d.label : undefined };
  }
  if (d.type === ERROR && typeof d.message === 'string') return { type: ERROR, message: d.message };
  return null;
}

/**
 * Listens for the opener's answer, then tells the opener the viewer is ready.
 * Returns a function that stops listening, or null when nothing opened this
 * window (a bookmarked link, a reload after the game page closed).
 */
export function receiveFromOpener(onHandoff: (h: Handoff, origin: string) => void, win: HandoffWindow = window): (() => void) | null {
  const opener = win.opener;
  if (!opener) return null;
  const onMessage = (e: MessageEvent) => {
    if (e.source !== opener) return;
    const h = parseHandoff(e.data);
    if (h) onHandoff(h, e.origin);
  };
  // Listen first, then speak: the answer can only come after "ready".
  win.addEventListener('message', onMessage);
  // "*": "ready" carries nothing, and the viewer does not know who opened it.
  opener.postMessage({ type: READY, version: VERSION }, '*');
  return () => win.removeEventListener('message', onMessage);
}
