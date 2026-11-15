// Pure helpers for moving through a game and through the page, kept out of
// the components so they can be tested without a browser.

import { isTradeTalk } from './events';
import type { RecordEvent } from './types';

export const TABS = ['start', 'replay', 'performance'] as const;
export type Tab = (typeof TABS)[number];

/** "#/performance" -> "performance"; anything unknown is the start page. */
export function tabFromHash(hash: string): Tab {
  const name = hash.replace(/^#\/?/, '').split(/[/?]/)[0];
  return (TABS as readonly string[]).includes(name) ? (name as Tab) : 'start';
}

/**
 * The state at the start of the next (direction 1) or previous (-1) turn:
 * the state right after a dice roll. State i is the one after event i - 1.
 */
export function turnStart(events: RecordEvent[], pos: number, direction: 1 | -1): number {
  const last = events.length;
  for (let i = pos + direction; i > 0 && i <= last; i += direction) {
    if (events[i - 1].action === 'roll_dice') return i;
  }
  return direction === 1 ? last : 0;
}

/**
 * How long autoplay shows the state after `event`, in milliseconds at speed 1:
 * long enough to see a roll, quick through trade chatter and turn ends.
 */
export function playbackDelay(event: RecordEvent | undefined): number {
  if (!event) return 900;
  if (isTradeTalk(event) || event.action === 'end_turn') return 120;
  if (event.action === 'roll_dice') return 700;
  return 450;
}
