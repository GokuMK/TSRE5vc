# Task 01 - Making humans

Status: design, 2026-11-11. Nothing installed or built.

## Goal
A repeatable way from MPFB in Blender to a glTF file that TSRE can draw and
animate. Tools installed, one skeleton chosen for every human, and
example humans (a man, a woman, a young person) with idle and walk clips.
Loading them into TSRE is task 02.

Decided (the user, 2026-11-11):
- The generator is a separate project, `humans-gen`, beside `tree-gen` and
  `assets-gen`. Assets are made on this machine, the only place where
  Blender and MPFB get installed.
- Humans belong to routes: shapes in `SHAPES`, textures in `TEXTURES`, one
  shared clips file in `SHAPES`, the list in the route's `humans.dat`
  (roadmap, "Where humans live"). TSRE's own humans go into TSRE's default
  route assets (task assets 03).

## Tools

| Tool | What for | Licence |
|---|---|---|
| Blender 4.2 or newer | Modelling, rigging, retargeting, export. Scripts run headless (`blender --background --python`). Not installed on this box yet (Arch package `blender`). | GPL (output is ours) |
| MPFB 2 | MakeHuman's human generator as a Blender extension (extensions.blender.org) | Code GPLv3; its bundled assets CC0 |
| MPFB asset packs | Skins, eyes, hair, clothes. The system assets are CC0. Community packs carry their own licences (some CC-BY), checked one by one. | Per pack |
| Khronos glTF Validator | Checking every export | Apache 2.0 |

Retargeting with Blender's own constraints and a bake, from our script. No
paid add-on.

## Clip sources

| Source | Licence | Fit |
|---|---|---|
| Quaternius Universal Animation Library 1 and 2 | CC0 | 120+ and 130+ clips: walking in 8 directions, idles, sitting and more. Its own humanoid rig, so retargeting is needed. The free tier has most clips (about 70% of the second library). The paid Source tier adds the `.blend` files. |
| CMU Graphics Lab motion capture | Free for any use | MPFB has a rig made for it ("CMU MB"), so no retargeting. Raw capture, though: clips need trimming and looping. |
| Mixamo | Free to use in your own projects; its files may not be passed on | Not for files TSRE ships. Users may use it for their own humans. |
| Our own | Ours | Keyed in Blender, for what the others lack (stepping into a train). |

Proposal: Quaternius first (idle, walk), CMU for what is missing.

## One skeleton for everyone
Every TSRE human uses the same rig: the same joints, with the same names
and the same hierarchy. Then:

- one clip plays on every human,
- task 03's crowd shares sampled poses between people,
- player humans later use the same clips.

Bodies still differ in height and proportions through the bone lengths of
their rest pose. Clips carry rotations, plus the hips' movement, which is
scaled by leg length.

MPFB's rigs:

- **default**: has face bones, too many for a crowd,
- **default no toes**,
- **game engine**,
- **CMU MB**.

Step 2 compares them: joint counts, names, how each exports to glTF, and
how well Quaternius' clips retarget onto it. Target: about 60 joints at
most on the most detailed level.

## The example humans
The user, 2026-11-12: Polish or European average passengers. For the first
try a man, a woman and a young person, to check how one skeleton's clips
fit different heights:

| Human | Age | Height (about) |
|---|---|---|
| Man | adult, 35-45 | 178-180 cm (Polish average) |
| Woman | adult, 35-45 | 165 cm (Polish average) |
| Young | 11-12 | 145-150 cm |

Each of them:
- Everyday clothes for a Polish train, from the CC0 assets.
- Body: one of MPFB's proxies (lower-poly body meshes; which ones exist is
  checked in step 1), or the base mesh with the parts hidden under clothes
  deleted.
- Body, clothes, hair and eyes merged into **one mesh with one material**,
  their textures baked into one atlas (base colour, normal; ORM if it
  helps). That gives one draw per level.
- **Recolour regions** for task 03: a mask marking top, bottom, shoes and
  hair, so TSRE can tint each person differently. The mask goes in a vertex
  colour or in a free texture channel; decided in step 3 with task 03.
- **Levels in one file** with `MSFT_lod`, which TSRE already reads (task
  shapes 10). Starting targets, checked by eye in task 02: level 0 about
  8-10k triangles, level 1 2-3k, level 2 500-800.
- At most 4 joints per vertex (glTF's `JOINTS_0` and `WEIGHTS_0`), weights
  normalised.
- Metres, +Y up, facing glTF's front (+Z). The direction TSRE treats as
  forward when walking is recorded.

## Clips
First: `idle` (standing) and `walk` (normal pace, about 1.3-1.4 m/s).
Next: idle variants (phone, watch, bag), a slow walk, turning on the spot.
Start and stop come later; crossfades cover them first.

- **Loops**: the last frame matches the first. 30 frames per second.
- **Forward motion** (proposal): walk clips keep the hips moving forward in
  the file. TSRE measures the clip's speed at load (distance over the loop),
  plays it at the person's speed divided by the clip's speed, and removes
  the forward motion when sampling. Nothing extra is stored. ozz 0.16 has a
  tool for exactly this split (motion extraction).
- **Names**: glTF animation names in lower case, such as `idle`, `walk` and
  `idle_phone`. The crowd picks clips by name, and variants by suffix.
- **One shared clips file** (decided): a `.glb` with the skeleton and the
  animations and no mesh. Humans find their clips in it by joint names, so
  a new clip needs no new export of every human. A human's own file may
  still carry clips of its own.

## Export
Blender's glTF exporter:

- `.glb`, +Y up, modifiers applied,
- skins and animations, each action becoming one glTF animation,
- animations sampled at 30 fps, keys reduced,
- no shape keys,
- PBR materials, textures inside the `.glb`.

Every export is checked with the Khronos validator, then imported back into
Blender and compared.

## The generator
Scripts that make the files from recorded settings, so every human can be
made again:

- MPFB's values (sex, age, weight, height, proportions; clothes, hair,
  skin),
- the rig,
- the levels and the atlas bake,
- the clips' retargeting,
- the export.

They run in Blender without a window. MPFB's Python services should allow
this (checked in step 1). If they don't, the script loads MPFB's saved
human files (`.mhm`).

They live in `humans-gen` (decided). Its README lists every input and its
licence. Only CC0 inputs go into files TSRE ships; a route designer's own
humans follow the designer's rules.

## Steps
1. Install Blender, MPFB and the CC0 asset pack on this machine. Record
   the versions. Check MPFB's scripting from Blender without a window.
2. Choose the rig: export each candidate with one Quaternius clip
   retargeted onto it, pick one, record its joint list.
3. The example humans: body, clothes, merge, atlas, levels, recolour mask,
   export, validate.
4. Clips: `idle` and `walk` retargeted by script, loops checked, speed
   measured, written to the shared clips file.
5. Steps 3 and 4 as scripts that make the same files again.
6. Hand over to task 02: the files, the joint list, the clip names, and
   Blender renders from the front and side to compare with TSRE's.

## Acceptance
- The validator reports no errors.
- Blender plays `idle` and `walk` correctly after importing the export.
- The scripts rebuild the files.
- The licence of every input is recorded.

## Questions
None open.
