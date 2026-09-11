"""Validate corpus provenance before appending to an existing comparison run."""
import hashlib
import json


def file_hash(path):
    with open(path, 'rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def prepare_resume(rows, resultfile, executable, runner):
    provenance = {
        'executable_sha256': file_hash(executable),
        'runner_sha256': file_hash(runner),
        'resume_helper_sha256': file_hash(__file__),
    }
    completed = {}
    if resultfile.exists():
        for number, line in enumerate(resultfile.read_text().splitlines(), 1):
            try:
                result = json.loads(line)
                path = result['path']
            except (ValueError, KeyError, TypeError):
                raise SystemExit(f'Invalid result at {resultfile}:{number}; use a new --output directory.')
            if any(result.get(key) != value for key, value in provenance.items()):
                raise SystemExit('Result provenance is missing or differs from this executable/runner; '
                                 'use a new --output directory.')
            completed[path] = result
    pending = []
    for row in rows:
        digest = file_hash(row['source'])
        if file_hash(row['mirror']) != digest:
            raise SystemExit(f"Source/mirror mismatch: {row['path']}; rebuild the inventory.")
        previous = completed.get(row['path'])
        if previous is not None:
            if previous.get('sha256') != digest:
                raise SystemExit(f"Input changed: {row['path']}; use a new --output directory.")
        else:
            pending.append(row)
    return pending, provenance
