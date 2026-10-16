# Task 05 - Signal Scripts (`sigscr.dat`)

Status: designed 2026-10-16; decisions open (at the end).

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

## Decisions for the user

1. **A type without a script** (on a route with `sigscr.dat`): Open
   Rails' basic behaviour (clear when its block is clear, else stop) for
   parity, or task 02's built-in progression? Recommended: Open Rails'
   (the route author left it so, and Open Rails shows it so); the built-in
   rule then stays only for routes without any `sigscr.dat`.
2. **A script with problems** (a parse error, a function we do not
   have): its type treated as having no script (decision 1) and the
   problem reported? Recommended: partial evaluation could show wrong
   aspects silently.
3. **Open Rails' additions** (approach control, call-on, timing triggers,
   stored values, clear-ahead control, subtypes): none of the local routes
   uses them, and several need train data the core does not pass to
   signals yet (speeds, positions, call-on requests). Recommended: read
   and report them now (decision 2), implement them in a later task.
4. **Order:** this task now, before the Open Rails additions that need
   world data and the player's driving? Recommended (the user asked for
   it, 2026-10-16).
