// How the engine is built and checked: the pieces and how they connect, the
// bitmask a player's buildings live in (explorable on the example game), the
// tests that keep the rules equal to the game's, the data layout and the
// bot zoo that will play the opponents in simulations.

import { useEffect, useMemo, useState } from 'react';
import { getVertexCoords, hexCorners, hexToPixel, HEX_SIZE, RESOURCE_COLORS } from '@platform/utils/hexLayout';
import { PLAYER_COLORS } from '@platform/utils/labels';
import { Arena } from '../components/Arena';
import { Zoo } from '../components/Zoo';
import { exampleRecord, type OpenGame } from '../engine';
import { CORRECTNESS, GAME_LAYOUT, REPO_URL } from '../facts';
import { useOpenGame } from '../hooks';
import type { Topology } from '../types';

function Architecture() {
  const box = (title: string, lines: string[], tone = '') => (
    <div className={`arch-box ${tone}`}>
      <strong>{title}</strong>
      {lines.map((l) => (
        <span key={l}>{l}</span>
      ))}
    </div>
  );
  return (
    <div className="arch" role="img" aria-label="catan-project's rules and records feed the C++ engine, which is built three ways: native tools, a Python module for the difference test, and WebAssembly for this page">
      <div className="arch-col">
        <span className="arch-label">catan-project</span>
        {box('game.py', ['the reference rules', 'Python, FastAPI'])}
        {box('Game records', ['.json.gz after every game', 'every event, every roll'])}
        {box('Board.tsx', ['the board you see here', 'React, imported as is'])}
      </div>
      <div className="arch-arrow" aria-hidden="true">→</div>
      <div className="arch-col">
        <span className="arch-label">catan-analysis-engine</span>
        {box('C++20 engine', ['Game: 768 bytes, bitmasks', 'full rules, no heap', 'ReplayLog: every state of a game'], 'core')}
      </div>
      <div className="arch-arrow" aria-hidden="true">→</div>
      <div className="arch-col">
        <span className="arch-label">built three ways</span>
        {box('Native', ['catan_selfplay, catan_replay', 'Google Benchmark, perf'])}
        {box('Python module (pybind11)', ['difference test against game.py', 'after every single action'])}
        {box('WebAssembly (Emscripten)', ['this page: replay, benchmark', 'same checksum as native'], 'web')}
      </div>
    </div>
  );
}

/** Corner i of the board drawn where catan-project's Board draws it. */
function cornerPoint(topo: Topology, i: number) {
  const n = topo.nodes[i];
  return getVertexCoords({ hex: { q: n.q, r: n.r, s: -n.q - n.r }, direction: n.direction });
}

function Bitboard({ game, topo }: { game: OpenGame; topo: Topology }) {
  const state = game.state(game.stateCount - 1);
  const players = game.record.players;
  const [seat, setSeat] = useState(state.winner ?? 0);
  const [hover, setHover] = useState<number | null>(null);

  const p = state.players[seat];
  const owned = useMemo(() => new Set([...p.settlements, ...p.cities]), [p]);
  const others = useMemo(
    () => new Set(state.players.flatMap((q, s) => (s === seat ? [] : [...q.settlements, ...q.cities]))),
    [state, seat],
  );
  // The mask as the engine holds it: bit i set when the player builds on corner i.
  const mask = [...owned].reduce((m, i) => m | (1n << BigInt(i)), 0n);
  const hex = `0x${mask.toString(16).padStart(16, '0')}`;
  const extent = HEX_SIZE * 4.6;
  const color = PLAYER_COLORS[players[seat].color];

  return (
    <div className="bitboard">
      <div className="bitboard-board">
        <svg viewBox={`${-extent} ${-extent} ${extent * 2} ${extent * 2}`} role="img" aria-label="The 54 corners of the board, numbered as the engine numbers them">
          {topo.hexes.map((h, i) => {
            const cube = { q: h.q, r: h.r, s: -h.q - h.r };
            const c = hexToPixel(cube);
            return (
              <g key={i}>
                <polygon className="bb-hex" points={hexCorners(cube).map((pt) => `${pt.x},${pt.y}`).join(' ')} fill={RESOURCE_COLORS[game.board.hexes[i].terrain]} />
                <text className="bb-hex-label" x={c.x} y={c.y + 5} textAnchor="middle">
                  {game.board.hexes[i].number || ''}
                </text>
              </g>
            );
          })}
          {topo.nodes.map((_, i) => {
            const pt = cornerPoint(topo, i);
            const mine = owned.has(i);
            const theirs = others.has(i);
            return (
              <g key={i} onPointerEnter={() => setHover(i)} onPointerLeave={() => setHover(null)} className="bb-node-g">
                <circle
                  className={`bb-node${mine ? ' mine' : ''}${theirs ? ' theirs' : ''}${hover === i ? ' hover' : ''}`}
                  cx={pt.x}
                  cy={pt.y}
                  r={mine || hover === i ? 13 : 9}
                  style={mine ? { fill: color } : undefined}
                />
                <text className={`bb-node-label${mine ? ' mine' : ''}`} x={pt.x} y={pt.y + 4} textAnchor="middle">
                  {i}
                </text>
              </g>
            );
          })}
        </svg>
      </div>
      <div className="bitboard-side">
        <div className="seat-pick" role="radiogroup" aria-label="Player">
          {players.map((pl, s) => (
            <button key={s} type="button" role="radio" aria-checked={s === seat} className={s === seat ? 'on' : ''} onClick={() => setSeat(s)}>
              <span className="chip" style={{ background: PLAYER_COLORS[pl.color] }} />
              {pl.name}
            </button>
          ))}
        </div>
        <p className="muted small">
          {players[seat].name}'s settlements and cities at the end of the example game, as one <code>uint64_t</code>. Hover a
          corner to find its bit.
        </p>
        <div className="bits" aria-label="64 bits, bit 0 first">
          {Array.from({ length: 64 }, (_, i) => (
            <span
              key={i}
              className={`bit${owned.has(i) ? ' on' : ''}${i >= 54 ? ' unused' : ''}${hover === i ? ' hover' : ''}`}
              style={owned.has(i) ? { background: color } : undefined}
              onPointerEnter={() => i < 54 && setHover(i)}
              onPointerLeave={() => setHover(null)}
              title={i < 54 ? `bit ${i}: corner ${i}` : `bit ${i}: unused, the board has 54 corners`}
            >
              {i % 8 === 0 ? i : ''}
            </span>
          ))}
        </div>
        <dl className="mask-facts">
          <div>
            <dt>Value</dt>
            <dd className="mono">{hex}</dd>
          </div>
          <div>
            <dt>Buildings</dt>
            <dd className="mono">popcount = {owned.size}</dd>
          </div>
          <div>
            <dt>{hover === null ? 'Corner' : `Corner ${hover}`}</dt>
            <dd className="mono">{hover === null ? 'hover one' : `1 << ${hover} = ${owned.has(hover) ? 'set' : 'clear'}`}</dd>
          </div>
        </dl>
        <p className="small">
          With masks, a rule becomes a few instructions. The distance rule for one corner is a single AND: all buildings
          against a precomputed mask of that corner's neighbours. Counting buildings is one <code>popcount</code>. The 72
          sides use the same trick in two 64-bit words.
        </p>
      </div>
    </div>
  );
}

function Testing() {
  return (
    <div className="section two">
      <div>
        <div className="section-head">
          <h2>Same rules as the game, checked</h2>
          <p>
            The engine will judge moves made on the platform, so its rules must equal <code>game.py</code>'s exactly. A test
            loads both into one Python process and plays them side by side.
          </p>
        </div>
        <ol className="steps panel">
          <li>
            <b>Every recorded game</b>, event by event, and random games with random house rules.
          </li>
          <li>
            <b>Probes:</b> about 30% of the steps are random, mostly illegal actions. Both engines must refuse them alike.
          </li>
          <li>
            <b>After every action</b> it compares the full state and every seat's legal moves. The first difference stops the
            run and prints its path, like <code>$.players[1].resources[2]: python 0 vs c++ 1</code>.
          </li>
          <li>
            <b>{Math.round(CORRECTNESS.coverage * 100)}%</b> of the lines of <code>game.py</code> run under the test.
          </li>
        </ol>
      </div>
      <div>
        <div className="section-head">
          <h2>Testing the test</h2>
          <p>A test that never fails proves little. Twelve plausible bugs were planted in the C++ rules, one at a time.</p>
        </div>
        <div className="panel mutants">
          {CORRECTNESS.mutationRuns.map((r) => (
            <div key={r.label} className="mutant-row">
              <span className="small">{r.label}</span>
              <div className="mutant-cells" aria-label={`${r.caught} of ${CORRECTNESS.mutants} caught`}>
                {Array.from({ length: CORRECTNESS.mutants }, (_, i) => (
                  <span key={i} className={i < r.caught ? 'caught' : ''} />
                ))}
              </div>
              <span className="mono small">
                {r.caught}/{CORRECTNESS.mutants}
              </span>
            </div>
          ))}
          <p className="muted small">
            Examples: longest road counted from 4, a tie taking Largest Army, discarding half rounded up, a 2:1 port acting as
            3:1, an opponent's settlement not cutting a road.
          </p>
        </div>
      </div>
    </div>
  );
}

function Layout() {
  const lines = GAME_LAYOUT.bytes / GAME_LAYOUT.cacheLine;
  return (
    <div className="panel layout">
      <div className="cache-lines" aria-label={`${lines} cache lines of ${GAME_LAYOUT.cacheLine} bytes`}>
        {Array.from({ length: lines }, (_, i) => (
          <span key={i}>{i * GAME_LAYOUT.cacheLine}</span>
        ))}
      </div>
      <div className="layout-facts">
        <div>
          <span className="mono big">{GAME_LAYOUT.bytes} B</span>
          <span className="muted small">a whole game state, {lines} cache lines</span>
        </div>
        <div>
          <span className="mono big">0</span>
          <span className="muted small">pointers or heap allocations</span>
        </div>
        <div>
          <span className="mono big gold">{GAME_LAYOUT.copyNs} ns</span>
          <span className="muted small">to copy it, one memcpy</span>
        </div>
      </div>
      <p className="muted small">
        A search copies the position before every simulated game. At 21 ns per copy against about 93 µs per game, copying
        costs 0.02% of the time, so it needs no cleverness such as undoing moves.
      </p>
    </div>
  );
}

const TOOLS = ['C++20', 'Go', 'CMake presets', 'GoogleTest', 'Google Benchmark', 'ASan + UBSan', 'perf', 'pybind11', 'Emscripten + embind', 'TypeScript', 'React', 'Vite', 'Vitest'];

export function HowItWorks() {
  const [text, setText] = useState<string | null>(null);
  useEffect(() => {
    exampleRecord().then(setText, () => {});
  }, []);
  const { game, topo } = useOpenGame(text);

  return (
    <>
      <div className="page-head">
        <div>
          <h1>How it works</h1>
          <p className="lede">One C++ engine, built three ways, and kept equal to the game's own rules by a test that compares them after every move.</p>
        </div>
      </div>

      <section className="section first">
        <Architecture />
      </section>

      <section className="section">
        <div className="section-head">
          <h2>The board as bits</h2>
          <p>
            The engine numbers the 54 corners of the board 0 to 53. A player's buildings are then one 64-bit integer: bit{' '}
            <i>i</i> is set when they built on corner <i>i</i>.
          </p>
        </div>
        {game && topo ? <Bitboard game={game} topo={topo} /> : <div className="panel placeholder-block" aria-busy="true" />}
      </section>

      <Testing />

      <section className="section">
        <div className="section-head">
          <h2>Data layout</h2>
          <p>The state is one flat struct of fixed-size arrays and masks, so it sits in a few cache lines and copies in one go.</p>
        </div>
        <Layout />
      </section>

      <Zoo />

      <Arena />

      <section className="section">
        <div className="section-head">
          <h2>Tools</h2>
        </div>
        <div className="tools">
          {TOOLS.map((t) => (
            <span key={t}>{t}</span>
          ))}
        </div>
        <p className="muted small tools-note">
          Every number on this site comes from <a href={`${REPO_URL}/tree/main/docs`}>the docs in the repository</a>, with how
          to measure it again.
        </p>
      </section>
    </>
  );
}
