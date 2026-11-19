// The landing page: what the project is, in one look. The board on the right
// is the example game replaying itself through the engine.

import { useEffect, useState } from 'react';
import { PLAYER_COLORS } from '@platform/utils/labels';
import { useLastBenchmark } from '../bench';
import { fmt, LogBars, Stat, type Bar } from '../components/Charts';
import { GameBoard } from '../components/GameBoard';
import { exampleRecord } from '../engine';
import { CORRECTNESS, REPO_URL, SPEED, SPEEDUP_VS_PYTHON } from '../facts';
import { useOpenGame, usePlayback } from '../hooks';

function LiveBoard() {
  const [text, setText] = useState<string | null>(null);
  useEffect(() => {
    exampleRecord().then(setText, () => {});
  }, []);
  const { game, topo } = useOpenGame(text);
  const { pos } = usePlayback(game, { autoplay: true, loop: true });

  if (!game || !topo) return <div className="board-area placeholder" aria-busy="true" />;
  const state = game.state(pos);
  const players = game.record.players;
  return (
    <a className="live-board" href="#/replay" title="Open this game in the replay viewer">
      <GameBoard game={game} topo={topo} pos={pos} />
      <div className="live-caption">
        <span className="live-dot" /> Replaying an example game · turn {state.turn}
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

const BUILDING_BLOCKS = [
  {
    icon: '⬡',
    title: 'Rules as bitmasks',
    text: 'The 54 corners and 72 sides of the board fit in 64- and 128-bit masks. A whole game state is 768 bytes with no pointers, so copying it for a simulation is one memcpy.',
  },
  {
    icon: '≡',
    title: 'Checked against the original',
    text: `A difference test runs the C++ engine and catan-project's Python rules side by side and compares the full state after every action. It covers ${Math.round(CORRECTNESS.coverage * 100)}% of the Python rules and caught ${CORRECTNESS.mutantsCaught} of ${CORRECTNESS.mutants} planted bugs.`,
  },
  {
    icon: '◈',
    title: 'Same engine in the browser',
    text: 'Emscripten compiles the same C++ to WebAssembly. Random games played here give the same checksum as the native build, so the page runs the real engine, not a copy.',
  },
];

const ROADMAP = [
  { done: true, title: 'Rules engine in C++20', text: 'Full rules, difference-tested against the Python game' },
  { done: true, title: 'Performance', text: 'Profiled with perf; 2.6× faster after two targeted fixes' },
  { done: true, title: 'WebAssembly replay viewer', text: 'This page' },
  { done: true, title: 'Bot zoo', text: 'Sixteen styles of bot, to play varied games (in Replay and How it works)' },
  { done: true, title: 'Chance to win', text: 'Monte Carlo in Web Workers, split into luck and decisions (in Replay)' },
  { done: true, title: 'Arena and tuned zoo', text: 'SPRT matches, Catanatron, and MAP-Elites: the best bot of each style (in How it works)' },
  { done: true, title: 'Initial placement', text: 'The first graded decision: every corner of the setup by successive halving (in Replay)' },
  { done: true, title: 'Search', text: 'A bot that searches (ISMCTS) without seeing hidden cards; the arena and Web Workers (in How it works and Performance)' },
  { done: false, title: 'Value network', text: 'A learned evaluation of positions, trained on self-play' },
  { done: false, title: 'Post-game review', text: 'Which moves won or lost the game, shown here move by move' },
];

export function Start() {
  const yours = useLastBenchmark();
  const bars: Bar[] = SPEED.map((s) => ({
    label: s.short,
    value: s.gamesPerSecond,
    note: s.note,
    tone: s.short === 'Native C++' ? 'gold' : 'muted',
  }));
  if (yours) {
    bars.push({ label: 'Your browser', value: yours.games / yours.seconds, note: 'WebAssembly, measured just now', tone: 'green' });
  }

  return (
    <>
      <section className="hero">
        <div className="hero-text">
          <p className="eyebrow">C++20 · WebAssembly · bitboards</p>
          <h1>A Catan rules engine that plays {fmt(SPEED[2].gamesPerSecond)} games a second</h1>
          <p className="lede">
            The foundation of a post-game analyzer for Catan. It runs right here in your browser: replay a finished game move
            by move, or measure the engine on your own CPU.
          </p>
          <div className="cta">
            <a className="button primary" href="#/replay">
              Watch a game
            </a>
            <a className="button" href="#/performance">
              Run the benchmark
            </a>
            <a className="button ghost" href={REPO_URL}>
              Source on GitHub ↗
            </a>
          </div>
        </div>
        <LiveBoard />
      </section>

      <section className="stats">
        <Stat value={SPEED[2].gamesPerSecond} label="games per second, one core" />
        <Stat value={SPEEDUP_VS_PYTHON} suffix="×" label="faster per move than the Python rules" tone="gold" />
        <Stat value={0} label="differences from the reference rules" tone="green" />
        <Stat value={CORRECTNESS.coverage * 100} suffix="%" label="of the reference rules exercised" />
      </section>

      <section className="section">
        <div className="section-head">
          <h2>Speed</h2>
          <p>
            Whole four-player games between random players, per second, on one core. The scale is logarithmic: each line
            is ten times the one before.
          </p>
        </div>
        <div className="panel">
          <LogBars bars={bars} unit="games / s" />
          {!yours && (
            <p className="panel-foot">
              <a href="#/performance">Add your browser to this chart →</a>
            </p>
          )}
        </div>
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
          <a href="#/how-it-works">Explore the bitboard and the tests in How it works →</a>
        </p>
      </section>

      <section className="section">
        <div className="section-head">
          <h2>Roadmap</h2>
          <p>The engine is step one. The goal is a review that tells you, after the game, which decisions mattered.</p>
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
