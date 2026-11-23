# tsection-review

Review tools for procedural complex track
([tracks task 17](../../docs/tasks/tracks/17-trprofile-complex-tracks.md)).
They read a `tsection.dat` (UTF-16 or UTF-8) and work in MSTS shape space:
+x right, +z forward, curve angles and path rotations positive to the right,
paths listed from left to right.

Python 3 with numpy (`review.py`, `assembly.py`); `rsvg-convert` turns the
plans into PNG; `assembly.py`'s textured panel uses ImageMagick (`magick`)
and TSRE's own ACE converter (`build/TSRE5vc --aceconv`).

## review.py: shapes grouped by geometry

```bash
python3 review.py ~/ORTS/TS_STARTER_ROUTE/GLOBAL/tsection.dat \
    --prefix 'A\d+t' --csv a-namespace.csv \
    --world ~/ORTS/OR_PSK_NEW/ROUTES/PRUDNIK/WORLD \
    --shapes ~/ORTS/*/GLOBAL/SHAPES --jobs 6
```

Every path is sampled every 0.25 m with both rails at the gauge (1.435 m).
For each shape it finds:

- `switches`: path pairs that start at one point with one heading;
- `rail_crossings` and `crossing_angles`: rails of different paths crossing
  (frogs, diamond crossings), not counting a switch's first metre;
- `tie_overlap`: metres of path closer than a plain tie (2.6 m) to another
  path's centre line, where separate ties would collide.

Groups: `1 single path`, `2 parallel paths, separate` (today's
LEFT/MIDDLE/RIGHT families), `3 junction`, `4 diamond crossing`,
`5 overlapping paths (frog pieces)`, and by name `6 tunnel`, `7 bridge`,
`8 ...` (buffers, turntables, level crossings, water, fuel, scales, derails),
`9 road`. Groups 3 to 5 need the COMPLEX member.

`--world` counts TrackObjs by their `SectionIdx`, so reskins such as
DBTracks' `DB/DB30_A1tFrog7_5dLft.s` count with the shape whose geometry
they use. Compressed W files are skipped (counted in the summary line).
`--shapes` notes which GLOBAL/SHAPES folders hold each shape file. The CSV
also has each shape's gauge (`SectionSize`'s first value), flags and the
tsection's `_INFO` header lines above its run of shapes (set names,
credits).

A whole Build 00054 file (14,629 shapes) takes about five minutes with
`--jobs 6`.

## assembly.py: several objects together

```bash
python3 assembly.py ~/ORTS/OR_PSK_NEW/GLOBAL/tsection.dat \
    ~/ORTS/OR_PSK_NEW/ROUTES/PRUDNIK/WORLD/w-005373+014872.w 157 slip.svg \
    --uids 149 150 155 156 --scale 30 \
    --shapes ~/ORTS/OR_PSK_NEW/GLOBAL/SHAPES \
    --textures ~/ORTS/OR_PSK_NEW/GLOBAL/TEXTURES \
    --excluded-parts '*Pnt7_5d*Slip*' Tie,Fastener,Bed \
    --included-track-shape '*DblSlip7_5d*' A1tPnt7_5dLftSlip.s 0 0 -10 0 \
    --included-track-shape '*DblSlip7_5d*' A1tPnt7_5dRgtSlip.s 1.632 0 -9.893 -7.5 \
    --included-track-shape '*DblSlip7_5d*' A1tPnt7_5dRgtSlip.s -1.632 0 14.893 172.5 \
    --included-track-shape '*DblSlip7_5d*' A1tPnt7_5dLftSlip.s 0 0 15 180
```

A centre TrackObj (by UiD) and its neighbours (`--uids`, else all within
`--radius`), in the centre object's frame. Panels: the static shapes'
distance level 0 textured from above, the tie plans with the filter values
(`--excluded-parts` leaves parts out of an object, as the filter's
`ExcludedParts`; an object with `--included-track-shape` plans its ties and
fastenings over those shapes' paths too, as the filter's
`IncludedTrackShape`, their rails only with `--included-rails`), and
every object laying its own ties. The W file's QDirection gives the
heading as -2 atan2(qy, qw) in shape space's sense;
the joints of PRUDNIK's double slips meet to the centimetre with it.

## tieplan.py: phase 1 tie planner prototype

```bash
python3 tieplan.py ~/ORTS/TS_STARTER_ROUTE/GLOBAL/tsection.dat plans \
    A1tPnt7_5dLft.s A1tFrog7_5dLft.s A1tYPnt10d.s 1a:A1tFrog7_5dLft.s
for f in plans/*.svg; do rsvg-convert "$f" -o "${f%.svg}.png"; done
```

One SVG plan per shape, z up the page: blue bearers (shared ties), brown own
ties, red dots where a fastening goes, grey each path's ballast footprint,
dashed red centre lines with the path numbers at their ends. A `1a:` prefix
lays every tie square to the reference line (no hand-over to own ties).

The rules are the design's (task 17, "Ties"); the constants at the top of
the file stand in for the COMPLEX member's values (`MAX_SHARED_ANGLE`,
`STEEP_LENGTHEN` for crossings steeper than it). Plans made for the task
live in the assets-gen repository, `reports/complex-tracks/`.
