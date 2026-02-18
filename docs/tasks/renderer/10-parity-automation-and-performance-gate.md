# Task 10 - Parity Automation And Performance Gate

## Objective
Create repeatable validation to decide when legacy pipeline can be retired.

## Scope
- Deterministic A/B capture workflow (legacy vs gather).
- Image diff reporting (color and optional depth).
- Picking parity sampling report.
- Render counters by category and pass.
- Performance summary (draw calls, frame time, state-change proxies).

## Suggested Touch Points
- Add scripts/tooling under a new tools folder or build scripts
- Optional logging hooks in renderer and gather traversal
- `docs` instructions for running parity checks

## Requirements
- Validation can be run by AI agents and humans.
- Artifacts are easy to compare in PR reviews.

## Acceptance Criteria
- A small representative route set passes parity thresholds.
- Selection/picking parity has no critical mismatches.
- Gather mode has no major performance regression vs legacy on test set.
- Decision note recorded: keep legacy fallback or proceed with deprecation.

## Out Of Scope
- Removing legacy path without passing all gates.
