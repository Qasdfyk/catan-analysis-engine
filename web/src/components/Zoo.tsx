// The bot zoo of catan-project: what each archetype builds and who beats
// whom, from a tournament of zoo bots (numbers in facts.ts).

import type { CSSProperties } from 'react';
import { PLATFORM_URL, ZOO } from '../facts';

const pct = (x: number) => `${Math.round(x * 100)}%`;

/** Green above an even score, red below, stronger the further from 50%. */
function cellStyle(share: number): CSSProperties {
  const strength = Math.min(1, Math.abs(share - 0.5) * 3);
  const color = share >= 0.5 ? '127, 196, 106' : '229, 83, 75';
  return { background: `rgba(${color}, ${0.08 + 0.4 * strength})` };
}

/** A share drawn as a bar in the resource's colour, with the number. */
function Share({ value, colors }: { value: number; colors: string }) {
  return (
    <span className="zoo-share">
      <span className="zoo-bar" style={{ width: `${Math.round(value * 60)}px`, background: colors }} />
      <span className="mono">{pct(value)}</span>
    </span>
  );
}

export function Zoo() {
  const names = ZOO.archetypes.map((a) => a.name);
  return (
    <div className="section zoo">
      <div>
        <div className="section-head">
          <h2>A zoo of bots</h2>
          <p>
            To judge a move, the engine will play the rest of the game out many times, so the opponents in those games must play
            as differently as people do. They come from a zoo: four building archetypes times four trading personalities, with
            every weight shaken by about 20% each game.
          </p>
        </div>
        <div className="panel zoo-scroll">
          <table className="zoo-table">
            <thead>
              <tr>
                <th>archetype</th>
                <th>opening on ore + wheat</th>
                <th>on wood + brick</th>
                <th>cities</th>
                <th>settl.</th>
                <th>roads</th>
                <th>dev cards</th>
              </tr>
            </thead>
            <tbody>
              {ZOO.archetypes.map((a) => (
                <tr key={a.name}>
                  <th scope="row">
                    {a.name}
                    <span className="muted small">{a.note}</span>
                  </th>
                  <td>
                    <Share value={a.setupOreWheat} colors="linear-gradient(90deg, var(--ore), var(--wheat))" />
                  </td>
                  <td>
                    <Share value={a.setupWoodBrick} colors="linear-gradient(90deg, var(--wood), var(--brick))" />
                  </td>
                  <td className="mono">{a.cities.toFixed(1)}</td>
                  <td className="mono">{a.settlements.toFixed(1)}</td>
                  <td className="mono">{a.roads.toFixed(1)}</td>
                  <td className="mono">{a.devCards.toFixed(1)}</td>
                </tr>
              ))}
            </tbody>
          </table>
          <p className="muted small">
            Per player, at the end of {ZOO.games} games of four zoo bots. The opening is the share of the dice that pay the two
            starting settlements. Bots and tournament are in{' '}
            <a href={`${PLATFORM_URL}/blob/main/docs/bot-zoo.md`}>catan-project</a>.
          </p>
        </div>
      </div>
      <div>
        <div className="section-head">
          <h2>Who beats whom</h2>
          <p>How often the row finished above the column at the same table. No archetype wins everything, which is the point.</p>
        </div>
        <div className="panel zoo-scroll h2h-panel">
          <table className="h2h">
            <thead>
              <tr>
                <th />
                {names.map((n) => (
                  <th key={n} scope="col">
                    {n}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {names.map((a) => (
                <tr key={a}>
                  <th scope="row">{a}</th>
                  {names.map((b) => {
                    const share = (ZOO.headToHead as Record<string, Record<string, number>>)[a][b];
                    return a === b ? (
                      <td key={b} className="muted">
                        —
                      </td>
                    ) : (
                      <td key={b} className="mono" style={cellStyle(share)}>
                        {pct(share)}
                      </td>
                    );
                  })}
                </tr>
              ))}
            </tbody>
          </table>
          <p className="muted small">
            Wins per seat: {ZOO.archetypes.map((a) => `${a.name} ${pct(a.win)}`).join(', ')}; an even share is 25%.
          </p>
        </div>
      </div>
    </div>
  );
}
