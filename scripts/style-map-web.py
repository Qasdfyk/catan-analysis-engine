#!/usr/bin/env python3
"""Writes web/src/style-map.json, the map of styles on the demo's How it works
page, from the MAP-Elites archive and the hand-made zoo on the same deals:

    build/release/tools/catan_map_elites --threads 16                  # data/elites.json
    build/release/tools/catan_map_elites --describe 4000 --seed 1001 \\
        --out data/zoo-styles.json --threads 16                        # same deals as the final scores
    scripts/style-map-web.py
    scripts/style-map-web.py --check    # fails if the page's file is out of date

Only the final scores go to the page: they are the honest ones
(include/catan/map_elites.hpp).
"""
import json
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parent.parent
archive = json.loads((root / "data/elites.json").read_text())
zoo = json.loads((root / "data/zoo-styles.json").read_text())
run = archive["run"]
if zoo["seed"] != run["final_seed"] or zoo["games"] != run["final"]:
    raise SystemExit("data/zoo-styles.json was not played on the final deals of data/elites.json")


def r(x, n=3):
    return round(x, n)


elites = []
for e in archive["elites"]:
    f = e["final"]
    w = e["bot"]["weights"]
    t = e["bot"]["trading"]
    elites.append({
        "cell": e["cell"],
        "bins": e["bins"],
        "from": e["bot"]["origin"],
        "win": r(f["win_rate"]),
        "half": r(f["margin"]),
        "cityShare": r(f["city_share"], 2),
        "trades": r(f["trades"], 2),
        "devCards": r(f["dev_cards"], 2),
        "genes": {
            "city": r(w["city"], 2),
            "settlement": r(w["settlement"], 2),
            "devCard": r(w["dev_card"], 2),
            "port": r(w["port"], 2),
            "variety": r(w["variety"], 2),
            "roadReach": w["road_reach"],
            "leaderFocus": r(w["leader_focus"], 2),
            "offersPerTurn": t["offers_per_turn"],
            "embargo": t["embargo"],
            "neutral": t["neutral"],
        },
    })

out = {
    "grid": archive["grid"],
    "games": run["final"],
    "candidates": run["candidates"],
    "gamesPlayed": run["games_played"],
    "elites": elites,
    "zoo": [
        {"name": z["name"], "cell": z["cell"], "win": r(z["eval"]["win_rate"]), "half": r(z["eval"]["margin"])}
        for z in zoo["zoo"]
    ],
}
path = root / "web/src/style-map.json"
text = json.dumps(out, indent=1) + "\n"
if "--check" in sys.argv:
    if path.read_text() != text:
        raise SystemExit(f"{path.relative_to(root)} is out of date: run scripts/style-map-web.py")
    print(f"{path.relative_to(root)} matches data/elites.json")
    sys.exit(0)
path.write_text(text)
print(f"wrote {path.relative_to(root)}: {len(elites)} elites, {len(out['zoo'])} zoo bots")
