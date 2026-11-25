// Words for the move review: a move as a short phrase ("a city on 6·8·5"),
// "pips" being the dots under a number (how many of 36 rolls give it),
// and "why" from the facts the engine found (explain() in review.hpp).
//
// The engine picks the reason; this file only says it. Each template states
// what the two moves do, never more: the grade itself comes from the
// playouts, and a template must not claim what they did not measure.

import type { ActionJson, MoveReview, Why } from './grades';
import type { BoardInfo, RecordPlayer, Topology } from './types';

export interface Names {
  players: RecordPlayer[];
  board: BoardInfo;
  topo: Topology;
}

const name = (n: Names, seat: number) => n.players[seat]?.name ?? `seat ${seat}`;

/** A corner by the numbers around it, "6·8·5"; "the coast" where it has none. */
export function cornerText(n: Names, node: number): string {
  const numbers = (n.topo.nodes[node]?.hexes ?? []).map((h) => n.board.hexes[h]?.number ?? 0).filter((x) => x > 0);
  return numbers.length > 0 ? numbers.join('·') : 'the coast';
}

/** A hex by its number and terrain, "the 8 wheat"; "the desert". */
export function hexText(n: Names, hex: number): string {
  const h = n.board.hexes[hex];
  if (!h) return `hex ${hex}`;
  return h.terrain === 'desert' ? 'the desert' : `the ${h.number} ${h.terrain}`;
}

function cards(c: Record<string, number> | undefined): string {
  const parts = Object.entries(c ?? {})
    .filter(([, k]) => k > 0)
    .map(([r, k]) => `${k} ${r}`);
  return parts.length > 0 ? parts.join(', ') : 'nothing';
}

/** A move as a phrase that fits "Best: …" and "You played …". */
export function actionText(a: ActionJson, n: Names): string {
  const t = a.target ?? -1;
  switch (a.kind) {
    case 'build_settlement':
      return `a settlement on ${cornerText(n, t)}`;
    case 'upgrade_city':
      return `a city on ${cornerText(n, t)}`;
    case 'build_road': {
      const ends = n.topo.edges[t]?.nodes;
      return ends ? `a road ${cornerText(n, ends[0])} – ${cornerText(n, ends[1])}` : 'a road';
    }
    case 'move_robber':
      return `the robber on ${hexText(n, t)}`;
    case 'steal':
      return `stealing from ${name(n, t)}`;
    case 'trade_with_bank':
      return `trading ${a.first} for ${a.second} with the bank`;
    case 'offer_trade':
      return `offering ${cards(a.give)} for ${cards(a.get)}`;
    case 'confirm_trade':
      return `the trade with ${name(n, t)}`;
    case 'buy_dev_card':
      return 'buying a development card';
    case 'play_knight':
      return 'playing a knight';
    case 'play_road_building':
      return 'playing road building';
    case 'play_year_of_plenty':
      return `playing year of plenty (${a.first}, ${a.second})`;
    case 'play_monopoly':
      return `playing monopoly on ${a.first}`;
    case 'end_turn':
      return 'ending the turn';
    default:
      return a.kind.replace(/_/g, ' ');
  }
}

const pct = (x: number) => `${Math.round(100 * x)}%`;
const capital = (s: string) => s.charAt(0).toUpperCase() + s.slice(1);
const list = (xs: string[]) => (xs.length < 2 ? (xs[0] ?? '') : `${xs.slice(0, -1).join(', ')} or ${xs[xs.length - 1]}`);
const article = (x: string) => (x === 'city' || x === 'settlement' || x === 'road' || x === 'development card' ? `a ${x}` : x);
const sum = (xs: number[], skip: number) => xs.reduce((a, x, i) => (i === skip ? a : a + x), 0);

/** One or two sentences: why the best move is better. Null for best and good. */
export function whyText(r: MoveReview, n: Names): string | null {
  const why: Why | null = r.why;
  if (!why) return null;
  const best = actionText(r.best, n);
  const seat = r.seat;
  switch (why.kind) {
    case 'wins_now':
      return why.played_now > 0
        ? `${capital(best)} wins the game this turn in ${pct(why.best_now)} of the playouts; your move, in ${pct(why.played_now)}.`
        : `${capital(best)} wins the game this turn in ${pct(why.best_now)} of the playouts; your move cannot win it this turn.`;
    case 'robber': {
      const leader = why.leader;
      const own = why.played_blocks[seat] - why.best_blocks[seat];
      const lead = leader >= 0 ? why.best_blocks[leader] - why.played_blocks[leader] : 0;
      const others = sum(why.best_blocks, seat) - sum(why.played_blocks, seat);
      const parts: string[] = [];
      if (lead > 0) {
        parts.push(
          `${capital(best)} takes ${why.best_blocks[leader]} pips from ${name(n, leader)}, the leader; yours took ${why.played_blocks[leader]}.`,
        );
      } else if (others > 0) {
        parts.push(`${capital(best)} blocks ${sum(why.best_blocks, seat)} pips of your opponents; yours blocked ${sum(why.played_blocks, seat)}.`);
      }
      if (own > 0) parts.push(`Your hex also blocked ${why.played_blocks[seat]} pips of your own.`);
      if (parts.length > 0) return parts.join(' ');
      // By pips the move played was as good or better: say so, and that the
      // playouts see something else (whom it lets you rob, where it goes next).
      return `By pips your hex was no worse: it blocked ${sum(why.played_blocks, seat)} of your opponents' pips, ${hexText(n, why.best_hex)} would block ${sum(why.best_blocks, seat)}. The playouts favour ${hexText(n, why.best_hex)} for what comes after: whom you can rob, and where the robber goes next.`;
    }
    case 'victim': {
      const b = why.best_victim;
      const p = why.played_victim;
      return `${name(n, b)} has ${why.points[b]} points and ${why.cards[b]} cards; ${name(n, p)} has ${why.points[p]} points and ${why.cards[p]} cards.`;
    }
    case 'longest_road':
      return `${capital(best)} takes the Longest Road: 2 points.`;
    case 'largest_army':
      return `${capital(best)} takes the Largest Army: 2 points.`;
    case 'points':
      return `${capital(best)} scores now: ${why.best_points} points instead of ${why.played_points}.`;
    case 'production':
      return why.city
        ? `A city there doubles ${why.numbers.join(', ')}: ${why.pips} more pips${why.played_pips > 0 ? `, against ${why.played_pips} for yours` : ''}.`
        : `A settlement there collects on ${why.numbers.join(', ')}: ${why.pips} pips${why.played_pips > 0 ? `, against ${why.played_pips} for yours` : ''}.`;
    case 'port':
      return why.port === '3:1'
        ? `${capital(best)} sits on a 3:1 port, which you did not have: any 3 cards for 1.`
        : `${capital(best)} sits on the 2:1 ${why.port} port, which you did not have: 2 ${why.port} for any 1 card.`;
    case 'enables':
      return `After ${best} you can pay for ${list(why.builds.map(article))}; after your move you cannot.`;
    case 'road_spot':
      return `${capital(best)} reaches a corner to settle on, ${why.numbers.join('·')} (${why.pips} pips); yours reaches none.`;
    case 'road_length': {
      const holder = why.holder >= 0 && why.holder !== seat ? `; ${name(n, why.holder)} holds the Longest Road with ${why.holder_length}` : '';
      return `${capital(best)} makes your road ${why.best_length} long, yours ${why.played_length}${holder}.`;
    }
    case 'ended_turn':
      return `You ended the turn with the cards for ${best}.`;
    case 'dev_card': {
      const army =
        why.holder === seat
          ? 'you hold the Largest Army'
          : why.holder >= 0
          ? `${name(n, why.holder)} holds the Largest Army with ${why.holder_knights} knights, you have played ${why.my_knights}`
          : `nobody holds the Largest Army yet (3 knights), you have played ${why.my_knights}`;
      return `Of the ${why.unseen} development cards you had not seen, ${why.points} are victory points and ${why.knights} knights; ${army}.`;
    }
    case 'later':
      return 'Nothing changes on the spot; the difference shows up later in the playouts, as the game goes on.';
  }
}
