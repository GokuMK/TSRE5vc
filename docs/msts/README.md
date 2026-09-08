# MSTS research reports mirrored into TSRE

Last synchronization: 2026-09-08, from the separate MSTS reverse-engineering
workspace. These reports are included here so another TSRE checkout can read
the findings without access to that workspace.

| Report | Scope |
| --- | --- |
| [Terrain T-file fields](tsre-msts-terrain-tfile-field-usage.md) | Token/field layout, MSTS consumers, dated TSRE audit, shader pairing and multiple-set/water follow-ups |
| [Terrain profile compatibility](msts-orts-terrain-profile-compatibility.md) | Samples/patches, stock and patched MSTS, pinned ORTS master/unstable geometry and water limitations |
| [Multiple patch sets and water](msts-orts-multiple-patchsets-and-water.md) | MSTS last-set versus ORTS first-set selection, water predicate/history and proposed tests |
| [Shader pairing and procedural fallback](msts-terrain-shader-pairing-and-procedural-fallback.md) | Ordinary paired versus distant flat material indexing; historical procedural design proposals |
| [Adaptive LOD findings](msts-terrain-adaptive-lod-analysis.md) | Static E/AS/error-bias analysis and original terrain limits |
| [Custom terrain-grid diagnosis](msts-custom-terrain-grid-compatibility.md) | Historical rejection analysis before the later terrain patches |
| [Adaptive LOD investigation brief](msts-terrain-adaptive-lod-executable-analysis.md) | Original exploratory task, not a current implementation checklist |
| [ACE format and legacy TSRE audit](tsre-msts-ace-file-field-usage.md) | Detailed field/record layout and MSTS executable evidence; TSRE comparisons describe legacy AceLib, not the new implementation |
| [ACE implementation and MSRE tests](tsre-ace-v2-implementation-and-msre-tests.md) | New AceLib/Texture integration, tests, old/new timings and live encodings/resolutions through 4096 |

## Scope and provenance

The source counterparts are `reports/<same filename>` in the MSTS workspace,
except the investigation brief, which is at that workspace's root. The original
analysis dates, executable identities, pinned ORTS revisions and distinction
between static predictions and runtime observations remain in the reports.
Synchronization is not a new executable audit or a review of today's ORTS branches.

The five existing terrain documents were compared with their source counterparts.
The T-file and profile reports received the newer shader/patch-set/water findings.
The adaptive-LOD findings and original grid diagnosis already matched in substance;
their repository-local references were retained. The investigation brief retains
its newer TSRE task links and clarifies the 60-byte fields versus 61-byte labeled
block payload. New focused terrain reports accompany the new cross-references.

These are adapted mirrors, not blind replacements: repository-local navigation,
TSRE's experimental N2048 notes and the local Bin 1.9/R64 patcher description are
preserved. The current terrain-patch envelope is `N<=1024`, `P<=32`, `R<=64`;
older R16/R32
limits in dated investigations describe the original executable or an earlier
experimental build, not the latest patch. Use the profile report for the
current envelope and its runtime-test qualifications.

Evidence references under `analysis/`, `scripts/`, `test_data/`, `proprietary/`
or Wine `logs/` belong to the separate MSTS research workspace unless explicitly
linked into this repository. They are not bundled here. No proprietary executable,
route asset or screenshot was copied as part of this documentation sync.

For current TSRE behavior rather than a historical proposal/source audit, see
the [terrain task index](../tasks/terrain/README.md) and
[ACE API/integration guide](../features/ace-library.md). The patcher shipped with
TSRE is documented in [extra/MSTS/bin-1.9](../../extra/MSTS/bin-1.9/README.md).

Windows-host access and execution still require explicit user approval. A report
describing a proposed test does not itself authorize that test.
