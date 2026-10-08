# OpenStreetMap data from local files

TSRE can read OpenStreetMap (OSM) data from `.osm.pbf` files on your disk
instead of downloading it from the OSM servers. Local data covers large areas
quickly and does not depend on the network. It feeds the terrain tile map
today and is meant to feed procedural generation later.

## Setting up

1. **Download one or more extracts.** For example, take the per-province files
   from [Geofabrik](https://download.geofabrik.de/europe/poland.html):
   `pomorskie-latest.osm.pbf` and so on. A whole country works too (Poland is
   2.1 GB), but regional files convert faster and need much less memory.
2. **Put the files in one directory.** Only files directly in that directory
   are read; subdirectories are ignored. To use a different set of data,
   point TSRE at a different directory.
3. **Set the directory** in **Settings > Settings Editor > Maps and geodata >
   Geodata > OpenStreetMap data directory** (`core.paths.osmData`).

## First use: conversion

The downloaded files are ordered by OSM id, not by place, so TSRE converts
each file once into a spatially sorted copy, `<name>.tsre.osm.pbf`, next to
it. The copy is still a standard PBF file that other OSM tools can open. It
keeps:

- every tagged node, way and relation with all its tags;
- the road and rail network topology;
- coordinates stored on the ways.

Untagged nodes and edit metadata are dropped.

The first time a tile needs an area covered by a file that has not been
converted yet, TSRE asks before converting. The question lists the files and
the memory and disk space the conversion needs, compared with what is free.
Conversion then runs in the background with a progress dialog, and you can
cancel it. If you choose **Not now**, TSRE does not ask about that file again
until you restart it, and the tile map uses the OSM servers for that area.

Measured on a 6-core desktop:

| File | Conversion | Memory | Temporary disk | Result |
|---|---:|---:|---:|---:|
| One province (pomorskie, 118 MB) | 2–3 s | about 0.4 GB | about 0.35 GB | about 130 MB |
| Poland (2.1 GB) | 25–35 s (about 50 s on 4 cores) | about 4.7 GB | about 6.6 GB | 2.4 GB |

Memory and temporary disk space are freed when the conversion ends.

### Keeping or deleting the download

**Settings > Maps and geodata > Geodata > Downloaded OSM file after conversion**
(`geo.osm.originalAfterConversion`) chooses what happens to the download:

- **Keep both files** (default).
- **Delete the download.** The converted copy holds the same data, so this
  saves about half of the space.

The conversion question also has a checkbox for this choice.

### Updating

Download a newer file into the same directory under its usual name. TSRE
records which download each converted copy came from (name, size and date),
recognises that the download changed, and offers to convert it again.

### Files TSRE creates

| File | Purpose |
|---|---|
| `<name>.tsre.osm.pbf` | The converted, spatially sorted copy. |
| `<name>.tsre.osm.pbf.idx` | A small index cache. TSRE recreates it if you delete it. |

During a conversion, `<name>.tsre.osm.pbf.part` and a
`<name>.tsre.osm.pbf.tmp/` directory exist next to the file. They are removed
when the conversion finishes or is cancelled.

### Overlapping files

Files may overlap; for example, Poland and its provinces in one directory. TSRE
uses all converted files and reports each OSM object once, taking it from the
newest file. Keeping one file per area saves disk space.

## Terrain tile map

The **Map** window of a terrain tile draws from local data when a converted
file covers the tile, and from the OSM servers otherwise. Local data also
draws areas mapped as multipolygons, such as lakes, forests and parks, which
the server path does not.

A 2 km tile loads and draws in about 0.1–0.25 s, against several seconds when
downloaded.

## Licence and attribution

OpenStreetMap data is © OpenStreetMap contributors and available under the
[Open Database License](https://www.openstreetmap.org/copyright); TSRE credits
this in its About window. Routes containing objects made from OSM data need
their own "© OpenStreetMap contributors" attribution. That is the route
designer's responsibility.
