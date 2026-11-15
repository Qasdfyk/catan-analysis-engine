// How fast the engine is: run it here, compare with the published numbers.

import { useState } from 'react';
import { runBenchmark, useLastBenchmark } from '../bench';
import { fmt, LogBars, type Bar } from '../components/Charts';
import { NATIVE_CHECKSUMS, PROFILE, REPO_URL, SPEED, THREADS } from '../facts';

const GAME_COUNTS = [5_000, 20_000, 100_000];

function Runner() {
  const last = useLastBenchmark();
  const [games, setGames] = useState(20_000);
  const [running, setRunning] = useState(false);
  const [error, setError] = useState('');

  const run = async () => {
    setRunning(true);
    setError('');
    try {
      await runBenchmark(games);
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
            One Web Worker plays whole random games, the same games as <code>catan_selfplay --games N --seed 1</code> in the
            native build.
          </p>
        </div>
        <div className="segmented" role="radiogroup" aria-label="Games">
          {GAME_COUNTS.map((n) => (
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
      {error && <p className="message error">The worker failed: {error}</p>}
      {last && !running && (
        <div className="result">
          <div className="result-main">
            <span className="result-number">{fmt(last.games / last.seconds)}</span>
            <span className="muted">games / s in this browser</span>
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
          <Verdict games={last.games} checksum={last.checksum} refused={last.refused} />
        </div>
      )}
    </div>
  );
}

/** Whether the browser played exactly the games the native build plays. */
function Verdict({ games, checksum, refused }: { games: number; checksum: string; refused: number }) {
  const native = NATIVE_CHECKSUMS[games];
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

function ThreadChart() {
  const w = 520;
  const h = 220;
  const pad = { l: 44, r: 24, t: 24, b: 40 };
  const maxT = 16;
  const maxS = 16;
  const x = (t: number) => pad.l + (Math.log2(t) / Math.log2(maxT)) * (w - pad.l - pad.r);
  const y = (s: number) => h - pad.b - (Math.log2(s) / Math.log2(maxS)) * (h - pad.t - pad.b);
  const base = THREADS[0].gamesPerSecond;
  const points = THREADS.map((p) => [x(p.threads), y(p.gamesPerSecond / base)] as const);
  return (
    <svg className="chart" viewBox={`0 0 ${w} ${h}`} role="img" aria-label="Speed-up by number of threads">
      {[1, 2, 4, 8, 16].map((t) => (
        <g key={t}>
          <line className="grid" x1={x(t)} x2={x(t)} y1={pad.t} y2={h - pad.b} />
          <text className="axis" x={x(t)} y={h - pad.b + 18} textAnchor="middle">
            {t}
          </text>
          <line className="grid" x1={pad.l} x2={w - pad.r} y1={y(t)} y2={y(t)} />
          <text className="axis" x={pad.l - 8} y={y(t) + 4} textAnchor="end">
            {t}×
          </text>
        </g>
      ))}
      <line className="ideal" x1={x(1)} y1={y(1)} x2={x(16)} y2={y(16)} />
      <text className="label muted-fill" x={x(8) - 6} y={y(8) - 8} textAnchor="end">
        ideal
      </text>
      <polyline className="series gold" points={points.map((p) => p.join(',')).join(' ')} />
      {THREADS.map((p, i) => (
        <g key={p.threads}>
          <circle className="dot gold" cx={points[i][0]} cy={points[i][1]} r={4} />
          {i > 0 && (
            <text className="label" x={points[i][0]} y={points[i][1] - 10} textAnchor="middle">
              {(p.gamesPerSecond / base).toFixed(1)}×
            </text>
          )}
        </g>
      ))}
      <text className="axis" x={(w + pad.l) / 2} y={h - 4} textAnchor="middle">
        threads
      </text>
    </svg>
  );
}

export function Performance() {
  const yours = useLastBenchmark();
  const bars: Bar[] = SPEED.map((s) => ({
    label: s.name,
    value: s.gamesPerSecond,
    note: s.note,
    tone: s.short === 'Native C++' ? 'gold' : 'muted',
  }));
  if (yours) bars.push({ label: 'This engine, your browser', value: yours.games / yours.seconds, note: 'WebAssembly, one worker', tone: 'green' });

  return (
    <>
      <div className="page-head">
        <div>
          <h1>Performance</h1>
          <p className="lede">
            A search engine for Catan will play millions of games, so every microsecond of a move counts. Measured on whole
            four-player games between random players.
          </p>
        </div>
      </div>

      <Runner />

      <section className="section">
        <div className="section-head">
          <h2>Against other engines</h2>
          <p>
            Games per second on one core, log scale. The Python engines run the same random player, so the bars compare
            engines, not players.
          </p>
        </div>
        <div className="panel">
          <LogBars bars={bars} unit="games / s" />
        </div>
      </section>

      <section className="section two">
        <div>
          <div className="section-head">
            <h2>Threads</h2>
            <p>
              Games share nothing, so threads split them with no locks. Scaling still flattens: one busy core turbo-boosts
              higher than eight, and threads 9–16 are hyper-threads sharing a core.
            </p>
          </div>
          <div className="panel">
            <ThreadChart />
            <p className="muted small">Native build, i7-11800H (8 cores, 16 threads), {fmt(THREADS[4].gamesPerSecond)} games/s at 16 threads.</p>
          </div>
        </div>
        <div>
          <div className="section-head">
            <h2>Where the time went</h2>
            <p>
              <code>perf</code> showed the CPU retiring 3.5 instructions per cycle with almost no cache misses: the engine was
              not waiting for memory, it was doing too much work.
            </p>
          </div>
          <div className="panel profile">
            <div className="profile-row">
              <span>Longest road search</span>
              <div className="profile-bar">
                <div style={{ width: `${PROFILE.before.longestRoadShare * 100}%` }} />
              </div>
              <span className="mono">{Math.round(PROFILE.before.longestRoadShare * 100)}%</span>
            </div>
            <p className="muted small">
              of all samples: every player's road was recomputed after every road and settlement. Fix one: recompute only
              the players a move can affect. Fix two: find the legal roads with a few mask operations instead of 72 checks,
              30 times faster.
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

      <p className="muted small">
        Method and raw numbers: <a href={`${REPO_URL}/blob/main/docs/performance.md`}>docs/performance.md</a>.
      </p>
    </>
  );
}
