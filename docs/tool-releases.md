# ACE tool releases

The two standalone tools have independent releases in this repository:

| Tool | Tag example | Download |
| --- | --- | --- |
| Explorer thumbnails | `ace-thumbnails-v1.0.0` | `ace-thumbnails-1.0.0-windows-x64.zip` |
| GIMP ACE exporter | `gimp-ace-v1.0.0` | `gimp-ace-1.0.0-windows-x64.zip` |

These tags do not trigger the TSRE workflow (`v*-build.*`). All tool releases
are published as GitHub **prereleases**, including plain versions such as
`1.0.0`, to exclude them from the repository's **Latest** slot. The downloads
remain public. Workflows also set `--latest=false`, but that flag alone does
not prevent GitHub selecting a tool when no stable TSRE release is designated
**Latest**.
Each ZIP includes the production binary, installation instructions/helpers,
source commit and link, and third-party notices. Tests and SDK DLLs are excluded.
A matching `.sha256` asset gives the ZIP checksum.

`THIRD-PARTY-NOTICES.txt` consolidates miniz and build-environment notices into
one file, preserving the original terms and component/file names. Identical
texts are included once with all their attributions. The SDK inventory also
contains build tools and dependencies not shipped in the ZIP; it is retained
for notice coverage, not as a statement that every listed component is linked.
This does not relicense third-party code or change TSRE's license.

The first binary packages target Windows x64. The GIMP source continues to
support native Linux builds through its standalone CMake project. A Windows EXE
is not a Linux package; build against the GIMP SDK installed on that system.

## Test the workflows without releasing

After these files reach GitHub's default branch, open **Actions**, select
**ACE Thumbnails release** or **GIMP ACE Exporter release**, and choose
**Run workflow** on `main`. Download the resulting artifact after it passes.
Manual runs build and test but never publish, even when a tag is selected.
Branch runs use a `0.0.0-dev.<run-number>` package version.

The thumbnail workflow uses MSYS2 UCRT64 GCC and verifies that the DLL imports
only Windows system libraries. The GIMP workflow uses MSYS2 CLANG64 and runs the
export tests plus the production plug-in inside MSYS2's GIMP. Package versions
are recorded in `toolchain.txt`. This CI test does not establish compatibility
with every official GIMP build: test the downloaded EXE with the intended
official Windows GIMP before tagging a release. Initial development was
tested with official GIMP 3.2.4.

## Publish from main

Commit and push the workflow changes first. From a clean, up-to-date `main`,
tag the reviewed commit and push only the desired tool's tag. For example:

```powershell
git switch main
git pull --ff-only
git tag -a ace-thumbnails-v1.0.0 -m "ACE Thumbnails 1.0.0: initial standalone release"
git push origin ace-thumbnails-v1.0.0
```

For the exporter, use `gimp-ace-v1.0.0` instead. Versions are independent;
releasing one tool does not require releasing the other or TSRE.
Use `major.minor.patch`, optionally with a suffix such as `-rc.1`; every version
uses GitHub's prerelease setting. The annotated tag message becomes the release
notes, so include the tool's changes and relevant compatibility notes there.

A tag push builds, tests, packages, and publishes automatically using
`GITHUB_TOKEN`; no personal token is needed. The publish job alone has
repository write permission. GitHub CLI uploads the assets before publishing.
If publication fails after creating a draft, inspect that draft before retrying;
the workflow does not overwrite an existing release. For a published correction,
use a new version rather than moving the existing tag.

## Package a local build

Build only the desired standalone project; a TSRE build is not needed:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File extra/ace-thumbnails/build.ps1 -Test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File extra/common/release/package.ps1 -Tool ace-thumbnails -Version 1.0.0

powershell.exe -NoProfile -ExecutionPolicy Bypass -File extra/gimp-ace/build.ps1 -Test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File extra/common/release/package.ps1 -Tool gimp-ace -Version 1.0.0
```

Output is under ignored `dist/<tool>/`. The script accepts `-BuildDirectory`
and `-OutputDirectory` and refuses to overwrite an existing ZIP. Use
`-RuntimeLicenseDirectory <toolchain>/share/licenses` to include the runtime
notices when distributing a local build; CI does this automatically.
Local source links refer to HEAD, so commit the sources before distributing.
The packaging script does not rebuild or install either tool.

End users should download the named Windows ZIP under the release's **Assets**.
GitHub's automatic **Source code** archives are for building, not installation.
Verify a download with `Get-FileHash <zip> -Algorithm SHA256` and compare it
with the accompanying `.sha256` file.
