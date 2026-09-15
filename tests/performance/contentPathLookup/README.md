# Content-path lookup microbenchmark

Review-only, standalone Qt Core executable. It enumerates ENG/WAG names under a
supplied gameroot's immediate TRAINS/TRAINSET subdirectories, matching EngLib's
inventory depth. It never parses or modifies content and creates no GUI.

The five timed strategies insert those paths into an initially empty cache. The
first four use the same linear scan and report comparisons/hits. The final mode
uses a logical-key hash. All entries are treated as successfully loaded, so the
linear comparison count is the all-loaded case, not a count measured inside the
editor. Key creation/storage is included in the cached/hash timings. File parsing,
OPENRAILS selection, physical-identity checks on actual cache hits, and GUI/event
processing are deliberately excluded. The fixture uses distinct real path names;
these strategies are timing comparisons, not complete alternative cache designs.

```sh
cmake -S tests/performance/contentPathLookup -B /tmp/content-path-bench \
  -DCMAKE_BUILD_TYPE=Release -DTSRE_SOURCE=/root/TSRE5vc
cmake --build /tmp/content-path-bench
/tmp/content-path-bench/content_path_lookup_benchmark /path/to/gameroot
```

For native Windows measurement, copy this standalone project into an ignored
build directory and set TSRE_SOURCE to the Windows checkout and CMAKE_PREFIX_PATH
to the native Qt installation. Use the native compiler/Qt DLL directory in PATH.

## Cached linear lookup versus an additional hash index (WSL)

`content_path_index_benchmark` tests the follow-up decision independently of the
old repeated-key regression. Both variants use a numeric-ID
`std::unordered_map<int, unique_ptr<Entry>>`, pointer indirection, a loaded flag,
and a stored `QString hashid`. The indexed variant additionally maintains
`QHash<QString,int>`; hits retrieve and validate the numeric-map entry. This is an
EngLib-shaped microbenchmark, not a call into the complete editor or Eng parser.

```sh
cmake -S tests/performance/contentPathLookup -B /tmp/tsre-content-index-bench -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DTSRE_SOURCE=/root/TSRE5vc
cmake --build /tmp/tsre-content-index-bench --target content_path_index_benchmark
/tmp/tsre-content-index-bench/content_path_index_benchmark /mnt/c/trainsim
```

Only directory/file names are enumerated from the supplied installation, before
any timing. No content is opened or changed. Case-fold duplicate names, if any,
are counted and deduplicated before timing. All entries represent loaded assets.

- Cold insertion includes one production `ContentPath::key()` call per path,
  duplicate detection, entry allocation, numeric-map insertion, and index growth.
  Neither container is reserved. Destruction is excluded.
- Lookup caches are populated outside timing. Case-variant hits use a fixed-seed
  shuffled selection, repeated when the query count exceeds the asset count.
  Misses append `.missing`; these can benefit from QString's length rejection.
- `prepared-key` isolates lookup on already computed keys; `with-key` includes
  production key preparation once per request, as current EngLib does.
- Every lookup result is checked against the expected numeric ID (or -1), including
  case-variant requests. Both variants perform the same result validation.
- Each row reports the median and range of three rounds after one warmup; variant
  execution order alternates. Submillisecond timings are indicative, not precise.
- Synthetic fixtures cover 10,000 and 100,000 entries. At 100,000 entries only
  bounded lookup batches are timed, not the quadratic cold insertion workload.

Raw output: [wsl-index-result.txt](wsl-index-result.txt). Analysis is in
[the performance review](../../../docs/tasks/core/conedit-case-path-performance-review.md).
Parsing, rendering, GUI work, index invalidation and memory consumption are not
benchmarked. No production hash index is added by this test project.
