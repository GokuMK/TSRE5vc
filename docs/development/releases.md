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
executable with its runtime DLLs and Qt plugins, and publishes a GitHub
prerelease.

For compatibility with the historical version sequence, the `0.7.6` series
starts at build 5. New base-version series start at build 1. For example:

```text
v0.7.6-build.5
v0.7.6-build.6
v0.7.7-build.1
```
