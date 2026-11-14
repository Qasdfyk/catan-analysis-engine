// catan-project's Board, as the viewer shows it: nothing to click, and the
// marks of the last move computed the way the game computes them
// (builtPieces, producingTiles), so a move looks the same in both.

import { createRoot, type Root } from 'react-dom/client';
import { Board, type BoardTargets } from '@platform/components/Board';
import { builtPieces, NO_BUILT, producingTiles } from '@platform/utils/changes';
import type { GameView, Player } from '@platform/types/game';

const NO_TARGETS: BoardTargets = { settlements: [], roads: [], cities: [], robberHexes: [] };
const ignore = () => {};

export interface BoardProps {
  view: GameView;
  /** The state before the shown move; null at the start. */
  before: GameView | null;
  players: Player[];
  /** The shown move was a roll: light the tiles that produced. */
  rolled: boolean;
}

function BoardView({ view, before, players, rolled }: BoardProps) {
  const producing = rolled && view.dice_roll !== null ? producingTiles(view, view.dice_roll) : [];
  return (
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
  );
}

let root: Root | null = null;

/** Draws the board into `container`; later calls update it in place. */
export function renderBoard(container: HTMLElement, props: BoardProps): void {
  root ??= createRoot(container);
  root.render(<BoardView {...props} />);
}
