// The setup review on the page: a heat map over the corners of the board
// while a setup settlement is shown, and a panel grading all eight of them.
// Numbers come from setup.ts; both fill in as the workers finish rounds.

import { useState } from 'react';
import { HEX_SIZE, getVertexCoords } from '@platform/utils/hexLayout';
import { PLAYER_COLORS } from '@platform/utils/labels';
import { bestValue, BUDGET, heatColor, verdict, type Review, type SetupMove, type SetupRun } from '../setup';
import type { RecordPlayer, Topology } from '../types';
import { position } from '../view';
import { fmt } from './Charts';
import { seatName } from './GamePanels';

// The viewBox of catan-project's Board, so the overlay lands on its corners.
const EXTENT = HEX_SIZE * 5.3;
const VIEW_BOX = `${-EXTENT} ${-EXTENT} ${EXTENT * 2} ${EXTENT * 2}`;

const pct = (x: number) => `${(100 * x).toFixed(1)}%`;
// A loss as a change of chance: −3.2 lost, +0.4 gained, 0.0 for the best corner itself.
const pp = (x: number) => {
  const v = Math.abs(100 * x).toFixed(1);
  return v === '0.0' ? v : `${x > 0 ? '−' : '+'}${v}`;
};

/** Drawn over the board: every legal corner of the settlement in focus, coloured by how far behind the best it is. */
export function SetupHeat({ review, move, topo, players }: { review: Review; move: SetupMove; topo: Topology; players: RecordPlayer[] }) {
  const top = bestValue(review);
  const at = (node: number) => getVertexCoords(position(topo.nodes[node]));
  return (
    <svg className="board setup-heat" viewBox={VIEW_BOX} aria-label="Chance to win from each corner">
      {review.candidates.map((c, i) => {
        const p = at(c.node);
        const behind = top - c.value;
        const strong = c.finalist || !c.dropped;
        return (
          <g key={c.node} opacity={strong ? 0.95 : 0.6}>
            <title>
              corner {c.node}: {pct(c.value)} chance to win ({fmt(c.playouts)} playouts
              {c.dropped ? ', dropped by the halving' : c.finalist ? ', finalist' : ''})
            </title>
            <circle cx={p.x} cy={p.y} r={strong ? 13 : 8} fill={heatColor(behind)} stroke="#111" strokeWidth={1.5} />
            {i === review.best && review.done && <circle cx={p.x} cy={p.y} r={19} fill="none" stroke="#fff" strokeWidth={3} strokeDasharray="5 4" />}
            {c.node === move.node && (
              <circle cx={p.x} cy={p.y} r={18} fill="none" stroke={PLAYER_COLORS[players[move.seat]?.color] ?? '#fff'} strokeWidth={4} />
            )}
            {c.finalist && (
              <text x={p.x} y={p.y - 22} className="setup-heat-label">
                {Math.round(100 * c.value)}
              </text>
            )}
          </g>
        );
      })}
    </svg>
  );
}

const VERDICT = { best: 'best', noise: 'within noise', loss: 'loses' } as const;

/**
 * The settlements of the setup: the corner played, the best one, the loss, a
 * verdict. The player the review is for by default, everyone on request.
 * Click a row to go there.
 */
export function SetupPanel({
  run,
  players,
  pos,
  focus,
  onSeek,
}: {
  run: SetupRun | null;
  players: RecordPlayer[];
  pos: number;
  focus: number;
  onSeek: (pos: number) => void;
}) {
  const [everyone, setEveryone] = useState(false);
  if (!run) return null;
  const done = run.reviews.filter((r) => r?.done).length;
  const playouts = run.reviews.reduce((a, r) => a + (r?.playouts ?? 0), 0);
  return (
    <div className="panel chart-panel setup-panel">
      <div className="chart-head">
        <h3>{everyone ? 'Initial placement' : `${seatName(players, focus)}'s initial placement`}</h3>
        <span className="muted small">
          {run.error
            ? run.error
            : run.precomputed
              ? `${fmt(playouts)} playouts, computed ahead by the same engine`
            : done < run.moves.length
              ? `reviewing… ${done} of ${run.moves.length} settlements · ${fmt(playouts)} playouts`
              : `${fmt(playouts)} playouts in ${run.seconds.toFixed(1)} s on ${run.workers} workers`}
        </span>
      </div>
      <table className="setup-table">
        <thead>
          <tr>
            <th>Player</th>
            <th className="num">Played</th>
            <th className="num">Best</th>
            <th className="num">Loss (pp)</th>
            <th />
          </tr>
        </thead>
        <tbody>
          {run.moves.map((m, i) => {
            if (!everyone && m.seat !== focus) return null;
            const r = run.reviews[i];
            const v = r ? verdict(r) : null;
            const shown = pos === m.state || pos === m.state + 1;
            return (
              <tr key={i} className={shown ? 'current' : ''} onClick={() => onSeek(m.state + 1)}>
                <td>
                  <span className="chip" style={{ background: PLAYER_COLORS[players[m.seat]?.color] ?? '#888' }} />
                  {seatName(players, m.seat)} <span className="muted small">#{m.round + 1}</span>
                </td>
                <td className="num mono">{r && r.played >= 0 ? pct(r.candidates[r.played].value) : '…'}</td>
                <td className="num mono">{r?.done ? pct(r.candidates[r.best].value) : '…'}</td>
                <td className="num mono">
                  {r?.loss ? (
                    <>
                      {pp(r.loss.loss)} <span className="muted">± {(196 * r.loss.error).toFixed(1)}</span>
                    </>
                  ) : (
                    '…'
                  )}
                </td>
                <td>{v && <span className={`verdict ${v}`}>{VERDICT[v]}</span>}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
      <label className="small muted check">
        <input type="checkbox" checked={everyone} onChange={(e) => setEveryone(e.target.checked)} /> all players
      </label>
      <p className="muted small chart-note">
        For each settlement of the setup, every corner the player could have taken is played out to the end by
        MAP-Elites bots. Successive halving gives every corner {BUDGET.first} playouts, keeps the better half and
        doubles theirs, until {BUDGET.finalists} are left (plus the corner played); then fresh {fmt(BUDGET.confirm)}{' '}
        playouts each decide the loss, so the best corner is not flattered by the noise that picked it. ± is the 95%
        interval. On the board: green is as good as the best, red 15 points or more behind; the ring marks the corner
        played, the dashed ring the best one, numbers the finalists' chances.
      </p>
    </div>
  );
}
