# Task 09 - Seasonal Shape Textures (open)

## Scope

Small fixes to how MSTS shapes choose their seasonal texture directory from
the `.sd` `ESD_Alternative_Texture` flags. Not a seasonal rework: terrain,
transfers and the season setting stay as they are.

## Current rule (2026-10-08)

`TerrainSeason::shapeTextureDirectory(flags, season)`, used by
`SFileLegacy` and `SFileComplex`:

- Clear and rain seasons are their season: SpringClear and SpringRain use
  SPRING, AutumnClear and AutumnRain AUTUMN, WinterClear and WinterRain
  WINTER, Summer the main directory. Terrain falls back from rain the same
  way.
- The snow seasons use their own directory (SPRINGSNOW, AUTUMNSNOW,
  WINTERSNOW) for shapes with that flag.
- Winter, WinterRain and every snow season use SNOW for shapes with the
  Snow or SnowTrack flag, overriding the above.
- An `.sd` without the tag, or no `.sd`, means no alternative textures.

Fixed so far: clear seasons never matched (settings store `WinterClear`
and so on); `SFileLegacy` tested the Snow bit for SnowTrack, left out
SummerSnow and treated a missing tag as every flag set; rain seasons used
the main directory.

## Open

The rule is what TSRE did, made consistent; it is not yet checked against
Open Rails, whose behaviour route authors target. To compare with the ORTS
source and decide case by case:

- Clear and rain Winter with the Snow or SnowTrack flag: SNOW, or WINTER?
- Snow seasons: their own directory or SNOW, and which wins when both
  flags are set.
- A flagged shape whose seasonal file is missing: TSRE has no fallback to
  the main directory (the texture is missing); terrain falls back with a
  warning.
- The Night (0x100) and Underground flags are ignored.
- The season setting's description says rain textures for shapes are not
  implemented; reword once the rule is settled.
