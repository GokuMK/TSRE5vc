# Settings System Phase 0 Catalogue

Master roadmap: [settings-system-plan.md](settings-system-plan.md).

Status: historical Phase 0 record; superseded by the approved Phase 2A audit.

## Scope

The original provisional catalogue was implemented in
`src/settings/DraftSettingsCatalog.cpp`. It was produced from the persisted-key
branches in `Game::load()`, the static defaults in `Game.cpp`, `settings.txt`, and
an initial call-site scan. That executable file has since been updated with the
approved Phase 2A corrections; this document describes how its initial version
was produced rather than the current final mapping.

The legacy parser recognizes 76 distinct keys. The initial draft registry gave
each one:

- A namespaced key.
- A type and initial default.
- A group, display name, and description.
- A best-effort range or enum list where useful.
- The old settings-file key.
- The current C++ symbol.
- A provisional owning subsystem.
- A provisional direct/cached/startup integration classification.
- A flag indicating whether a `Game` member is expected to remain necessary.

The registry is executable catalogue data: Part 1 used it to generate the first
self-describing profile and exercise the Settings Editor. The generated JSON keeps
the legacy and implementation references visible, so the Part 2 review does not
depend on developer memory.

## Important limitations

- Ranges and ownership are preliminary and require call-site review in Phase 2A.
- Some current keys combine startup selection, session state, and preferences;
  Phase 2A may move them out of the preferences profile.
- The Phase 2A review corrected the original network interpretation: `serverAuth`
  is an authentication-mode enum, while the composite `serverLogin` value is the
  temporary secret reference pending a later endpoint/user/password split.
- Runtime-only Flex logging fields are intentionally excluded because they are
  command-line/test controls rather than persisted preferences.
- The catalogue remains centralized during Part 1 to avoid engine wiring. Approved
  definitions move to their owning subsystems during Part 2.

## Audit source

The approved current mapping, including legacy commands removed from profile
generation and native settings added in their place, is documented in
`settings-phase2a-audit.md`. Definitions retain `legacy.fileKeys`,
`legacy.codeSymbols`, and temporary `implementation` metadata where applicable.
