#!/usr/bin/env python3
"""Compares an activity's trains in TSRE's simulation with Open Rails.

Reads TSRE's events (the simulation-route suite with TSRE_SIM_EVENTS set)
and Open Rails' dispatcher samples (scripts/orts-simulation-compare.sh).
Open Rails numbers its trains: the player's 0, the activity's loose
consists from 1, then the AI trains in the activity's traffic order. Each
Open Rails train's stops and stands are paired with the same train's in
TSRE by the distance it had travelled there; the times are compared
(TSRE minus Open Rails).

Usage: scripts/orts-compare-stops.py <tsre events.jsonl> <open rails samples.jsonl>
"""
import json
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module  # noqa: E402

dispatcher = import_module("orts-dispatcher-stops")
clock = dispatcher.clock

# Paired when this near (metres, or this share of the distance travelled);
# stands further apart (a train may stop at another signal of the same
# meet).
NEAR_METRES = 400.0
NEAR_SHARE = 0.02
NEAR_WAIT_METRES = 1500.0
# Stands shorter than this are left out (seconds).
SHORTEST_WAIT = 20.0


def ours(path):
    """The activity line and, per TSRE train, its plan fields, stops and
    waits."""
    activity, trains = None, {}
    for line in open(path):
        if not line.strip():
            continue
        event = json.loads(line)
        if event["kind"] == "activity":
            activity = event
            continue
        train = trains.setdefault(event["train"], {"service": event["service"], "time": event["serviceTime"],
                                                   "traffic": event["traffic"], "player": event["player"],
                                                   "stops": [], "waits": [], "started": None})
        kind = event["kind"]
        if kind == "started":
            train["started"] = event["time"]
        elif kind == "arrived":
            train["stops"].append({"arrived": event["time"], "departed": None, "travelled": event["travelled"],
                                   "platform": event["platform"], "booked": event["booked"]})
        elif kind == "departed" and train["stops"]:
            train["stops"][-1]["departed"] = event["time"]
        elif kind == "resumed" and event["time"] - event["since"] >= SHORTEST_WAIT:
            train["waits"].append((event["since"], event["time"], event["travelled"]))
    return activity, trains


def near(a, b, metres=NEAR_METRES):
    return a is not None and b is not None and abs(a - b) <= max(metres, NEAR_SHARE * max(a, b))


def pair(theirs, mine, at, metres=NEAR_METRES):
    """Each of theirs with the nearest unpaired of mine by distance."""
    used, pairs = set(), []
    for item in theirs:
        best = None
        for k, candidate in enumerate(mine):
            if k in used or not near(at(item), at(candidate), metres):
                continue
            if best is None or abs(at(candidate) - at(item)) < abs(at(mine[best]) - at(item)):
                best = k
        if best is not None:
            used.add(best)
        pairs.append((item, mine[best] if best is not None else None))
    return pairs, [m for k, m in enumerate(mine) if k not in used]


def main(events_path, samples_path):
    activity, mine = ours(events_path)
    theirs = dispatcher.read(samples_path)
    loose = activity["loose"] if activity else 0
    by_number = {}
    for train in mine.values():
        number = 0 if train["player"] else loose + 1 + train["traffic"]
        by_number[str(number)] = train
    stop_differences, wait_differences = [], []
    print("%s: Open Rails sampled %s to %s" % (activity["name"] if activity else events_path,
                                               clock(min(t["first"] for t in theirs.values())),
                                               clock(max(t["last"] for t in theirs.values()))))
    for number in sorted(theirs, key=lambda n: int(n) if n.isdigit() else 0):
        other = theirs[number]
        train = by_number.get(number)
        if train is None:
            print("train %s (%s): no TSRE train" % (number, other["name"]))
            continue
        print("train %s = %s %s" % (number, train["service"], "(player)" if train["player"] else clock(train["time"])))
        seen_from, seen_to = other["first"], other["last"]
        # Paired by distance with any of TSRE's (its trains may be far
        # ahead or behind); those left over listed when in the time Open
        # Rails was watched.
        pairs, left = pair(other["stops"], train["stops"], lambda s: s["travelled"])
        for theirs_stop, my_stop in pairs:
            if my_stop is None:
                print("  stop at %.0f m: Open Rails %s, TSRE none" % (theirs_stop["travelled"] or 0,
                                                                      clock(theirs_stop["arrived"])))
                continue
            difference = my_stop["arrived"] - theirs_stop["arrived"]
            stop_differences.append(difference)
            print("  stop %-22s arrived TSRE %s, Open Rails %s (%+5.0f s)" % (my_stop["platform"][:22],
                  clock(my_stop["arrived"]), clock(theirs_stop["arrived"]), difference))
        for my_stop in left:
            if seen_from <= my_stop["arrived"] <= seen_to:
                print("  stop %-22s arrived TSRE %s, Open Rails none" % (my_stop["platform"][:22],
                                                                        clock(my_stop["arrived"])))
        their_waits = [w for w in other["waits"] if w[2] - w[1] >= SHORTEST_WAIT]
        pairs, left = pair(their_waits, train["waits"], lambda w: w[-1], NEAR_WAIT_METRES)
        for (mode, since, until, travelled), mine_wait in pairs:
            if mine_wait is None:
                print("  wait at %.0f m: Open Rails %s %s to %s (%d s), TSRE none"
                      % (travelled or 0, mode, clock(since), clock(until), until - since))
                continue
            wait_differences.append((mine_wait[1] - mine_wait[0]) - (until - since))
            print("  wait at %.0f m: TSRE %s to %s (%d s), Open Rails %s to %s (%d s)"
                  % (travelled or 0, clock(mine_wait[0]), clock(mine_wait[1]), mine_wait[1] - mine_wait[0],
                     clock(since), clock(until), until - since))
        for since, until, travelled in left:
            if seen_from <= since <= seen_to:
                print("  wait at %.0f m: TSRE %s to %s (%d s), Open Rails none"
                      % (travelled, clock(since), clock(until), until - since))
    if stop_differences:
        print("stops paired: %d, TSRE minus Open Rails: median %+.0f s, mean %+.0f s, from %+.0f to %+.0f s"
              % (len(stop_differences), statistics.median(stop_differences), statistics.mean(stop_differences),
                 min(stop_differences), max(stop_differences)))
    if wait_differences:
        print("waits paired: %d, TSRE's longer by: median %+.0f s" % (len(wait_differences),
                                                                    statistics.median(wait_differences)))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    main(sys.argv[1], sys.argv[2])
