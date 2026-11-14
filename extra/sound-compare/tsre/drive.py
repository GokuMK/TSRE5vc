#!/usr/bin/env python3
"""The ET22's drive for TSRE's capture (renderer-capture's timeline): the
same steps as the MaSzyna side's driver (../maszyna), one continuous run.

    drive.py OUT [--activity evegrain.act] [--train 1] [--brake 0.45]

writes OUT/drive.json. TSRE's MaSzyna locos start hot, so the run first
shuts the loco down (pantographs, main switch, converter, compressor,
battery), waits for it to go quiet, then starts it step by step.
"""
import argparse
import json
import os


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--activity", default="evegrain.act")
    ap.add_argument("--train", type=int, default=1)
    ap.add_argument("--brake", type=float, default=0.45, help="trainBrake for the service braking")
    ap.add_argument("--speed", type=float, default=100.0, help="km/h to power to")
    ap.add_argument("--keep-battery", action="store_true", help="the battery left on in the shutdown")
    args = ap.parse_args()

    def step(after=0.0, commands=None, mark=None, done=None, every=None, until=None, max_=None, end=False):
        s = {"after": after}
        if commands:
            s["commands"] = commands
        if mark:
            s["mark"] = mark
        if done:
            s["done"] = done
        if every:
            s["every"] = every
        if until:
            s["until"] = until
        if max_:
            s["max"] = max_
        if end:
            s["end"] = True
        return s

    charged = ["brakePipe", ">=", 490000]
    timeline = [
        # The activity's train may start rolling: stopped by service braking,
        # its pipe charged again, held by the loco's own brake.
        step(2, [["throttle", 0], ["shuntField", 0], ["trainBrake", args.brake]], mark="stopping",
             until=["speedKmh", "<=", 0.5], max_=180, done="stopped_first"),
        step(2, [["trainBrake", 0.05], ["engineBrake", 1]], mark="hold", until=charged, max_=90,
             done="pipe_charged_first"),
        # TSRE's hot start undone: everything off.
        step(2, [["pantograph1", "down"], ["pantograph2", "down"], ["circuitBreaker", "open"],
                 ["converter_sw", False], ["compressor_sw", False]], mark="shutdown"),
        step(0.5, [["circuitBreaker", "released"]]),
    ]
    if not args.keep_battery:
        timeline.append(step(15, [["battery", "off"]], mark="battery_off"))
    timeline += [
        # The drive (as ../maszyna's driver).
        step(15, mark="start"),
        step(0, [["battery", "on"]], mark="battery"),
        step(5, [["pantograph1", "up"], ["pantograph2", "up"]], mark="pantographs"),
        step(10, [["circuitBreaker", "close"]], mark="main_switch"),
        step(0.5, [["circuitBreaker", "released"]]),
        step(5, [["converter_sw", True]], mark="converter"),
        step(5, [["compressor_sw", True]], mark="compressor", until=["mainReservoir", ">=", 700000], max_=240,
             done="reservoir"),
        # Released: overcharge until the pipe is charged, then running.
        step(2, [["trainBrake", 0], ["engineBrake", 0], ["direction", 1]], mark="release", until=charged,
             max_=90, done="pipe_charged"),
        step(0, [["trainBrake", 0.05]]),
        step(3, [["throttle", "+"]], mark="power", every=2,
             until=[["speedKmh", ">=", args.speed], ["throttle", ">=", 0.999]], max_=400),
        step(0, [["shuntField", "+"]], mark="shunt_field", every=3, until=["speedKmh", ">=", args.speed], max_=240,
             done="100kmh"),
        step(0, [["throttle", 0], ["shuntField", 0]], mark="power_off"),
        step(10, [["trainBrake", args.brake]], mark="brake", until=["speedKmh", "<=", 0.5], max_=300,
             done="stopped"),
        step(20, mark="end", end=True),
    ]
    out = os.path.abspath(args.out)
    spec = {
        "activity": args.activity,
        "aiTrains": "none",
        "simulateTo": "start",
        "simulationDriven": args.train,
        "simulationChosen": args.train,
        "width": 320,
        "height": 200,
        "pickGrid": [1, 1],
        "timingFrames": 1,
        "shadows": False,
        "settle": {"minFrames": 20, "stableFrames": 2, "maxFrames": 60, "maxSeconds": 300},
        "output": os.path.join(out, "capture"),
        "views": [{
            "name": "drive",
            "train": args.train,
            "trainView": "cab",
            "runFor": 1200,
            "runLive": True,
            "timeline": timeline,
            # MaSzyna's vigilance (security.lua's alerter) acknowledged, as
            # the MaSzyna side's driver does.
            "periodic": {"every": 30, "commands": [["alerter", "on"]], "release": [["alerter", "off"]],
                         "mark": "alerter"},
        }],
    }
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "drive.json"), "w") as f:
        json.dump(spec, f, indent=1)


if __name__ == "__main__":
    main()
