# B-key terrain status

The detailed-terrain creation dialog starts with a read-only **Current status**
section for the selected World tile:

- **Terrain tile:** whether its `.t` descriptor exists on disk, physical size in
  metres, and samples / patches per side. The descriptor filename is shown;
  its full path is available in the tooltip. An unreadable descriptor is reported
  as existing with an unreadable layout, not as a missing tile.
- **QuadTree:** not populated, or populated at the selected node's size in
  metres. In the deprecated simple backend, which does not expose the detailed
  QuadTree through the common API, the status is reported as unavailable.

These are independent observations: MSTS RGE can populate a QuadTree node
without generating terrain, or generate terrain before saving QuadTree changes.
Neither combination is automatically classified as corruption.

The probe checks the registered descriptor first, then the enclosing MSTS tile
filenames from 2048 m through 524288 m, including unpopulated locations. If
multiple covering descriptors exist, each is listed; listing a file does not
mean the current QuadTree will select or load it. Only detailed `tiles` are
inspected, not `lo_tiles`. World tiles remain independent 2048 m cells.

`TFile::readLayoutInfo()` uses the existing compressed/uncompressed SIMIS reader
and bounded block parsing to extract sample count, sample spacing and patch
count. It does not instantiate terrain, allocate patch/material arrays, load
raw buffers or generate meshes. File existence is not a promise that all raw
buffers are present or that the complete tile is valid/editable.

The status display does not populate, repair or save QuadTree entries. Existing
creation/replacement actions and write protection are unchanged.

Verification: build and the SIMIS token suite cover compressed/plain metadata,
different physical sizes and sample/patch grids, missing files and truncation.
