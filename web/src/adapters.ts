// Records from anywhere, turned into the one format the engine reads.
//
// The engine (src/record.cpp) reads exactly one format: catan-project's game
// record, version 1 (docs/game-record.md in catan-project). A record from
// another place goes through an adapter first, here in the page, and the
// engine never learns where it came from. Adapters live in TypeScript, not
// C++, because whatever reads a foreign game (a page, a browser extension)
// runs in JavaScript anyway, and one input format keeps the engine and its
// difference test small. The contract is in docs/converters.md.
//
// Every way into the viewer (a file, a dropped file, an example, a record
// sent by the game page) goes through readRecord().

import type { GameRecord } from './types';

/** A record that cannot be read, with a reason a player can understand. */
export class RecordFormatError extends Error {}

export interface Adapter {
  /** Shown in messages: "a catan-project record". */
  name: string;
  /** Whether a parsed document is in this adapter's format. Cheap, never throws. */
  recognizes(doc: unknown): boolean;
  /**
   * The document as a game record the engine reads. Throws RecordFormatError
   * when it is this format but cannot be used (a newer version, a part missing).
   */
  toRecord(doc: unknown): GameRecord;
}

/** Plain JSON or gzip (what the platform downloads), told apart by the first two bytes. */
export async function readRecordBytes(bytes: ArrayBuffer): Promise<string> {
  const head = new Uint8Array(bytes, 0, Math.min(2, bytes.byteLength));
  if (head[0] === 0x1f && head[1] === 0x8b) {
    const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'));
    return await new Response(stream).text();
  }
  return new TextDecoder().decode(bytes);
}

const isObject = (x: unknown): x is Record<string, unknown> => typeof x === 'object' && x !== null && !Array.isArray(x);

/** The engine's own format: checked, passed through unchanged. */
export const catanProject: Adapter = {
  name: 'a catan-project record',
  recognizes: (doc) => isObject(doc) && doc.format === 'catan-game-record',
  toRecord(doc) {
    const r = doc as Record<string, unknown>;
    if (r.version !== 1) {
      throw new RecordFormatError(`it is version ${String(r.version)} of the game record, and this page reads version 1`);
    }
    // The engine would refuse these too, but with a message about JSON keys.
    const players = r.players;
    if (!Array.isArray(players) || players.length < 2 || players.length > 4) {
      throw new RecordFormatError('it does not list 2 to 4 players');
    }
    if (!isObject(r.board) || !Array.isArray(r.board.tiles) || r.board.tiles.length !== 19) {
      throw new RecordFormatError('its board does not have 19 tiles');
    }
    if (!Array.isArray(r.events)) throw new RecordFormatError('it has no list of moves');
    const bad = r.events.findIndex((e) => !isObject(e) || typeof e.action !== 'string' || typeof e.seat !== 'number');
    if (bad >= 0) throw new RecordFormatError(`move ${bad + 1} has no action or no seat`);
    return doc as GameRecord;
  },
};

/** Known formats, tried in order. A new platform adds its adapter here. */
export const ADAPTERS: readonly Adapter[] = [catanProject];

export interface ReadRecord {
  /** The record as text, ready for the engine and its workers. */
  text: string;
  record: GameRecord;
  /** Which adapter read it. */
  from: string;
}

/**
 * Bytes (plain or gzipped) or text, in any known format, as a record the
 * engine reads. Throws RecordFormatError saying what is wrong.
 */
export async function readRecord(input: string | ArrayBuffer, adapters: readonly Adapter[] = ADAPTERS): Promise<ReadRecord> {
  const text = typeof input === 'string' ? input : await readRecordBytes(input);
  let doc: unknown;
  try {
    doc = JSON.parse(text);
  } catch {
    throw new RecordFormatError('it is not JSON');
  }
  const adapter = adapters.find((a) => a.recognizes(doc));
  if (!adapter) {
    throw new RecordFormatError(`it is not a game record this page knows (it reads ${adapters.map((a) => a.name).join(', ')})`);
  }
  const record = adapter.toRecord(doc);
  // The engine's own format keeps its text: no second copy of a large game.
  return { text: record === doc ? text : JSON.stringify(record), record, from: adapter.name };
}
