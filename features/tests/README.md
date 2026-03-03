# TSRE Tests

TSRE5vc currently ships a very small built-in test runner intended for regression testing of “pure math” helpers (like `Flex`).

## CLI

- List suites:
  - `TSRE5vc.exe --test-list`
- Run a suite:
  - `TSRE5vc.exe --test --test-suite flex --test-cases features/tests/cases/flex.jsonl`
- Verbose output:
  - `TSRE5vc.exe --test --test-suite flex --test-cases features/tests/cases/flex.jsonl --test-verbose`

Notes:
- Tests run **headless** (`Game::gui = false`) and exit without starting the GUI.
- If `--test-suite` is omitted, it currently defaults to `flex`.
- If `--test-cases` is omitted for `flex`, it defaults to `features/tests/cases/flex.jsonl`.

## Flex captures (from the editor)

To record real editor scenarios into a replayable cases file:

- Enable capture logging:
  - `TSRE5vc.exe --flex-log`
  - (optional) `TSRE5vc.exe --flex-log --flex-log-candidates`
  - (optional) `TSRE5vc.exe --flex-log --flex-log-file features/tests/captures/my-session.jsonl`
- Use AutoFlex in the editor as usual.
- TSRE writes JSONL to `features/tests/captures/` (default filename is timestamped).
- Move/rename the capture file into `features/tests/cases/` to make it a baseline test file.

The Flex JSONL record format is documented in `features/tests/flex-jsonl.md`.

