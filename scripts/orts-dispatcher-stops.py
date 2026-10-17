#!/usr/bin/env python3
"""Reads the dispatcher samples scripts/orts-simulation-compare.sh writes and
lists, per Open Rails train, its station stops (arrival and departure, as
the samples bracket them) and the times it stood stopped elsewhere (at a
signal or behind another train). scripts/orts-compare-stops.py uses it.

Usage: scripts/orts-dispatcher-stops.py <samples.jsonl>
"""
import json
import re
import sys

# Metres in the units the dispatcher page shows distances in.
UNITS = {"m": 1.0, "km": 1000.0, "yd": 0.9144, "mi": 1609.344}


def clock(seconds):
    seconds = int(round(seconds)) % 86400
    return "%02d:%02d:%02d" % (seconds // 3600, seconds // 60 % 60, seconds % 60)


def metres(text):
    """A distance as the dispatcher page shows it ("3.1 km", "95.6 mi")."""
    found = re.match(r"\s*(-?[0-9.]+)\s*([a-z]+)", text or "")
    if not found or found.group(2) not in UNITS:
        return None
    return float(found.group(1)) * UNITS[found.group(2)]


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


def samples_of(path):
    """The samples, one a line or written across lines."""
    with open(path) as samples:
        text = samples.read()
    decoder = json.JSONDecoder()
    position = 0
    while position < len(text):
        while position < len(text) and text[position].isspace():
            position += 1
        if position >= len(text):
            break
        try:
            sample, position = decoder.raw_decode(text, position)
        except json.JSONDecodeError:
            break
        if isinstance(sample, dict) and "time" in sample:
            yield sample


def read(path):
    """Per Open Rails train number: its name (as shown, cut short), when it
    was first and last seen, its stops (arrived, departed, travelled in
    metres) and its stands elsewhere (mode, since, until, travelled). Open
    Rails counts the distance travelled from 0 again after a reversal: it
    is carried on here."""
    trains = {}
    previous_time = None
    for sample in samples_of(path):
        time = float(sample["time"])
        # Between this sample and the one before.
        at = time if previous_time is None else (previous_time + time) / 2.0
        for row in rows(sample):
            number = row["Train"].split()[0]
            shown = metres(row.get("Travelled", ""))
            train = trains.setdefault(number, {"name": row.get("Consist", ""), "mode": None, "since": at,
                                               "first": time, "stops": [], "waits": [], "travelled": shown,
                                               "shown": shown, "carried": 0.0})
            if shown is not None and train["shown"] is not None and shown < train["shown"] - 200.0:
                train["carried"] += train["shown"]
            if shown is not None:
                train["shown"] = shown
            travelled = None if shown is None else shown + train["carried"]
            train["last"] = time
            mode = row.get("AI mode", "").split(" ")[0]
            if mode != train["mode"]:
                if train["mode"] == "STA" and train["stops"]:
                    train["stops"][-1]["departed"] = at
                if train["mode"] in ("STP", "WTP") and at - train["since"] >= 5.0:
                    train["waits"].append((train["mode"], train["since"], at, train["travelled"]))
                if mode == "STA":
                    train["stops"].append({"arrived": at, "departed": None, "booked": row.get("AI data", ""),
                                           "travelled": travelled})
                train["mode"] = mode
                train["since"] = at
            train["travelled"] = travelled
        previous_time = time
    return trains


def main(path):
    trains = read(path)
    for number in sorted(trains, key=lambda n: int(n) if n.isdigit() else 0):
        train = trains[number]
        print("train %s %s" % (number, train["name"]))
        for stop in train["stops"]:
            print("  stop   arrived %s departed %s (departure shown %s) travelled %.0f m"
                  % (clock(stop["arrived"]), clock(stop["departed"]) if stop["departed"] else "-",
                     stop["booked"], stop["travelled"] or 0.0))
        for mode, since, until, travelled in train["waits"]:
            print("  %s from %s to %s (%d s) at %.0f m" % (mode, clock(since), clock(until), until - since,
                                                         travelled or 0.0))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    main(sys.argv[1])
