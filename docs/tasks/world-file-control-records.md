# World-file control records

Status: future task; not part of the settings-system implementation.

## Problem

`Tr_Watermark` is a world-file serialization control record that marks the
beginning of a static-detail-level group. It is currently represented by
`TrWatermarkObj` and stored in `Tile::obiekty` alongside editable world
objects. This preserves its position for the unsorted save path and for tile
streaming, but incorrectly models the record as a world object.

`TrWatermarkObj` therefore uses the canonical textual type `tr_watermark` but
the non-object `WorldObj::none` type ID.

## Future review

- Decide whether parsed `Tr_Watermark` records should be discarded during
  loading and always regenerated when saving.
- Verify whether retaining watermarks has any meaningful value when object
  sorting is disabled. Unsorted objects may make detail-level boundaries
  ineffective or ambiguous.
- If exact unsorted round-tripping remains required, move watermarks out of
  `Tile::obiekty` into an ordered world-file record representation that can
  preserve control-record positions without treating them as editable objects.
- Cover normal save, sorted save, `Tile::saveToStream`, server/client tile
  transfer, and files containing multiple watermark levels.

Do not remove the current pseudo-objects until those save and streaming paths
have an explicit replacement.
