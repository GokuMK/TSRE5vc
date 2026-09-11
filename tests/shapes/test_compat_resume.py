"""Synthetic provenance checks; no application, GL or proprietary assets needed."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from compat_resume import file_hash, prepare_resume


class ResumeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.exe = self.base / 'app'
        self.exe.write_bytes(b'original executable')
        self.source = self.base / 'shape.s'
        self.source.write_bytes(b'original shape')
        self.mirror = self.base / 'mirror.s'
        self.mirror.symlink_to(self.source)
        self.runner = self.base / 'runner.py'
        self.runner.write_bytes(b'original driver')
        self.results = self.base / 'results.jsonl'
        self.rows = [{'path': 'shape.s', 'source': str(self.source), 'mirror': str(self.mirror)}]

    def saved_run(self):
        pending, provenance = prepare_resume(self.rows, self.results, self.exe, self.runner)
        self.assertEqual(pending, self.rows)
        result = dict(provenance, path='shape.s', sha256=file_hash(self.source))
        self.results.write_text(json.dumps(result) + '\n')

    def test_unchanged_run_skips_completed(self):
        self.saved_run()
        pending, _ = prepare_resume(self.rows, self.results, self.exe, self.runner)
        self.assertEqual(pending, [])

    def test_changed_executable_rejected(self):
        self.saved_run()
        self.exe.write_bytes(b'changed executable')
        with self.assertRaisesRegex(SystemExit, 'provenance'):
            prepare_resume(self.rows, self.results, self.exe, self.runner)

    def test_changed_input_rejected_even_with_same_size_and_timestamp(self):
        import os
        self.saved_run()
        stat = self.source.stat()
        self.source.write_bytes(b'modified shape')
        os.utime(self.source, ns=(stat.st_atime_ns, stat.st_mtime_ns))
        with self.assertRaisesRegex(SystemExit, 'Input changed'):
            prepare_resume(self.rows, self.results, self.exe, self.runner)

    def test_changed_runner_rejected(self):
        self.saved_run()
        self.runner.write_bytes(b'changed driver')
        with self.assertRaisesRegex(SystemExit, 'provenance'):
            prepare_resume(self.rows, self.results, self.exe, self.runner)

    def test_mismatched_mirror_rejected(self):
        self.mirror.unlink()
        self.mirror.write_bytes(b'wrong shape')
        with self.assertRaisesRegex(SystemExit, 'Source/mirror mismatch'):
            prepare_resume(self.rows, self.results, self.exe, self.runner)

    def test_truncated_results_rejected(self):
        self.results.write_text('{"path":')
        with self.assertRaisesRegex(SystemExit, 'Invalid result'):
            prepare_resume(self.rows, self.results, self.exe, self.runner)

    def test_both_drivers_reject_old_results_without_touching_them(self):
        (self.base / 'manifest.json').write_text(json.dumps(self.rows))
        output = self.base / 'saved'
        output.mkdir()
        results = output / 'results.jsonl'
        original = json.dumps({'path': 'shape.s', 'sha256': file_hash(self.source)}) + '\n'
        results.write_text(original)
        for name in ('legacy_compat_compare.py', 'three_compat_compare.py'):
            with self.subTest(driver=name):
                run = subprocess.run([sys.executable, str(Path(__file__).with_name(name)),
                                      '--work', str(self.base), '--exe', str(self.exe),
                                      '--output', 'saved'], capture_output=True, text=True)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn('provenance', run.stderr)
                self.assertEqual(results.read_text(), original)
                self.assertEqual(list(output.iterdir()), [results])


if __name__ == '__main__':
    unittest.main()
