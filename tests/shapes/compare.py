#!/usr/bin/env python3
"""Run one process per private stock shape; isolate legacy leaks/crashes.

Use under xvfb-run for GL checks. Outputs stay at the explicitly supplied path.
No proprietary asset bytes are copied into the repository.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--exe', required=True, type=pathlib.Path)
parser.add_argument('--corpus', required=True, type=pathlib.Path)
parser.add_argument('--output', required=True, type=pathlib.Path)
parser.add_argument('--gl', action='store_true')
args = parser.parse_args()
paths = sorted(p for p in args.corpus.rglob('*') if p.suffix.lower() == '.s' and p.is_file())
args.output.mkdir(parents=True, exist_ok=True)
failed = 0
with (args.output / 'results.jsonl').open('w') as output:
    for index, path in enumerate(paths):
        started = time.monotonic()
        command = [str(args.exe.resolve()), '--test', '--test-suite',
                   'shape-complex-corpus-gl' if args.gl else 'shape-complex-corpus',
                   '--test-cases', str(path.resolve())]
        result = {'path': str(path.relative_to(args.corpus)),
                  'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
        try:
            run = subprocess.run(command, capture_output=True, text=True, timeout=90)
            log = run.stdout + run.stderr
            result['exit_code'] = run.returncode
            for line in log.splitlines():
                start = line.find('{')
                if start >= 0 and '"path"' in line[start:]:
                    row = json.loads(line[start:])
                    row.pop('path', None)
                    result.update(row)
            (args.output / f'{index:03d}.log').write_text(log)
        except subprocess.TimeoutExpired:
            result['timeout'] = True
        failed += int(result.get('exit_code', 1) != 0)
        result['wall_seconds'] = time.monotonic() - started
        output.write(json.dumps(result) + '\n')
        output.flush()
        print(f'{index + 1}/{len(paths)} {result["path"]}: {result.get("exit_code", "timeout")}', flush=True)

raise SystemExit(1 if failed else 0)
