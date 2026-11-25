// The move review on the page: the grade of the move at the shown position
// with the best move and "why", the count of grades, the moves that lost
// the most, and the best move marked on the board. Numbers come from
// grades.ts, words from why.ts.

import { getEdgeEndpoints, getVertexCoords, HEX_SIZE, hexToPixel } from '@platform/utils/hexLayout';
import { PLAYER_COLORS } from '@platform/utils/labels';
import {
  BUDGET,
  countGrades,
  GRADE_LABEL,
  GRADE_MARK,
  reviewAt,
  shown,
  SHOWN_GRADES,
  worstMoves,
  type ActionJson,
  type GradeRun,
  type MoveReview,
} from '../grades';
import type { Topology } from '../types';
import { position } from '../view';
import { actionText, whyText, type Names } from '../why';
import { fmt } from './Charts';
import { seatName } from './GamePanels';

// The viewBox of catan-project's Board, so the overlay lands on its corners.
const EXTENT = HEX_SIZE * 5.3;
const VIEW_BOX = `${-EXTENT} ${-EXTENT} ${EXTENT * 2} ${EXTENT * 2}`;

const pct = (x: number) => `${(100 * x).toFixed(0)}%`;
const pp = (x: number) => `${(100 * x).toFixed(1)} pp`;

/** A grade as the chess-style mark, coloured by grade. */
export function GradeMark({ review }: { review: MoveReview }) {
  const g = shown(review.grade);
  return (
    <span className={`grade-mark ${g}`} title={GRADE_LABEL[g]}>
      {GRADE_MARK[g]}
    </span>
  );
}

/** Where a move happens on the board, if anywhere: a corner, a side or a hex. */
function Spot({ a, topo, color, dashed }: { a: ActionJson; topo: Topology; color: string; dashed?: boolean }) {
  const t = a.target ?? -1;
  const dash = dashed ? '6 5' : undefined;
  if ((a.kind === 'build_settlement' || a.kind === 'upgrade_city') && topo.nodes[t]) {
    const p = getVertexCoords(position(topo.nodes[t]));
    return <circle cx={p.x} cy={p.y} r={17} fill="none" stroke={color} strokeWidth={4} strokeDasharray={dash} />;
  }
  if (a.kind === 'build_road' && topo.edges[t]) {
    const [p, q] = getEdgeEndpoints(position(topo.edges[t]));
    return <line x1={p.x} y1={p.y} x2={q.x} y2={q.y} stroke={color} strokeWidth={9} strokeLinecap="round" strokeDasharray={dash} opacity={0.85} />;
  }
  if (a.kind === 'move_robber' && topo.hexes[t]) {
    const h = topo.hexes[t];
    const p = hexToPixel({ q: h.q, r: h.r, s: -h.q - h.r });
    return <circle cx={p.x} cy={p.y} r={HEX_SIZE * 0.62} fill="none" stroke={color} strokeWidth={5} strokeDasharray={dash} />;
  }
  return null;
}

/** Drawn over the board while a graded move is shown: the best move dashed white, the move played in the player's colour. */
export function BestMoveSpot({ run, pos, topo, color }: { run: GradeRun | null; pos: number; topo: Topology; color: string }) {
  const at = reviewAt(run, pos);
  if (!at || shown(at.review.grade) === 'good') return null;
  const { review } = at;
  return (
    <svg className="board best-move" viewBox={VIEW_BOX} aria-label="The best move">
      <Spot a={review.played} topo={topo} color={color} />
      <Spot a={review.best} topo={topo} color="#fff" dashed />
    </svg>
  );
}

function Progress({ run }: { run: GradeRun }) {
  const done = run.reviews.filter(Boolean).length;
  const total = run.decisions.length;
  if (run.precomputed) return <span className="muted small">{total} decisions, computed ahead by the same engine</span>;
  if (total > 0 && done === total) {
    return (
      <span className="muted small">
        {total} decisions in {run.seconds.toFixed(0)} s on {run.workers} workers
      </span>
    );
  }
  return (
    <span className="muted small">
      grading… {done} of {total || '?'} decisions
    </span>
  );
}

function Current({ review, made, names }: { review: MoveReview; made: boolean; names: Names }) {
  const g = shown(review.grade);
  const why = whyText(review, names);
  const same = review.grade === 'best';
  return (
    <div className={`move-card ${g}`}>
      <div className="move-card-head">
        <GradeMark review={review} />
        <strong>{GRADE_LABEL[g]}</strong>
        <span className="muted small">{made ? `turn ${review.turn}` : `next move · turn ${review.turn}`}</span>
      </div>
      <p>
        Played: <b>{actionText(review.played, names)}</b>
        {!same && (
          <>
            <br />
            Best: <b className="best-move-text">{actionText(review.best, names)}</b>
          </>
        )}
      </p>
      {same ? (
        <p className="muted small">
          The review's best move: {pct(review.played_value)} chance to win after it, {review.candidates} moves compared.
        </p>
      ) : (
        <p className="muted small">
          Chance to win after the best move {pct(review.best_value)}, after this one {pct(review.played_value)}: {pp(review.loss)} lost
          ± {pp(1.96 * review.error)}
          {review.within_noise ? ', within the noise, so counted as good' : ''}.
        </p>
      )}
      {why && <p className="why">{why}</p>}
    </div>
  );
}

/**
 * The review of one player's moves: the move at the shown position, the
 * count of grades so far, and the moves that lost the most (click to go).
 */
export function MoveReviewPanel({
  run,
  names,
  pos,
  focus,
  onSeek,
}: {
  run: GradeRun | null;
  names: Names;
  pos: number;
  focus: number;
  onSeek: (pos: number) => void;
}) {
  const color = PLAYER_COLORS[names.players[focus]?.color] ?? '#888';
  const at = reviewAt(run, pos);
  const counts = countGrades(run);
  const worst = worstMoves(run);
  return (
    <div className="panel chart-panel move-review">
      <div className="chart-head">
        <h3>
          <span className="chip" style={{ background: color }} /> {seatName(names.players, focus)}'s moves
        </h3>
        {run ? <Progress run={run} /> : <span className="muted small">starting the engine…</span>}
      </div>
      {run?.error && <p className="message error small">The move review failed: {run.error}</p>}
      <div className="grade-counts">
        {SHOWN_GRADES.map((g) => (
          <span key={g} className={`grade-count ${g}`}>
            <span className={`grade-mark ${g}`}>{GRADE_MARK[g]}</span> <b className="mono">{counts[g]}</b> {GRADE_LABEL[g]}
          </span>
        ))}
      </div>
      {at ? (
        <Current review={at.review} made={at.made} names={names} />
      ) : (
        <p className="muted small move-card-empty">
          Step to one of {seatName(names.players, focus)}'s moves to see its grade, or pick one below. Graded: building,
          buying and playing cards, trades they offer, the robber. Not graded: the setup (it has its own review), answers to
          offers, discards.
        </p>
      )}
      {worst.length > 0 && (
        <>
          <h4 className="small muted">Moves that lost the most</h4>
          <ol className="worst">
            {worst.map((r) => (
              <li key={r.state}>
                <button type="button" className={pos === r.state + 1 ? 'on' : ''} onClick={() => onSeek(r.state + 1)}>
                  <GradeMark review={r} /> turn {r.turn}: {actionText(r.played, names)}
                  <span className="mono small"> −{pp(r.loss)}</span>
                </button>
              </li>
            ))}
          </ol>
        </>
      )}
      <p className="muted small chart-note">
        Each decision: the moves the searching bot would consider, narrowed by successive halving, then {fmt(BUDGET.confirm)}{' '}
        fresh playouts for the best one and the move played, with the same dice and hidden cards for both. Hidden cards are
        drawn from what the player could know, never from the record. Grades by the chance to win lost: inaccuracy from 3 pp,
        mistake from 6, blunder from 12.
      </p>
    </div>
  );
}

/**
 * A finished review at a glance, for the start page: the count of grades and
 * the moves that lost the most, each with the best move and why. A click
 * takes the board there.
 */
export function KeyMoves({ run, names, onSeek, count = 3 }: { run: GradeRun; names: Names; onSeek: (pos: number) => void; count?: number }) {
  const counts = countGrades(run);
  const color = PLAYER_COLORS[names.players[run.seat]?.color] ?? '#888';
  return (
    <div className="panel chart-panel move-review">
      <div className="chart-head">
        <h3>
          <span className="chip" style={{ background: color }} /> {seatName(names.players, run.seat)}'s moves
        </h3>
        <Progress run={run} />
      </div>
      <div className="grade-counts">
        {SHOWN_GRADES.map((g) => (
          <span key={g} className={`grade-count ${g}`}>
            <span className={`grade-mark ${g}`}>{GRADE_MARK[g]}</span> <b className="mono">{counts[g]}</b> {GRADE_LABEL[g]}
          </span>
        ))}
      </div>
      <div className="key-moves">
        {worstMoves(run, count).map((r) => (
          <button type="button" key={r.state} className="key-move" onClick={() => onSeek(r.state + 1)} title="Show this move on the board">
            <Current review={r} made names={names} />
          </button>
        ))}
      </div>
    </div>
  );
}
