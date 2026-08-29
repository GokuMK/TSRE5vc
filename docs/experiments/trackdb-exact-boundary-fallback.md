# TrackDB exact-boundary fallback

`trackdb-exact-boundary-fallback.patch` preserves the previously tested
placement-only workaround for elevated multi-section TrackShapes. It is not
the active implementation. Keep it only as a recovery reference until the
full TDB frame solution has received enough route testing.

The fallback accumulates a TrackShape in its rigid local plane, overwrites
each inserted subsection boundary, and assigns an independent pitch to each
subsection. It fixed final positions, but it could not make generic TDB
sampling and line rendering follow the same geometry because it did not store
the complete pitch/yaw/third-angle frame.

Do not apply the patch on top of the full-frame implementation. To use it as a
fallback, first revert the full-frame TDB changes on a temporary branch, then
apply the patch there and resolve the small `placeTrack()` context if needed.
The file intentionally remains a plain Git patch so the old implementation is
inspectable without compiling or including dead production classes.
