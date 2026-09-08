# Creating a release

Releases are created from a clean `main` branch that exactly matches
`origin/main`. Preview the next release tag without changing the repository:

```powershell
.\scripts\release.cmd -DryRun
```

Create the release by running:

```powershell
.\scripts\release.cmd
```

The script fetches the remote tags, increments the highest build number for the
current base version from `VERSION`, creates an annotated tag, and pushes it to
GitHub. The tag triggers `.github/workflows/release.yml`, which builds the
Windows application, prepares an ignored `dist/` directory, packages the
executable with its runtime DLLs, Qt plugins and the complete tracked
`appdata/0.7/` directory, and publishes a GitHub prerelease. Shaders, brush images
and UI images therefore come from the same tagged revision as the executable;
normal release startup does not need to download appdata.

## Runtime resources versus local assets

- `appdata/0.7/` is Git-tracked and included in the release ZIP. Its global
  `procedural/` directory is empty apart from the Git directory placeholder.
- `assets/` is ignored and is not packaged. The former global procedural sample
  files were moved locally to `assets/procedural_examples/`. Startup downloads
  `https://koniec.org/tsre5/data/appdata/procedural_examples.tar` if that directory
  is missing, including on the first run of a bundled release or fresh clone.
  Existing examples are never overwritten or automatically updated. Downloading
  does not activate them: global `appdata/0.7/procedural/` stays empty. Failure
  logs a warning, continues startup and retries on a later run if still missing.
- Existing installations can keep `appdata/0.697/`, but current builds use only
  `appdata/0.7/`. Extract the complete release ZIP when updating, not just the EXE.
- If `appdata/0.7/` is missing, startup retains the online fallback at
  `https://koniec.org/tsre5/data/appdata/0.7.tar`. The server archive must contain
  the `0.7/` directory and matching resources, with no procedural examples.
  Publishing that archive is separate from GitHub release creation. HTTP/network
  failures do not create an empty version directory that would suppress retry.
- Both downloads use temporary staging directories beneath their destination
  parent and install by directory rename only after successful extraction. The
  confined resource extractor validates checksums, sizes, paths and writes;
  only regular files/directories beneath the expected archive root are allowed.
  Partial/invalid downloads do not become installed directories. The legacy TAR
  extraction path for other consumers is unchanged.
- Both bundled and downloaded installations create missing Consist Editor and
  Shape Viewer launchers; existing launcher files are left unchanged.

Release packaging checks that the tracked shader directory exists and that no
procedural examples were accidentally included. Assets are never copied into
the distribution. The legacy downloader remains a recovery path, not an update
mechanism for an existing appdata directory.

Verification (2026-09-08): all 83 moved files were hash-verified unchanged;
34 appdata files (including the empty-directory marker) are tracked, and the
50 procedural example files remain ignored. Local ZIP inspection found the
new shader/brush paths and no assets, examples or old-version directory. Release
build and the procedural OpenGL suite passed. Isolated startup with bundled
appdata created both launchers without downloading; startup with the version
directory missing requested `0.7.tar` and left the directory absent after failure.
At initial testing the archive returned 404. The user subsequently uploaded both
`0.7.tar` and `procedural_examples.tar`; HTTPS requests now return 200 and their
top-level directories match the installer. No GitHub release or server upload
was performed by the agent as part of these packaging/download changes.

Download follow-up verification: isolated first startup downloaded all 50
procedural example files and matched their hashes to the originals; a second
startup made no resource request and preserved file modification times. Removing
only the isolated `appdata/0.7/` triggered successful online recovery; all 34
recovered files matched the tracked bundle, with no global procedural content.
The installation rename uses absolute source/destination paths. Eleven confined
archive checks passed (valid archive, truncation, checksum corruption, wrong
root, absolute/traversal/Windows-normalized paths, links and oversized payload).
Logs: `build/resource-download-first.log`, `build/resource-download-appdata.log`;
local extraction probe: `build/resource-archive-check.cpp`.

For compatibility with the historical version sequence, the `0.7.6` series
starts at build 5. New base-version series start at build 1. For example:

```text
v0.7.6-build.5
v0.7.6-build.6
v0.7.7-build.1
```
