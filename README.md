![TSRE](http://koniec.org/tsre5/1.png)

# TSRE5
Train Sim game engine and MSTS / OR editors and tools. 

This is my TSRE5 project ported from Qt5 to Qt6 and from Makefile + Netbeans to Cmake + VSCode. 
Netbeans version here:
https://github.com/GokuMK/TSRE5

See more:

Route Editor: 
https://www.trainsim.com/vbts/showthread.php?323507-New-Route-Editor

Consist Editor: 
https://www.trainsim.com/vbts/showthread.php?324496-New-amazing-Consist-Editor-for-OR-and-MSTS-for-FREE

Official Forum:
http://www.onrails.eu/

Homepage, User Manual and build downloads:
http://koniec.org/tsre5/

## Creating a release

Release builds use tags such as `v0.7.6-build.5`. After committing and pushing
the changes on `main`, run:

```powershell
.\scripts\release.cmd
```

For compatibility with the historical version sequence, the `0.7.6` series
starts at build 5. New version series start at build 1. The script increments
the highest matching tag number and pushes an annotated tag. GitHub
Actions then builds the Windows application, prepares the ignored `dist/`
directory, creates a ZIP with the required runtime DLLs, and publishes a GitHub
prerelease. To preview the next version without creating a tag, use
`.\scripts\release.cmd -DryRun`.

