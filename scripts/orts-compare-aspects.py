#!/usr/bin/env python3
"""Compares the aspects trains see ahead in TSRE's simulation with Open Rails'.

Reads TSRE's events (the simulation-route suite with TSRE_SIM_EVENTS set:
every 10 s each running train's next stop signal, its aspect and how far
ahead) and Open Rails' dispatcher samples (scripts/orts-simulation-compare.sh:
each train's next signal aspect and its distance). A sample is paired with
TSRE's record of the same train for the same signal (the signal's place on
the train's way within 100 m) taken within a minute; the aspects are
compared, the player's train apart from the AI trains. Samples of a train
standing at a station are left out (TSRE records running trains).

With Open Rails' signal trace (--trace: its log from a run of the local
trace build, TSRE_OR_TRACE_SIGNALS set to what --trace-items prints; task
simulation 09's report on E30's block signals), the samples where Open
Rails shows a more restrictive aspect only because a signal ahead is not
yet cleared in Open Rails, its block clear, are set aside: Open Rails
clears the signals ahead of a train one at a time as it comes up, and a
signal not cleared reads as stop to the one before it. Real automatic
block signals show what is on the track, as TSRE's cleared ones do. They
are told apart as signals on plain track (a fixed route: automatic block
signals) and signals with a junction in their block (stations' signals,
set by a dispatcher). The rest is compared again.

Usage: scripts/orts-compare-aspects.py <tsre events.jsonl> <open rails samples.jsonl> [--trace <open rails log>]
       scripts/orts-compare-aspects.py --trace-items <tsre events.jsonl>
         (the TDB items to trace, for TSRE_OR_TRACE_SIGNALS: each signal
         TSRE's trains see)
"""
import bisect
import collections
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module  # noqa: E402

dispatcher = import_module("orts-dispatcher-stops")

# Open Rails' names for the aspects, by SignalType's number.
NAMES = {0: "STOP", 1: "SPRC", 2: "REST", 3: "APP1", 4: "APP2", 5: "APP3", 6: "CLR1", 7: "CLR2"}
RANK = {name: number for number, name in NAMES.items()}
# Open Rails' trace names.
TRACED = {"STOP": "STOP", "STOP_AND_PROCEED": "SPRC", "RESTRICTING": "REST", "APPROACH_1": "APP1",
          "APPROACH_2": "APP2", "APPROACH_3": "APP3", "CLEAR_1": "CLR1", "CLEAR_2": "CLR2"}
AT_STOP = ("STOP", "SPRC", "REST")
SAME_SIGNAL = 100.0
SAME_TIME = 60.0
# How far ahead the signal at stop that makes an aspect restrictive is
# looked for (a 4-aspect block: the third signal; signals passing a stop
# on, cleared with their blocks clear, are followed past).
CHAIN = 6


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
                (event["time"], event["travelled"] + event["ahead"], NAMES.get(event["aspect"], "?"),
                 event.get("item", -1)))
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
            standing = row.get("AI mode", "").split(" ")[0] == "STA"
            if travelled is None:
                continue
            if number in shown and travelled < shown[number] - 200.0:
                carried[number] += shown[number]
            shown[number] = travelled
            if ahead is None or not aspect or standing:
                continue
            trains[number].append((time, travelled + carried[number] + ahead, aspect))
    return trains


def trace_items(events_path):
    """The TDB items for TSRE_OR_TRACE_SIGNALS: each signal TSRE's trains
    see (so the ones after each on their ways too)."""
    ways = collections.defaultdict(list)
    for line in open(events_path):
        if not line.strip():
            continue
        event = json.loads(line)
        if event["kind"] == "signal" and event.get("item", -1) >= 0:
            way = ways[event["train"]]
            if not way or way[-1] != event["item"]:
                way.append(event["item"])
    items = set()
    for way in ways.values():
        items.update(way)
    return ",".join(str(item) for item in sorted(items))


def traced(path):
    """Open Rails' signal trace: per TDB head item, the signal's states in
    time order (time, its NORMAL head's aspect, cleared, next signal's
    first item, block state, fixed route, held)."""
    timeline = collections.defaultdict(list)
    line_re = re.compile(r"TSRETRACE (\d\d):(\d\d):(\d\d) SIGNAL (\S+) heads=\[(.*?)\] enabled=(.*?) hold=(\S+) "
                         r"nextNormal=(\S+) route=\[.*?\] ref=\S+ tc=\S+ node=\S+ block=(\S+) fixed=(\S+)")
    for line in open(path, errors="replace"):
        found = line_re.search(line)
        if not found:
            continue
        h, m, s = (int(found.group(k)) for k in (1, 2, 3))
        aspect = None
        for head in found.group(5).split(","):
            parts = head.split(":")
            if len(parts) >= 4 and parts[2] == "NORMAL" and aspect is None:
                aspect = TRACED.get(parts[3], parts[3])
        following = found.group(8)
        state = (h * 3600 + m * 60 + s, aspect, found.group(6) != "none",
                 int(following.split("/")[0]) if not following.startswith("-") else -1,
                 found.group(9), found.group(10) == "True", found.group(7) != "None")
        for item in found.group(4).split("/"):
            timeline[int(item)].append(state)
    return timeline


def state_at(timeline, item, time):
    states = timeline.get(item)
    if not states:
        return None
    k = bisect.bisect_right([state[0] for state in states], time) - 1
    return states[k] if k >= 0 else None


def clearing_window(timeline, item, time):
    """Why Open Rails' signal shows a restrictive aspect: follows its next
    signals to the first at stop. When that one is not cleared in Open
    Rails (its clearing window: set aside): "plain track" if its block, on
    its fixed route, is clear; "junction" for a signal with a junction in
    its block (Open Rails reads no block for those while not cleared). A
    signal at stop though cleared, its block clear and not held, passes a
    stop on from its own next signals (CMK_E30's PKP_BLOK_SBL): followed
    past. "untraced", or None (kept: an occupied block, a hold, a signal
    cleared and at stop for its own reasons)."""
    for _ in range(CHAIN):
        state = state_at(timeline, item, time)
        if state is None:
            return "untraced"
        _, aspect, cleared, following, block, fixed, held = state
        if aspect in AT_STOP:
            if not cleared and fixed and block == "CLEAR":
                return "plain track"
            if not cleared and not fixed:
                return "junction"
            if not (cleared and block == "CLEAR" and not held):
                return None
        if following < 0:
            return None
        item = following
    return None


def main(events_path, samples_path, trace_path=None):
    timeline = traced(trace_path) if trace_path else None
    aside = collections.Counter()
    kept_pairs, kept_same = 0, 0
    kept_differ = collections.Counter()
    activity, mine = ours(events_path)
    loose = activity["loose"] if activity else 0
    by_number = {}
    for (player, traffic), records in mine.items():
        by_number["0" if player else str(loose + 1 + traffic)] = records
    pairs, same = collections.Counter(), collections.Counter()
    differ = collections.Counter()
    by_item = collections.Counter()
    for number, samples in theirs(samples_path).items():
        records = by_number.get(number)
        if not records:
            continue
        who = "player" if number == "0" else "AI"
        for time, place, aspect in samples:
            best = None
            for record in records:
                if abs(record[0] - time) <= SAME_TIME and abs(record[1] - place) <= SAME_SIGNAL:
                    if best is None or abs(record[0] - time) < abs(best[0] - time):
                        best = record
            if best is None:
                continue
            pairs[who] += 1
            if best[2] == aspect:
                same[who] += 1
            else:
                differ[(who, aspect, best[2])] += 1
                by_item[(best[3], aspect, best[2])] += 1
            if timeline is None:
                continue
            why = None
            if best[2] != aspect and RANK.get(aspect, 9) < RANK.get(best[2], 9):
                why = clearing_window(timeline, best[3], time)
            if why in ("plain track", "junction"):
                aside[why] += 1
            else:
                if why == "untraced":
                    aside["untraced (kept)"] += 1
                kept_pairs += 1
                kept_same += best[2] == aspect
                if best[2] != aspect:
                    kept_differ[(who, aspect, best[2])] += 1
    total, equal = sum(pairs.values()), sum(same.values())
    print("%s: %d samples paired, the same aspect in %d (%.0f %%); AI trains %d of %d, the player's %d of %d"
          % (activity["name"] if activity else events_path, total, equal, 100.0 * equal / max(total, 1),
             same["AI"], pairs["AI"], same["player"], pairs["player"]))
    for (who, theirs_aspect, mine_aspect), count in differ.most_common(8):
        print("  %s: Open Rails %s, TSRE %s: %d" % (who, theirs_aspect, mine_aspect, count))
    if timeline is not None:
        print("  set aside, Open Rails' clearing window (a signal ahead not yet cleared there): %d before "
              "signals on plain track (automatic block signals, their blocks clear), %d before signals with a "
              "junction in their block (stations' signals: when their routes are set); %d differing samples "
              "not traced (kept)" % (aside["plain track"], aside["junction"], aside["untraced (kept)"]))
        plain_kept = total - aside["plain track"]
        print("  without the plain track ones: %d samples, the same aspect in %d (%.0f %%); without both: %d, "
              "the same in %d (%.0f %%)"
              % (plain_kept, equal, 100.0 * equal / max(plain_kept, 1), kept_pairs, kept_same,
                 100.0 * kept_same / max(kept_pairs, 1)))
        for (who, theirs_aspect, mine_aspect), count in kept_differ.most_common(8):
            print("    left: %s: Open Rails %s, TSRE %s: %d" % (who, theirs_aspect, mine_aspect, count))
    if os.environ.get("ORTS_ASPECTS_BY_SIGNAL"):
        # The signals (TSRE's next signal's first NORMAL head, its TDB item)
        # that differ most.
        for (item, theirs_aspect, mine_aspect), count in by_item.most_common(int(os.environ["ORTS_ASPECTS_BY_SIGNAL"])):
            print("  item %s: Open Rails %s, TSRE %s: %d" % (item, theirs_aspect, mine_aspect, count))


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--trace-items":
        print(trace_items(sys.argv[2]))
    elif len(sys.argv) == 3:
        main(sys.argv[1], sys.argv[2])
    elif len(sys.argv) == 5 and sys.argv[3] == "--trace":
        main(sys.argv[1], sys.argv[2], sys.argv[4])
    else:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
