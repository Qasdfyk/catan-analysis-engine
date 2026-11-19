// catan-project's Board, as the viewer shows it: nothing to click, and the
// marks of the last move computed the way the game computes them
// (builtPieces, producingTiles), so a move looks the same in both.

import { useMemo, type ReactNode } from 'react';
import { Board, type BoardTargets } from '@platform/components/Board';
import { builtPieces, NO_BUILT, producingTiles } from '@platform/utils/changes';
import type { OpenGame } from '../engine';
import type { Topology } from '../types';
import { toGameView, toPlayers } from '../view';

const NO_TARGETS: BoardTargets = { settlements: [], roads: [], cities: [], robberHexes: [] };
const ignore = () => {};

interface Props {
  game: OpenGame;
  topo: Topology;
  /** The state to draw: 0 before the first event, i after event i - 1. */
  pos: number;
  /** Drawn over the board in its coordinates (the setup heat map). */
  overlay?: ReactNode;
}

export function GameBoard({ game, topo, pos, overlay }: Props) {
  const { view, before, players, producing } = useMemo(() => {
    const state = game.state(pos);
    const view = toGameView(state, game.record, topo);
    const before = pos > 0 ? toGameView(game.state(pos - 1), game.record, topo) : null;
    const rolled = game.record.events[pos - 1]?.action === 'roll_dice';
    return {
      view,
      before,
      players: toPlayers(game.record, state),
      producing: rolled && view.dice_roll !== null ? producingTiles(view, view.dice_roll) : [],
    };
  }, [game, topo, pos]);

  return (
    <div className="board-area">
      <div className="board-stack">
        <Board
          game={view}
          players={players}
          targets={NO_TARGETS}
          onSettlement={ignore}
          onRoad={ignore}
          onCity={ignore}
          onRobber={ignore}
          producing={producing}
          recent={before ? builtPieces(before, view) : NO_BUILT}
        />
        {overlay}
      </div>
    </div>
  );
}
