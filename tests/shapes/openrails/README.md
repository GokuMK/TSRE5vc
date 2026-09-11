# Isolated Open Rails shape parser benchmark

This small console harness links the local, **unchanged** Open Rails `ShapeFile.cs`, `SBR.cs`, `STFReader.cs` and `TokenID.cs`. It uses the real managed MonoGame assembly for math types. It does not build the game, load textures, initialize graphics, or copy Open Rails sources into TSRE.

Local setup used for the comparison:

```sh
DOTNET_CLI_TELEMETRY_OPTOUT=1 /root/msts/analysis/dotnet-sdk/dotnet build \
  tests/shapes/openrails/ShapeParserBench.csproj -c Release \
  -o /tmp/sfile-openrails-bench \
  -p:OrtsSource=/root/openrails/Source \
  -p:MonoGameAssembly=/root/.nuget/packages/monogame.framework.desktopgl/3.8.1.303/lib/net6.0/MonoGame.Framework.dll

DOTNET_TieredCompilation=0 /root/msts/analysis/dotnet-sdk/dotnet \
  /tmp/sfile-openrails-bench/ShapeParserBench.dll \
  /root/msts/proprietary/msts_app/TRAINS/CD_193_290/CD_193290.s \
  /root/msts/proprietary/msts_app/TRAINS/CD_193_290/CD_193290_MS.s \
  /root/msts/proprietary/msts_app/TRAINS/CD_193_290/CD_193290_FG.s \
  > /tmp/sfile-openrails-results.jsonl 2>/tmp/sfile-openrails-warnings.log
```

Run benchmark processes sequentially, without concurrent builds or other benchmarks. Each file is measured with validation disabled and enabled. The constructor includes file access, decompression, structured parsing and optionally `ShapeFile.Validate()`. Each case has an initial call, four warmups, then eleven timed samples. Forced GC occurs before each sample, outside timing; object allocation and any GC during construction remain inside timing. `cold_ms` means the first call for that case, **not** a cold filesystem cache. This is a Linux .NET 8 measurement of these sources, not a measurement inside the Windows Open Rails game.

`allocated_bytes` counts managed allocation during construction; `retained_bytes` estimates the live managed delta after collection with the final shape kept alive. Neither is peak memory or process RSS, and neither includes native allocations. Counts of points, matrices, LODs and subobjects help detect incomplete parsing; they do not prove semantic equivalence of every field.

The same executable accepts the temporary UTF-16 exports from `tsre_shape_parser_bench --export-text`. Originals and generated asset content must stay outside the repository. See the [parser investigation](../../../docs/tasks/shapes/reports/sfile-parser-performance.md) for source hashes, results and limitations.
