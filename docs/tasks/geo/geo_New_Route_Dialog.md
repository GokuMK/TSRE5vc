# New Route dialog

## Scope

Replace the legacy fixed-size New Route dialog with a TSRE-styled workflow for selecting the route start, choosing its geographic projection, and calculating the new route's tile/projection metadata.

This task uses the projection architecture documented in `geo_Projection_TM_Implementation_Notes.md`, but the UI and route-creation workflow are maintained here as a separate task.

## Required layout and behavior

- Retain the required **Name ID** field.
- Start latitude and longitude are initially empty.
- Use a TSRE main-label section titled **Choose Route Starting Point**.
- Place search uses a `QCompleter` popup, visually comparable to a web search suggestion popup. Suggestions must not be embedded in the dialog layout and must not resize the dialog.
- Show the chosen place in a separate non-editable field.
- Show explicit **Latitude** and **Longitude** labels. Both coordinates remain editable.
- Pasting `lat, lon` into either coordinate field atomically fills both fields.
- Keep the complete projection section visible; disable it until valid start coordinates exist instead of hiding controls.
- Use a TSRE main-label section titled **Route Geographic Projection**.
- The projection combo uses the standard TSRE combo popup style (`combobox-popup: 0`).
- Projection defaults to **Transverse Mercator**. It also offers **Local Ellipsoidal Equirectangular - Legacy TSRE** and **Interrupted Goode Homolosine - Legacy MSTS**.
- IGH disables all local-origin and route-offset controls without hiding them and clears their current values.
- Projection-origin search also uses a `QCompleter` popup and a separate non-editable selected-origin field.
- Show explicit labels for origin latitude/longitude and projection tile-offset X/Z.
- Origin latitude/longitude and projection tile-offset X/Z are read-only/disabled by default.
- **Custom origin**, positioned above those fields, unlocks origin latitude/longitude and projection tile-offset X/Z.
- Projection tile-offset X/Z defaults to `0,0` and remains visible while disabled.
- **New route** and **Cancel** use a `QDialogButtonBox`.

## Preset assets

### Places

`assets/geo/geo_cities_presets.txt` is the local working copy. It is UTF-8,
tab-separated, and currently contains 34,145 records with 19 columns. Required
columns are:

- GeoNames ID `[0]`
- name `[1]`
- ASCII name `[2]`
- comma-separated aliases `[3]`
- latitude `[4]`
- longitude `[5]`
- country code `[8]`

Start loading it on a worker thread when the dialog opens so file parsing does not delay the window. Disable the place search only until the worker publishes the completed index. Use a compact place vector and a normalized name/alias index mapping names to place IDs. Build only a bounded popup result model; do not populate a UI widget with the complete alias set.

The large text file remains beneath the ignored `assets/` tree and is not in the
application ZIP. The shipped fallback is the ordinary Deflate ZIP
`appdata/0.7/geo/geo_cities_presets.zip`. TSRE already embeds the `miniz` ZIP
reader. If the asset text file is absent, the same worker extracts only the
expected `geo_cities_presets.txt` entry, limits its expanded size, writes it
atomically to `assets/geo/`, and then parses it. It does not expose a general ZIP
extractor or accept archive paths.

### Projection origins

`appdata/0.7/geo/geo_projection_presets_countries.json` is a UTF-8 array of 257 objects with `countryCode`, `countryName`, `name`, `lat`, `lon`, and `k0`.

Selecting a city automatically selects the geographically nearest projection preset having the same country code. Manual coordinates fall back to the globally nearest preset. Canada, China, Russia, and the United States contain multiple origins, so country code alone is not a unique key.

Parse and retain `k0`. For Transverse Mercator it is passed through route
creation, saved in the TRK and applied by the converter. Legacy TSRE and MSTS
IGH projections continue to use their established scale.

## Route creation integration

- A validated result contains route name, start latitude/longitude, projection type, the named projection parameters (origin, raw metre offsets and `k0`), and the calculated route start tile.
- IGH creates no `TsreGeoProjection` metadata and derives the MSTS tile through the IGH converter.
- Local projections set both `Trk::geoProjectionType` and the optional value-semantic `Trk::geoProjection` before the first save.
- Projection tile-offset X is converted to raw metres as `X * 2048`. Z uses the established normalized-boundary convention, `(Z + 1) * 2048`, so the projection center occupies the requested tile at local Z=`0`.
- The actual route start tile is calculated from the start latitude/longitude through the fully configured converter; it is not supplied by the offset fields.
- With **Custom origin** enabled, **Create origin offset using MSTS IGH** converts the origin latitude/longitude through the MSTS IGH converter and fills projection tile-offset X/Z with the resulting MSTS tile. This provides a reusable migration aid for routes moving from IGH to a local projection.
- A missing/malformed asset displays a non-fatal status and still permits manual coordinate entry.

## Validation and translation

- Validate the trimmed route ID before creation and repeat the validation when
  the button is pressed.
- Require 2–64 characters and allow ASCII letters, digits, spaces, hyphens and
  underscores. Reject path punctuation and Windows reserved device names.
- Reject an existing route directory case-insensitively, including on a
  case-sensitive host, because routes may later be moved to Windows.
- Keep the validation message in a permanently allocated row so its appearance
  does not resize the dialog.
- All user-facing dialog text and validation errors belong to the
  `NewRouteWindow` translation context. Keep English and Polish catalogs in
  sync.

## Verification

- Place parser and normalized alias lookup.
- Country-restricted nearest origin and global fallback.
- `k0` parsing, application, round trip and TRK persistence.
- Coordinate-pair paste and range validation.
- Stable dialog geometry while suggestions open/close and controls enable/disable.
- TM route start tile is derived correctly after applying the projection tile offsets.
- IGH and TM projection results reach initial TRK creation and survive save/reload.

The focused internal suite is `--test-suite new-route`. It covers place/alias
parsing, ZIP fallback extraction, country-restricted origin selection, name
validation, coordinate-pair parsing, projection tile-offset mapping, variable
TM `k0`, safe automatic defaults and TRK projection save/reload. Dialog geometry
and complete on-disk route creation remain manual integration checks.

Verification on 2026-09-19: the two-job release build completed and all 27
focused `new-route` cases passed. Both translation catalogs compiled with no
unfinished entries.

## Future optional performance work

The current worker removes dialog-opening latency, but every new dialog still
parses the city file again. If profiling shows repeated opens matter, add a
process-wide immutable cache whose completed index is shared by dialogs. The
cache must publish only a fully built index, coalesce concurrent requests, and
must not retain dialog/QObject pointers. This is deliberately staged as an
optional improvement, not part of the first implementation.

## Implementation status: `k0` and route creation ownership

- `GeoProjectionParameters` now owns origin latitude/longitude, metre offsets
  and `k0`; `Trk` stores it by value in `std::optional`.
- Non-unit TM scale is saved as `TsreGeoProjectionScaleFactor`. Older TRKs
  default to `1.0`. The TM converter applies it to the common forward/inverse
  scale; legacy TSRE and IGH behavior is unchanged.
- `Game::newRoute*` and `Route::createNew()` were removed. Both the dialog and
  automatic startup construct an owned `Trk::createNewRouteTemplate()` and pass
  it directly to the non-UI `RouteCreator`; there is no pending static slot.
- Automatic defaults are IGH at tile `-5000,15000`. GUI creation offers the
  extended TRK editor; headless creation does not. Its buttons are Apply/Skip
  for creation and Apply/Discard for an existing route.
- Creation uses a sibling staging directory, checked writer results and
  `TerrainLibQt`, then renames the completed directory into place. `Route::load`
  is now a strict loader.
- Full `TerrainLibQt`/`QuadTree` ownership cleanup remains in the separate
  memory-allocation task; no uncertain shared/raw ownership is deleted here.

The two sections below preserve the original review rationale. Their actionable
items are implemented, except for the separately staged ownership cleanup.

## Original review: applying projection scale factor `k0` (implemented)

The preset loader already validates and retains `k0`, but the value currently
stops at the new-route hand-off. A complete implementation needs coordinated
changes rather than only multiplying the UI values:

1. Replace the raw four-double projection convention with a named projection
   parameter value containing origin latitude/longitude, raw metre offsets and
   `k0` (default `1.0`). Keep an adapter for existing call sites while migrating.
2. Add a TRK field with a default of `1.0` and a backward-compatible token, for
   example `TsreGeoProjectionScaleFactor ( 0.9996 )`. A separate token is clearer
   than silently changing the four-value `TsreGeoProjection` shape. Old TRKs
   without it retain `1.0`; loaders that ignore unknown TSRE tokens remain safe.
3. Pass the named parameters through `GeoWorldCoordinateConverter::Create`.
   The TM converter should apply `k0` to both axes consistently. In the current
   Krüger implementation the cleanest point is the common rectifying-radius
   scale used by `forwardRaw` and `inverseRaw`; the origin northing must be
   calculated using that same scaled model.
4. Do not apply `k0` to legacy equirectangular or IGH calculations. Preserve it
   only where needed for round-trip metadata, or validate it as TM-only.
5. Add known-reference forward/inverse cases against the source CRS definition,
   round-trip tests with non-unit `k0`, a `k0=1` regression, and old/new TRK
   save/reload tests before enabling the preset value in created routes.

## Original review: removing new-route state from `Game` (superseded by direct creation)

The current `Game::newRoute*` values are temporary transport state and should be
removed. Defaults already belong to `Trk`, but a loaded-TRK default and a
new-route policy are not necessarily the same thing, so the preferred design is:

1. Give `Trk` proper value semantics for projection data first (for example
   `std::optional<GeoProjectionParameters>` instead of the owning raw
   `double *tsreProjection`). Its current raw pointer has shallow-copy and
   lifetime hazards.
2. Provide `Trk::createNewRouteTemplate()` (or an equivalently named factory)
   returning `std::unique_ptr<Trk>`. The normal constructor supplies intrinsic
   TRK defaults; the factory supplies any new-route-specific policy. The dialog
   applies the chosen name, start tile and projection directly to that object.
3. For the smallest refactor compatible with the current startup flow, store a
   **private one-shot** `std::unique_ptr<Trk>` in `Route`, exposed only through
   `setPendingTrkTemplate(...)` and `takePendingTrkTemplate()`. `createNew()`
   consumes it, or calls the factory when none was supplied. Never expose a
   public mutable `static Trk *`: it has ambiguous ownership, can leak, can be
   reused accidentally by a second creation attempt, and can dangle.
4. `createNew()` sets only identity values that necessarily depend on the final
   normalized route directory, opens the existing TRK editor, and saves. Once
   consumed, no new-route state remains. This also makes cancellation and retry
   behavior explicit.
5. Longer term, pass the owned template/request through the startup controller
   into the `Route` instance instead of using any static pending slot. That is
   architecturally cleaner, but it is a wider change than needed to remove the
   `Game` arguments now.

In short, the proposed `Route::TrkTemplate` direction is sound as a transitional
handoff, but it should be private, uniquely owned and consumed exactly once;
the dialog should not publish another group of global scalar arguments.
