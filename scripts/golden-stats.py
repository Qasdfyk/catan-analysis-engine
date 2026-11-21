#!/usr/bin/env python3
"""Summary of a golden positions file (catan_golden, docs/expert.md).

    scripts/golden-stats.py data/golden-b.jsonl.gz

Prints how many positions of each phase, how many moves each has, how sure
the values are, and how the moves played in the games compare with the best
ones: the numbers quoted in docs/expert.md.
"""
import gzip
import json
import math
import statistics
import sys
from collections import Counter


def read(path):
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt") as f:
        return [json.loads(line) for line in f if line.strip()]


def main(path):
    rows = read(path)
    print(f"{len(rows)} positions")
    print("phases:", dict(Counter(r["phase"] for r in rows).most_common()))
    moves = [len(r["moves"]) for r in rows]
    print(f"moves per position: median {statistics.median(moves)}, mean {statistics.mean(moves):.1f}, max {max(moves)}")
    playouts = sum(m["halving_playouts"] + m.get("playouts", 0) for r in rows for m in r["moves"])
    print(f"playouts: {playouts:,} ({playouts / len(rows):,.0f} per position)")

    ses = []
    for r in rows:
        best = r["moves"][r["best"]]
        p, n = best["value"], best["playouts"]
        ses.append(math.sqrt(max(p * (1 - p), 1e-9) / n))
    print(f"standard error of the best move's value: median {100 * statistics.median(ses):.2f} pp, max {100 * max(ses):.2f} pp")

    played = [r for r in rows if r["played"] >= 0]
    same = sum(r["played"] == r["best"] for r in played)
    losses = []
    for r in played:
        if r["played"] == r["best"]:
            losses.append(0.0)
            continue
        losses.append(r["moves"][r["best"]]["value"] - r["moves"][r["played"]]["value"])
    print(f"positions with the move played: {len(played)}; the played move is the best one in {same} ({100 * same / len(played):.0f}%)")
    big = [x for x in losses if x > 0.03]
    print(f"loss of the move played: mean {100 * statistics.mean(losses):.2f} pp; more than 3 pp in {len(big)} positions ({100 * len(big) / len(played):.0f}%)")

    # Winner's curse: the halving value of the best move against its fresh value
    inflated = [r["moves"][r["best"]]["halving_value"] - r["moves"][r["best"]]["value"] for r in rows]
    print(f"halving value of the best move minus its confirmation: mean {100 * statistics.mean(inflated):+.2f} pp")
    flipped = 0
    for r in rows:
        finals = [i for i, m in enumerate(r["moves"]) if "value" in m and i != r["played"]]
        if finals and max(finals, key=lambda i: r["moves"][i]["value"]) != r["best"]:
            flipped += 1
    print(f"positions where another finalist did better in the confirmation: {flipped} ({100 * flipped / len(rows):.0f}%)")


if __name__ == "__main__":
    main(sys.argv[1])
