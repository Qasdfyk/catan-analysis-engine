// The handoff between the game page and the viewer, with fake windows: what
// the viewer accepts, and that it listens before it says it is ready.

import { describe, expect, it } from 'vitest';
import { type HandoffWindow, expectsHandoff, parseHandoff, receiveFromOpener } from './handoff';

function fakeWindow(withOpener = true) {
  const sent: unknown[] = [];
  const listeners: ((e: MessageEvent) => void)[] = [];
  const opener = { postMessage: (m: unknown) => sent.push({ message: m, listening: listeners.length }) };
  const win: HandoffWindow = {
    opener: withOpener ? opener : null,
    addEventListener: (_, l) => listeners.push(l),
    removeEventListener: (_, l) => listeners.splice(listeners.indexOf(l), 1),
  };
  const deliver = (data: unknown, source: unknown = opener, origin = 'http://localhost:5173') =>
    listeners.forEach((l) => l({ data, source, origin } as MessageEvent));
  return { win, opener, sent, listeners, deliver };
}

describe('handoff', () => {
  it('waits for a record only when the link asks for it', () => {
    expect(expectsHandoff('#/replay?from=opener')).toBe(true);
    expect(expectsHandoff('#/replay')).toBe(false);
    expect(expectsHandoff('#/replay?from=elsewhere')).toBe(false);
  });

  it('accepts only the messages of the protocol', () => {
    expect(parseHandoff({ type: 'catan-review:record', version: 1, record: '{}', label: 'Game r1', seat: 2 })).toEqual({
      type: 'catan-review:record',
      record: '{}',
      label: 'Game r1',
      seat: 2,
    });
    // A seat that cannot be one is dropped, not trusted.
    expect(parseHandoff({ type: 'catan-review:record', version: 1, record: '{}', seat: 7 })).toMatchObject({ seat: undefined });
    expect(parseHandoff({ type: 'catan-review:record', version: 1, record: '{}', seat: '1' })).toMatchObject({ seat: undefined });
    expect(parseHandoff({ type: 'catan-review:error', version: 1, message: 'gone' })).toEqual({ type: 'catan-review:error', message: 'gone' });
    expect(parseHandoff({ type: 'catan-review:record', version: 2, record: '{}' })).toBeNull();
    expect(parseHandoff({ type: 'catan-review:record', version: 1, record: 42 })).toBeNull();
    expect(parseHandoff('catan-review:record')).toBeNull();
    expect(parseHandoff(null)).toBeNull();
  });

  it('listens before it tells the opener it is ready', () => {
    const { win, sent } = fakeWindow();
    receiveFromOpener(() => {}, win);
    expect(sent).toEqual([{ message: { type: 'catan-review:ready', version: 1 }, listening: 1 }]);
  });

  it('takes the record from its opener and from no one else', () => {
    const { win, deliver } = fakeWindow();
    const got: unknown[] = [];
    const stop = receiveFromOpener((h, origin) => got.push([h, origin]), win)!;
    const message = { type: 'catan-review:record', version: 1, record: '{}' };
    deliver(message, { postMessage() {} }); // another window
    deliver({ type: 'something-else' }); // another script on the opener
    deliver(message);
    expect(got).toEqual([[{ type: 'catan-review:record', record: '{}', label: undefined, seat: undefined }, 'http://localhost:5173']]);
    stop();
    deliver(message);
    expect(got).toHaveLength(1);
  });

  it('says so when nothing opened the window', () => {
    const { win, listeners } = fakeWindow(false);
    expect(receiveFromOpener(() => {}, win)).toBeNull();
    expect(listeners).toHaveLength(0);
  });
});
