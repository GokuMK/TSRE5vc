"""Capture real TSRE saves and compare structured values, ignoring whitespace.

Run with the Qt/MinGW runtime environment configured. Outputs must be a new
directory. Original content is read-only; SHA-256 hashes are verified afterward.
No tolerance silently hides differences: decimal, float32 and structural changes
are reported separately. This is an audit, not a pass/fail fidelity assertion.
"""
import argparse
from collections import Counter, defaultdict
from decimal import Decimal, InvalidOperation
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse(path):
    data = path.read_bytes()
    text = data.decode('utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()"]+', text)
    assert tokens.pop(0).startswith('SIMISA'), path
    pos = 0

    def body(nested=False):
        nonlocal pos
        atoms, children = [], []
        while pos < len(tokens):
            token = tokens[pos]
            pos += 1
            if token == ')':
                assert nested, path
                return atoms, children
            if pos < len(tokens) and tokens[pos] == '(':
                pos += 1
                children.append((token.lower(), body(True)))
            else:
                assert token != '(', path
                atoms.append(token[1:-1] if token.startswith('"') else token)
        assert not nested, path
        return atoms, children

    tree = body()
    flat = {}

    def flatten(node, prefix):
        atoms, children = node
        flat[prefix] = atoms
        occurrences = Counter()
        for tag, child in children:
            occurrences[tag] += 1
            key = str(occurrences[tag])
            if tag in ('tracknode', 'tracksection', 'trackpath') and child[0]:
                key = 'id=' + child[0][0]
            elif tag.endswith('item'):
                ids = [n[0][0] for name, n in child[1] if name == 'tritemid']
                if ids:
                    key = 'id=' + ids[0]
            flatten(child, prefix + '/' + tag + '[' + key + ']')
    flatten(tree, '')
    return flat


def f32(value):
    return struct.pack('<f', float(value))


def compare(before, after):
    a, b = parse(before), parse(after)
    counts, groups = Counter(), defaultdict(Counter)
    precision6 = Counter()
    precision6_examples = []
    reordered_references = []
    examples = defaultdict(list)
    max_absolute = defaultdict(float)

    def record(kind, path, old, new, index=None):
        counts[kind] += 1
        field = path.rsplit('/', 1)[-1].split('[')[0]
        if field == 'trvectorsections' and index is not None and index > 0:
            field += '.param[' + str((index - 1) % 16) + ']'
        elif index is not None:
            field += '[' + str(index) + ']'
        groups[kind][field] += 1
        bucket = kind + ':' + field
        if len(examples[bucket]) < 4:
            examples[bucket].append(dict(path=path, index=index, before=old, after=new))

    for path in sorted(a.keys() | b.keys()):
        if path not in a or path not in b:
            record('added_block' if path not in a else 'removed_block', path, a.get(path), b.get(path))
            continue
        aa, bb = a[path], b[path]
        if len(aa) != len(bb):
            record('atom_count', path, len(aa), len(bb))
        for i, (x, y) in enumerate(zip(aa, bb)):
            if x == y:
                counts['identical_atoms'] += 1
                continue
            try:
                dx, dy = Decimal(x), Decimal(y)
                if not (dx.is_finite() and dy.is_finite()):
                    raise InvalidOperation
            except InvalidOperation:
                record('text', path, x, y, i)
                continue
            if dx == dy:
                counts['numeric_spelling_only'] += 1
                continue
            kind = 'same_float32' if f32(x) == f32(y) else 'changed_float32'
            record(kind, path, x, y, i)
            if '/tritemref[' not in path:
                same = format(float(x), '.6g') == format(float(y), '.6g')
                precision6['same' if same else 'different'] += 1
                if not same and len(precision6_examples) < 12:
                    precision6_examples.append(dict(path=path, index=i, before=x, after=y))
            field = path.rsplit('/', 1)[-1].split('[')[0]
            max_absolute[kind + ':' + field] = max(max_absolute[kind + ':' + field], float(abs(dx - dy)))
    def references(flat):
        result = defaultdict(list)
        for path, values in flat.items():
            if '/tritemref[' in path:
                result[path.rsplit('/', 1)[0]].append(tuple(values))
        return result
    old_refs, new_refs = references(a), references(b)
    for path in old_refs.keys() & new_refs.keys():
        old, new = old_refs[path], new_refs[path]
        if old != new and Counter(old) == Counter(new):
            reordered_references.append(path)
    return dict(counts=counts, fields=groups, examples=examples, max_absolute=max_absolute,
                changed_numbers_at_six_significant_digits=precision6,
                six_significant_digit_examples=precision6_examples,
                reordered_reference_blocks=sorted(reordered_references))


def copy_metadata(source, target):
    target.mkdir(parents=True, exist_ok=True)
    for file in source.iterdir():
        if file.is_file() and file.suffix.lower() in ('.dat', '.trk') and not (target / file.name).exists():
            shutil.copy2(file, target / file.name)
    if (source / 'OPENRAILS').is_dir():
        shutil.copytree(source / 'OPENRAILS', target / 'OPENRAILS', dirs_exist_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.exe, args.root, args.output = (p.resolve() for p in (args.exe, args.root, args.output))
    args.output.mkdir(parents=True, exist_ok=False)
    routes = sorted(p for p in (args.root / 'ROUTES').iterdir() if p.is_dir() and list(p.glob('*.tdb')))
    originals = [f for route in routes for f in route.iterdir() if f.is_file() and f.suffix.lower() in ('.tdb', '.tit', '.dat')]
    originals.extend((args.root / 'GLOBAL').glob('*.dat'))
    hashes = {str(f): digest(f) for f in originals}
    (args.output / 'source-hashes.json').write_text(json.dumps(hashes, indent=2))
    report = dict(root=str(args.root), routes={})
    for route in routes:
        entry = report['routes'][route.name] = {}
        for pass_number in (1, 2):
            destination = args.output / route.name / ('pass' + str(pass_number))
            destination.parent.mkdir(parents=True, exist_ok=True)
            source = args.root if pass_number == 1 else destination.parent / 'pass1' / 'saved'
            if pass_number == 2:
                copy_metadata(args.root / 'GLOBAL', source / 'GLOBAL')
                copy_metadata(route, source / 'ROUTES' / route.name)
            with (destination.parent / ('pass' + str(pass_number) + '.log')).open('w') as log:
                result = subprocess.run([str(args.exe), '--test', '--test-suite', 'tdb-roundtrip',
                    '--game-root', str(source), '--route', route.name, '--test-cases', str(destination)],
                    cwd=args.exe.parent.parent, stdout=log, stderr=subprocess.STDOUT, timeout=120)
            if result.returncode:
                raise RuntimeError(f'{route.name}: pass {pass_number} exited {result.returncode}')
        tdb = next(route.glob('*.tdb')).name
        for filename in (tdb, 'tsection.dat'):
            if not (route / filename).exists():
                entry[filename] = {'source_missing': True}
                continue
            raw = args.output / route.name / 'pass1' / 'raw' / 'ROUTES' / route.name / filename
            precise = args.output / route.name / 'pass1' / 'float32' / 'ROUTES' / route.name / filename
            saved = args.output / route.name / 'pass1' / 'saved' / 'ROUTES' / route.name / filename
            second = args.output / route.name / 'pass2' / 'saved' / 'ROUTES' / route.name / filename
            entry[filename] = {
                'source_to_float32': compare(route / filename, precise),
                'float32_to_raw': compare(precise, raw),
                'source_to_raw': compare(route / filename, raw),
                'raw_to_save': compare(raw, saved),
                'source_to_save': compare(route / filename, saved),
                'save_to_second_save': compare(saved, second),
            }
        print(route.name, {name: data.get('source_to_save', {}).get('counts', {}) for name, data in entry.items()}, flush=True)
        (args.output / 'report.json').write_text(json.dumps(report, indent=2))
    report['source_hashes_unchanged'] = all(digest(Path(f)) == h for f, h in hashes.items())
    (args.output / 'report.json').write_text(json.dumps(report, indent=2))
    assert report['source_hashes_unchanged'], 'Source files changed during audit'


if __name__ == '__main__':
    main()
