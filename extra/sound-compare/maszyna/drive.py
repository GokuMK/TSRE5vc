#!/usr/bin/env python3
"""Drives MaSzyna's ET22 through the sound comparison sequence with xdotool key presses.

Reads the keys from eu07_input-keyboard.ini, the vehicle state from <out>/state.csv (written once per
simulated second by the sound log patch) and writes <out>/marks.csv: t,event with t from the latest
state.csv row when the action was sent. Quits eu07 at the end (WM_DELETE_WINDOW through xdotool).

  drive.py --out <dir> --display :91 --pid <eu07 pid> --keys <eu07_input-keyboard.ini>
"""

import argparse
import csv
import os
import subprocess
import sys
import time

# eu07_input-keyboard.ini key names -> X keysyms
KEYPAD = {'+': 'KP_Add', '-': 'KP_Subtract', '*': 'KP_Multiply', '/': 'KP_Divide', '.': 'KP_Decimal',
          'enter': 'KP_Enter'}
NAMED = {'space': 'space', 'backspace': 'BackSpace', 'enter': 'Return', 'tab': 'Tab', 'insert': 'Insert',
         'delete': 'Delete', 'home': 'Home', 'end': 'End', 'pageup': 'Prior', 'pagedown': 'Next',
         'pause': 'Pause', 'esc': 'Escape', 'escape': 'Escape', '=': 'equal', '-': 'minus', ';': 'semicolon',
         "'": 'apostrophe', ',': 'comma', '.': 'period', '/': 'slash', '\\': 'backslash', '[': 'bracketleft',
         ']': 'bracketright', '`': 'grave', 'left': 'Left', 'right': 'Right', 'up': 'Up', 'down': 'Down'}
MODIFIERS = {'shift': 'shift', 'ctrl': 'ctrl', 'alt': 'alt'}


def keysym(name):
    if name.startswith('num_'):
        rest = name[4:]
        return KEYPAD.get(rest, 'KP_' + rest)
    if name in NAMED:
        return NAMED[name]
    if len(name) > 1 and name[0] == 'f' and name[1:].isdigit():
        return 'F' + name[1:]
    return name


def read_bindings(path):
    bindings = {}
    with open(path, encoding='latin-1') as ini:
        for line in ini:
            line = line.split('//')[0].strip()
            if not line:
                continue
            parts = line.split()
            command, keys = parts[0], parts[1:]
            if not keys or keys == ['none']:
                continue
            mods = [MODIFIERS[k] for k in keys[:-1] if k in MODIFIERS]
            bindings[command] = '+'.join(mods + [keysym(keys[-1])])
    return bindings


class Driver:

    def __init__(self, args):
        self.out = args.out
        self.env = dict(os.environ, DISPLAY=args.display)
        self.pid = args.pid
        self.keys = read_bindings(args.keys)
        self.state_path = os.path.join(self.out, 'state.csv')
        self.marks = open(os.path.join(self.out, 'marks.csv'), 'w', buffering=1)
        self.marks.write('t,event\n')
        self.window = None
        self.row = None
        self.last_ack = None
        self.notch_period = 2.0
        self.braking = False

    # --- eu07 window and keys -------------------------------------------------------------------------
    def xdotool(self, *args, check=True):
        result = subprocess.run(['xdotool', *args], env=self.env, capture_output=True, text=True)
        if check and result.returncode != 0:
            raise RuntimeError('xdotool %s: %s' % (' '.join(args), result.stderr.strip()))
        return result.stdout.strip()

    def find_window(self, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.alive()
            ids = self.xdotool('search', '--onlyvisible', '--name', '.', check=False).split()
            for wid in ids:
                name = self.xdotool('getwindowname', wid, check=False)
                if 'eu07' in name.lower() or 'maszyna' in name.lower():
                    self.window = wid
                    return wid
            time.sleep(1)
        raise RuntimeError('no eu07 window')

    def focus(self):
        self.xdotool('windowactivate', '--sync', self.window, check=False)

    def key(self, command, count=1, seconds=0.3):
        """presses the key of the command, held long enough to span a few simulation frames: impulse
        switches (pantographs, line breaker) act only while their key is down"""
        for _ in range(count):
            self.hold(command, seconds)
            time.sleep(0.15)

    def hold(self, command, seconds):
        self.focus()
        self.xdotool('keydown', self.keys[command])
        time.sleep(seconds)
        self.xdotool('keyup', self.keys[command])

    def alive(self):
        try:
            os.kill(self.pid, 0)
        except OSError:
            raise RuntimeError('eu07 exited')

    # --- state ----------------------------------------------------------------------------------------
    def read_state(self):
        try:
            with open(self.state_path) as f:
                rows = list(csv.DictReader(f))
        except FileNotFoundError:
            return None
        if rows:
            row = rows[-1]
            try:
                self.row = {k: float(v) for k, v in row.items()}
            except (TypeError, ValueError):
                pass  # a row being written
        return self.row

    def t(self):
        return self.row['t'] if self.row else 0.0

    def mark(self, event):
        self.read_state()
        self.marks.write('%d,%s\n' % (self.t(), event))
        print('[drive] t=%d %s  %s' % (self.t(), event, self.summary()), flush=True)

    def summary(self):
        r = self.row
        if not r:
            return ''
        return ('v=%.1f mc=%d sc=%d I=%.0f main=%d conv=%d comp=%d bat=%d pant=%d/%d bc=%.2f pipe=%.2f res=%.2f'
                % (r['speed_kmh'], r['mc'], r['sc'], r['current_a'], r['main_switch'], r['converter'],
                   r['compressor'], r['battery'], r['panto_front'], r['panto_rear'], r['brake_cyl_bar'],
                   r['pipe_bar'], r['main_res_bar']))

    def step(self):
        """one poll: refresh the state and acknowledge the vigilance device every 30 s once the battery is on"""
        self.alive()
        time.sleep(0.2)
        self.read_state()
        if self.last_ack is not None and self.t() - self.last_ack >= 30:
            self.key('alerteracknowledge')
            self.last_ack = self.t()
            self.mark('ack')

    def wait(self, seconds):
        end = self.t() + seconds
        while self.t() < end:
            self.step()

    def wait_until(self, condition, timeout):
        """waits (simulated seconds) until condition(row) holds; returns True when it did"""
        end = self.t() + timeout
        while self.t() < end:
            self.step()
            if condition(self.row):
                return True
        return False

    # --- the drive ------------------------------------------------------------------------------------
    def run(self):
        self.find_window(600)
        print('[drive] eu07 window %s' % self.window, flush=True)
        while self.read_state() is None:
            self.alive()
            time.sleep(0.5)
        self.focus()

        # 1. in the cab, cold, windows shut
        self.mark('start')
        self.wait(10)

        # 2. battery. The cab signal (SHP) alarms when the battery comes on and MaSzyna takes its
        # acknowledgement, like the ground relay reset the line breaker needs, only with the reverser out of
        # neutral: reverser forward, acknowledge, reset the relays
        self.key('batterytoggle')
        self.mark('battery')
        self.wait(2)
        self.key('reverserincrease')
        self.mark('reverser')
        self.key('alerteracknowledge')
        self.last_ack = self.t()
        self.mark('ack')
        self.key('motoroverloadrelayreset')
        self.mark('relay_reset')
        self.wait(3)

        # 3. both pantographs (impulse switches: the keys are held), they rise in about 6 s
        self.key('pantographtogglefront', seconds=0.5)
        self.key('pantographtogglerear', seconds=0.5)
        self.mark('pantographs')
        if self.wait_until(lambda r: r['panto_front'] and r['panto_rear'], 10):
            self.mark('pantographs_up')
        self.wait(8)

        # 4. line breaker: held past its closing delay, it closes on release
        for attempt in range(3):
            self.hold('linebreakertoggle', 1.5)
            self.mark('main_switch' if attempt == 0 else 'main_switch_retry')
            if self.wait_until(lambda r: r['main_switch'], 5):
                break
            self.key('motoroverloadrelayreset')
        else:
            raise RuntimeError('the line breaker does not close')
        self.wait(5)

        # 5. converter
        self.key('convertertoggle')
        self.mark('converter')
        self.wait(5)

        # 6. compressor, until the main reservoir has 7 bar
        self.key('compressortoggle')
        self.mark('compressor')
        if self.wait_until(lambda r: r['main_res_bar'] >= 7.0, 240):
            self.mark('main_res_7bar')
        else:
            self.mark('main_res_timeout')

        # 7. brakes released (the reverser is forward since step 2). The FV4a does not charge a brake pipe
        # below 2.75 bar unless the releaser is held: held until the pipe has 3 bar
        self.key('trainbrakerelease')
        self.key('independentbrakedecreasefast', 3)
        self.mark('release')
        self.focus()
        self.xdotool('keydown', self.keys['independentbrakebailoff'])
        self.mark('releaser')
        try:
            charged = self.wait_until(lambda r: r['pipe_bar'] >= 3.0, 30)
        finally:
            self.xdotool('keyup', self.keys['independentbrakebailoff'])
        if not charged:
            raise RuntimeError('the brake pipe does not charge')
        if self.wait_until(lambda r: r['brake_cyl_bar'] < 0.2 and r['pipe_bar'] >= 4.8, 90):
            self.mark('brakes_released')

        # 8. master controller one notch every 2 s through series and parallel, then the shunting steps,
        # to 100 km/h
        self.mark('power')
        last = self.t()
        zero_current = 0
        while self.row['speed_kmh'] < 100.0:
            self.step()
            self.check_emergency()
            r = self.row
            if not r['main_switch']:
                # line breaker knocked out: controllers to 0, relays reset, breaker closed again, notch slower
                self.mark('main_switch_tripped')
                self.controllers_to_zero()
                self.key('motoroverloadrelayreset')
                self.hold('linebreakertoggle', 1.5)
                self.wait_until(lambda r: r['main_switch'], 5)
                self.mark('main_switch_reclosed')
                self.notch_period += 2.0
                last = self.t()
                continue
            zero_current = zero_current + 1 if r['mc'] > 0 and r['current_a'] == 0 and r['speed_kmh'] > 1.0 else 0
            if zero_current >= 2:
                # overload relay tripped: controllers to 0, reset, notch slower
                self.mark('overload_tripped')
                self.controllers_to_zero()
                self.key('motoroverloadrelayreset')
                self.mark('overload_reset')
                self.notch_period += 2.0
                zero_current = 0
                last = self.t()
                continue
            if self.t() - last < self.notch_period:
                continue
            last = self.t()
            mc, sc = r['mc'], r['sc']
            self.key('mastercontrollerincrease', seconds=0.2)
            self.wait(1)
            if self.row['mc'] == mc:
                # master controller at its last position: next shunting step
                self.key('secondcontrollerincrease', seconds=0.2)
                self.wait(1)
                if self.row['sc'] == sc:
                    self.mark('notch_none')
        self.mark('100kmh')
        self.controllers_to_zero()
        self.mark('power_off')

        # 9. coast
        self.wait(10)
        self.check_emergency()

        # 10. service brake (FV4a full service: brake pipe 5 -> 3.5 bar) until stopped
        self.key('trainbrakeservice')
        self.mark('brake')
        self.braking = True
        if self.wait_until(lambda r: r['speed_kmh'] < 0.5, 300):
            self.mark('stopped')
        else:
            self.mark('stop_timeout')

        # 11. wait, quit
        self.wait(20)
        self.mark('end')
        self.marks.close()
        self.xdotool('windowquit', self.window, check=False)
        return 0

    def controllers_to_zero(self):
        """shunting steps first, then the master controller, back to 0"""
        for _ in range(30):
            r = self.row
            if r['sc'] > 0:
                self.key('secondcontrollerdecreasefast', seconds=0.2)
            elif r['mc'] > 0:
                self.key('mastercontrollerdecreasefast', seconds=0.2)
            else:
                return
            self.wait(1)
        raise RuntimeError('the controllers do not go back to 0')

    def check_emergency(self):
        """the brake pipe below 3 bar while running means an emergency brake application"""
        if not self.braking and self.row['pipe_bar'] < 3.0:
            self.mark('emergency_brake')
            raise RuntimeError('emergency brake application')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', required=True)
    parser.add_argument('--display', required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--keys', required=True)
    args = parser.parse_args()
    try:
        return Driver(args).run()
    except RuntimeError as error:
        print('[drive] error: %s' % error, file=sys.stderr, flush=True)
        return 1


if __name__ == '__main__':
    sys.exit(main())
