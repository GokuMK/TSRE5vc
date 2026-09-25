# Navi Window and country places

Implementation task started 2026-09-25. This work is related to geographic
route creation, but is intentionally separate from the projection and New
Route dialog tasks.

## Scope

- Accept a pasted `latitude, longitude` or `latitude; longitude` pair in either
  Navi coordinate field and populate both fields.
- Add a place search field beneath the marker-source selector. Its
  `QCompleter` searches the current source and selects the corresponding entry
  in the existing, unfiltered places combo.
- Use marker indices as selection identity so duplicate display names remain
  distinct.
- Generate route-local place sources by country during interactive route
  creation and through **Route -> Generate country places...**.
- Permit multiple generated countries. Regenerating one country atomically
  replaces only that country's file.

## Country inference

The country selected with a New Route starting-place preset is authoritative.
Otherwise TSRE uses the country nearest the route start latitude/longitude and
falls back to the selected projection-origin preset. For existing routes the
start position is reconstructed from both the TRK start tile and its local X/Z
position. The menu always presents the inferred country as an editable
selection.

## Route-local format

Generated sources use standard KML 2.2 files named
`tsre-country-CC.kml`. `CoordsCountryPlaces`, derived from `Coords`, recognizes
that convention. Ordinary KML continues through `CoordsKml`.

TSRE metadata uses arbitrary XML permitted by KML `ExtendedData`, with the
namespace `urn:tsre5:geo-places:1`. Direct namespaced elements avoid the
repetitive KML `Data/value` representation and allow aliases to be repeated:

```xml
<ExtendedData>
  <tsre:placeId>756135</tsre:placeId>
  <tsre:asciiName>Warsaw</tsre:asciiName>
  <tsre:alias>Warschau</tsre:alias>
</ExtendedData>
```

The document-level metadata records the format version and country code. Each
placemark retains the source ID, native display name, ASCII name, aliases, and
coordinates. Other KML applications can display the points while ignoring or
preserving TSRE metadata.

## Compatibility and implementation boundaries

- `Coords::Marker` gains additive source/search metadata and double-precision
  latitude/longitude. Existing rendering and public marker-list behavior are
  retained.
- Country generation uses the shipped city-preset fallback and writes through
  `QSaveFile`.
- City loading and menu generation run on worker threads. UI models and route
  marker replacement remain on the GUI thread.
- A country-file failure does not discard a successfully created route; the New
  Route flow reports the warning and the menu can regenerate the file later.
- Broad `Coords` ownership and renderer-resource cleanup remain outside this
  task.

## Tests

The New Route suite covers country indexing and inference, namespaced KML
writing, absence of `value` wrappers, KML metadata/alias reload, alias search,
and the shared coordinate-pair parser.
