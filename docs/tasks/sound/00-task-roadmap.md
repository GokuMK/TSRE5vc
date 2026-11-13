# Sound Task Roadmap

This folder holds the tasks for TSRE's sound: playing MSTS sound content
(`.sms` definitions and their WAV files) for trains, world sound sources and
the environment, in the Route Editor's play mode and later in the game.

## Execution Order
- [ ] `01-sound-system.md` (on `feature/sound`: miniaudio instead of OpenAL,
  a RIFF WAV reader with loop points, MSTS trigger semantics, route sound
  sources, track sounds, train sounds from motion with a stand-in driver;
  next: environment sounds; engine and brake sounds wait for a train
  simulation)
- [ ] `02-train-sounds-from-models.md` (the user's, 2026-10-29, on
  `feature/driving`): a driven train's sounds from its vehicle models: the
  loco scripts' sound variables, each car's brake cylinder, engine RPM,
  tractive effort, the cab's sounds on the driven cab; steps 1-4 done; AI
  trains' sounds next (ideas in the doc)
- [ ] [`03-cab-windows.md`](03-cab-windows.md) (the user's, 2026-11-13):
  open cab windows let the outside in, at the window, growing as it opens,
  two never louder than outside: the ridden car's outside `.sms` and track
  sound through them, other outside sounds louder, MaSzyna's soundproofing
  toward its window column. Built on `feature/maszyna`; next: the closed
  cab's muffling, air rushing past the window (notes in the doc).
