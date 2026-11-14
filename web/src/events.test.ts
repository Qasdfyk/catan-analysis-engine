import { describe, expect, it } from 'vitest';
import { cardsText, eventText } from './events';
import type { RecordPlayer } from './types';

const players: RecordPlayer[] = [
  { seat: 0, name: 'Alice', color: 'red', kind: 'human', bot_level: null },
  { seat: 1, name: 'Ada', color: 'blue', kind: 'bot', bot_level: 'hard' },
];

describe('eventText', () => {
  it('adds up the dice', () => {
    expect(eventText({ turn: 4, seat: 0, action: 'roll_dice', payload: {}, outcome: { dice: [4, 5] } }, players)).toBe(
      'rolls 4 + 5 = 9',
    );
  });

  it('names the victim and the stolen card', () => {
    const e = { turn: 6, seat: 0, action: 'steal', payload: { victim_seat: 1 }, outcome: { resource: 'wheat' } };
    expect(eventText(e, players)).toBe('steals wheat from Ada');
  });

  it('lists the cards of an offer', () => {
    const e = { turn: 9, seat: 0, action: 'offer_trade', payload: { give: { wheat: 1, ore: 2 }, get: { wood: 1 } } };
    expect(eventText(e, players)).toBe('offers 1 wheat, 2 ore for 1 wood');
  });

  it('falls back to the action name', () => {
    expect(eventText({ turn: 1, seat: 0, action: 'something_new', payload: {} }, players)).toBe('something_new');
  });
});

describe('cardsText', () => {
  it('skips zeros', () => expect(cardsText({ wood: 0, ore: 1 })).toBe('1 ore'));
  it('says nothing for an empty hand', () => expect(cardsText({})).toBe('nothing'));
});
