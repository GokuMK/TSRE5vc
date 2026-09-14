# Core infrastructure tasks

TSRE implementation plans and follow-ups belong here. MSTS executable research
and historical format audits remain under [docs/msts](../../msts/README.md).

| Document | Purpose / status |
| --- | --- |
| [Case-sensitive filepaths and game-root repair](case-sensitive-filepaths.md) | Stage 1 accepted as the first read-only planner version; stage A runtime path handling is next, followed by repair execution |
| [Case-sensitive filepaths: stage-1 evaluation](case-sensitive-filepaths-stage1.md) | Accepted scope, CLI usage, real-root evaluations, known coverage gaps, and handoff to stage A |
| [Large trainsim scan review](case-sensitive-filepaths-trainsim-review.md) | Latest user-run result: all 226 shared-field conflicts resolved; 161 documented error groups remain |
| [Native token-ID migration](native-token-id-migration.md) | Reviewed design and historical baseline; token mechanism implemented, parser follow-up still outstanding |
| [Native token IDs and binary parser implementation](native-token-ids-and-binary-parser-implementation.md) | Separates token changes, parser behavior, cleanup and verification; records the minimum QuadTree writer correction |
| [Route Editor server/client rework](server-client-rework.md) | Deferred multiplayer redesign issues: update atomicity, buffer ownership, framing, versioning and integration coverage |
| [FileBuffer usage, consumers and TODOs](../../features/file-buffer.md) | Current API, recovery examples, consumer inventory and canonical remaining-work checklist |
| [Token allocation](../../features/native-token-ids.md) | Native IDs, extension assignments and compatibility rules |

The FileBuffer TODO list is a review backlog, not blanket authorization for
parser rewrites. TSRE's recovery-first viewing behavior must be preserved.
