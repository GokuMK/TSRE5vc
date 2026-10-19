# Terrain painting, vegetation scattering and bulk instance submission

Status: draft, not implemented. Written 2026-10-18.

## Objective

Painting a terrain material should drive both the existing ground texture and
optional grass, flowers, small stones, bushes or trees. Keep painting as the
authoring source; instancing is the rendering representation and does not
prevent material-driven placement of larger vegetation.

Extend the existing route material library with an optional scatter-preset
reference. Do not replace the working texture generation/baked fallback pipeline.
Materials without a preset behave as before. Material UiDs are stable identities;
tile-local byte IDs must be resolved through that tile's palette before use.

## Data flow and ownership

`painted material UiD + scatter preset + seed + exclusions + terrain height`
produces cached instance records, grouped into spatial cells and submitted as
batches referencing shared meshes/materials and their ordinary LODs.

- A preset defines weighted shape variants, density in **clump instances per
  square metre**, spacing, scale/yaw ranges, slope/height filters, visibility,
  density reduction and shadow policy. A grass clump may contain many blades;
  explicitly distinguish mesh instances from the blades/cards inside them.
- Save material assignments, preset references, deterministic seeds and authored
  overrides. Treat generated placements/GPU buffers as rebuildable caches with
  bounded residency, not millions of world objects in the route files.
- Allow vegetation exclusion/density overrides independently of the material:
  clear track margins or keep woodland soil without automatically adding trees.
  First use an explicit mask; automatic track/road-derived exclusion can follow.
- Grass/flowers use shared clump meshes. Small stones remain rigid instances;
  at sufficient distance the terrain texture carries subpixel detail. Large
  bushes use tree-like LOD/visibility policies. These are per-preset policies,
  not three incompatible storage systems.
- Keep manually placed Static/Forest objects independent. Painted tree scatter
  later consumes the same shape/impostor assets, with clear rules against
  accidental duplicates where manual objects and automatic scatter overlap.

## Spatial layout and terrain fitting

Use terrain tiles (commonly 2048 m) for ownership/streaming and existing 64/128 m
patches as possible generation/cache units. Benchmark smaller 16/32 m visibility
cells for grass; these are starting experiments, not a fixed format constraint.
Generation, storage, culling and draw batches need not use identical boundaries.

Anchor each clump at sampled terrain height; optionally align partially to its
normal. Keep rocks rigid. A single Y translation cannot fit a repeated merged
patch mesh to different terrain. Reuse candidate patterns where worthwhile,
but derive final placement from each location's height, slope, masks and seed.

Use stable world-coordinate hashing, deterministic boundary ownership and a
spacing halo so regeneration order/tile streaming cannot create seams, duplicate
instances or density stripes. Cache keys must include source/palette identity,
preset revision, seed and relevant terrain/exclusion revisions; equal painted
patterns alone are not enough to share final transforms. Height edits invalidate
placements without requiring a terrain material repaint.

Generate asynchronously on load or dirty edits with bounded work/residency and
stale-result rejection. Regenerate affected cells plus filter/spacing halos.
Do not regenerate or upload every clump merely because the camera moved.
Use stable subsets for distance/quality density changes and controlled fading;
match distant grass colour to the ground material. Keep wind deformation in
the shader and expand bounds for its displacement when wind is introduced.

## Renderer work: a bounded bulk path

Current QueueRenderer captures individual 16-float matrices, groups submissions
and copies visible matrices into an upload array. Group instancing currently
also depends on packet identity and selection compatibility. This is useful
existing machinery, but a high-density producer should not create a WorldObj
or one heap object/queue submission per clump.

1. Benchmark existing grouped submission using one shared clump mesh and fixed
   placements, before tying the experiment to terrain material editing.
2. Add a generic instance-span/buffer submission path to the existing queue:
   shared mesh/material packet, instance-buffer reference/range, cell bounds,
   coordinate origin and selection/shadow policy. Cull cells before expanding
   work; preserve batching by resource, material and LOD. Reuse this API for
   other producers rather than creating a second vegetation renderer.
3. Reuse persistent instance storage and update dirty ranges where supported.
   A first implementation may retain the renderer's matrices; compact position,
   yaw, scale and variation records are an optimization to measure, not an
   entry requirement. Document any unavoidable visible-data copies/uploads.
4. Handle backend capacity limits by bounded chunks, not catastrophic fallback
   to one draw per blade/clump. Preserve camera-relative/tile-origin precision,
   resource lifetime across queued frames and main/shadow visibility differences.
5. Define editor picking (for example patch/cell ownership initially); avoid
   per-clump selection IDs silently defeating instancing. Implement the renderer
   abstraction for both supported OpenGL and QRhi paths, with measured behavior.

Instancing reduces submission and duplicated mesh storage; it does not remove
vertex, alpha-overdraw or shadow cost. At 16 float32 values each, one million
matrices alone occupy 64 MB decimal, before queue data, additional CPU/GPU copies
and meshes. Do not use that number as the total system memory estimate.

## Milestones and acceptance

1. **Render-only benchmark:** one clump mesh, fixed deterministic cells, 10k,
   50k, 100k and 250k instances. Compare existing and bulk submission with the
   same visible geometry. Add a million-instance stress case only if useful.
2. **One painted grass material:** optional preset reference, terrain anchoring,
   deterministic cache/streaming, distance visibility and density reduction.
   Demonstrate a painted patch on a slope and its distant texture transition.
3. **Editing correctness:** paint/erase/undo, height edits, exclusions, save/load,
   preset edits and tile-edge regeneration. Verify stable placement, no seam
   duplicates, bounded cache growth and rejection of stale async results.
4. **Mixed ground cover:** grass, flowers and opaque stones with separate
   density/visibility/shadow policies. Extend to bushes and trees only after
   their shared shape/LOD/impostor path is validated.

For each benchmark record hardware, backend, resolution, FOV/camera height,
distance, clumps/m2, triangles/clump, coverage and shadow settings. Count
candidate, resident, main-view submitted and shadow instances separately.
Report CPU generation/submission time, bytes uploaded, CPU/GPU memory, draw
calls, GPU main/shadow time and frame-time spikes during movement/painting.
Measure both ground-level and elevated railway views. A count without these
conditions is not a transferable budget; choose final presets from measurements.

The first prototype does not need GPU placement, compute/indirect drawing,
interactive trampling, a new terrain material format or globally persistent
clump actors. Merged meshes remain a possible measured special case for fixed
debris, not the assumed representation of all ground cover.

## Dependencies and references

- [Existing material library](../../features/terrain-material-library.md)
- [Existing procedural materials](terrain-procedural-materials.md)
- [Native shape LOD experiment](../shapes/10-gltf-native-lod.md)
- [Trees and impostor STF](../world/vegetation-objects.md)
- [Unreal Landscape Grass workflow](https://dev.epicgames.com/documentation/en-us/unreal-engine/grass-quick-start-in-unreal-engine)
- [Sucker Punch: procedural grass](https://www.gdcvault.com/play/1027033/Advanced-Graphics-Summit-Procedural-Grass)
- [AMD procedural grass example](https://gpuopen.com/learn/mesh_shaders/mesh_shaders-procedural_grass_rendering/)

These sources illustrate techniques, not TSRE performance guarantees. Prototype
scattering/bulk submission can proceed independently once its priority is chosen;
it does not require completing optional foliage backlighting.
