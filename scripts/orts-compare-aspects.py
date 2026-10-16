#!/usr/bin/env python3
"""Compares the aspects trains see ahead in TSRE's simulation with Open Rails'.

Reads TSRE's events (the simulation-route suite with TSRE_SIM_EVENTS set:
every 10 s each running train's next stop signal, its aspect and how far
ahead) and Open Rails' dispatcher samples (scripts/orts-simulation-compare.sh:
each train's next signal aspect and its distance). A sample is paired with
TSRE's record of the same train for the same signal (the signal's place on
the train's way within 100 m) taken within a minute; the aspects are
compared.

Usage: scripts/orts-compare-aspects.py <tsre events.jsonl> <open rails samples.jsonl>
"""
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module  # noqa: E402

dispatcher = import_module("orts-dispatcher-stops")

# Open Rails' names for the aspects, by SignalType's number.
NAMES = {0: "STOP", 1: "SPRC", 2: "REST", 3: "APP1", 4: "APP2", 5: "APP3", 6: "CLR1", 7: "CLR2"}
SAME_SIGNAL = 100.0
SAME_TIME = 60.0


def ours(path):
    activity, trains = None, collections.defaultdict(list)
    for line in open(path):
        if not line.strip():
            continue
        event = json.loads(line)
        if event["kind"] == "activity":
            activity = event
        elif event["kind"] == "signal":
            trains[(event["player"], event["traffic"])].append(
                (event["time"], event["travelled"] + event["ahead"], NAMES.get(event["aspect"], "?")))
    return activity, trains


def theirs(path):
    """Per Open Rails train number: (time, the next signal's place along its
    way, its aspect), the distance travelled carried over reversals."""
    trains = collections.defaultdict(list)
    shown, carried = {}, collections.defaultdict(float)
    for sample in dispatcher.samples_of(path):
        time = float(sample["time"])
        for row in dispatcher.rows(sample):
            number = row["Train"].split()[0]
            travelled = dispatcher.metres(row.get("Travelled", ""))
            ahead = dispatcher.metres(row.get("Distance", ""))
            aspect = row.get("Signal", "")
            if travelled is None:
                continue
            if number in shown and travelled < shown[number] - 200.0:
                carried[number] += shown[number]
            shown[number] = travelled
            if ahead is None or not aspect:
                continue
            trains[number].append((time, travelled + carried[number] + ahead, aspect))
    return trains


def main(events_path, samples_path):
    activity, mine = ours(events_path)
    loose = activity["loose"] if activity else 0
    by_number = {}
    for (player, traffic), records in mine.items():
        by_number["0" if player else str(loose + 1 + traffic)] = records
    pairs, same = 0, 0
    differ = collections.Counter()
    for number, samples in theirs(samples_path).items():
        records = by_number.get(number)
        if not records:
            continue
        for time, place, aspect in samples:
            best = None
            for record in records:
                if abs(record[0] - time) <= SAME_TIME and abs(record[1] - place) <= SAME_SIGNAL:
                    if best is None or abs(record[0] - time) < abs(best[0] - time):
                        best = record
            if best is None:
                continue
            pairs += 1
            if best[2] == aspect:
                same += 1
            else:
                differ[(aspect, best[2])] += 1
    print("%s: %d samples paired, the same aspect in %d (%.0f %%)"
          % (activity["name"] if activity else events_path, pairs, same, 100.0 * same / max(pairs, 1)))
    for (theirs_aspect, mine_aspect), count in differ.most_common(8):
        print("  Open Rails %s, TSRE %s: %d" % (theirs_aspect, mine_aspect, count))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    main(sys.argv[1], sys.argv[2])
