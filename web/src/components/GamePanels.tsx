// What the board does not show: who is to move, the players' hands and
// awards, and the list of moves. All of it is read from one engine state.

import { memo, useEffect, useRef, useState, type CSSProperties, type ReactNode } from 'react';
import { PLAYER_COLORS } from '@platform/utils/labels';
import type { OpenGame } from '../engine';
import { eventText, isTradeTalk } from '../events';
import { DEV_CARDS, RESOURCES, type GameState, type RecordPlayer } from '../types';

export function seatName(players: RecordPlayer[], seat: number | null): string {
  return seat === null ? '' : (players[seat]?.name ?? `seat ${seat}`);
}

export function Chip({ seat, players }: { seat: number; players: RecordPlayer[] }) {
  return <span className="chip" style={{ background: PLAYER_COLORS[players[seat]?.color] ?? '#888' }} />;
}

function Cards({ counts, dim = true }: { counts: number[]; dim?: boolean }) {
  return (
    <span className="cards">
      {RESOURCES.map((r, i) => (
        <span key={r} className={`res ${r}${dim && counts[i] === 0 ? ' none' : ''}`} title={r}>
          {counts[i]}
        </span>
      ))}
    </span>
  );
}

export function Status({ game, pos }: { game: OpenGame; pos: number }) {
  const state = game.state(pos);
  const players = game.record.players;
  const event = pos > 0 ? game.record.events[pos - 1] : null;
  return (
    <div className="panel status">
      <div className="status-line">
        {state.game_over ? (
          <strong>{seatName(players, state.winner)} wins</strong>
        ) : (
          <>
            <Chip seat={state.current} players={players} />
            <span>
              <strong>{seatName(players, state.current)}</strong> to move
            </span>
            <span className="phase">{state.phase.replace(/_/g, ' ')}</span>
          </>
        )}
        {state.dice && (
          <span className="dice-pair">
            <span className="die">{state.dice[0]}</span>
            <span className="die">{state.dice[1]}</span>
          </span>
        )}
      </div>
      <div className="last">
        {event ? `${seatName(players, event.seat)} ${eventText(event, players)}` : 'Start of the game'}
      </div>
      <div className="bank">
        <span className="muted">Bank</span> <Cards counts={state.bank} dim={false} />
      </div>
    </div>
  );
}

/** "bot hard", or for a zoo bot its trading personality (its archetype is in the name). */
export function botTag(player: RecordPlayer): string {
  if (player.bot_style) return `zoo · ${player.bot_style.personality}`;
  return `bot ${player.bot_level ?? ''}`.trim();
}

export function Players({ state, players }: { state: GameState; players: RecordPlayer[] }) {
  return (
    <div className="players">
      {state.players.map((p, seat) => {
        const info = players[seat];
        const dev = DEV_CARDS.flatMap((c, i) => {
          const n = p.dev_cards[i] + p.new_dev_cards[i];
          return n > 0 ? [`${n}× ${c.replace(/_/g, ' ')}`] : [];
        }).join(', ');
        const toMove = state.current === seat && !state.game_over;
        return (
          <div
            key={seat}
            className={`panel player${toMove ? ' to-move' : ''}${state.winner === seat ? ' winner' : ''}`}
            style={{ '--seat': PLAYER_COLORS[info?.color] ?? '#888' } as CSSProperties}
          >
            <div className="player-head">
              <Chip seat={seat} players={players} />
              <span className="who">
                <strong className="name">{info?.name ?? `seat ${seat}`}</strong>
                {info?.kind === 'bot' && <span className="tag">{botTag(info)}</span>}
              </span>
              <span className="points">
                {p.points}
                {p.hidden_points > 0 && (
                  <span className="hidden-points" title="victory point cards">
                    +{p.hidden_points}
                  </span>
                )}
              </span>
            </div>
            <Cards counts={p.resources} />
            <div className="muted small">
              road {state.road_lengths[seat]} · knights {p.knights}
              {dev && ` · ${dev}`}
            </div>
            <div className="badges">
              {state.longest_road === seat && <span className="badge">longest road</span>}
              {state.largest_army === seat && <span className="badge">largest army</span>}
            </div>
          </div>
        );
      })}
    </div>
  );
}

/** Every move of the game, by turn. Built once per game; only the highlight follows pos. */
const MoveList = memo(function MoveList({ game, onPick }: { game: OpenGame; onPick: (pos: number) => void }) {
  const players = game.record.players;
  const items: ReactNode[] = [];
  let turn = -1;
  game.record.events.forEach((e, i) => {
    // A heading above the first event of every turn after the setup.
    if (e.action === 'roll_dice' && e.turn !== turn) {
      turn = e.turn;
      items.push(
        <li key={`t${i}`} className="turn">
          Turn {e.turn}
        </li>,
      );
    }
    items.push(
      <li key={i} data-i={i} className={`event${isTradeTalk(e) ? ' talk' : ''}`} onClick={() => onPick(i + 1)}>
        <Chip seat={e.seat} players={players} />
        <b>{seatName(players, e.seat)}</b> {eventText(e, players)}
      </li>,
    );
  });
  return <>{items}</>;
});

export function MoveLog({ game, pos, onPick }: { game: OpenGame; pos: number; onPick: (pos: number) => void }) {
  const list = useRef<HTMLOListElement>(null);
  const [showTalk, setShowTalk] = useState(true);

  // Highlighting by hand: re-rendering 500 rows on every step of autoplay is wasted work.
  useEffect(() => {
    const el = list.current;
    if (!el) return;
    el.querySelector('.current')?.classList.remove('current');
    const current = el.querySelector<HTMLElement>(`[data-i="${pos - 1}"]`);
    if (current) {
      current.classList.add('current');
      // Scroll the list only, never the page.
      const top = current.offsetTop - el.offsetTop;
      if (top < el.scrollTop || top > el.scrollTop + el.clientHeight - current.offsetHeight) {
        el.scrollTop = top - el.clientHeight / 2;
      }
    }
  }, [pos, game]);

  return (
    <div className="panel log-panel">
      <div className="log-head">
        <h3>Moves</h3>
        <label>
          <input type="checkbox" checked={showTalk} onChange={(e) => setShowTalk(e.target.checked)} /> trade offers
        </label>
      </div>
      <ol ref={list} className={`log${showTalk ? '' : ' hide-talk'}`}>
        <MoveList game={game} onPick={onPick} />
      </ol>
    </div>
  );
}
