// Reading records through the adapters. The engine's own test (engine.test.ts)
// opens every shipped record through readRecord(); these check the errors and
// the contract an adapter for another platform relies on.

import { readFileSync } from 'node:fs';
import { gzipSync } from 'node:zlib';
import { describe, expect, it } from 'vitest';
import { type Adapter, RecordFormatError, catanProject, readRecord } from './adapters';
import type { GameRecord } from './types';

const text = readFileSync(new URL('../../tests/data/simulation-seed1.json', import.meta.url), 'utf8');
const record = JSON.parse(text) as GameRecord;
const bytes = (b: Uint8Array) => b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength) as ArrayBuffer;

/** A record with one part changed, as text. */
const changed = (change: (r: Record<string, unknown>) => void) => {
  const r = structuredClone(record) as unknown as Record<string, unknown>;
  change(r);
  return JSON.stringify(r);
};

const reason = async (input: string | ArrayBuffer, adapters?: Adapter[]) => {
  try {
    await readRecord(input, adapters);
  } catch (e) {
    expect(e).toBeInstanceOf(RecordFormatError);
    return (e as Error).message;
  }
  throw new Error('read without an error');
};

describe('readRecord', () => {
  it('reads text, plain bytes and gzip to the same record', async () => {
    const plain = await readRecord(bytes(new TextEncoder().encode(text)));
    const zipped = await readRecord(bytes(gzipSync(text)));
    expect(plain.text).toBe(text);
    expect(zipped.text).toBe(text);
    expect((await readRecord(text)).record.events.length).toBe(record.events.length);
  });

  it('says why a file is not a record', async () => {
    expect(await reason('not json')).toBe('it is not JSON');
    expect(await reason('{"format": "chess-pgn"}')).toMatch(/not a game record this page knows \(it reads a catan-project record\)/);
    expect(await reason(changed((r) => (r.version = 2)))).toBe('it is version 2 of the game record, and this page reads version 1');
    expect(await reason(changed((r) => (r.players = [])))).toBe('it does not list 2 to 4 players');
    expect(await reason(changed((r) => ((r.board as { tiles: unknown[] }).tiles.length = 18)))).toBe('its board does not have 19 tiles');
    expect(await reason(changed((r) => delete r.events))).toBe('it has no list of moves');
    expect(await reason(changed((r) => ((r.events as unknown[])[3] = { seat: 0 })))).toBe('move 4 has no action or no seat');
  });

  it('runs a foreign format through its adapter and hands the engine the converted text', async () => {
    // A made-up wrapper, only to exercise the interface: a real one comes with
    // a real platform (docs/converters.md).
    const wrapped: Adapter = {
      name: 'a wrapped record',
      recognizes: (doc) => typeof doc === 'object' && doc !== null && 'wrapped' in doc,
      toRecord: (doc) => catanProject.toRecord((doc as { wrapped: unknown }).wrapped),
    };
    const read = await readRecord(JSON.stringify({ wrapped: record }), [catanProject, wrapped]);
    expect(read.from).toBe('a wrapped record');
    expect(JSON.parse(read.text)).toEqual(record);
    // The converted record goes through the same checks as a native one.
    expect(await reason(JSON.stringify({ wrapped: { ...record, version: 3 } }), [wrapped])).toMatch(/version 3/);
  });
});
