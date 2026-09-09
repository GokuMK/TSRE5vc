TSRE ACE Exporter - GIMP 3, Windows x64

Requires the native 64-bit Windows GIMP installation. Developed and tested with
GIMP 3.2.4. The package uses GIMP's own DLLs; it needs neither Qt nor MSYS2.
The exact SDK used by a CI build is listed in toolchain.txt.

Installation
1. Close GIMP and extract this ZIP.
2. Double-click install.cmd. It detects the profile version from GIMP installed
   in C:\Program Files\GIMP 3 and copies the plug-in into your user profile.
3. Start GIMP, then use File > Export As and choose an .ace filename.

For another GIMP location, run this from PowerShell in the extracted folder:
  .\install.cmd -GimpRoot 'D:\Applications\GIMP 3'

Manual installation: in GIMP's Preferences > Folders > Plug-ins, find the user
plug-in directory. Create a file-tsre-ace folder inside it and copy
file-tsre-ace.exe there. For GIMP 3.2 the usual location is:
  %APPDATA%\GIMP\3.2\plug-ins\file-tsre-ace\file-tsre-ace.exe

To upgrade, close GIMP and install the new file over the old one.
To uninstall, close GIMP and remove that file-tsre-ace folder.
The downloaded folder can be deleted after installation.

This is an ACE export plug-in, not an ACE importer. Linux users should build
from source following extra/gimp-ace/README.md; this EXE is Windows-only.
See BUILD.txt for the version and source. Report problems at:
https://github.com/GokuMK/TSRE5vc/issues
