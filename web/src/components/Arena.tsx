// How strong the bots are: a few arena matches, picked to show the control,
// the platform's hard bot against Catanatron, and the searching bot against
// both (all matches in docs/arena.md; numbers in facts.ts).

import { ARENA, REPO_URL } from '../facts';

const pct = (x: number) => `${(x * 100).toFixed(1)}%`;

/** The win rate on a 0-100% axis, its 95% interval and the even share. */
function WinBar({ win, half }: { win: number; half: number }) {
  const w = 160;
  const x = (v: number) => Math.min(1, Math.max(0, v)) * w;
  const above = win - half > 0.25;
  const below = win + half < 0.25;
  const color = above ? 'var(--green)' : below ? 'var(--danger)' : 'var(--muted)';
  return (
    <svg width={w} height={18} viewBox={`0 0 ${w} 18`} role="img" aria-label={`${pct(win)}, 95% interval ±${pct(half)}`}>
      <line x1={0} x2={w} y1={9} y2={9} stroke="var(--line)" />
      <line x1={x(0.25)} x2={x(0.25)} y1={2} y2={16} stroke="var(--muted)" strokeDasharray="2 2" />
      <line x1={x(win - half)} x2={x(win + half)} y1={9} y2={9} stroke={color} strokeWidth={3} strokeLinecap="round" />
      <circle cx={x(win)} cy={9} r={4} fill={color} />
    </svg>
  );
}

export function Arena() {
  return (
    <section className="section">
      <div className="section-head">
        <h2>How strong the bots are</h2>
        <p>
          One candidate plays three copies of an opponent, so an equal player wins 25%; a sequential test (SPRT) stops a match
          once the answer is clear. Catanatron, the strongest public Catan bot, plays through a bridge under our rules. The
          searching bot (ISMCTS) sees only what its own seat sees.
        </p>
      </div>
      <div className="panel zoo-scroll">
        <table className="zoo-table">
          <thead>
            <tr>
              <th>candidate</th>
              <th>against 3×</th>
              <th>wins</th>
              <th>
                0 <span className="muted">· 25% ·</span> 100%
              </th>
              <th>games</th>
            </tr>
          </thead>
          <tbody>
            {ARENA.matches.map((m) => (
              <tr key={`${m.candidate}-${m.opponents}`}>
                <th scope="row">
                  {m.candidate}
                  {m.note && <span className="muted small">{m.note}</span>}
                </th>
                <td>{m.opponents}</td>
                <td className="mono">
                  {pct(m.win)} <span className="muted">±{(m.half * 100).toFixed(1)}</span>
                </td>
                <td>
                  <WinBar win={m.win} half={m.half} />
                </td>
                <td className="mono">{m.games.toLocaleString('en-US')}</td>
              </tr>
            ))}
          </tbody>
        </table>
        <p className="muted small">
          ± is the 95% interval. More matches, the bot zoo and the test sets: <a href={`${REPO_URL}/blob/main/docs/arena.md`}>docs/arena.md</a>.
        </p>
      </div>
    </section>
  );
}
