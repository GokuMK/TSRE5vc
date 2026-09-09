ACE Explorer Thumbnails - Windows x64

Installation
1. Extract this entire ZIP to a permanent folder in your user account.
2. Double-click install.cmd. Administrator rights are not required.
3. Open a folder containing ACE textures in Explorer with large icons enabled.
   Restart Explorer or sign out and back in if the handler is not picked up.

Keep the DLL in that folder while installed. To remove it, run uninstall.cmd
before deleting the folder. To upgrade, uninstall the old copy first; Explorer
may hold the DLL open until Explorer is restarted or you sign out.

Registration affects only your Windows account, preserves the default ACE
application and restores the previous thumbnail handler when uninstalled.
No Qt, MSYS2 or compiler runtime installation is required.
Textures up to 8192 x 8192 are supported. Explorer's existing cached thumbnails
can persist after an upgrade.

Manual installation: %SystemRoot%\System32\regsvr32.exe "full-path\AceThumbnails.dll"
Manual removal:      %SystemRoot%\System32\regsvr32.exe /u "full-path\AceThumbnails.dll"

See BUILD.txt for the exact version and source. Report problems at:
https://github.com/GokuMK/TSRE5vc/issues
