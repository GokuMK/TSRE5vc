# Denmark DHM/Terrain and catalogue isolation

Implemented and validated 2026-09-19 on `feature/geo-terrain`.

## Catalogue failure and fix

The Denmark template introduced `authentication.type=query-api-key`, which the
old parser did not support. Its first invalid object caused an empty result for
the entire catalogue, leaving HGT as the only source in the selectors.

The parser now skips invalid objects, collects diagnostics identifying them and
continues with valid entries before and after each failure. Generation accepts
the remaining valid catalogue and includes the warnings in its report. Settings
logs the diagnostics and the height dialog displays them on opening. Invalid
saved selections still fail explicitly; they do not silently choose HGT.
Malformed JSON and invalid top-level structure remain file-level failures.

Regression checks include unknown authentication, unsupported CRS, duplicate
IDs, non-object entries, invalid grids and malformed/root-invalid JSON. All 18
current dataset definitions load successfully.

## Denmark configuration

- ID: `dk.datafordeler.dhm.terraen`; WCS 1.0.0 coverage `dhm_terraen`.
- Native 0.4 m terrain, requested and cached at **1 m**, EPSG:25832. Shared local
  UTM conversion is sufficient; no new projection or dependency was needed.
- `requestFormat=GTiff` follows the [official WCS example](https://datafordeler.dk/dataoversigt/danmarks-hoejdemodel-dhm/dhm-wcs/).
  The TIFF decoder continues to use `format=image/tiff`.
- `authentication.type=query-api-key`, parameter `apikey`, secret reference
  `geo.elevation.dk.datafordeler.apiKey` in the active profile's `secrets.json`.
- 1024-pixel cores, one connection, DVR90 heights, valid zero and negative
  elevations. Default HGT fallback; no vertical datum conversion.

The downloader adds the percent-encoded key only to the outgoing request.
Public request URLs and cache metadata omit it. Error reporting avoids Qt's
URL-bearing error string for query-authenticated requests. Both authentication
methods restrict redirects to the same origin. Credentials are scoped to a
single wave; existing valid caches work without obtaining a new response.

## Live validation

Using the user's existing profile key through the production Qt downloader:

- A 32 x 32 probe returned supported uncompressed Float32 TIFF at exact 1 m
  spacing in EPSG:25832; first height was approximately 8.4846 m.
- Full 256 x 256 grid at 8 m spacing around `(720000,6174000)` in EPSG:25832:
  **65,536 primary samples, zero HGT, zero NoData/unavailable samples**.
  Nine 1026 x 1026 blocks downloaded in about **57.0 seconds**. Output height
  range was approximately **0.506..15.597 m**.
- Repeating used **nine cache hits, zero downloads**, approximately **946 ms**,
  with the same height range.
- Scanned all nine TIFFs and nine metadata files: no raw or percent-encoded key
  value present; metadata URLs contain no `apikey` parameter.

Release application build succeeded; application UI suites were not run.
Standalone checks: **347 passed**, covering catalogue isolation, query-key
encoding, unchanged public URLs, credential isolation between waves, HTTP failure
redaction and cross-origin redirect rejection, alongside existing elevation tests.
Probe source, local sample, logs and caches are in ignored `build-dk-research/`;
no secret is copied into tracked evidence. Python's TLS backend rejected the
server certificate chain during preliminary probing; Qt's normal verified TLS
path succeeded. Certificate verification was not disabled.

Remaining: manual application testing and broader national/coverage-edge checks.
[Distant terrain](distant-terrain-elevation.md) remains a separate open issue;
these detailed-terrain results do not approve Denmark for distant acquisition.
