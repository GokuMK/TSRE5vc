# TSRE Tests

TSRE5vc currently ships a small built-in test runner. The `flex-point` suite
contains self-contained automated Flex regression tests. The `flex` suite
replays editor captures and is optional because its input is prepared
separately in the 3D editor.

## CLI

- List suites:
  - `TSRE5vc.exe --test-list`
- Run the default self-contained Flex tests:
  - `TSRE5vc.exe --test`
  - This defaults to `flex-point`.
- Run a captured Flex replay:
  - `TSRE5vc.exe --test --test-suite flex --test-cases features/tests/cases/flex.jsonl`
- Verbose output:
  - `TSRE5vc.exe --test --test-suite flex-point --test-verbose`

Notes:

- Tests run **headless** (`Game::gui = false`) and exit without starting the GUI.
- If `--test-suite` is omitted, it defaults to `flex-point`.
- If `--test-cases` is omitted for `flex`, it defaults to
  `features/tests/cases/flex.jsonl`.
- If that implicit captured baseline does not exist, `flex` is skipped and
  returns success. A missing path supplied explicitly with `--test-cases`
  remains an error.
- An existing replay file with no valid `flex_case` records remains an error.

## Flex captures (from the editor)

To record real editor scenarios into a replayable cases file:

- Enable capture logging:
  - `TSRE5vc.exe --flex-log`
  - (optional) `TSRE5vc.exe --flex-log --flex-log-candidates`
  - (optional) `TSRE5vc.exe --flex-log --flex-log-file features/tests/captures/my-session.jsonl`
- Use AutoFlex in the editor as usual.
- TSRE writes JSONL to `features/tests/captures/` (default filename is timestamped).
- Move/rename the capture file into `features/tests/cases/` to make it a baseline test file.
- Run the `flex` suite only when capture-replay coverage is wanted. Normal
  automated Flex verification uses `flex-point` and requires no capture file.

The Flex JSONL record format is documented in `features/tests/flex-jsonl.md`.
