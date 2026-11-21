# Records from other places

The engine reads one format: catan-project's [game record](https://github.com/Qasdfyk/catan-project/blob/main/docs/game-record.md), version 1. A game from anywhere else is turned into that format first, by an adapter in the web page (`web/src/adapters.ts`), and the engine never learns where it came from.

## Why in TypeScript, before the engine

- Whatever reads a game from another site (a page, a browser extension) runs in JavaScript, so the adapter can run next to it.
- One input format keeps the C++ reader (`src/record.cpp`) and the difference test against catan-project's rules as they are.
- The native tools (`catan_replay`, `catan_winrate`, `catan_setup`) read records on disk; a foreign game is converted once and saved as a record.

## The interface

```ts
interface Adapter {
  name: string;                         // "a catan-project record", shown in messages
  recognizes(doc: unknown): boolean;    // cheap, never throws
  toRecord(doc: unknown): GameRecord;   // throws RecordFormatError with a reason a player can read
}
```

`readRecord(input)` takes text or bytes (plain or gzipped JSON), parses it, asks each adapter in `ADAPTERS` in turn whether it recognizes the document, and returns the record as text for the engine. Every way into the viewer goes through it: a file, a dropped file, an example game, and a record handed over by the game page ([web.md](web.md#review-this-game)).

Today the list holds one adapter, catan-project's own, which checks the version, the players, the board and the moves and passes the text through unchanged. The checks give a message about the game ("it is version 2 of the game record, and this page reads version 1") instead of one about a JSON key from the engine.

## What an adapter for another platform must fill in

A record describes a game completely: the board, the players, every accepted action in order, the outcome of every chance (dice, stolen cards, drawn development cards) and who saw each hidden outcome. A foreign log usually shows only what one player saw: "Bob stole a card from you" without the card. An adapter for such a log cannot fill every outcome, and the review must then fill the unknown ones by sampling, as the ISMCTS bot already does for hidden hands ([mcts.md](mcts.md)). That is part of the move review (step 13), not of the adapter: the adapter's job is to say what it does not know, not to guess it.

No such adapter exists. Records from Colonist need the company's written permission first ([plan.md](plan.md#why-the-colonist-step-is-conditional)); until then the interface is designed and tested on catan-project records only (`web/src/adapters.test.ts`).
