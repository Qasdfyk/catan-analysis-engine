// From board indices to pixels. The engine numbers hexes, corners and sides;
// topologyJson() gives each its cube coordinates, and this file places them
// on a pointy-topped grid, the way catan-project draws its board.

import type { Topology } from './types';

export interface Point {
  x: number;
  y: number;
}

/** Distance from a hex center to its corners. */
export const HEX_SIZE = 60;
const SQRT3 = Math.sqrt(3);

/** Center of hex (q, r); r grows to the south, q to the east. */
export function hexCenter(q: number, r: number, size = HEX_SIZE): Point {
  return { x: size * SQRT3 * (q + r / 2), y: size * 1.5 * r };
}

/**
 * Corner `direction` of a hex: 0 is the upper right corner, then clockwise
 * (topology.hpp). With y pointing down, that is -30 degrees plus 60 per step.
 */
export function hexCorner(center: Point, direction: number, size = HEX_SIZE): Point {
  const angle = ((-30 + 60 * direction) * Math.PI) / 180;
  return { x: center.x + size * Math.cos(angle), y: center.y + size * Math.sin(angle) };
}

export interface Layout {
  hexes: Point[];
  nodes: Point[];
  /** Both ends of each side. */
  edges: [Point, Point][];
}

export function layout(t: Topology, size = HEX_SIZE): Layout {
  const hexes = t.hexes.map((h) => hexCenter(h.q, h.r, size));
  const nodes = t.nodes.map((n) => hexCorner(hexCenter(n.q, n.r, size), n.direction, size));
  const edges = t.edges.map(([a, b]) => [nodes[a], nodes[b]] as [Point, Point]);
  return { hexes, nodes, edges };
}

/** `p` moved `by` pixels further from the board's center. */
export function pushOut(p: Point, by: number): Point {
  const length = Math.hypot(p.x, p.y) || 1;
  return { x: p.x + (p.x / length) * by, y: p.y + (p.y / length) * by };
}

/** The hex corner points as an SVG polygon attribute. */
export function hexPolygon(center: Point, size = HEX_SIZE): string {
  return [0, 1, 2, 3, 4, 5]
    .map((d) => hexCorner(center, d, size))
    .map((p) => `${p.x.toFixed(1)},${p.y.toFixed(1)}`)
    .join(' ');
}
