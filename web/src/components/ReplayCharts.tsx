// Charts under the replay. The points chart is also a timeline: click or drag
// on it to jump. Dice and income are counted up to the shown move, so they
// grow as the game plays.

import { useRef, type PointerEvent } from 'react';
import { PLAYER_COLORS } from '@platform/utils/labels';
import { DICE_WAYS, diceCounts, income, total, type GameStats } from '../stats';
import { RESOURCES, type RecordPlayer } from '../types';
import { seatName } from './GamePanels';

const seatColor = (players: RecordPlayer[], seat: number) => PLAYER_COLORS[players[seat]?.color] ?? '#888';

interface ChartProps {
  stats: GameStats;
  players: RecordPlayer[];
  pos: number;
}

export function PointsChart({ stats, players, pos, onSeek }: ChartProps & { onSeek: (pos: number) => void }) {
  const svg = useRef<SVGSVGElement>(null);
  const w = 760;
  const h = 170;
  const pad = { l: 28, r: 12, t: 12, b: 24 };
  const last = stats.points.length - 1;
  const top = Math.max(10, ...stats.points[last]);
  const x = (i: number) => pad.l + (i / Math.max(1, last)) * (w - pad.l - pad.r);
  // Seats with equal points would hide each other: shift each line by a pixel.
  const y = (p: number, seat: number) => pad.t + (1 - p / top) * (h - pad.t - pad.b) + (seat - 1.5) * 1.2;

  // A step line through the states where the seat's points changed.
  const paths = players.map((_, seat) => {
    let d = `M${x(0)},${y(stats.points[0][seat], seat)}`;
    for (let i = 1; i <= last; i++) {
      if (stats.points[i][seat] !== stats.points[i - 1][seat]) d += `H${x(i)}V${y(stats.points[i][seat], seat)}`;
    }
    return `${d}H${x(last)}`;
  });

  const ticks = stats.turnStarts.filter((t) => t.turn % 10 === 0);

  const seek = (e: PointerEvent) => {
    const box = svg.current?.getBoundingClientRect();
    if (!box) return;
    const fx = ((e.clientX - box.left) / box.width) * w;
    onSeek(Math.round(((fx - pad.l) / (w - pad.l - pad.r)) * last));
  };

  return (
    <div className="panel chart-panel">
      <div className="chart-head">
        <h3>Victory points</h3>
        <span className="muted small">click or drag to jump</span>
      </div>
      <svg
        ref={svg}
        className="chart seekable"
        viewBox={`0 0 ${w} ${h}`}
        role="img"
        aria-label="Victory points of each player over the game"
        onPointerDown={(e) => {
          (e.target as Element).setPointerCapture?.(e.pointerId);
          seek(e);
        }}
        onPointerMove={(e) => e.buttons === 1 && seek(e)}
      >
        {Array.from({ length: top / 2 + 1 }, (_, k) => k * 2).map((p) => (
          <g key={p}>
            <line className="grid" x1={pad.l} x2={w - pad.r} y1={y(p, 1.5)} y2={y(p, 1.5)} />
            <text className="axis" x={pad.l - 8} y={y(p, 1.5) + 4} textAnchor="end">
              {p}
            </text>
          </g>
        ))}
        {ticks.map((t) => (
          <text key={t.turn} className="axis" x={x(t.pos)} y={h - 6} textAnchor="middle">
            turn {t.turn}
          </text>
        ))}
        <rect className="played" x={pad.l} y={pad.t} width={Math.max(0, x(pos) - pad.l)} height={h - pad.t - pad.b} />
        {paths.map((d, seat) => (
          <path key={seat} d={d} className="points-line" stroke={seatColor(players, seat)} />
        ))}
        <line className="cursor" x1={x(pos)} x2={x(pos)} y1={pad.t - 4} y2={h - pad.b} />
        {players.map((_, seat) => (
          <circle key={seat} cx={x(pos)} cy={y(stats.points[pos][seat], seat)} r={4} fill={seatColor(players, seat)} className="cursor-dot" />
        ))}
      </svg>
    </div>
  );
}

export function DiceChart({ stats, players, pos }: ChartProps) {
  const counts = diceCounts(stats, pos);
  const rolls = counts.reduce((a, b) => a + b, 0);
  const expected = DICE_WAYS.map((n) => (rolls * n) / 36);
  const top = Math.max(4, ...counts, ...expected);
  const current = stats.rolls.find((r) => r.pos === pos);
  const sevens = counts[5];
  return (
    <div className="panel chart-panel">
      <div className="chart-head">
        <h3>Dice</h3>
        <span className="muted small">
          {rolls} rolls · {sevens} sevens (expected {(rolls / 6).toFixed(1)})
        </span>
      </div>
      <div className="dice-chart" role="img" aria-label="How often each dice sum came up, against the expected count">
        {counts.map((n, i) => (
          <div key={i} className="dice-col">
            <div className="dice-bar-area">
              <div
                className={`dice-bar${current?.sum === i + 2 ? ' now' : ''}${i === 5 ? ' seven' : ''}`}
                style={{ height: `${(n / top) * 100}%` }}
                title={`${i + 2}: ${n} times, expected ${expected[i].toFixed(1)}`}
              />
              <div className="dice-expected" style={{ bottom: `${(expected[i] / top) * 100}%` }} />
            </div>
            <span className="dice-sum">{i + 2}</span>
          </div>
        ))}
      </div>
      <p className="muted small chart-note">
        Bars: rolls so far. Lines: what fair dice give on average for this many rolls.
        {current && (
          <>
            {' '}
            Now: {seatName(players, current.seat)} rolled <b className="gold">{current.sum}</b>.
          </>
        )}
      </p>
    </div>
  );
}

export function IncomeChart({ stats, players, pos }: ChartProps) {
  const rows = income(stats, pos);
  const top = Math.max(10, ...rows.map((r) => total(r.produced)));
  return (
    <div className="panel chart-panel">
      <div className="chart-head">
        <h3>Cards from the dice</h3>
        <span className="muted small">by resource, so far</span>
      </div>
      <div className="income">
        {rows.map((r, seat) => (
          <div key={seat} className="income-row">
            <span className="income-name">
              <span className="chip" style={{ background: seatColor(players, seat) }} />
              {seatName(players, seat)}
            </span>
            <div className="income-bar">
              {RESOURCES.map((res, i) =>
                r.produced[i] > 0 ? (
                  <div key={res} className={`res-seg ${res}`} style={{ width: `${(r.produced[i] / top) * 100}%` }} title={`${r.produced[i]} ${res}`} />
                ) : null,
              )}
            </div>
            <span className="income-total mono">{total(r.produced)}</span>
            <span className="income-extra muted small">
              lost {r.stolenFrom} to the robber · stole {r.stoleFromOthers} · discarded {r.discarded}
            </span>
          </div>
        ))}
      </div>
      <div className="legend small muted">
        {RESOURCES.map((res) => (
          <span key={res}>
            <span className={`swatch res-seg ${res}`} /> {res}
          </span>
        ))}
      </div>
    </div>
  );
}
