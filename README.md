![TSRE](http://koniec.org/tsre5/1.png)

# TSRE5
Train Sim game engine and MSTS / OR editors and tools. 

This is my TSRE5 project ported from Qt5 to Qt6 and from Makefile + Netbeans to Cmake + VSCode. 
Netbeans version here:
https://github.com/GokuMK/TSRE5

GitHub release ZIPs include the matching runtime resources in `appdata/0.7/`.
Extract the full ZIP when updating so shaders stay in sync with the executable.
`appdata/` is version-controlled; `assets/` contains local, untracked content and
is not included in releases. Online appdata download remains a fallback if
`appdata/0.7/` is missing. On first startup, procedural examples are downloaded
into `assets/procedural_examples/` if absent; they are not enabled automatically.
See [release packaging](docs/development/releases.md).

See more:

Route Editor: 
https://www.trainsim.com/vbts/showthread.php?323507-New-Route-Editor

Consist Editor: 
https://www.trainsim.com/vbts/showthread.php?324496-New-amazing-Consist-Editor-for-OR-and-MSTS-for-FREE

Official Forum:
http://www.onrails.eu/

Homepage, User Manual and build downloads:
http://koniec.org/tsre5/
