# Task 05 - Signal Scripts (`sigscr.dat`)

Status: done (phases 1 to 4, 2026-10-16, branch `feature/simulation`).
Decisions taken are at the end.

Sources:
- `msts-analysis/reports/signalling/signal-scripting.md` (MSTS's and Open
  Rails' parsers and evaluators; Lua decided for new TSRE scripts, legacy
  `sigscr.dat` required);
- task 02 (`02-signals-and-track-authority.md`): the network, clearing,
  the built-in aspect rule this task replaces where a route has scripts;
- Open Rails (a behavioural reference, no code taken):
  `SignalScripts.cs` (reading), `SIGSCRfile.cs` (evaluation),
  `SignalObject`/`SignalHead` (`block_state`, `route_set`, `next_sig_lr`,
  `def_draw_state`, `dist_multi_sig_mr`);
- the local routes' scripts: EUROPE1/2, JAPAN1/2, USA1/2, BNSF_SCENIC, the
  global and template ones.

## Progress

**Phase 1, reading (done 2026-10-16).** `SignalScript`
(`src/tsre/sim`): the text without comments, sections from each line
beginning `SCRIPT` (`REM SCRIPT` skipped), a lexer, a recursive-descent
parser into a flat tree (names bound: locals, externals, functions,
constants), problems with file and line; a loop (`for`, `while`) reported
and passed over. `TrackNetwork` reads the files `sigcfg.dat` lists
(`SigCfg::path` kept: beside it, else in the route) and gives each type
its rule. Every local script reads without a problem.

**Phase 2, evaluating (done 2026-10-16).** The interpreter (integers,
truncating division, by zero zero, locals cleared, `state` and
`draw_state` kept, `&&`/`||` and arithmetic left to right of equal
precedence, both sides of a condition evaluated) and the functions on the
network: `enabled` (cleared), `block_state` (clear while cleared, else
obstructed), `route_set`, `next_sig_lr`/`mr` (the n-th: `next_nsig_lr`,
stopping at STOP), `this_sig_lr`/`mr` (none of NORMAL: CLEAR_2),
`opp_sig_lr`/`mr` (only while cleared), `dist_multi_sig_mr`,
`sig_feature`, `def_draw_state`, `debug_*`. `TrackNetwork::nextSignalWhere`
finds the next signal with heads of a function (or facing the other way).

**Phase 3, in the core (done 2026-10-16).** `evaluateAspects` runs a
head's script (its type's rule Script), Open Rails' choice (Basic: its
least restrictive aspect while cleared, else its most restrictive), or
task 02's rule. What a script reads beyond its signal's next stop signal
(kept as before) is recorded per head; the head runs again when one of
those signals' aspects, or a junction on its searches, changes. The
scripts' problems and the types without one go to the input's warnings
(the panel's status line, Simulate). Fixtures: the language (each
construct, each problem, Open Rails' order of evaluation), a line by
script (a number plate showing caution instead of stop, the homes before
it clear, a distant reading the homes up to the next distant), a type
without a script, a script with problems (the built-in rule, told), the
aspects following a train.

**Phase 4, compared (done 2026-10-16).** `scripts/orts-compare-aspects.py`:
the route suite's events (with `TSRE_SIM_EVENTS`) carry every 10 s each
running train's next signal and its aspect (`SimulationCore::
nextSignalAhead`); each Open Rails dispatcher sample (a train's next
signal aspect and distance) is paired with the same train's record for
the same signal within a minute. The same aspect, by script (built-in
rule):

| Route, activity | samples | scripts | built-in |
|---|---|---|---|
| EUROPE1 `aftstorm` | 651 | 99 % | 99 % |
| USA2 `itr_e1_s1_w1_t1` | 31 | 97 % | 94 % |
| BNSF_SCENIC `3_CanadianImports` (15 minutes) | 45 | 100 % | (1 paired) |
| EUROPE2 `itr_e1_s1_w1_t1` | 607 | 80 % | 80 % |
| JAPAN1, from 10:30 | 2739 | 69 % | 68 % |
| JAPAN2 `itr_e1_s1_w1_t1` | 228 | 62 % | 62 % |

On BNSF the built-in rule gave approach aspects (with their 40 mph)
where the scripts clear: its trains ran kilometres behind Open Rails'
(so few samples pair). Elsewhere the scripts and the built-in rule
choose the same: the stops against Open Rails are as before (EUROPE1
−14 s, EUROPE2 −25 s, JAPAN2 0 s, USA2 −27 s, JAPAN1 from 10:30 +2 s).

**Station exit signals (2026-10-16).** Most of the differences left were
task 02's hold of a station stop's exit signal (Open Rails' forced red)
taken where Open Rails does not take it, or kept when it lets it go. Now
as Open Rails (`Train.CreateStationStop`, `Signals` platform end
signals, `AITrain` station departures, `Train.CheckTrainWaitingForSignal`):
the exit signal only on the platform's track node; beyond 150 m a block
with any junction or crossing (not a facing junction only); a train
stopping beyond the signal held only when 60 % of it stands at the
platform stopped short of it (Open Rails' clearing distance: 15 m for
the player's train and trains of fewer than 10 vehicles); a signal held
once per stop sharing it, each taking it off two minutes before its
departure and, still listed, as the train leaves; a hold let go when the
train stands with the signal next and its present stop is not the
signal's (JAPAN2's Dan: let go standing at Sakamoto; EUROPE2's Hatting:
at Inzing, where the train stops past Inzing's own exit). The same
aspect now:

| Route, activity | samples | before | now |
|---|---|---|---|
| EUROPE1 `aftstorm` | 612 | 99 % | 100 % |
| USA1 `smmrex` (12:00 to 12:40) | 1998 | 98 % | 98 % |
| USA2 `itr_e1_s1_w1_t1` | 32 | 97 % | 94 % |
| BNSF_SCENIC `3_CanadianImports` | 142 | 99 % | 99 % |
| EUROPE2 `itr_e1_s1_w1_t1` | 545 | 81 % | 97 % |
| JAPAN1, from 10:30 | 2295 | 76 % | 77 % |
| JAPAN2 `itr_e1_s1_w1_t1` | 178 | 60 % | 87 % |

The stops against Open Rails: as before but USA2 (−27 s before, +47 s
now): its 1.2 km freight trains now have Belton's and Glacier Park's exit
signals held (their blocks hold a trailing junction), as Open Rails holds
them (STOP 3.4 km ahead braking for Belton, cleared standing), and pass
the approach signals before at their 40 mph as Open Rails' trains do
(Open Rails 7 min 17 s from signal 205 to standing at Belton, ours
7 min 12 s); the trains were already some 30 s behind Open Rails' before
that, which the missing holds had made up.

## Left

- **Clearing ahead:** Open Rails often shows a signal one step more
  restrictive than ours (JAPAN1's APP3 where ours shows CLR2, 251 samples;
  JAPAN2's player at signal 46): the signal after next not yet cleared in
  Open Rails where ours is. Both clear as many signals ahead (the type's
  `SignalNumClearAhead` less its NORMAL heads, passed on); Open Rails
  passes a request on only when the signal's whole block is reserved and
  within the train's route, from a signal's update. Not a distance limit:
  Open Rails asks for the next signal at any distance once the previous
  one is passed, as ours clears without one. To follow.
- **Station stops:** JAPAN1's Isehara and Kakio exits show STOP in Open
  Rails for its 200 m trains, which the rule above (on the platform alone)
  does not hold; probably Open Rails' stop counting other platforms of the
  same name along the route (a longer platform, the train stopping
  further back), not checked. Open Rails stops a short train 15 m short of
  a held signal, ours 31 m (our driver stops SignalMargin short of any
  signal at stop). Open Rails' check of a platform near a reversal reads
  the train's first reversal (its own quirk), not taken.
- **Open Rails' additions** (approach control, call-on, timing triggers,
  stored values, clear-ahead control, subtypes): read and reported, run
  in a later task (decision 3). `block_state` OCCUPIED (a train asking a
  signal whose block another train occupies: call-on) with them.

## Why

Task 02 gives each head an aspect from a built-in rule (the stop / caution
/ clear progression of the type's aspects). Each route's `sigscr.dat`
defines its own: which aspect follows which, plates and features (BNSF's
number and gradient plates choose STOP_AND_PROCEED or RESTRICTING), heads
that read other heads of their signal, distants that read every home
signal up to the next distant. The rule was a stand-in; the user asked
(2026-10-16) whether it is right to have it hardcoded: it is not, where a
route has scripts.

## Current state

- `SigCfg` (`SimisTextReader`) reads `ScriptFiles` and Open Rails'
  `ORTSScript` per type; nothing reads the scripts.
- The scripts are plain 8-bit text in a small C-like language (not SIMIS):
  `SCRIPT name` sections (`REM SCRIPT` ones skipped), `extern float` names
  of the externals and functions used, `float` locals, assignments,
  `if`/`else`, blocks, `||`, `&&`, `!`, comparisons with and without `#`
  (`==#`, `!=#`), `+`, the constants `SIGASP_*`, `SIGFN_*`, `BLOCK_*`,
  `SIGFEAT_*`. A script belongs to the type of its name.
- The local routes call only MSTS's own functions: `block_state`,
  `route_set`, `next_sig_lr`, `def_draw_state`, `sig_feature`,
  `dist_multi_sig_mr` (EUROPE1/2's distants), `this_sig_lr` (BNSF),
  `debug_header`/`debug_out` (a `REM`'d test). None of Open Rails'
  additions (approach control, call-on, timing triggers, stored values,
  clear-ahead control, subtypes: about 35 functions).
- Open Rails: uppercases the text; integer locals and arithmetic (division
  truncates, by zero gives zero); locals cleared for each evaluation,
  `state` and `draw_state` kept from the last; a head whose type has no
  script shows its least restrictive aspect when its block is clear, else
  its most restrictive (clear or stop only); script errors are warnings,
  some repaired (a missing `)`), an unknown function reported.

## Design

### 1. Reading

Our own lexer and recursive-descent parser for the language
(`src/tsre/sim/SignalScript*`): case-insensitive; `//` and `/* */`
comments; `SCRIPT` sections, `REM SCRIPT` skipped; declarations; the
statements and expressions above with C's precedence (`!`, `* / %`,
`+ -`, comparisons, `&&`, `||`); a `#` comparison as the plain one (Open
Rails' integers make them the same). Each script becomes a small tree,
names bound once: externals, functions, constants, local slots.
Problems with file and line ("`sigscr.dat` line 47: unknown function
`approach_control_speed`"). The files are those `sigcfg.dat` lists, read
beside it (the route's, or its `OpenRails` folder's as task 02 takes
`sigcfg.dat`); a type's script is its `ORTSScript`, else its name.

### 2. Evaluating

As Open Rails' interpreter: integers, truncating division, by zero zero;
locals cleared each time; `state` and `draw_state` written by the script
and kept between evaluations; a `draw_state` the script leaves unset
follows `def_draw_state(state)`. Functions, on the network task 02 made:
- `enabled`: the signal is cleared for a train;
- `block_state()`: BLOCK_CLEAR while its block (to the next stop signal,
  over the switches as set) holds no train or consist other than one it
  is cleared for, BLOCK_OCCUPIED when it does, BLOCK_JN_OBSTRUCTED when a
  junction in it is held for another way;
- `route_set()`: a junction-linked head's branch is the way set (as now);
  true for a head without a link;
- `next_sig_lr(fn)`, `next_sig_mr(fn)`: the next signal ahead with heads
  of function `fn`, its least (most) restrictive of them (STOP when none);
  `next_nsig_lr(fn, n)` the n-th; `this_sig_lr/mr(fn)` this signal's
  other heads; `opp_sig_lr/mr(fn)` the signal facing the other way at the
  same place; `dist_multi_sig_mr(fn1, fn2)` the most restrictive `fn1`
  head up to the next signal with an `fn2` head;
- `sig_feature(feat)`: the head's flags (number and gradient plates,
  USER1 to USER4); `def_draw_state(aspect)`: the type's draw state for it;
  `debug_header`, `debug_out`: nothing.

### 3. In the core

`evaluateAspects` runs a head's script where its type has one, task 02's
choice otherwise (decision 1). The incremental evaluation stays: each
evaluation records what it read (the signals and heads, the junctions on
its searches, the sections of `block_state`, its own signal's clearing)
and the head is queued again when one of them changes; a ring of signals
is cut off as now. Scripts are parsed once per input (with the network,
immutable, shared by every run).

### 4. In the editor

Script problems (file, line, what) go to the input's warnings: the
Timetable window's Simulate and the panel's simulation show them as they
show others. Heads whose type has no script are counted in the same list.
No script editor in this task.

## Phases

1. **Reading:** lexer, parser, binding, problems; every local script
   read without one (but the `REM`'d test); fixtures for each construct
   and each problem.
2. **Evaluating:** the interpreter and the functions, fixtures on
   hand-made layouts (a three-aspect line, plates, a junction-linked pair
   of heads, a distant reading several homes, `this_sig_lr`).
3. **In the core:** scripts in `evaluateAspects`, the dependencies, the
   fallback, the warnings; the scaling suite (incremental aspects) and the
   route suites as before.
4. **Compared:** the routes against Open Rails again (USA2 and BNSF_SCENIC
   for their plates and advance approaches, EUROPE1/2 for their
   distants); the aspects shown in 3D and on the map.

## Decisions (user, 2026-10-16)

1. Open Rails' parity where `sigscr.dat` exists: a type without a script
   shows Open Rails' choice (clear while cleared, else stop); task 02's
   built-in rule where there is no `sigscr.dat`, or issues prevent using
   it (a script with problems).
2. A script with problems is not run and the problem is reported.
3. Open Rails' additions: read, implemented later.
4. This task now.
