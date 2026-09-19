# Baden-Wurttemberg DGM1: WCS 2 research

Researched 2026-09-19 against `feature/geo-terrain` at `593f0cc`.
Research only: no application source/configuration changes, builds or TSRE tests.
Live HTTP requests and a standalone Python TIFF inspector were used.

## Conclusion

WCS 2.0.1 works, but switching protocols does **not** restore fractional heights.
Both protocols returned uncompressed, single-band **UInt16** GeoTIFFs. After
working around WCS 2's output-size problem, their pixel arrays were byte-for-byte
identical for a small cutout and a production-size block.

There are two separate compatibility problems:

1. The service requires **E/N for SUBSET, X/Y for SCALESIZE**. Our WCS 2 builder
   currently uses the same configurable axes for both parameters.
2. At native resolution, even a correctly named SCALESIZE can return one column
   fewer than requested. A requested 1026 x 1026 block returned 1025 x 1026.
   This fails our strict grid validation.

Keep BW on WCS 1 for now. It returned the requested dimensions and does not lose
any precision relative to the tested WCS 2 output. Existing footprint averaging
can soften integer-height terraces, but cannot recover the missing measurements.
The dataset's **1 m** label describes horizontal spacing, not vertical precision.

## Service and advertised capabilities

Endpoint:
`https://owsproxy.lgl-bw.de/owsproxy/wcs/WCS_INSP_BW_Hoehe_Coverage_DGM1`

Coverage: `EL.ElevationGridCoverage`.

- WCS 1.0.0 and 2.0.1 are available.
- WCS 2 advertises TIFF, JPEG2000, PNG, JPEG and ECW; changing an encoding does
  not by itself establish better elevation precision.
- Advertised CRSs: EPSG:25832, EPSG:4326 and EPSG:4258.
- Extensions include subsetting, scaling, CRS conversion, interpolation and
  range subsetting. There is only one advertised coverage and one range band.
- DescribeCoverage uses `E N` on the CRS envelope and `X Y` on the rectified grid.
- Native extent: `(387999.5,5263999.5)` to `(611000.5,5520000.5)`.
- Reported grid steps are `1.0000000000001454` and `-0.9999999999995346` metres.
- Range metadata claims `0..255` and radiance units. Actual TIFFs contain heights
  above 255 and UInt16 samples; that metadata is not a reliable numeric contract.

Sources: live [GetCapabilities](https://owsproxy.lgl-bw.de/owsproxy/wcs/WCS_INSP_BW_Hoehe_Coverage_DGM1?SERVICE=WCS&REQUEST=GetCapabilities&VERSION=2.0.1),
[DescribeCoverage](https://owsproxy.lgl-bw.de/owsproxy/wcs/WCS_INSP_BW_Hoehe_Coverage_DGM1?SERVICE=WCS&REQUEST=DescribeCoverage&VERSION=2.0.1&COVERAGEID=EL.ElevationGridCoverage),
and the [LGL WCS guide, 10 July 2025](https://www.lgl-bw.de/export/sites/lgl/Produkte/Galerien/Dokumente/Kundeninformation_WCS_2_0.pdf).

## Measured responses

Small box A: `513000,5405000,513032,5405032` in EPSG:25832.
Production box B: `511999,5405695,513025,5406721`, corresponding to the current
1024 m core grid with a one-pixel halo on each side.

| Request | Result |
| --- | --- |
| WCS 1, A, WIDTH=32 / HEIGHT=32 | 32 x 32, exact 1 m spacing, UInt16, heights 314..328 |
| WCS 2, A, SUBSET E/N, SCALESIZE E/N | HTTP 404, `ScaleAxisUndefined` |
| WCS 2, A, SUBSET E/N, SCALESIZE X/Y=32 | 31 x 32, spacing 1.0322580645 x 1 m, UInt16 |
| WCS 2, A, no scaling | Same dimensions and spacing as previous row |
| WCS 2, A, explicit native SUBSETTINGCRS/OUTPUTCRS | Still 31 x 32 |
| WCS 2, A shifted by +0.5 m | Still 31 x 32; shifting to half-metre bounds is not the fix |
| WCS 2, A, SCALESIZE X/Y=16 | 16 x 16, 2 m spacing, UInt16 |
| WCS 2, A, SCALESIZE X/Y=64 | 64 x 64, 0.5 m spacing, UInt16; no extra vertical precision |
| WCS 2, A, SCALEFACTOR=2 / 0.5 | 62 x 64 / 15 x 16; inherits the native size discrepancy |
| WCS 1, B, WIDTH=1026 / HEIGHT=1026 | 1026 x 1026, exact 1 m spacing, UInt16, heights 268..383 |
| WCS 2, B, SCALESIZE X/Y=1026 | 1025 x 1026, spacing 1.0009756098 x 1 m, UInt16 |
| WCS 2, B, SCALESIZE X/Y=1027 | 1027 x 1027, approximately 0.99902629 m spacing; not our requested grid |
| WCS 2, second small location at 512800,5452000 | UInt16 again, heights 214..225 |

TIFF tags confirm BitsPerSample=16, SampleFormat=1 (unsigned integer),
Compression=1, one band, EPSG:25832 and NoData=0. No fractional samples or
scale/offset metadata were present in the inspected WCS TIFFs.

### Native-size rounding experiment

Increasing only the east bounding coordinate by **0.00000001 m** made WCS 2
return the requested size in both A and B:

- A: 32 x 32, east-west spacing `1.0000000003128662` m.
- B: 1026 x 1026, east-west spacing `1.000000000009758` m.
- Their UInt16 pixel arrays exactly match the corresponding WCS 1 arrays.

SHA-256 of the matching pixel arrays, excluding TIFF metadata:

```text
A f575008b20b49890cdd1578b675950cc27b070679533e0069a557b8c75f59537
B 627a88ed0739846de136bcf52b5a6776998f0a5936f2c49ee8de4d7bef2c7111
```

This supports a server-side floating-point rounding explanation, consistent with
the slightly non-unit step in DescribeCoverage. The exact server implementation
is unknown. The epsilon is a diagnostic experiment, **not an implemented or
generally validated workaround**. Do not loosen raster validation to accept a
missing column while pretending the spacing is still exactly 1 m.

### Other protocol quirks

- WCS 2 accepts `FORMAT=image/tiff`, but rejected `FORMAT=GeoTIFF`. WCS 1 accepts
  the latter, as our current provider requests.
- The advertised interpolation URIs for `linear` and `cubic` were rejected with
  `InvalidParameterValue`. The short token `bilinear` succeeded, changed some
  sample values, and still returned UInt16 with no fractional heights.
- Explicit native CRS parameters did not resolve the output-size problem.

## Reproduce a WCS 2 request

Append these parameters to the endpoint, URL-encoding parameter values:

```text
SERVICE=WCS
VERSION=2.0.1
REQUEST=GetCoverage
COVERAGEID=EL.ElevationGridCoverage
SUBSET=E(513000,513032)
SUBSET=N(5405000,5405032)
SCALESIZE=X(32),Y(32)
FORMAT=image/tiff
```

This reproduces the 31 x 32 response. Replacing the SCALESIZE axes with E/N
reproduces the axis error. Changing the east bound to `513032.00000001`
reproduces the 32 x 32 rounding experiment.

Raw responses, URLs, TIFF-tag summaries and the isolated Python probe are in the
ignored `build-bw-wcs-research/` directory. They are research artifacts, not new
application dependencies or committed regression fixtures.

## Better precision: separate download product

The official [DGM1 test-data ZIP](https://lgl-bw.de/downloads/Produkte/Testdaten/Galerien/Dokumente/Testdaten_DGM1_GEOTIFF.zip)
was downloaded independently. Its TIFF is **Float32**, 850 x 800 at 1 m spacing,
EPSG:25832 with DHHN2016 height metadata. It uses LZW compression, floating-point
predictor 3 and 512 x 512 storage tiles. This is a different delivery product;
its compressed pixel values were not decoded in this investigation and its
vintage was not matched to the WCS coverage. The [official sample page](https://lgl-forum.lgl-bw.de/Produkte/Testdaten/Testdaten-3D-Produkte/)
also illustrates DGM coordinates with fractional heights.

LGL's [2026 product announcement](https://www.lgl-bw.de/artikel-detailseite/Erstellung-zukuenftig-ausschliesslich-rasterbasiert/)
describes DGM025 GeoTIFF and DGM1 XYZ downloads through its Open GeoData portal.
That is the more promising next investigation for preserving source precision.
Neither a full portal tile nor a direct-download provider was validated here.
The current TIFF reader does not support the sample's compression/predictor.

## Follow-up decisions

- [ ] If another service needs it, add separate configurable subset and scaling
  axes to the generic WCS 2 provider, preserving existing defaults. BW alone does
  not justify switching protocols for a quality improvement that was not found.
- [ ] Investigate original BW DGM1 XYZ / raster tile downloads for fractional
  heights, including grid registration, NoData, tile addressing and provenance.
- [ ] Keep any future precision fix separate from shared terrain downsampling:
  filtering quantized values improves appearance, not source accuracy.

No smoothing, catalogue, cache, decoder or provider behavior was changed.
