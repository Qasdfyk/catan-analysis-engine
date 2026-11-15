import { describe, expect, it } from 'vitest';
import { playbackDelay, tabFromHash, turnStart } from './nav';
import type { RecordEvent } from './types';

const ev = (action: string): RecordEvent => ({ turn: 0, seat: 0, action, payload: {} });

describe('tabFromHash', () => {
  it('reads the tab after #/', () => {
    expect(tabFromHash('#/performance')).toBe('performance');
    expect(tabFromHash('#replay')).toBe('replay');
    expect(tabFromHash('#/replay?x=1')).toBe('replay');
  });
  it('falls back to the start page', () => {
    expect(tabFromHash('')).toBe('start');
    expect(tabFromHash('#/nope')).toBe('start');
  });
});

describe('turnStart', () => {
  // States: 0 | after build | after roll (2) | after end | after roll (4) | after end
  const events = ['build_settlement', 'roll_dice', 'end_turn', 'roll_dice', 'end_turn'].map(ev);
  it('finds the state after the next roll', () => {
    expect(turnStart(events, 0, 1)).toBe(2);
    expect(turnStart(events, 2, 1)).toBe(4);
    expect(turnStart(events, 4, 1)).toBe(5);
  });
  it('finds the state after the previous roll', () => {
    expect(turnStart(events, 5, -1)).toBe(4);
    expect(turnStart(events, 4, -1)).toBe(2);
    expect(turnStart(events, 2, -1)).toBe(0);
  });
});

describe('playbackDelay', () => {
  it('lingers on rolls and hurries through chatter', () => {
    expect(playbackDelay(ev('roll_dice'))).toBeGreaterThan(playbackDelay(ev('build_road')));
    expect(playbackDelay(ev('offer_trade'))).toBeLessThan(playbackDelay(ev('build_road')));
  });
});
