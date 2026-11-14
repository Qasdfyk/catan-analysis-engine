// One line of English per recorded event, for the move list. The viewer
// shows a finished game, so it names hidden things too: the stolen card,
// the development card bought.

import type { CardMap, RecordEvent, RecordPlayer } from './types';

const DEV_CARD_NAMES: Record<string, string> = {
  knight: 'a knight',
  victory_point: 'a victory point',
  road_building: 'road building',
  year_of_plenty: 'year of plenty',
  monopoly: 'monopoly',
};

export function cardsText(cards: CardMap | undefined): string {
  const parts = Object.entries(cards ?? {})
    .filter(([, n]) => (n ?? 0) > 0)
    .map(([name, n]) => `${n} ${name}`);
  return parts.length > 0 ? parts.join(', ') : 'nothing';
}

/** "rolls 4 + 5 = 9", without the player's name. */
export function eventText(e: RecordEvent, players: RecordPlayer[]): string {
  const p = e.payload ?? {};
  const o = e.outcome ?? {};
  const name = (seat: unknown) =>
    typeof seat === 'number' ? (players.find((x) => x.seat === seat)?.name ?? `seat ${seat}`) : '?';
  switch (e.action) {
    case 'build_settlement':
      return 'builds a settlement';
    case 'build_road':
      return 'builds a road';
    case 'upgrade_city':
      return 'builds a city';
    case 'roll_dice': {
      const [a, b] = (o.dice as [number, number] | undefined) ?? [0, 0];
      return `rolls ${a} + ${b} = ${a + b}`;
    }
    case 'discard':
      return `discards ${cardsText(p.cards as CardMap)}`;
    case 'move_robber':
      return 'moves the robber';
    case 'steal':
      return o.resource ? `steals ${o.resource} from ${name(p.victim_seat)}` : `steals from ${name(p.victim_seat)}`;
    case 'trade_with_bank':
      return `trades ${p.give} for ${p.get} with the bank`;
    case 'offer_trade':
      return `offers ${cardsText(p.give as CardMap)} for ${cardsText(p.get as CardMap)}`;
    case 'respond_trade':
      return p.accept ? 'accepts the offer' : 'declines the offer';
    case 'confirm_trade':
      return `trades with ${name(p.partner_seat)}`;
    case 'cancel_trade':
      return 'withdraws the offer';
    case 'buy_dev_card':
      return o.card ? `buys a development card (${DEV_CARD_NAMES[o.card as string] ?? o.card})` : 'buys a development card';
    case 'play_knight':
      return 'plays a knight';
    case 'play_road_building':
      return 'plays road building';
    case 'play_year_of_plenty':
      return `plays year of plenty: ${p.first} and ${p.second}`;
    case 'play_monopoly':
      return `plays monopoly on ${p.resource}`;
    case 'end_turn':
      return 'ends the turn';
    default:
      return e.action;
  }
}

/** Trade chatter (offers, answers, withdrawn offers) the list can hide. */
export function isTradeTalk(e: RecordEvent): boolean {
  return e.action === 'offer_trade' || e.action === 'respond_trade' || e.action === 'cancel_trade';
}
