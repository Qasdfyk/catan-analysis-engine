// The map of styles of step 10c: MAP-Elites keeps the strongest bot of every
// style, and this shows them, one slice of the grid at a time (data in
// style-map.json, written by scripts/style-map-web.py from data/elites.json).

import { useState } from 'react';
import type { CSSProperties } from 'react';
import { REPO_URL } from '../facts';
import MAP from '../style-map.json';

type Elite = (typeof MAP.elites)[number];

const pct = (x: number) => `${(x * 100).toFixed(1)}%`;
const CITY_BINS = MAP.grid.city_share.length + 1;
const TRADE_BINS = MAP.grid.trades.length + 1;
const DEV_BINS = MAP.grid.dev_cards.length + 1;

/** "< a", "a–b", …, "z+" for the bins between edges. */
function binLabels(edges: readonly number[], format: (x: number) => string): string[] {
  const out = [`< ${format(edges[0])}`];
  for (let i = 1; i < edges.length; i++) out.push(`${format(edges[i - 1])}–${format(edges[i])}`);
  out.push(`${format(edges[edges.length - 1])}+`);
  return out;
}

const cityLabels = binLabels(MAP.grid.city_share, (x) => `${Math.round(x * 100)}%`);
const tradeLabels = ['none', ...binLabels(MAP.grid.trades, (x) => `${x}`).slice(1)];
const devLabels = binLabels(MAP.grid.dev_cards, (x) => `${x}`);

const cellIndex = (city: number, trades: number, dev: number) => city + CITY_BINS * (trades + TRADE_BINS * dev);
const byCell = new Map<number, Elite>(MAP.elites.map((e) => [e.cell, e]));
const zooByCell = new Map<number, string[]>();
for (const z of MAP.zoo) zooByCell.set(z.cell, [...(zooByCell.get(z.cell) ?? []), z.name]);
const best = MAP.elites.reduce((a, b) => (b.win > a.win ? b : a));
const hard = MAP.zoo.find((z) => z.name === 'balanced/fair');
const bestZoo = MAP.zoo.reduce((a, b) => (b.win > a.win ? b : a));

/** Green above an even 25%, red below, stronger the further away. */
function cellStyle(win: number): CSSProperties {
  const strength = Math.min(1, Math.abs(win - 0.25) * 5);
  const color = win >= 0.25 ? '127, 196, 106' : '229, 83, 75';
  return { background: `rgba(${color}, ${0.08 + 0.5 * strength})` };
}

function Detail({ elite, zoo }: { elite: Elite | undefined; zoo: string[] }) {
  if (!elite) {
    return (
      <div className="panel style-detail">
        <p className="muted">No bot of this style was found. Some corners of the map are hard to reach, such as many cities with no trading at all.</p>
      </div>
    );
  }
  const g = elite.genes;
  const genes: [string, string][] = [
    ['city / settlement / dev card', `${g.city} / ${g.settlement} / ${g.devCard}`],
    ['port, variety', `${g.port}, ${g.variety}`],
    ['roads behind the Longest Road', `${g.roadReach}`],
    ['Robber on the leader', g.leaderFocus > 0 ? `×${(1 + g.leaderFocus).toFixed(2)}` : 'as hard'],
    ['offers per turn', `${g.offersPerTurn}`],
    ['refuses', ['nobody', 'the leader', 'anyone ahead'][g.embargo]],
    ['takes even trades', g.neutral ? 'yes' : 'no'],
  ];
  return (
    <div className="panel style-detail">
      <div className="style-detail-head">
        <span className="mono style-win">{pct(elite.win)}</span>
        <span className="muted small">± {(elite.half * 100).toFixed(1)} against 3 random zoo bots</span>
      </div>
      <p className="small muted">
        mutated from <b>{elite.from}</b>; plays {Math.round(elite.cityShare * 100)}% of its building points as cities,
        {` ${elite.trades}`} trades and {elite.devCards} development cards a game
      </p>
      <dl className="style-genes">
        {genes.map(([k, v]) => (
          <div key={k}>
            <dt>{k}</dt>
            <dd className="mono">{v}</dd>
          </div>
        ))}
      </dl>
      {zoo.length > 0 && <p className="small muted">Hand-made zoo bots of this style: {zoo.join(', ')}</p>}
    </div>
  );
}

export function StyleMap() {
  const [dev, setDev] = useState(best.bins[2]);
  const [cell, setCell] = useState(best.cell);
  const pickDev = (d: number) => {
    setDev(d);
    const b = byCell.get(cell);
    const city = b ? b.bins[0] : 0;
    const trades = b ? b.bins[1] : 0;
    setCell(cellIndex(city, trades, d));
  };
  return (
    <div className="section zoo">
      <div className="section-head">
        <h2>A map of styles</h2>
        <p>
          The zoo's weights were set by hand. Tuning them for wins alone would turn every bot into the same bot, and the
          simulations need opponents that play differently. MAP-Elites keeps both: styles are cut into cells by what a bot
          actually does in its games (cities against settlements, trades, development cards), and every cell keeps the strongest
          bot seen in that style. New bots are mutated copies of elites. {MAP.candidates.toLocaleString('en-US')} candidates,{' '}
          {(MAP.gamesPlayed / 1e6).toFixed(1)} million games.
        </p>
      </div>
      <div className="style-map">
        <div className="panel">
          <div className="style-controls">
            <span className="muted small">development cards a game</span>
            <div className="segmented" role="radiogroup" aria-label="Development cards a game">
              {devLabels.map((label, d) => (
                <button key={label} role="radio" aria-checked={dev === d} className={dev === d ? 'on' : ''} onClick={() => pickDev(d)}>
                  {label}
                </button>
              ))}
            </div>
          </div>
          <div className="style-grid" style={{ gridTemplateColumns: `auto repeat(${CITY_BINS}, minmax(0, 1fr))` }}>
            {Array.from({ length: TRADE_BINS }, (_, row) => TRADE_BINS - 1 - row).map((t) => (
              <div className="style-row" key={t} style={{ display: 'contents' }}>
                <span className="style-axis mono">{tradeLabels[t]}</span>
                {Array.from({ length: CITY_BINS }, (_, c) => {
                  const index = cellIndex(c, t, dev);
                  const e = byCell.get(index);
                  const zoo = zooByCell.get(index);
                  return (
                    <button
                      key={c}
                      className={`style-cell${index === cell ? ' on' : ''}${e ? '' : ' empty'}`}
                      style={e ? cellStyle(e.win) : undefined}
                      onClick={() => setCell(index)}
                      aria-label={`${cityLabels[c]} cities, ${tradeLabels[t]} trades: ${e ? pct(e.win) : 'empty'}`}
                    >
                      <span className="mono">{e ? `${Math.round(e.win * 100)}` : '·'}</span>
                      {zoo && <span className="style-zoo" title={zoo.join(', ')} />}
                    </button>
                  );
                })}
              </div>
            ))}
            <span />
            {cityLabels.map((l) => (
              <span key={l} className="style-axis mono center">
                {l}
              </span>
            ))}
          </div>
          <p className="muted small style-legend">
            Across: share of building points from cities. Up: trades with players a game. Numbers: wins in 100 games against
            three random zoo bots (25 is even). <span className="style-zoo inline" /> a hand-made zoo bot plays this style.
          </p>
        </div>
        <Detail elite={byCell.get(cell)} zoo={zooByCell.get(cell) ?? []} />
      </div>
      <p className="muted small">
        {MAP.elites.length} of {CITY_BINS * TRADE_BINS * DEV_BINS} styles found. The strongest elite wins {pct(best.win)},
        the best hand-made bot ({bestZoo.name}) {pct(bestZoo.win)}
        {hard && bestZoo !== hard ? `, the hard level ${pct(hard.win)}` : ''}, all on the same{' '}
        {MAP.games.toLocaleString('en-US')} deals that played no part in the search. See{' '}
        <a href={`${REPO_URL}/blob/main/docs/map-elites.md`}>docs/map-elites.md</a>.
      </p>
    </div>
  );
}
