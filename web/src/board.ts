// The board as one SVG string, redrawn for every state. A full board is a
// few hundred elements, which the browser redraws faster than a frame, so
// there is no diffing here: what changed since the previous state is only
// highlighted.

import { HEX_SIZE, type Layout, hexPolygon, pushOut } from './geometry';
import type { BoardInfo, GameState, RecordPlayer } from './types';

export const PLAYER_COLORS: Record<string, string> = {
  red: '#d33a2c',
  blue: '#2f6fd0',
  white: '#f4f1e8',
  orange: '#ee8a1d',
};

const TERRAIN_COLORS: Record<string, string> = {
  wood: '#3f7a3a',
  brick: '#bf5f36',
  sheep: '#9cc95a',
  wheat: '#e6c14a',
  ore: '#8a8f99',
  desert: '#dcc79a',
};

/** Pieces that appeared in this state, to draw with a glow. */
export interface Changes {
  nodes: Set<number>;
  edges: Set<number>;
  robberMoved: boolean;
}

export function changesBetween(before: GameState | null, after: GameState): Changes {
  const nodes = new Set<number>();
  const edges = new Set<number>();
  if (before) {
    after.players.forEach((p, seat) => {
      const was = before.players[seat];
      // A city is new when it was not a city before: an upgrade keeps the
      // corner but is still the move to show.
      for (const n of p.settlements) if (!was.settlements.includes(n)) nodes.add(n);
      for (const n of p.cities) if (!was.cities.includes(n)) nodes.add(n);
      for (const e of p.roads) if (!was.roads.includes(e)) edges.add(e);
    });
  }
  return { nodes, edges, robberMoved: before !== null && before.robber !== after.robber };
}

// Dots under a number: how many of the 36 dice outcomes roll it.
const pips = (n: number) => (n === 0 ? 0 : 6 - Math.abs(7 - n));

export function boardSvg(
  geo: Layout,
  board: BoardInfo,
  state: GameState,
  players: RecordPlayer[],
  changes: Changes,
): string {
  const out: string[] = [];
  const s = HEX_SIZE;
  out.push(`<svg viewBox="${-5.4 * s} ${-4.8 * s} ${10.8 * s} ${9.6 * s}" role="img" aria-label="Board">`);
  // Sea: a big hexagon behind the land.
  out.push(`<polygon class="sea" points="${hexPolygon({ x: 0, y: 0 }, s * 5.3)}" transform="rotate(30)"/>`);

  // Ports: a marker off the coast with a line to each of its two corners.
  for (const port of board.ports) {
    const [a, b] = port.nodes.map((n) => geo.nodes[n]);
    const mid = pushOut({ x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 }, s * 0.55);
    const resource = port.type.split('_')[0];
    const fill = port.type === '3:1' ? '#fdf8ec' : TERRAIN_COLORS[resource];
    out.push(`<g class="port"><title>${port.type} port</title>`);
    for (const p of [a, b]) out.push(`<line x1="${p.x}" y1="${p.y}" x2="${mid.x}" y2="${mid.y}"/>`);
    out.push(`<circle cx="${mid.x}" cy="${mid.y}" r="${s * 0.26}" fill="${fill}"/>`);
    out.push(`<text x="${mid.x}" y="${mid.y}">${port.type === '3:1' ? '3:1' : '2:1'}</text></g>`);
  }

  board.hexes.forEach((h, i) => {
    const c = geo.hexes[i];
    out.push(`<polygon class="hex" points="${hexPolygon(c, s * 0.97)}" fill="${TERRAIN_COLORS[h.terrain]}">`);
    out.push(`<title>hex ${i}: ${h.terrain}${h.number ? ` ${h.number}` : ''}</title></polygon>`);
    if (h.number) {
      const hot = h.number === 6 || h.number === 8 ? ' hot' : '';
      out.push(`<circle class="token" cx="${c.x}" cy="${c.y}" r="${s * 0.3}"/>`);
      out.push(`<text class="number${hot}" x="${c.x}" y="${c.y - s * 0.03}">${h.number}</text>`);
      const dots = pips(h.number);
      for (let d = 0; d < dots; d++) {
        const x = c.x + (d - (dots - 1) / 2) * s * 0.07;
        out.push(`<circle class="pip${hot}" cx="${x}" cy="${c.y + s * 0.17}" r="${s * 0.025}"/>`);
      }
    }
  });

  // Robber: a dark pawn on its hex, offset so the number stays readable.
  const rc = geo.hexes[state.robber];
  if (rc) {
    const glow = changes.robberMoved ? ' changed' : '';
    out.push(`<g class="robber${glow}" transform="translate(${rc.x - s * 0.45} ${rc.y - s * 0.12})">`);
    out.push(`<title>Robber</title><ellipse cx="0" cy="${s * 0.22}" rx="${s * 0.15}" ry="${s * 0.06}"/>`);
    out.push(`<path d="M ${-s * 0.11} ${s * 0.2} Q 0 ${-s * 0.25} ${s * 0.11} ${s * 0.2} Z"/>`);
    out.push(`<circle cx="0" cy="${-s * 0.1}" r="${s * 0.09}"/></g>`);
  }

  // Roads under buildings, so a settlement covers the road ends.
  state.players.forEach((p, seat) => {
    const color = PLAYER_COLORS[players[seat]?.color] ?? '#888';
    for (const e of p.roads) {
      const [a, b] = geo.edges[e];
      // Shorten each road a little so it does not run into the corners.
      const ax = a.x + (b.x - a.x) * 0.16;
      const ay = a.y + (b.y - a.y) * 0.16;
      const bx = b.x + (a.x - b.x) * 0.16;
      const by = b.y + (a.y - b.y) * 0.16;
      const cls = changes.edges.has(e) ? 'road changed' : 'road';
      out.push(`<g class="${cls}"><title>Road of ${players[seat]?.name} (side ${e})</title>`);
      out.push(`<line class="road-edge" x1="${ax}" y1="${ay}" x2="${bx}" y2="${by}"/>`);
      out.push(`<line class="road-fill" x1="${ax}" y1="${ay}" x2="${bx}" y2="${by}" stroke="${color}"/></g>`);
    }
  });

  state.players.forEach((p, seat) => {
    const color = PLAYER_COLORS[players[seat]?.color] ?? '#888';
    const who = players[seat]?.name;
    for (const n of p.settlements) {
      const { x, y } = geo.nodes[n];
      const cls = changes.nodes.has(n) ? 'building changed' : 'building';
      const w = s * 0.17;
      out.push(`<g class="${cls}"><title>Settlement of ${who} (corner ${n})</title>`);
      out.push(
        `<path fill="${color}" d="M ${x - w} ${y + w} V ${y - w * 0.2} L ${x} ${y - w * 1.2} L ${x + w} ${y - w * 0.2} V ${y + w} Z"/></g>`,
      );
    }
    for (const n of p.cities) {
      const { x, y } = geo.nodes[n];
      const cls = changes.nodes.has(n) ? 'building changed' : 'building';
      const w = s * 0.17;
      out.push(`<g class="${cls}"><title>City of ${who} (corner ${n})</title>`);
      out.push(
        `<path fill="${color}" d="M ${x - w * 1.6} ${y + w} V ${y - w * 0.4} L ${x - w * 0.8} ${y - w * 1.3} L ${x} ${y - w * 0.4} H ${x + w * 1.6} V ${y + w} Z"/></g>`,
      );
    }
  });

  out.push('</svg>');
  return out.join('');
}
