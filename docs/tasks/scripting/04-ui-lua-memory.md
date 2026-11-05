# Task 04 - The UI's Lua memory on long drives

Status (2026-11-05): cause found, fixed on `fix/ui-lua-memory`
(`../TSRE5vc-uilua`), not merged.

Task 01's leak fix (807c7228, in build.10) ended the abort ("Out of memory
while driving", 2026-10-31). Build.10 still fills the UI's Lua state on a
long drive, without a leak.

## The report (a build.10 log, the user, 2026-11-05)

`~/NetBeansProjects/TSRE5_feedback/issues/nvidia/log_PSK EIP Waw-BB (1).txt`
(not in the repo): RTX 3060 Ti, PRUDNIK, the reporter's own activity
`m61_Tow_JSB_B-Myszkow.act`, a long drive with the HUD and the loco panel.
- Near Katowice Zawodzie four `loco.lua … ran longer than its limit of 100
  ms`, then 5103 `[ui] not enough memory` until the user quit: the
  frames fell to 5 fps (about 26 minutes in, the ride going on for 15
  more).
- Nothing else in the log fails, so the process had memory: the state's
  own 64 MB limit refused it.

## What happens

No leak. Through a whole drive the state holds about 150 KB live (D2
below: a full collection every minute, 151 KB each time, no table
growing). Lua's incremental collector, paced by the bytes allocated,
falls behind:
- each frame hands Lua short-lived userdata with finalizers: `loco.lua`'s
  `GetElementById` and `.style` proxies (about 50 a frame), and for each
  model message an event, its parameters and the document;
- a finalized userdata lives through its cycle and goes in the next, and
  the next cycle starts when the heap reaches 250 % of what the last one
  marked: many small finalized objects raise that mark, cycle after
  cycle;
- a slow frame takes more ticks' messages (`sound` triggers too, a tick
  every 10 ms), so a heavy place feeds it; near the limit the collector's
  work slows the frames further.

## Measured

Release, the `rmlui` suite's soak (the loco panel and the HUD, 60 values,
one core, no forced collections):

| Load | incremental (Lua's 250 / 200) | incremental 100 / 400 | generational |
|---|---|---|---|
| 50 `sound` messages a frame, 4000 frames | 17.5 MB, 734 µs a frame | 232 KB, 734 µs | 382 KB, 709 µs |
| 1 message a frame, 20,000 frames | 1.1 MB, 596 µs | 199 KB, 665 µs | 297 KB, 603 µs |

```bash
QT_QPA_PLATFORM=offscreen TSRE_SOUND_DEVICE=none TSRE_UI_SOAK_FRAMES=4000 \
  TSRE_UI_SOAK_MESSAGES=50 TSRE_UI_SOAK_MESSAGE_NAME=sound TSRE_UI_SOAK_NOGC=1 \
  TSRE_UI_SOAK_NONET=1 TSRE_UI_GC=inc ./build/TSRE5vc --test --test-suite rmlui
```

Real drives on lavapipe (`tests/renderer/ui-lua-drive.json`: EC130 driven
from Katowice, the panel and the HUD, 16 minutes in real time;
`TSRE_UI_MEMORY_LOG=60`, the lighter settings in its notes). The driven
EC130 sent 10 messages in 16 minutes.
- D (32 minutes) and D3 (16), the incremental collector: from 1.8 to
  6.3 MB, the lows after each cycle rising.
- D2, a full collection every minute (`TSRE_UI_MEMORY_CENSUS`): 151 KB
  live, the registry (33 entries) and the globals (67) unchanged.
- D4, the dropped discarding (below): crept as D3.
- D5, the fix (generational, no switches): 159 to 221 KB through the 16
  minutes, the backstop never reached.

## The fix (on `fix/ui-lua-memory`)

- The UI's state collects generationally (`Runtime::start`,
  `src/tsre/ui/UiRuntime.cpp`); `TSRE_UI_GC=inc` gives back the
  incremental collector, to compare. (The trials' incremental parameters,
  100 / 400 in the table, were set through switches since dropped.)
- A backstop: two full collections once the state holds 16 MB
  (`Runtime::collectIfGrown`, after the frame's documents). One
  collection is not enough: 8.2 MB went to 3.5 MB in 6.7 ms, the finalized
  userdata left for the next; 23.8 MB took 37.5 ms.
- Every message still goes to the panel.

Diagnostics with it, off by default:
- `TSRE_UI_MEMORY_LOG=<s>`: every so many seconds the state's bytes, its
  main stack, the registry's and the globals' entries, the loco messages
  by name, and each backstop collection; `TSRE_UI_MEMORY_CENSUS` adds the
  live set after full collections and the largest tables.
- The `rmlui` soak: `TSRE_UI_SOAK_FRAMES`, `TSRE_UI_SOAK_MESSAGES`,
  `TSRE_UI_SOAK_MESSAGE_NAME`, `TSRE_UI_SOAK_NOGC` (no collections at its
  checkpoints), `TSRE_UI_SOAK_NONET` (no backstop).

Check, `rmlui` suite: 2000 frames of 50 messages leave the state within
4 MB with no full collection.

## Tried and dropped: discarding messages

The first fix gave the panel only the last 8 messages a frame and no
`sound` (the C++ side plays the sounds; `loco.lua` drops them). Dropped
(the user, 2026-11-05):
- it breaks the documented contract
  (`docs/features/simulation-messages.md`: "every sent message reaches
  the UI as a `trainmessage` event", one "for each message since the last
  paint");
- a document that takes messages as one-off events (an alarm, an ATP
  intervention, a door event, a message log, a cab display) would miss
  some exactly when frames are slow: hard to notice, hard to reproduce;
- it did nothing for a drive without messages (D3: 10 messages in 16
  minutes, the heap still crept up); the collector did.

## Later: the messaging system (options, not started)

Ways for the models' messages to reach the UI more cheaply, every one
kept; the user's call.
1. **A frame's messages as one list.** `train.messages`, set with
   `train.values` before `trainupdate`: every message in order, a table
   entry each instead of an event with its userdata. `loco.lua` reads it
   in `locoUpdate`; `trainmessage` goes, or stays for documents that use
   it. Changes the documented API: the doc, `loco.lua`, the `rmlui` suite.
2. **Sound triggers apart.** `sound` messages are SMS trigger numbers for
   the cars' sounds, of no use to the UI today. A list of their own keeps
   them out of the per-message cost, and a document that wants them still
   reads them.

Related, not messaging: `loco.lua`'s lookups. Its `GetElementById` calls
(about 50 a frame) make most of the userdata the collector chases.
Elements kept from `build` (they live as long as the document) would make
none; a kept element must not outlive its document.
