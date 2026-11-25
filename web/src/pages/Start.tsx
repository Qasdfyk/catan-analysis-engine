// The landing page, for players: was it the dice or your decisions? The
// example game replays on the board while its review, computed ahead of time
// (scripts/review-example.mjs), follows along: the chance curve, the luck
// split, and the heat map of the setup while the settlements go down.

import { useEffect, useState } from 'react';
import { PLAYER_COLORS } from '@platform/utils/labels';
import { GameBoard } from '../components/GameBoard';
import { SetupHeat, SetupPanel } from '../components/SetupReview';
import { KeyMoves } from '../components/MoveReview';
import { LuckPanel, WinChart } from '../components/WinChances';
import { exampleRecord, type OpenGame } from '../engine';
import { CORRECTNESS, PLATFORM_URL, REPO_URL, SPEED } from '../facts';
import { useOpenGame, usePlayback } from '../hooks';
import { reviewSeat } from '../nav';
import { exampleReview, type ExampleReview } from '../review';
import { focusMove } from '../setup';
import type { Topology } from '../types';
import { fmt } from '../components/Charts';

function LiveBoard({ game, topo, pos, review }: { game: OpenGame; topo: Topology; pos: number; review: ExampleReview | null }) {
  const state = game.state(pos);
  const players = game.record.players;
  // While a setup settlement is shown, the board says how good each corner was.
  const m = review ? focusMove(review.setup.moves, pos) : null;
  const setupReview = m === null ? undefined : review?.setup.reviews[m];
  const heat =
    review && m !== null && setupReview ? <SetupHeat review={setupReview} move={review.setup.moves[m]} topo={topo} players={players} /> : null;
  return (
    <a className="live-board" href="#/replay" title="Open this game in the replay viewer">
      <GameBoard game={game} topo={topo} pos={pos} overlay={heat} />
      <div className="live-caption">
        <span className="live-dot" /> {heat ? 'Reviewing the setup' : 'Replaying the example game'} · turn {state.turn}
      </div>
      <div className="live-score">
        {state.players.map((p, seat) => (
          <span key={seat} className={state.current === seat ? 'on' : ''}>
            <span className="chip" style={{ background: PLAYER_COLORS[players[seat].color] }} />
            {players[seat].name} <b>{p.points}</b>
          </span>
        ))}
      </div>
    </a>
  );
}

const HOW = [
  {
    title: 'Play',
    text: 'A game on catan-project, against friends or bots. Nothing watches you during the game: the review only ever looks at finished games.',
  },
  {
    title: 'Click "Review this game"',
    text: 'The game page hands its record to this page, tab to tab. It is not uploaded anywhere: the review runs on your computer.',
  },
  {
    title: 'See where it was won',
    text: 'The chance to win after every move, how much of it the dice gave or took, how your opening settlements compare with every other corner, and every later decision graded against the best move, with why.',
  },
];

const BUILDING_BLOCKS = [
  {
    icon: '⬡',
    title: 'Rules as bitmasks',
    text: `The rules are C++20 with the board as 64- and 128-bit masks: ${fmt(SPEED[2].gamesPerSecond)} whole games a second on one core, which is what playing out thousands of games per move needs.`,
  },
  {
    icon: '≡',
    title: 'Checked against the original',
    text: `A difference test runs the engine and catan-project's Python rules side by side after every action. It covers ${Math.round(CORRECTNESS.coverage * 100)}% of the Python rules and caught ${CORRECTNESS.mutantsCaught} of ${CORRECTNESS.mutants} planted bugs.`,
  },
  {
    icon: '◈',
    title: 'Same engine in the browser',
    text: 'Emscripten compiles the same C++ to WebAssembly, and Web Workers run the playouts in parallel, so the review needs no server.',
  },
];

const ROADMAP = [
  { done: true, title: 'Rules engine in the browser', text: "C++20, difference-tested against the Python game, built to WebAssembly with each player's chance to win" },
  { done: true, title: 'Bots and search', text: "A zoo of bot styles, an SPRT arena, and an ISMCTS bot that sees only its own cards, now the game's expert level" },
  { done: true, title: 'Review after the game', text: 'Finish a game on catan-project, click "Review this game", and the review opens here' },
  { done: true, title: 'Move review', text: 'Every decision graded against the best move, judged only on what the player could see, with a short why' },
  { done: false, title: 'How strong a player is', text: 'An accuracy score from the grades, checked on games of people' },
  { done: false, title: 'Value network', text: 'A learned evaluation of positions, trained on self-play only' },
  { done: false, title: 'Colonist games', text: "Only with Colonist's written permission, which its Terms require" },
];

export function Start() {
  const [text, setText] = useState<string | null>(null);
  const [review, setReview] = useState<ExampleReview | null>(null);
  useEffect(() => {
    exampleRecord().then(setText, () => {});
    exampleReview().then(setReview, () => {});
  }, []);
  const { game, topo } = useOpenGame(text);
  const { pos, go } = usePlayback(game, { autoplay: true, loop: true });
  // The review is Alice's, the one person in the example; the bots are context.
  const focus = game ? reviewSeat(game.record.players) : 0;

  return (
    <>
      <section className="hero">
        <div className="hero-text">
          <p className="eyebrow">A post-game review for Catan</p>
          <h1>Was it the dice, or your decisions?</h1>
          <p className="lede">
            After a game, see each player's chance to win move by move, how much of it the dice decided, how good the opening
            settlements were, and which decisions cost the game. It is computed in your browser by a C++ engine that plays the rest of the game out
            thousands of times.
          </p>
          <div className="cta">
            <a className="button primary" href="#/replay">
              Review a game
            </a>
            <a className="button" href="#/engine">
              How it works
            </a>
            <a className="button ghost" href={REPO_URL}>
              Source on GitHub ↗
            </a>
          </div>
        </div>
        {game && topo ? <LiveBoard game={game} topo={topo} pos={pos} review={review} /> : <div className="board-area placeholder" aria-busy="true" />}
      </section>

      {game && topo && review && (
        <section className="section example-review">
          <div className="section-head">
            <h2>The example game, reviewed</h2>
            <p>
              Seen by Alice, who played three hard bots: her line on top, the bots faded as context. The chart follows the board
              above; click it to jump. The same review runs live on any game you open in <a href="#/replay">Replay</a>.
            </p>
          </div>
          <WinChart run={review.chances} players={game.record.players} stateCount={game.stateCount} pos={pos} focus={focus} onSeek={go} />
          <div className="charts">
            <LuckPanel run={review.chances} players={game.record.players} pos={pos} focus={focus} />
            <SetupPanel run={review.setup} players={game.record.players} pos={pos} focus={focus} onSeek={go} />
          </div>
          <div className="section-head sub">
            <h3>Every decision, graded</h3>
            <p>
              Alice was out of the game early, so here are the moves of {game.record.players[review.grades.seat]?.name}, who lost
              by a point: each against the best move, judged only on what that player could see. Click one to see it on the board; in <a href="#/replay">Replay</a> every
              move of the game is graded, and the best move is drawn on the board.
            </p>
          </div>
          <KeyMoves run={review.grades} names={{ players: game.record.players, board: game.board, topo }} onSeek={go} />
        </section>
      )}

      <section className="section">
        <div className="section-head">
          <h2>Review your own game</h2>
        </div>
        <ol className="how">
          {HOW.map((h, i) => (
            <li className="panel" key={h.title}>
              <span className="how-step">{i + 1}</span>
              <h3>{h.title}</h3>
              <p>{h.text}</p>
            </li>
          ))}
        </ol>
        <p className="panel-foot">
          <a href={PLATFORM_URL}>catan-project on GitHub ↗</a> · or drop a saved game record on the{' '}
          <a href="#/replay">Replay</a> tab.
        </p>
      </section>

      <section className="section">
        <div className="section-head">
          <h2>How it is built</h2>
        </div>
        <div className="blocks">
          {BUILDING_BLOCKS.map((b) => (
            <div className="panel block" key={b.title}>
              <div className="block-icon" aria-hidden="true">
                {b.icon}
              </div>
              <h3>{b.title}</h3>
              <p>{b.text}</p>
            </div>
          ))}
        </div>
        <p className="panel-foot">
          <a href="#/engine">Speed, tests and bots →</a>
        </p>
      </section>

      <section className="section">
        <div className="section-head">
          <h2>Roadmap</h2>
          <p>The goal is a review that tells you, after the game, which decisions mattered.</p>
        </div>
        <ol className="roadmap">
          {ROADMAP.map((r) => (
            <li key={r.title} className={r.done ? 'done' : ''}>
              <span className="roadmap-mark" aria-label={r.done ? 'done' : 'planned'} />
              <div>
                <strong>{r.title}</strong>
                <span className="muted"> · {r.text}</span>
              </div>
            </li>
          ))}
        </ol>
      </section>
    </>
  );
}
