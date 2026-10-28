#!/usr/bin/env python3
"""Compares a train driven in TSRE and in Open Rails by the same commands
(task driving 02, step 5).

TSRE's side: the CSV of the simulation-route suite's TSRE_SIM_DRIVE mode
(TSRE_SIM_DRIVE_LOG). Open Rails' side: the samples of
scripts/orts-drive-run.sh (its driving display, read every half second).
Prints both every `step` seconds and how they differ: speed, the brake
pipe and cylinder, distance (Open Rails': its speed integrated), and when
each reached some speeds and stopped.

Usage: drive-compare.py <tsre.csv> <openrails-drive.jsonl> [step seconds]
"""
import csv
import json
import re
import sys

MPH = 0.44704
KMH = 1 / 3.6
PSI = 6894.757293168
BAR = 100000.0


def number(text, units):
    """A display's number in SI: "45.2 mph", "89 psi", "6.1 bar"."""
    if not text:
        return None
    match = re.search(r"(-?\d+(?:\.\d+)?)\s*([a-zA-Z/%]*)", text)
    if not match:
        return None
    value = float(match.group(1))
    return value * units.get(match.group(2).lower(), 1.0)


def openrails(path):
    samples = []
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        sample = json.loads(line)
        display = sample.get("display") or []
        row = {"seconds": sample["seconds"]}
        for item in display:
            first, last = (item.get("FirstCol") or "").strip(), (item.get("LastCol") or "").strip()
            if first == "Speed":
                row["speed"] = number(last, {"mph": MPH, "km/h": KMH, "kmh": KMH, "m/s": 1.0})
            elif first == "Throttle":
                row["throttle"] = number(last, {"%": 0.01})
            elif first == "Train brake":
                row["trainBrakeText"] = last
            elif first == "" and last.startswith("EQ"):
                row["equalising"] = number(last[2:], {"psi": PSI, "bar": BAR})
            elif first == "" and last.startswith("BC"):
                # The loco's, then the last car's.
                cylinder, _, pipe = last.partition("BP")
                prefix = "" if "brakeCylinder" not in row else "last"
                row[prefix + ("brakeCylinder" if not prefix else "BrakeCylinder")] = number(cylinder[2:], {"psi": PSI, "bar": BAR})
                row[prefix + ("brakePipe" if not prefix else "BrakePipe")] = number(pipe, {"psi": PSI, "bar": BAR})
        if row.get("speed") is not None:
            samples.append(row)
    # Distance: the speed integrated.
    travelled = 0.0
    for i, row in enumerate(samples):
        if i > 0:
            before = samples[i - 1]
            travelled += 0.5 * (row["speed"] + before["speed"]) * (row["seconds"] - before["seconds"])
        row["travelled"] = travelled
    return samples


def tsre(path):
    rows = []
    for row in csv.DictReader(open(path)):
        rows.append({key: float(value) for key, value in row.items()})
    return rows


def at(samples, seconds, key):
    """A value at a time, linear between samples."""
    for i in range(1, len(samples)):
        a, b = samples[i - 1], samples[i]
        if a["seconds"] <= seconds <= b["seconds"]:
            if key not in a or key not in b or a[key] is None or b[key] is None:
                return None
            span = b["seconds"] - a["seconds"]
            share = (seconds - a["seconds"]) / span if span > 0 else 0.0
            return a[key] + (b[key] - a[key]) * share
    return None


def reached(samples, speed, after=0.0):
    for row in samples:
        if row["seconds"] >= after and row.get("speed") is not None and row["speed"] >= speed:
            return row["seconds"]
    return None


def stopped(samples, after):
    for row in samples:
        if row["seconds"] > after and row.get("speed") is not None and row["speed"] < 0.05:
            return row
    return None


def main():
    ours, theirs = tsre(sys.argv[1]), openrails(sys.argv[2])
    step = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
    end = min(ours[-1]["seconds"], theirs[-1]["seconds"])
    print("| s | TSRE m/s | OR m/s | TSRE m | OR m | TSRE pipe psi | OR pipe psi | TSRE cyl psi | OR cyl psi "
          "| TSRE last pipe | OR last pipe | TSRE last cyl | OR last cyl |")
    print("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    t = 0.0

    def fmt(value, scale=1.0, digits=1):
        return "-" if value is None else f"{value / scale:.{digits}f}"

    while t <= end + 1e-9:
        print("| %g | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            t, fmt(at(ours, t, "speed")), fmt(at(theirs, t, "speed")),
            fmt(at(ours, t, "travelled"), 1, 0), fmt(at(theirs, t, "travelled"), 1, 0),
            fmt(at(ours, t, "brakePipe"), PSI), fmt(at(theirs, t, "brakePipe"), PSI),
            fmt(at(ours, t, "brakeCylinder"), PSI), fmt(at(theirs, t, "brakeCylinder"), PSI),
            fmt(at(ours, t, "lastBrakePipe"), PSI), fmt(at(theirs, t, "lastBrakePipe"), PSI),
            fmt(at(ours, t, "lastBrakeCylinder"), PSI), fmt(at(theirs, t, "lastBrakeCylinder"), PSI)))
        t += step
    print()
    def seconds(value):
        return "never" if value is None else "%.1f s" % value

    for speed in (10.0, 20.0, 30.0, 40.0):
        print("reached %g m/s: TSRE %s, Open Rails %s" % (speed, seconds(reached(ours, speed)),
                                                          seconds(reached(theirs, speed))))
    top_ours = max(row["speed"] for row in ours)
    top_theirs = max(row["speed"] for row in theirs)
    print("top speed: TSRE %.2f m/s, Open Rails %.2f m/s" % (top_ours, top_theirs))
    peak_ours = max(ours, key=lambda row: row["speed"])["seconds"]
    peak_theirs = max(theirs, key=lambda row: row["speed"])["seconds"]
    a, b = stopped(ours, peak_ours), stopped(theirs, peak_theirs)
    if a and b:
        print("stopped: TSRE at %.1f s after %.0f m braking, Open Rails at %.1f s after %.0f m braking" % (
            a["seconds"], a["travelled"] - at(ours, peak_ours, "travelled"),
            b["seconds"], b["travelled"] - at(theirs, peak_theirs, "travelled")))


if __name__ == "__main__":
    main()
