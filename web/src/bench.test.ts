import { describe, expect, it } from 'vitest';
import { startWorker } from './bench';

/** Stands in for a Web Worker: answers each message as `reply` says. */
class FakeWorker {
  onmessage: ((msg: { data: unknown }) => void) | null = null;
  onerror: ((err: { message: string; preventDefault(): void }) => void) | null = null;
  terminated = false;
  constructor(private reply: 'ready' | 'load-error' | 'engine-error') {}
  postMessage() {
    queueMicrotask(() => {
      if (this.reply === 'load-error') this.onerror?.({ message: '', preventDefault() {} });
      else if (this.reply === 'engine-error') this.onmessage?.({ data: { error: 'RuntimeError: unreachable' } });
      else this.onmessage?.({ data: { ready: true } });
    });
  }
  terminate() {
    this.terminated = true;
  }
}

describe('startWorker', () => {
  it('tries again when the worker script fails to load once', async () => {
    const made: FakeWorker[] = [];
    const replies = ['load-error', 'ready'] as const;
    const make = () => {
      const w = new FakeWorker(replies[made.length]);
      made.push(w);
      return w as unknown as Worker;
    };
    const worker = await startWorker(make);
    expect(made).toHaveLength(2);
    expect(made[0].terminated).toBe(true);
    expect(worker).toBe(made[1]);
  });
  it('gives up after two failed loads', async () => {
    await expect(startWorker(() => new FakeWorker('load-error') as unknown as Worker)).rejects.toThrow('could not be loaded');
  });
  it('does not retry an error of the engine itself, and shows its message', async () => {
    let made = 0;
    const make = () => (made++, new FakeWorker('engine-error') as unknown as Worker);
    await expect(startWorker(make)).rejects.toThrow('RuntimeError: unreachable');
    expect(made).toBe(1);
  });
});
