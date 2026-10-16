#!/usr/bin/env python3
"""Reads the dispatcher samples scripts/orts-simulation-compare.sh writes and
lists, per Open Rails train, its station stops (arrival and departure, as
the samples bracket them) and the times it stood stopped elsewhere (at a
signal or behind another train).

Usage: scripts/orts-dispatcher-stops.py <samples.jsonl>
"""
import json
import sys


def clock(seconds):
    seconds = int(round(seconds)) % 86400
    return "%02d:%02d:%02d" % (seconds // 3600, seconds // 60 % 60, seconds % 60)


def rows(sample):
    table = sample["hud"].get("extraTable") or {}
    values, columns = table.get("values", []), table.get("nCols", 0)
    header = None
    for r in range(table.get("nRows", 0)):
        row = [(cell or "").strip() for cell in values[r * columns:(r + 1) * columns]]
        if row and row[0] == "Train":
            header = row
            continue
        if header is None or not row or not row[0]:
            continue
        yield dict(zip(header, row))


def main(path):
    trains = {}
    previous_time = None
    # One sample a line, or samples written across lines: read one after
    # the other.
    with open(path) as samples:
        text = samples.read()
    decoder = json.JSONDecoder()
    position = 0
    parsed = []
    while position < len(text):
        while position < len(text) and text[position].isspace():
            position += 1
        if position >= len(text):
            break
        try:
            sample, position = decoder.raw_decode(text, position)
        except json.JSONDecodeError:
            break
        parsed.append(sample)
    for sample in parsed:
        if not isinstance(sample, dict) or "time" not in sample:
            continue
        time = float(sample["time"])
        # Between this sample and the one before.
        at = time if previous_time is None else (previous_time + time) / 2.0
        for row in rows(sample):
            number = row["Train"].split()[0]
            train = trains.setdefault(number, {"name": row.get("Consist", ""), "mode": None,
                                               "since": at, "stops": [], "waits": []})
            mode = row.get("AI mode", "").split(" ")[0]
            if mode != train["mode"]:
                if train["mode"] == "STA":
                    train["stops"][-1]["departed"] = at
                if train["mode"] in ("STP", "WTP") and at - train["since"] >= 5.0:
                    train["waits"].append((train["mode"], train["since"], at, train["travelled"]))
                if mode == "STA":
                    train["stops"].append({"arrived": at, "departed": None, "booked": row.get("AI data", ""),
                                           "travelled": row.get("Travelled", "")})
                train["mode"] = mode
                train["since"] = at
            train["travelled"] = row.get("Travelled", "")
        previous_time = time
    for number in sorted(trains, key=lambda n: int(n) if n.isdigit() else 0):
        train = trains[number]
        print("train %s %s" % (number, train["name"]))
        for stop in train["stops"]:
            print("  stop   arrived %s departed %s (departure shown %s) travelled %s"
                  % (clock(stop["arrived"]), clock(stop["departed"]) if stop["departed"] else "-",
                     stop["booked"], stop["travelled"]))
        for mode, since, until, travelled in train["waits"]:
            print("  %s from %s to %s (%d s) at %s" % (mode, clock(since), clock(until), until - since, travelled))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    main(sys.argv[1])
