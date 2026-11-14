#!/usr/bin/env python3
"""Two runs of one scenario side by side (task driving 10): TSRE's CSV (the
train-compare suite) and MaSzyna's (maszyna-mover), the columns of
tools/train-compare/README.md.

Prints a Markdown table every `step` seconds (speed, distance, the pipe, the
loco's, the first and the last car's cylinders, the main reservoir) and, for
each brake application in the scenario's commands, when and where each train
stopped after it.

Usage: compare.py <scenario.json> <tsre.csv> <maszyna.csv> [step seconds]
"""

import csv
import json
import sys


def rows(path):
    with open(path, newline="") as f:
        return [{k: (float(v) if v not in ("", None) else None) for k, v in r.items()} for r in csv.DictReader(f)]


def at(table, t):
    """The row at or just before t."""
    best = None
    for r in table:
        if r["t"] <= t + 1e-6:
            best = r
        else:
            break
    return best


def fmt(v, digits=1):
    return "-" if v is None else f"{v:.{digits}f}"


def stop_after(table, t0):
    """When and where the train first stood after t0 (time, distance from t0)."""
    start = at(table, t0)
    if start is None:
        return None
    for r in table:
        if r["t"] > t0 and r["v"] is not None and abs(r["v"]) < 0.05:
            return r["t"] - t0, r["s"] - start["s"], start["v"]
    return None


def main():
    scenario = json.load(open(sys.argv[1]))
    tsre, mas = rows(sys.argv[2]), rows(sys.argv[3])
    step = float(sys.argv[4]) if len(sys.argv) > 4 else 10.0
    print(f"## {scenario['name']}\n\n{scenario.get('description', '')}\n")
    cols = [("v", "km/h", 1), ("s", "m", 0), ("pipe", "pipe bar", 2), ("cyl", "loco cyl bar", 2),
            ("cylFirst", "1st car cyl bar", 2), ("cylLast", "last cyl bar", 2), ("mr", "MR bar", 2)]
    head = ["s"] + [f"{name} TSRE | {name} MaSzyna" for _, name, _ in cols]
    print("| " + " | ".join(h for part in head for h in part.split(" | ")) + " |")
    print("|" + " --- |" * (1 + 2 * len(cols)))
    t = 0.0
    end = scenario.get("duration", 300)
    while t <= end + 1e-6:
        a, b = at(tsre, t), at(mas, t)
        cells = [fmt(t, 0)]
        for key, _, digits in cols:
            cells += [fmt(a[key] if a else None, digits), fmt(b[key] if b else None, digits)]
        print("| " + " | ".join(cells) + " |")
        t += step
    print()
    for c in scenario.get("commands", []):
        position = c.get("trainBrakePosition")
        if isinstance(position, (int, float)) and position > 0:
            for name, table in (("TSRE", tsre), ("MaSzyna", mas)):
                found = stop_after(table, c["t"])
                if found:
                    print(f"- position {position} at {c['t']} s from {found[2]:.1f} km/h: {name} stood "
                          f"{found[0]:.1f} s and {found[1]:.0f} m later")
                else:
                    print(f"- position {position} at {c['t']} s: {name} never stood")


if __name__ == "__main__":
    main()
