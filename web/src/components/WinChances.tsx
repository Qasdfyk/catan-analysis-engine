// The chance-to-win chart under the board (also a timeline to click), and
// the panel that splits each player's swing into luck and decisions.
// Numbers come from chances.ts; the chart fills in as the workers finish
// passes and gets smoother with each one.

import { useRef, type PointerEvent } from 'react';
import { PLAYER_COLORS } from '@platform/utils/labels';
import { chancesFrom, completePasses, luckError, split, turnAt, type ChanceRun } from '../chances';
import type { RecordPlayer } from '../types';
import { fmt } from './Charts';
import { seatName } from './GamePanels';

const seatColor = (players: RecordPlayer[], seat: number) => PLAYER_COLORS[players[seat]?.color] ?? '#888';
const pp = (x: number, sign = true) => `${sign && x >= 0 ? '+' : sign ? '−' : ''}${Math.abs(100 * x).toFixed(1)}`;

interface Props {
  run: ChanceRun | null;
  players: RecordPlayer[];
  stateCount: number;
  pos: number;
  /** The player the review is for: drawn on top, the others faded. */
  focus: number;
}

function Progress({ run }: { run: ChanceRun }) {
  const passes = completePasses(run.counts, run.points.length).length;
  const finished = run.done >= run.total;
  return (
    <span className="muted small">
      {run.precomputed
        ? `${fmt(run.total)} playouts, computed ahead by the same engine`
        : finished
        ? `${fmt(run.total)} playouts in ${run.seconds.toFixed(1)} s on ${run.workers} workers · ${fmt(run.total / Math.max(run.seconds, 1e-3))}/s`
        : `estimating… ${passes} of 10 passes · ${fmt(run.done)} playouts`}
    </span>
  );
}

export function WinChart({ run, players, stateCount, pos, focus, onSeek }: Props & { onSeek: (pos: number) => void }) {
  const svg = useRef<SVGSVGElement>(null);
  const w = 760;
  const h = 190;
  const pad = { l: 36, r: 12, t: 12, b: 24 };
  const last = Math.max(1, stateCount - 1);
  const x = (i: number) => pad.l + (i / last) * (w - pad.l - pad.r);
  const y = (c: number) => pad.t + (1 - c) * (h - pad.t - pad.b);

  const seats = players.length;
  const chances = run ? chancesFrom(run.counts, run.points.length, seats) : [];
  // One line per seat through the points estimated so far.
  const paths = players.map((_, seat) => {
    let d = '';
    chances.forEach((c, i) => {
      if (!c || !run) return;
      d += `${d ? 'L' : 'M'}${x(run.points[i].state).toFixed(1)},${y(c[seat]).toFixed(1)}`;
    });
    return d;
  });
  // The chance at the shown move: the last point at or before it.
  let shown: number[] | null = null;
  if (run) {
    for (let i = 0; i < run.points.length && run.points[i].state <= pos; i++) shown = chances[i] ?? shown;
  }

  const seek = (e: PointerEvent) => {
    const box = svg.current?.getBoundingClientRect();
    if (!box) return;
    const fx = ((e.clientX - box.left) / box.width) * w;
    onSeek(Math.round(((fx - pad.l) / (w - pad.l - pad.r)) * last));
  };

  return (
    <div className="panel chart-panel">
      <div className="chart-head">
        <h3>Chance to win</h3>
        {run ? <Progress run={run} /> : <span className="muted small">starting the engine…</span>}
      </div>
      {run?.error && <p className="message error small">Win chances failed: {run.error}</p>}
      <svg
        ref={svg}
        className="chart seekable"
        viewBox={`0 0 ${w} ${h}`}
        role="img"
        aria-label="Each player's chance to win over the game"
        onPointerDown={(e) => {
          (e.target as Element).setPointerCapture?.(e.pointerId);
          seek(e);
        }}
        onPointerMove={(e) => e.buttons === 1 && seek(e)}
      >
        {[0, 0.25, 0.5, 0.75, 1].map((c) => (
          <g key={c}>
            <line className="grid" x1={pad.l} x2={w - pad.r} y1={y(c)} y2={y(c)} />
            <text className="axis" x={pad.l - 8} y={y(c) + 4} textAnchor="end">
              {c * 100}%
            </text>
          </g>
        ))}
        <rect className="played" x={pad.l} y={pad.t} width={Math.max(0, x(pos) - pad.l)} height={h - pad.t - pad.b} />
        {/* The focused player last, so it is drawn over the others. */}
        {[...paths.keys()]
          .sort((a, b) => Number(a === focus) - Number(b === focus))
          .map(
            (seat) =>
              paths[seat] && (
                <path key={seat} d={paths[seat]} className={`points-line ${seat === focus ? 'focus' : 'faded'}`} stroke={seatColor(players, seat)} />
              ),
          )}
        <line className="cursor" x1={x(pos)} x2={x(pos)} y1={pad.t - 4} y2={h - pad.b} />
        {shown?.map((c, seat) => (
          <circle key={seat} cx={x(pos)} cy={y(c)} r={seat === focus ? 5 : 3} fill={seatColor(players, seat)} className="cursor-dot" />
        ))}
      </svg>
      <div className="legend small">
        {players.map((_, seat) => (
          <span key={seat} className={seat === focus ? 'focus' : 'muted'}>
            <span className="chip" style={{ background: seatColor(players, seat) }} /> {seatName(players, seat)}{' '}
            <b className="mono">{shown ? `${(100 * shown[seat]).toFixed(0)}%` : '…'}</b>
          </span>
        ))}
      </div>
    </div>
  );
}

/**
 * Per player: where the chance started, what the dice added or took, what
 * decisions added or took, where it ended. Bars run left (lost) or right
 * (gained) from the middle.
 */
export function LuckPanel({ run, players, pos, focus }: Omit<Props, 'stateCount'>) {
  const seats = players.length;
  const ready = run && run.points.length > 0 && completePasses(run.counts, run.points.length).length > 0;
  if (!run || !ready) {
    return (
      <div className="panel chart-panel">
        <div className="chart-head">
          <h3>Luck or decisions?</h3>
        </div>
        <p className="muted small">Waiting for the first pass over the whole game…</p>
      </div>
    );
  }
  const chances = chancesFrom(run.counts, run.points.length, seats) as number[][];
  const s = split(run.points, chances);
  const err = luckError(run.points, run.counts, seats);
  const top = Math.max(0.5, ...s.luck.map(Math.abs), ...s.decisions.map(Math.abs)) * 1.15;
  const turn = turnAt(run.points, chances, pos);

  // The whisker: 95% of where the value would land with endless playouts.
  const bar = (v: number, kind: 'luck' | 'decision', e: number | undefined) => {
    const at = (u: number) => `${Math.min(100, Math.max(0, 50 + (50 * u) / top))}%`;
    return (
      <div className="split-track">
        <div
          className={`split-bar ${kind}`}
          style={{ left: v < 0 ? at(v) : '50%', width: `${(50 * Math.abs(v)) / top}%` }}
        />
        {e !== undefined && (
          <div className="split-err" style={{ left: at(v - 2 * e), width: `calc(${at(v + 2 * e)} - ${at(v - 2 * e)})` }} />
        )}
      </div>
    );
  };

  return (
    <div className="panel chart-panel">
      <div className="chart-head">
        <h3>Luck or decisions?</h3>
        <span className="muted small">whole game, percentage points of chance to win</span>
      </div>
      <div className="split">
        <span />
        <span className="muted small">dice</span>
        <span className="muted small">decisions</span>
        <span className="muted small split-num">start → end</span>
        {/* The player the review is for first; the others are the context. */}
        {[focus, ...players.map((_, seat) => seat).filter((seat) => seat !== focus)].map((seat) => (
          <div key={seat} className={`split-row ${seat === focus ? 'focus' : 'faded'}`}>
            <span className="income-name">
              <span className="chip" style={{ background: seatColor(players, seat) }} />
              {seatName(players, seat)}
            </span>
            <div title={`dice: ${pp(s.luck[seat])} pp`}>
              {bar(s.luck[seat], 'luck', err?.[seat])}
              <span className="mono small">{pp(s.luck[seat])}</span>
            </div>
            <div title={`decisions: ${pp(s.decisions[seat])} pp`}>
              {bar(s.decisions[seat], 'decision', err?.[seat])}
              <span className="mono small">{pp(s.decisions[seat])}</span>
            </div>
            <span className="mono small split-num">
              {pp(s.start[seat], false)}% → {pp(s.end[seat], false)}%
            </span>
          </div>
        ))}
      </div>
      <p className="muted small chart-note">
        Dice: how each roll moved the chances. Decisions: everything between rolls, the setup, building, trades, the
        robber and cards included.{' '}
        {err
          ? `Whiskers: 95% intervals from ${completePasses(run.counts, run.points.length).length} independent passes. The split adds up 150 noisy differences, so it is only a rough guide; a precise one needs the value network planned for later.`
          : 'Intervals show after three passes.'}
        {turn && (
          <>
            {' '}
            Turn {turn.turn}: {seatName(players, turn.seat)}'s roll moved their chance{' '}
            <b className="gold">{pp(turn.roll[turn.seat])} pp</b>
            {turn.decisions && (
              <>
                , the moves after it <b className="gold">{pp(turn.decisions[turn.seat])} pp</b>
              </>
            )}
            .
          </>
        )}
      </p>
    </div>
  );
}
