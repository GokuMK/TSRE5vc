"""Reads a Very Sleepy profile (Profile-Cpu.ps1): the time under a root
function, its children, the functions by inclusive and own time; or, with
--callers, who calls a function and what it calls.

  python sleepy-report.py profile.sleepy [--root NAME] [--top N]
  python sleepy-report.py profile.sleepy --callers NAME

Names match by substring. A .sleepy file is a zip: Symbols.txt (address,
module, function) and Callstacks.txt (seconds, then the addresses from the
leaf to the root). Qt's DLLs resolve to their nearest export only, so their
function names mislead; the path through them is right.
"""
import argparse
import collections
import re
import zipfile


def load(path):
    z = zipfile.ZipFile(path)
    symbols = {}
    for line in z.read('Symbols.txt').decode('utf-8', 'replace').splitlines():
        m = re.match(r'(0x[0-9a-f]+) "([^"]*)" "([^"]*)"', line)
        if m:
            symbols[m.group(1)] = (m.group(2), m.group(3))

    def name(address):
        module, function = symbols.get(address, ('?', address))
        return function if module.lower().startswith('tsre5vc') else '%s!%s' % (module, function)

    stacks = []
    for line in z.read('Callstacks.txt').decode('utf-8', 'replace').splitlines():
        parts = line.split()
        if len(parts) >= 2:
            stacks.append((float(parts[0]), [name(a) for a in parts[1:]]))
    return stacks


def find(frames, pattern):
    hits = [i for i, f in enumerate(frames) if pattern in f]
    return hits[-1] if hits else -1


def report(stacks, root, top):
    under = [(t, f) for t, f in stacks if find(f, root) >= 0]
    total = sum(t for t, _ in under)
    print('samples %.2f s, under %s %.2f s' % (sum(t for t, _ in stacks), root, total))
    if total <= 0:
        return
    inclusive, own, children = collections.Counter(), collections.Counter(), collections.Counter()
    for t, frames in under:
        own[frames[0]] += t
        for f in set(frames):
            inclusive[f] += t
        i = find(frames, root)
        if i > 0:
            children[frames[i - 1]] += t
    for title, counter in (('children of the root', children), ('inclusive', inclusive), ('own', own)):
        print('\n-- %s (%% of the root):' % title)
        for f, t in counter.most_common(top):
            print('%6.1f%%  %s' % (100 * t / total, f))


def callers(stacks, pattern, top):
    up, down, paths = collections.Counter(), collections.Counter(), collections.Counter()
    total = 0.0
    for t, frames in stacks:
        i = find(frames, pattern)
        if i < 0:
            continue
        total += t
        if i + 1 < len(frames):
            up[frames[i + 1]] += t
        if i > 0:
            down[frames[i - 1]] += t
        paths[' < '.join(frames[i:i + 6])] += t
    print('%s: %.2f s' % (pattern, total))
    if total <= 0:
        return
    for title, counter, n in (('callers', up, top), ('callees', down, top), ('paths', paths, 6)):
        print('-- %s:' % title)
        for f, t in counter.most_common(n):
            print('%6.1f%%  %s' % (100 * t / total, f))


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('profile')
parser.add_argument('--root', default='RouteEditorGLWidget::paintScene')
parser.add_argument('--callers')
parser.add_argument('--top', type=int, default=30)
args = parser.parse_args()
stacks = load(args.profile)
if args.callers:
    callers(stacks, args.callers, args.top)
else:
    report(stacks, args.root, args.top)
