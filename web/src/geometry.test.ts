import { describe, expect, it } from 'vitest';
import { HEX_SIZE, hexCenter, hexCorner } from './geometry';

describe('geometry', () => {
  it('puts neighbors one hex width apart', () => {
    const a = hexCenter(0, 0);
    const b = hexCenter(0, 1); // south-east neighbor
    expect(Math.hypot(b.x - a.x, b.y - a.y)).toBeCloseTo(HEX_SIZE * Math.sqrt(3));
    expect(b.y).toBeGreaterThan(a.y);
    expect(b.x).toBeGreaterThan(a.x);
  });

  it('numbers corners clockwise from the upper right', () => {
    const c = { x: 0, y: 0 };
    expect(hexCorner(c, 0).x).toBeGreaterThan(0);
    expect(hexCorner(c, 0).y).toBeLessThan(0);
    expect(hexCorner(c, 2).x).toBeCloseTo(0);
    expect(hexCorner(c, 2).y).toBeCloseTo(HEX_SIZE); // bottom
    expect(hexCorner(c, 5).y).toBeCloseTo(-HEX_SIZE); // top
  });

  it('shares a corner between neighbors', () => {
    // Corner 0 (upper right) of a hex is corner 4 (upper left) of its east neighbor.
    const a = hexCorner(hexCenter(0, 0), 0);
    const b = hexCorner(hexCenter(1, 0), 4);
    expect(a.x).toBeCloseTo(b.x);
    expect(a.y).toBeCloseTo(b.y);
  });
});
