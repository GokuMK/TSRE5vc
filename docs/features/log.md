# The log (`log.txt`)

TSRE writes everything it logs to `log.txt` in its folder (beside the
program; a build in `build/` writes to the folder above it). The file is
emptied at every start: after a run that went wrong, copy it before starting
TSRE again. With `core.system.consoleOutput` on (and in test runs) the same
lines go to the console too.

## Lines

Each line starts with the time it was logged and its kind:

```
13:26:31.510 [I] TSRE5vc v0.7.8-build.11, started 2026-11-05 13:26:31 CET
13:26:31.553 [I] QRhi renderer: "QRhi Vulkan: NVIDIA GeForce RTX 3060 Ti, wide lines yes"
13:41:02.118 [E] GPU resource not created: instance arena (65536 KB); the memory lines count its failures
```

- `[I]` information, `[E]` a warning or an error, `[!]` critical, `[X]` fatal
  (TSRE stops).
- The first line has the version and the date. The time is the computer's
  clock, so a line can be matched against a video of the session.

## The memory line

From the 3D view, at its first frame and then once a minute:

```
14:05:31.207 [I] Memory after 15 min: GPU memory 6890 MB used, 7790 MB allowed, 8192 MB in all; shared 412 MB used, 8150 MB allowed, 16300 MB in all | QRhi used 5120 unused 1650 MB in 31 blocks | textures 3410 of 4915 MB budget, meshes 425 MB (3 MB waiting) | process 6100 MB private, 5900 MB working set (peak 6300) | system 12100 of 16300 MB, commit 21000 of 28000 MB | shapes 4552, world tiles 85, terrain tiles 120 | not created: none
```

(The numbers are made up.)

- **GPU memory**: the GPU's own memory, as its driver tells it (Vulkan's
  `VK_EXT_memory_budget`). "Used" is TSRE's share. "Allowed" is how much
  the system lets TSRE have before allocations fail, or Windows moves them
  out to the shared memory. "In all" is the size. **Shared** is the same
  for the RAM the GPU can reach (Windows' "shared GPU memory"). Without the
  extension only the sizes show. On lavapipe (the CPU's Vulkan) "used" is
  the whole system's RAM use. There is no GPU part under the OpenGL renderer.
- **QRhi**: the memory of QRhi's allocations, and what its blocks hold
  unused (Vulkan; the blocks stay while any allocation in them lives).
- **textures**: what the texture library holds (their GPU memory, and the
  pixels of textures waiting for their first draw) against the texture
  memory budget (`core.rendering.textureMemoryBudget`, see
  [textures-and-texlib.md](textures-and-texlib.md)); `(lowered from N)`
  while the GPU's memory runs short. **meshes**: the
  meshes' GPU buffers, and the data of meshes waiting for their first draw.
- **process**: the process's private memory and working set. **system**: the
  RAM in use of the RAM installed, and on Windows the commit charge of its
  limit (RAM plus page file).
- **shapes**, **world tiles**, **terrain tiles**: what is loaded.
- **not created**: the GPU resources TSRE could not create while running
  (mesh chunks and buffers, textures, the instance and uniform arenas, the
  UI's geometry and textures), each kind with how many times. The first
  failure of each kind gets a line of its own (`GPU resource not created`),
  with its size. Qt adds its own lines, such as `Failed to create buffer of
  size` or `Out of device memory`.

`TSRE_MEMORY_LOG=<seconds>` changes the interval, and `TSRE_MEMORY_LOG=0`
turns the line off.

## Other lines worth knowing

- `Texture memory budget N MB: …`: the budget and where it comes from, once.
  While the budget holds textures back, `Texture memory budget N MB: K
  textures released (… MB), now … MB` appears at most every 10 seconds.
- `Texture memory budget N MB (the setting's S MB): the GPU has R MB left of
  A MB allowed`: the budget lowered because the GPU's memory runs short
  (other programs, or TSRE's meshes and buffers); again when it changes by
  a tenth or more (at most every 10 seconds), and `Texture memory budget S
  MB again: …` when there is room again. `Texture memory budget N MB for a
  minute: a GPU resource could not be created` follows a failed create
  when the driver gives no figures.
- `[ui] not enough memory`: the UI's Lua ran out of its 64 MB
  (docs/tasks/scripting/04-ui-lua-memory.md).
- A click in the 3D view logs the selection id it read (`[I] <id>`, then
  `selector N`). 0 means nothing selectable was drawn there.

## What a click hits that has no id (`TSRE_SELECT_DEBUG=1`)

Some things are drawn in the selection pass without an id: the sky, the
sun and the moon, helper geometry, anything a producer submits without one.
A click on them reads 0, the same as empty space. Under
`TSRE_SELECT_DEBUG=1` each such draw gets an id of its own (selector 31,
numbered in the pass). The click still selects nothing, but it logs what
was drawn there:

```
[select-debug] draw 1 of 3 in the selection pass: other, sky pass, 1 instance(s) from (450.0, 292.0, -440.0) m, axes 1 1 1; untextured; mesh 811/1, float vertices of 19 floats, 5472 vertex bytes in 5632 bytes at 8063744 of vertex chunk 1; draws vertices 0 to 72, within its data
```

(The sun or the moon, at the camera, over Katowice on OR_PSK_NEW's PRUDNIK:
in a view there, those three were all the selection pass drew without an
id.)

- The category and pass say which part of the scene drew it.
- The place is the first instance's origin in the camera tile's frame; the
  axes are its scale. A huge, tiny or `NOT FINITE` transform shows here.
- The mesh: its slot and generation, its vertex format, and where its data
  lies (a part of a shared chunk, or a buffer of its own). `PAST ITS
  STORAGE` / `PAST IT` means the draw reads beyond its own part, into
  another mesh's data (task renderer 42's junk geometry). A draw's highest
  vertex index is not checked: the indices are not kept after upload.

The selection pass draws exactly what it draws without the switch; only
those ids change. It was made for task renderer 42 (junk polygons that a
click did not select): set it before starting TSRE, click the junk, and
send `log.txt`.

The frame log (`core.system.frameLog`) is another file: a CSV line a frame,
beside `log.txt`.
