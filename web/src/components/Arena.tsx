// The arena and the test sets of step 10b: fair matches between bots, a
// frozen set of positions to score win chances on, and positions with an
// obvious answer (numbers in facts.ts, from docs/arena.md).

import { ARENA, REPO_URL } from '../facts';

const pct = (x: number) => `${(x * 100).toFixed(1)}%`;

/** The win rate on a 0-50% axis, its 95% interval and the even share. */
function WinBar({ win, half }: { win: number; half: number }) {
  const w = 160;
  const x = (v: number) => (Math.min(0.5, Math.max(0, v)) / 0.5) * w;
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
  const worst = ARENA.brier[ARENA.brier.length - 1].value;
  return (
    <div className="section zoo">
      <div>
        <div className="section-head">
          <h2>The arena</h2>
          <p>
            Before any training, a fair way to tell which of two players is stronger. One candidate plays against three copies of
            an opponent; an equal player wins 25%. A sequential test (SPRT) stops a match as soon as the answer is clear.
            Catanatron, the strongest public Catan bot, plays through a bridge to a separate process, under our rules.
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
                  0 <span className="muted">· 25% ·</span> 50%
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
            ± is the 95% interval. Run them again with <code>catan_arena</code>; see{' '}
            <a href={`${REPO_URL}/blob/main/docs/arena.md`}>docs/arena.md</a>.
          </p>
        </div>
      </div>
      <div className="section two">
        <div>
          <div className="section-head">
            <h2>Scoring the chances</h2>
            <p>
              A frozen set of 4,000 positions from games the engine played itself, never used for training. Every way of
              estimating the chance to win is scored on it; anything new has to beat the baseline model clearly.
            </p>
          </div>
          <div className="panel zoo-scroll">
            <table className="zoo-table wrap">
              <tbody>
                {ARENA.brier.map((b) => (
                  <tr key={b.name}>
                    <th scope="row">{b.name}</th>
                    <td>
                      <span className="zoo-share">
                        <span
                          className="zoo-bar"
                          style={{ width: `${Math.round((b.value / worst) * 60)}px`, background: 'var(--gold)' }}
                        />
                        <span className="mono">{b.value.toFixed(3)}</span>
                      </span>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
            <p className="muted small">Brier score: mean squared error of the chance against who won. Lower is better.</p>
          </div>
        </div>
        <div>
          <div className="section-head">
            <h2>Obvious moves</h2>
            <p>Positions with one clear answer, each set up on real positions from 200 games. The hard bot:</p>
          </div>
          <div className="panel zoo-scroll">
            <table className="zoo-table wrap">
              <tbody>
                {ARENA.sanity.map((s) => (
                  <tr key={s.name}>
                    <th scope="row">{s.name}</th>
                    <td className="mono" style={{ color: s.passed === s.tried ? 'var(--green)' : 'var(--danger)' }}>
                      {s.passed} / {s.tried}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
            <p className="muted small">The future expert bot has to pass all of them.</p>
          </div>
        </div>
      </div>
    </div>
  );
}
