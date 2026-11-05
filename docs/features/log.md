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
  minute: a GPU resource could not be created` follows a failed create.
- `[ui] not enough memory`: the UI's Lua ran out of its 64 MB
  (docs/tasks/scripting/04-ui-lua-memory.md).

The frame log (`core.system.frameLog`) is another file: a CSV line a frame,
beside `log.txt`.
