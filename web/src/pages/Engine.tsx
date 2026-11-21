// The engine page: how it is built, that its rules equal the game's, how fast
// it is (measured in this browser too), the bitmask a player's buildings live
// in, and how strong its bots are. Kept to what a first visit needs; the full
// story and every number's method are in the repository's docs.

import { useEffect, useMemo, useState } from 'react';
import { getVertexCoords, hexCorners, hexToPixel, HEX_SIZE, RESOURCE_COLORS } from '@platform/utils/hexLayout';
import { PLAYER_COLORS } from '@platform/utils/labels';
import { runBenchmark, useLastBenchmark, type Bots } from '../bench';
import { Arena } from '../components/Arena';
import { fmt, LogBars, Stat, type Bar } from '../components/Charts';
import { exampleRecord, type OpenGame } from '../engine';
import { CORRECTNESS, NATIVE_CHECKSUMS, NATIVE_ZOO_CHECKSUMS, PROFILE, REPO_URL, SPEED, SPEEDUP_VS_PYTHON } from '../facts';
import { useOpenGame } from '../hooks';
import type { Topology } from '../types';

// A zoo bot thinks about every move, so its games take longer: fewer of them.
const GAME_COUNTS: Record<Bots, number[]> = { random: [5_000, 20_000, 100_000], zoo: [500, 2_000, 10_000] };
const CHECKSUMS: Record<Bots, Record<number, string>> = { random: NATIVE_CHECKSUMS, zoo: NATIVE_ZOO_CHECKSUMS };

function Runner() {
  const last = useLastBenchmark();
  const [bots, setBots] = useState<Bots>('random');
  const [games, setGames] = useState(20_000);
  const [running, setRunning] = useState(false);
  const [error, setError] = useState('');

  const run = async () => {
    setRunning(true);
    setError('');
    try {
      await runBenchmark(games, bots);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setRunning(false);
    }
  };

  return (
    <div className="panel runner">
      <div className="runner-controls">
        <div>
          <h3>Run it on your machine</h3>
          <p className="muted small">
            {bots === 'random' ? (
              <>
                One Web Worker plays whole games between random players, the same games as{' '}
                <code>catan_selfplay --games N --seed 1</code> in the native build.
              </>
            ) : (
              <>
                Zoo bots: the platform's hard bot with its archetypes and trading personalities, ported from Go to C++. They
                will be the opponents in the review's simulations. Same games as{' '}
                <code>catan_selfplay --games N --seed 1 --bots zoo</code>.
              </>
            )}
          </p>
        </div>
        <div className="segmented" role="radiogroup" aria-label="Players">
          {(['random', 'zoo'] as const).map((b) => (
            <button
              key={b}
              type="button"
              role="radio"
              aria-checked={bots === b}
              className={bots === b ? 'on' : ''}
              onClick={() => {
                setBots(b);
                setGames(GAME_COUNTS[b][1]);
              }}
            >
              {b === 'random' ? 'Random players' : 'Zoo bots'}
            </button>
          ))}
        </div>
        <div className="segmented" role="radiogroup" aria-label="Games">
          {GAME_COUNTS[bots].map((n) => (
            <button key={n} type="button" role="radio" aria-checked={games === n} className={games === n ? 'on' : ''} onClick={() => setGames(n)}>
              {fmt(n)} games
            </button>
          ))}
        </div>
        <button type="button" className="primary" onClick={run} disabled={running}>
          {running ? 'Playing…' : 'Run'}
        </button>
      </div>
      {running && <div className="progress" aria-label="running" />}
      {error && <p className="message error">The benchmark failed: {error}. Reload the page and try again.</p>}
      {last && !running && (
        <div className="result">
          <div className="result-main">
            <span className="result-number">{fmt(last.games / last.seconds)}</span>
            <span className="muted">games / s in this browser{last.bots === 'zoo' ? ', zoo bots' : ''}</span>
          </div>
          <dl className="result-grid">
            <div>
              <dt>Games</dt>
              <dd>{fmt(last.games)}</dd>
            </div>
            <div>
              <dt>Time</dt>
              <dd>{last.seconds.toFixed(2)} s</dd>
            </div>
            <div>
              <dt>Per game</dt>
              <dd>{((last.seconds / last.games) * 1e6).toFixed(0)} µs</dd>
            </div>
            <div>
              <dt>Moves / s</dt>
              <dd>{(last.actions / last.seconds / 1e6).toFixed(2)} M</dd>
            </div>
            <div>
              <dt>Moves per game</dt>
              <dd>{(last.actions / last.games).toFixed(0)}</dd>
            </div>
            <div>
              <dt>Checksum</dt>
              <dd className="mono">{last.checksum}</dd>
            </div>
          </dl>
          <Verdict games={last.games} checksum={last.checksum} refused={last.refused} native={CHECKSUMS[last.bots][last.games]} />
        </div>
      )}
    </div>
  );
}

/** Whether the browser played exactly the games the native build plays. */
function Verdict({ games, checksum, refused, native }: { games: number; checksum: string; refused: number; native?: string }) {
  const same = native === checksum;
  return (
    <p className={`verdict${refused === 0 && same ? ' ok' : native ? ' bad' : ''}`}>
      {native
        ? same
          ? `✓ Checksum matches the native build: WebAssembly played the same ${fmt(games)} games, move for move.`
          : `✗ The native build prints ${native} for these games: something differs.`
        : 'The checksum is a hash of every game; the native build prints the same one for the same games.'}
      {refused > 0 && ` ${refused} moves were refused: the engine and the player disagree.`}
    </p>
  );
}

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

const TOOLS = ['C++20', 'Go', 'CMake presets', 'GoogleTest', 'Google Benchmark', 'ASan + UBSan', 'perf', 'pybind11', 'Emscripten + embind', 'Web Workers', 'TypeScript', 'React', 'Vite', 'Vitest'];

export function Engine() {
  const [text, setText] = useState<string | null>(null);
  useEffect(() => {
    exampleRecord().then(setText, () => {});
  }, []);
  const { game, topo } = useOpenGame(text);

  const yours = useLastBenchmark();
  const bars: Bar[] = SPEED.map((s) => ({
    label: s.name,
    value: s.gamesPerSecond,
    note: s.note,
    tone: s.short === 'Native C++' ? 'gold' : 'muted',
  }));
  if (yours?.bots === 'random') bars.push({ label: 'This engine, your browser', value: yours.games / yours.seconds, note: 'WebAssembly, one worker', tone: 'green' });

  return (
    <>
      <div className="page-head">
        <div>
          <h1>The engine</h1>
          <p className="lede">
            One C++ engine, built three ways, kept equal to the game's own rules by a test that compares them after every move,
            and fast enough to play out thousands of games per decision.
          </p>
        </div>
      </div>

      <section className="stats">
        <Stat value={SPEED[2].gamesPerSecond} label="games per second, one core" />
        <Stat value={SPEEDUP_VS_PYTHON} suffix="×" label="faster per move than the Python rules" tone="gold" />
        <Stat value={0} label="differences from the reference rules" tone="green" />
        <Stat value={CORRECTNESS.coverage * 100} suffix="%" label="of the reference rules exercised" />
      </section>

      <section className="section first">
        <Architecture />
      </section>

      <Testing />

      <section className="section">
        <div className="section-head">
          <h2>Speed</h2>
          <p>
            Whole four-player games between random players, per second, on one core. The scale is logarithmic; the Python
            engines run the same random player, so the bars compare engines, not players.
          </p>
        </div>
        <div className="panel">
          <LogBars bars={bars} unit="games / s" />
        </div>
      </section>

      <section className="section two">
        <div>
          <Runner />
        </div>
        <div>
          <div className="panel profile">
            <h3>Where the time went</h3>
            <p className="muted small">
              <code>perf</code> showed 3.5 instructions per cycle and almost no cache misses: the engine was not waiting for
              memory, it was doing too much work.
            </p>
            <div className="profile-row">
              <span>Longest road search</span>
              <div className="profile-bar">
                <div style={{ width: `${PROFILE.before.longestRoadShare * 100}%` }} />
              </div>
              <span className="mono">{Math.round(PROFILE.before.longestRoadShare * 100)}%</span>
            </div>
            <p className="muted small">
              of all samples. Two fixes: recompute only the players a move can affect, and find legal roads with a few mask
              operations instead of 72 checks.
            </p>
            <div className="before-after">
              <div>
                <span className="muted small">before</span>
                <span className="mono big">{fmt(PROFILE.before.gamesPerSecond)}</span>
              </div>
              <span className="arrow">→</span>
              <div>
                <span className="muted small">after</span>
                <span className="mono big gold">{fmt(PROFILE.after.gamesPerSecond)}</span>
              </div>
              <span className="muted small">games / s · same games, same checksum</span>
            </div>
          </div>
        </div>
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
