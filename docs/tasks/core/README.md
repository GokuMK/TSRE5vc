# Core infrastructure tasks

TSRE implementation plans and follow-ups belong here. MSTS executable research
and historical format audits remain under [docs/msts](../../msts/README.md).

| Document | Purpose / status |
| --- | --- |
| [Native token-ID migration](native-token-id-migration.md) | Reviewed design and historical baseline; token mechanism implemented, parser follow-up still outstanding |
| [Native token IDs and binary parser implementation](native-token-ids-and-binary-parser-implementation.md) | Separates token changes, parser behavior, cleanup and verification; records the minimum QuadTree writer correction |
| [FileBuffer usage, consumers and TODOs](../../features/file-buffer.md) | Current API, recovery examples, consumer inventory and canonical remaining-work checklist |
| [Token allocation](../../features/native-token-ids.md) | Native IDs, extension assignments and compatibility rules |

The FileBuffer TODO list is a review backlog, not blanket authorization for
parser rewrites. TSRE's recovery-first viewing behavior must be preserved.
