#!/usr/bin/env python3
"""The ET22's sounds in TSRE against MaSzyna's, from the two runs' logs
(tsre/run.sh and maszyna/run.sh: sounds.csv, state.csv, marks.csv).

    compare.py TSRE_RUN MASZYNA_RUN [--out report.md]

Each sound is a key (MaSzyna's .mmd key; TSRE's "cab:" and "control:"
prefixes dropped for the match). Each second, a key's level is its loudest
voice's final gain (what it plays at, before the master volume). Compared:
- by phase of the drive (from the marks): standing with the converter and
  the compressor on, powering up, the last seconds at speed, braking,
  standing at the end; each key's median level;
- by speed while powering (20 km/h bins);
- the driving values at those speeds (motor current, main controller).
"""
import argparse
import csv
import math
import os
import statistics
from collections import defaultdict


def read_csv(path):
    with open(path, newline="") as f:
        rows = [r for r in f if not r.startswith("#")]
    return list(csv.DictReader(rows))


def number(v):
    try:
        x = float(v)
        return x if math.isfinite(x) else None
    except (TypeError, ValueError):
        return None


def norm_key(k):
    for p in ("cab:", "control:"):
        if k.startswith(p):
            return k[len(p):]
    return k


class Run:
    def __init__(self, folder, name):
        self.name = name
        self.levels = defaultdict(dict)  # key -> t -> loudest final
        self.files = defaultdict(set)
        for r in read_csv(os.path.join(folder, "sounds.csv")):
            t = number(r["t"])
            final = number(r["final"])
            if t is None or final is None:
                continue
            key = norm_key(r["key"]) or os.path.basename(r["file"])
            self.levels[key][t] = max(self.levels[key].get(t, 0.0), final)
            self.files[key].add(os.path.basename(r["file"]))
        self.state = defaultdict(dict)  # t -> name -> value
        path = os.path.join(folder, "state.csv")
        rows = read_csv(path)
        if rows and "name" in rows[0]:
            for r in rows:
                t = number(r["t"])
                if t is not None:
                    self.state[t][r["name"]] = r["value"]
        else:
            for r in rows:
                t = number(r["t"])
                if t is not None:
                    self.state[t].update({k: v for k, v in r.items() if k != "t"})
        self.marks = {}
        for r in read_csv(os.path.join(folder, "marks.csv")):
            ev, t = r["event"], number(r["t"])
            if ev not in self.marks and t is not None:
                self.marks[ev] = t
        self.seconds = sorted(self.state)

    def value(self, t, *names):
        s = self.state.get(t, {})
        for n in names:
            v = number(s.get(n))
            if v is not None:
                return v
        return None

    def speed(self, t):
        return self.value(t, "speedKmh", "speed_kmh")

    def phases(self):
        m = self.marks
        last = self.seconds[-1] if self.seconds else 0
        out = {}

        def span(a, b, pad=1):
            if a is None or b is None or b - a <= 2 * pad:
                return None
            return (a + pad, b - pad)

        out["standing, converter+compressor"] = span(m.get("compressor"), m.get("release"))
        out["powering"] = span(m.get("power"), m.get("100kmh") or m.get("power_off"))
        top = m.get("100kmh") or m.get("power_off")
        out["last 10 s of power"] = (top - 10, top) if top else None
        out["coasting"] = span(m.get("power_off"), m.get("brake"))
        out["braking"] = span(m.get("brake"), m.get("stopped"))
        out["stopped at the end"] = span(m.get("stopped"), m.get("end") or last)
        return out

    def level(self, key, seconds):
        lv = self.levels.get(key, {})
        values = [lv.get(t, 0.0) for t in seconds]
        return statistics.median(values) if values else None


def fmt(v):
    return "-" if v is None else f"{v:.3f}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tsre")
    ap.add_argument("maszyna")
    ap.add_argument("--out")
    args = ap.parse_args()
    runs = [Run(args.tsre, "TSRE"), Run(args.maszyna, "MaSzyna")]
    keys = sorted(set(runs[0].levels) | set(runs[1].levels))
    lines = ["# ET22 sounds: TSRE against MaSzyna", ""]
    lines += ["Each key's median level (its loudest voice's final gain, before the master volume).", ""]
    for r in runs:
        marks = ", ".join(f"{k} {v:.0f}" for k, v in sorted(r.marks.items(), key=lambda i: i[1])
                          if k != "alerter")
        lines.append(f"- {r.name} marks (s): {marks}")
    lines.append("")
    phases = [r.phases() for r in runs]
    for name in phases[0]:
        spans = [p.get(name) for p in phases]
        if not any(spans):
            continue
        secs = [[t for t in r.seconds if s and s[0] <= t <= s[1]] for r, s in zip(runs, spans)]
        lines.append(f"## {name}")
        lines.append("")
        for r, sec in zip(runs, secs):
            sp = [r.speed(t) for t in sec if r.speed(t) is not None]
            lines.append(f"- {r.name}: {len(sec)} s" + (f", {min(sp):.0f}-{max(sp):.0f} km/h" if sp else ""))
        lines.append("")
        lines.append("| key | TSRE | MaSzyna | ratio |")
        lines.append("|---|---|---|---|")
        for k in keys:
            a = runs[0].level(k, secs[0]) if secs[0] else None
            b = runs[1].level(k, secs[1]) if secs[1] else None
            if (a or 0) < 0.005 and (b or 0) < 0.005:
                continue
            ratio = "-" if not a or not b else f"{a / b:.2f}"
            lines.append(f"| {k} | {fmt(a)} | {fmt(b)} | {ratio} |")
        lines.append("")
    # By speed while powering.
    lines.append("## Powering, by speed")
    lines.append("")
    bins = [(lo, lo + 20) for lo in range(0, 120, 20)]
    for r in runs:
        r.power = phases[runs.index(r)].get("powering")
    header = "| key | " + " | ".join(f"{lo}-{hi} T / M" for lo, hi in bins) + " |"
    lines.append(header)
    lines.append("|---|" + "---|" * len(bins))
    binned = []
    for r in runs:
        b = []
        for lo, hi in bins:
            b.append([t for t in r.seconds if r.power and r.power[0] <= t <= r.power[1]
                      and r.speed(t) is not None and lo <= r.speed(t) < hi])
        binned.append(b)
    for k in keys:
        cells = []
        shown = False
        for i in range(len(bins)):
            a = runs[0].level(k, binned[0][i]) if binned[0][i] else None
            b = runs[1].level(k, binned[1][i]) if binned[1][i] else None
            shown = shown or (a or 0) >= 0.005 or (b or 0) >= 0.005
            cells.append(f"{fmt(a)} / {fmt(b)}")
        if shown:
            lines.append(f"| {k} | " + " | ".join(cells) + " |")
    lines.append("")
    for name, names in (("motor current (A)", ("motorCurrent", "current_a")),
                        ("main controller", ("mainController", "mc")),
                        ("motor rev/s", ("motorRps", "motor_rps"))):
        cells = []
        for i in range(len(bins)):
            pair = []
            for r, b in zip(runs, binned):
                vals = [r.value(t, *names) for t in b[i]]
                vals = [v for v in vals if v is not None]
                pair.append(fmt(statistics.median(vals)) if vals else "-")
            cells.append(" / ".join(pair))
        lines.append(f"| {name} | " + " | ".join(cells) + " |")
    lines.append("")
    lines.append("## Files by key")
    lines.append("")
    for k in keys:
        t = ", ".join(sorted(runs[0].files.get(k, []))) or "-"
        m = ", ".join(sorted(runs[1].files.get(k, []))) or "-"
        lines.append(f"- {k}: TSRE {t}; MaSzyna {m}")
    text = "\n".join(lines) + "\n"
    if args.out:
        with open(args.out, "w") as f:
            f.write(text)
    print(text)


if __name__ == "__main__":
    main()
