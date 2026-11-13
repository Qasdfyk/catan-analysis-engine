"""
Games per second of the Python engines, to set beside catan_selfplay.

  game.py      catan-project's rules with its random player
               (backend/app/services/random_player.py), the policy that
               catan_selfplay plays in C++. Run with the backend's Python:
                 ../catan-project/backend/.venv/bin/python tools/bench_python.py game.py

  catanatron   Catanatron (github.com/bcollazo/catanatron, GPL-3.0) with its
               own RandomPlayer, which picks uniformly among all legal
               actions. It is not part of this repository; install it in a
               separate virtual environment and run with that Python:
                 uv venv /tmp/catanatron && VIRTUAL_ENV=/tmp/catanatron uv pip install catanatron
                 /tmp/catanatron/bin/python tools/bench_python.py catanatron

The players differ between the three engines (Catanatron's random player
builds less often, so its games are longer), so compare time per action as
well as games per second. docs/performance.md has the numbers.
"""
import argparse
import os
import random
import statistics
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def game_py(backend):
    sys.path.insert(0, os.path.abspath(backend))
    from app.models.game import GameState
    from app.services.random_player import play_step

    def one(seed):
        random.seed(seed)  # game.py rolls dice and shuffles with the global generator
        rng = random.Random(seed)
        game = GameState.create_new_game(["A", "B", "C", "D"])
        actions = 0
        while not game.is_game_over and actions < 40_000:
            play_step(game, rng)
            actions += 1
        return actions, game.turn_number

    return one


def catanatron():
    from catanatron import Color, Game, RandomPlayer

    colors = [Color.RED, Color.BLUE, Color.WHITE, Color.ORANGE]

    def one(seed):
        random.seed(seed)
        game = Game([RandomPlayer(c) for c in colors], seed=seed)
        game.play()
        return len(game.state.actions), game.state.num_turns

    return one


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("engine", choices=["game.py", "catanatron"])
    parser.add_argument("--games", type=int, default=200)
    parser.add_argument("--backend", default=os.path.join(ROOT, "..", "catan-project", "backend"),
                        help="catan-project's backend directory (for game.py)")
    args = parser.parse_args()

    one = game_py(args.backend) if args.engine == "game.py" else catanatron()
    one(0)  # warm up: imports, caches

    actions, turns = [], []
    start = time.perf_counter()
    for seed in range(1, args.games + 1):
        a, t = one(seed)
        actions.append(a)
        turns.append(t)
    seconds = time.perf_counter() - start

    print(f"{args.engine}: {args.games} games, 4 players, Python {sys.version.split()[0]}")
    print(f"  {statistics.mean(actions):.1f} actions and {statistics.mean(turns):.1f} turns per game")
    print(f"time {seconds:.2f} s: {args.games / seconds:.1f} games/s, "
          f"{sum(actions) / seconds / 1e3:.1f} k actions/s, {seconds / args.games * 1e3:.2f} ms per game")


if __name__ == "__main__":
    main()
