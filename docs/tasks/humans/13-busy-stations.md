# 13 - Busy stations

Status: being built on `feature/humans` from 2026-11-20 (the user's go:
"1000 people limit cap, say 300 agents; review, design, do measurements
and implement").

## Goal
Up to 1000 people at a station (it was 250, a guess at Detour's cost; the
user asked for at least 500 in the design), the steering's cost bounded by
how many walk at once: at most 300 walking, the rest waiting standing
until a walker's place frees (so 1000 waiting for one train do not all
walk at once: the doors take about 10 a second anyway, the rest wait as
crowds do).

## What is there (the review)
- **Every platform person owns a Detour agent** of its station's crowd
  (`Place::crowd`, `dtCrowd::init(MostAtStation)`): waiting people get an
  idle one when made; alighting ones one when their door script ends;
  boarding ones give theirs back at the door (the door script walks
  them). `moveTo` sets a target (taking an agent if it has none:
  silently nothing when the crowd is full).
- **Detour steps every agent each frame** (up to 5 steps of 0.1 s when
  frames are slow), standing ones too: its obstacle avoidance samples for
  every agent with the flag, a target or not (`DetourCrowd.cpp` 1265).
- **The cap** (`MostAtStation` 250, `KeptForAlighting` 40) counts people:
  waiting ones stop at 210, alighting ones at 250; the player's character
  takes an agent of the same crowd.
- **A person's pose** is its own (`pos`, `way`, `speed`, gait or a clip):
  the drawing crowd idles one whose speed is 0, agent or not.
- **Measured** (`people-steering-benchmark`, a made-up 300 m platform,
  StationPeople's agent settings, Release, this machine's CPU, a step's
  mean of 200):

  | People | ms a step |
  |---|---|
  | 250 standing, steering flags (today) | 0.45 |
  | 250 standing, no flags | 0.12 |
  | 1000 standing, steering flags | 2.31 |
  | 1000 standing, no flags | 0.92 |
  | 1000 standing, no flags, 0.6 m reach | 0.41 |
  | 300 walking alone | 0.93 |
  | 300 walking, 700 standing with flags | 3.64 |
  | 300 walking, 700 standing, no flags | 2.27 |
  | 300 walking, 700 standing, no flags, 0.6 m reach | 1.90 |
  | 1000 walking | 4.76 |
  | Raszowa's density (135 m, 4.5 m across): 900 standing, flags | 2.62 |
  | the same: 300 walking, 600 standing, no flags, 0.6 m | 2.26 |
  | the same: 300 walking, 600 standing with flags | 4.14 |
  | the same: 900 walking | 5.30 |

## Design
- **Everyone keeps an agent; only walkers steer.** A standing person's
  agent has no steering flags and looks 0.6 m round it (0.41 ms for 1000):
  walkers still go round it and nudge it (Detour's collisions), as now.
  Agents only for walkers (the roadmap's first idea) would cost about 1 ms
  less at the worst, but walkers would pass through those standing.
- **Walkers capped:** a station's walkers (an agent with a move target)
  at most `MostWalking` (300). A move asked beyond that waits (its target,
  filter and pace kept; the person stands, idle, in its new state); each
  frame free places go to those waiting by priority:
  1. getting off (out of their door script, on their way off);
  2. to a door (boarding: the nearest at its door first);
  3. to a place by a door (a door's fan), nearest first;
  4. to the stopping train's stretch (approach), nearest first;
  5. leaving (a train gone, a group done);
  wandering about only while 40 places are free (never waits).
- **The cap of people:** `MostAtStation` 1000, `KeptForAlighting` 100;
  the crowd made for 1000 and the player's agent.
- **Switches** (developer options, read once): `TSRE_PEOPLE_MOST` (people
  a station), `TSRE_PEOPLE_WALKING` (walkers a station).
- **Measured live:** the people's log (`TSRE_PEOPLE_LOG=1`) adds walkers,
  those waiting to walk and the steering's time (Detour's steps alone).
- **Later:** the people's step on a worker between frames
  ([06](06-boarding.md), Later).
- **Measuring here:** live runs time the people with the software
  renderer on the same cores (and its slow frames step people at most
  0.5 s a frame, so a run at real pace leaves them behind): the headless
  benchmark gives the steering's cost; live runs here only the behaviour,
  the run slowed. A null renderer (QRhi's Null backend as a backend
  choice) would let live runs time the CPU alone: not now (the user,
  2026-11-20).

## As built
- `StationPeople.cpp`: `mostAtStation()` (1000, `TSRE_PEOPLE_MOST`),
  `keptForAlighting()` (a tenth, 4 to 100), `mostWalking()` (300,
  `TSRE_PEOPLE_WALKING`), `StrollRoom` 40; each station's crowd made for
  its most and the player's character.
- **Agents:** `agentParams(p, filter, walking)`: standing, no steering
  flags and a 0.6 m reach; walking, StationPeople's flags and 2 m.
  `addAgent` makes a standing one; `startMove` sets the walking ones and
  the target (one more walker of its station); `stopMove` (there, stuck,
  sent back, no spot) the standing ones; `dropAgent` counts its walker
  off.
- **Waiting to walk:** `moveTo` keeps a move asked while its station has
  `mostWalking()` walkers (`Person::pending`, its target, polygon and
  filter); the person stands in its new state. Those getting off (Leaving
  without a group) and those going in at a door (ToDoor) never wait.
  `dispatchWalkers` each step (before the crowds move): walkers counted,
  free places given by rank (getting off, to a door, to a door's place,
  to the train's stretch, the rest), the nearest first. A stroll about
  only while 40 places are free.
- **Figures:** `Stats::walkingAgents`, `waitingToWalk`, `steerMs`
  (Detour's steps alone); `TSRE_PEOPLE_LOG=1` logs them each second with
  the people's update.
- **Benchmark:** `people-steering-benchmark` (the table above).

## Checks
- `people-steering-benchmark` (15): the table above; this machine's CPU,
  Release, nothing else running. At a busy station's density the design
  costs 2.3 ms a step at its busiest (300 walking among 600 standing);
  the old way with 900 people 2.6 standing, 4.1 with 300 walking; no cap
  5.3.
- **Live** (PRUDNIK, PSK_2026, Raszowa from 12:10:50 with
  `TSRE_PASSENGERS_SCALE=50`, the run at a tenth of real time so the
  people step with it, `TSRE_PEOPLE_LOG=1`): 900 people (the old code
  stopped at 210); 300 walking at most as the train came, up to 600
  waiting their turn (those to the doors' places first), standing idle;
  the one getting off at 12:11:18, boarding from then, 21 on by 12:11:49
  (the old code at real pace about 20 by 12:12:00). Detour 30 to 40 ms a
  frame there: three steps a frame (0.3 s), the software renderer on the
  same cores (the benchmark's 2.3 ms a step without it).
- A run at real pace here left the people behind (frames of seconds, at
  most 0.5 s a frame for them): nobody boarded before the train left;
  and a view's run plays before its camera moves (a first view running
  far from the station had nobody there).
