#!/usr/bin/env python3
"""Compares the activity events TSRE's simulation fires with Open Rails'
(task simulation 06, phase 5).

TSRE's: the route suite's TSRE_SIM_EVENTS log of the activity (lines of
kind "event"); Open Rails': scripts/orts-activity-events.py's log
(orts-simulation-compare.sh with ORTS_EVENTS=1). Open Rails sends only
events with a message, so TSRE's without one are left out. Events are
matched by name, in order; each pair's times (Open Rails' minus TSRE's),
then those only one fired, are listed, up to the last time Open Rails
reached.

Usage: orts-compare-events.py <tsre.jsonl> <orts-events.jsonl>
"""
import json
import sys


def clock(seconds):
    s = int(seconds) % 86400
    return "%02d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60)


def main():
    tsre = [json.loads(line) for line in open(sys.argv[1]) if line.strip()]
    tsre = [e for e in tsre if e.get("kind") == "event" and e.get("message")]
    orts = [json.loads(line) for line in open(sys.argv[2]) if line.strip()]
    reached = max((e["time"] for e in orts), default=0.0)
    left = list(orts)
    pairs, only_tsre = [], []
    for e in tsre:
        match = next((o for o in left if o["name"] == e["name"]), None)
        if match is None:
            if e["time"] <= reached:
                only_tsre.append(e)
            continue
        left.remove(match)
        pairs.append((e, match))
    print("%-40s %9s %9s %7s" % ("event", "TSRE", "OR", "OR-TSRE"))
    for e, o in pairs:
        print("%-40s %9s %9s %+7.0f" % (e["name"][:40], clock(e["time"]), clock(o["time"]), o["time"] - e["time"]))
    for e in only_tsre:
        print("%-40s %9s %9s" % (e["name"][:40], clock(e["time"]), "-"))
    for o in left:
        print("%-40s %9s %9s" % (o["name"][:40], "-", clock(o["time"])))
    diffs = sorted(abs(o["time"] - e["time"]) for e, o in pairs)
    median = diffs[len(diffs) // 2] if diffs else 0.0
    print("both %d (median difference %.0f s), TSRE only %d, Open Rails only %d, up to %s"
          % (len(pairs), median, len(only_tsre), len(left), clock(reached)))


if __name__ == "__main__":
    main()
