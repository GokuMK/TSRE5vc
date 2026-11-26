# Task 11 - Open Rails' Signal Script Functions

Status: built 2026-11-26 (the user's go the same day: "Ok lets do then
easy + moderate except this one that waits for task additional sigobj
data in tdb"), branch `feature/simulation`. Tested on synthetic tracks
only: no local route uses these functions (none of `~/ORTS`'s scripts
call them), so they stay implemented but untested on a real route.

The user's brief (2026-11-26): of the Open Rails functions TSRE read but
did not run (a script calling one was not used: Open Rails' choice or the
built-in rule instead, task 05's decision 3), run the easy and moderate
ones; `HASHEAD` waits for the TDB's signal-object data
(`tdb-signal-extension.md`, it needs the world's optional heads).

## What runs now

Signal idents are TSRE's own: a signal's index among the simulation's
signals (Open Rails uses its own list). Scripts only pass on what these
functions return, so the numbers never matter; -1 is none, as in Open
Rails.

| Function | What it gives |
|---|---|
| `NEXT_SIG_ID(fn)` | the next signal with heads of `fn`, as `next_sig_lr` finds it (a NORMAL one past the end of the route the signal is cleared into: none) |
| `NEXT_NSIG_ID(fn, n)` | the n-th; unlike `next_nsig_lr` it does not stop at a signal at STOP |
| `OPP_SIG_ID(fn)` | the first signal with `fn` heads facing the other way behind this one, over the track held for the train it is cleared for (to the start of the section its rear is in); -1 while not cleared |
| `OPP_SIG_ID_TRAINPATH(fn)` | the same back along that train's path to its part's start |
| `ID_SIG_ENABLED(id)` | 1 while that signal is cleared for a train |
| `ID_SIG_LR(id, fn)` | that signal's least restrictive `fn` head (none: CLEAR_2 for NORMAL, else STOP; no signal: STOP) |
| `DIST_MULTI_SIG_MR_OF_LR(fn1, fn2)` | as `dist_multi_sig_mr`, each signal's least restrictive `fn1` head counted |
| `STORE_LVAR(i, v)`, `THIS_SIG_LVAR(i)`, `NEXT_SIG_LVAR(fn, i)`, `ID_SIG_LVAR(id, i)` | a signal's whole-number variables, kept from run to run; its heads write them, any head reads them (0 unset) |
| `THIS/NEXT/ID_SIG_HASNORMALSUBTYPE(st)` | 1 when the signal has a NORMAL head of that `ORTSNormalSubtype`; scripts name subtypes as Open Rails does, `ORSUBTYPE_<name>` (its index in `ORTSNormalSubtypes`) |
| `THIS_SIG_NOUPDATE()` | the signal no longer runs again for what it reads, only when cleared or given back (and for its switchstand's switch) |
| `SWITCHSTAND(a0, a1)` | `a0` while the first facing switch beyond the signal is set to its first route (Open Rails' route 0), else `a1`; run again whenever that switch changes |
| `ACTIVATE_TIMING_TRIGGER()`, `CHECK_TIMING_TRIGGER(s)` | the signal's trigger set to now; 1 once it was set more than `s` whole seconds ago (before any, the run's start counts) |
| `INCREASE/DECREASE/SET/RESET_SIGNALNUMCLEARAHEAD(n)` | how many signals a train clears ahead from this one: the type's value plus n, less n, n, the type's; Open Rails mode only (`OPENRAILS/sigcfg.dat`, or two values) |
| `TRAIN_HAS_REQUEST_STOP()` | 1 when the train coming has its next stop at the platform beside the signal, a request stop someone gets on at (`$req /pu`, Open Rails' pickupSet) |
| `TRAIN_REQUIRES_NEXT_SIGNAL(id, p)` | 1 when the cleared train's path goes up to signal `id` (`p` 0) or on past it (1) |
| `FIND_REQ_NORMAL_SIGNAL(st)` | the first signal on the train's path beyond this one with a NORMAL head of that subtype |
| `ROUTE_CLEARED_TO_SIGNAL(id)` | BLOCK_CLEAR when the track from this signal up to `id` is held for the train, else BLOCK_JN_OBSTRUCTED |
| `ROUTE_CLEARED_TO_SIGNAL_CALLON(id)` | the same, BLOCK_OCCUPIED where the train may call on |
| `approach_control_req_position`, `_speed` | read-only variables: the type's `ApproachControlSettings` in whole metres and metres per second (to the nearest, halves to even, as Open Rails' `Convert.ToInt32`); -1 none |

Open Rails' route holds whole track sections (cut at NORMAL heads): a
path ending inside one reaches the signal ending it, so
`TRAIN_REQUIRES_NEXT_SIGNAL(id, 0)`, `FIND_REQ_NORMAL_SIGNAL` and
`ROUTE_CLEARED_TO_SIGNAL` count that signal too (as `next_sig_lr` has
since task 09).

## How it fits TSRE

TSRE runs a signal's script when what it read changes, Open Rails every
update. What each function reads is recorded so the script runs again:

- another signal's cleared state or local variables (`ID_SIG_ENABLED`,
  the `*_LVAR` ones): when that signal is cleared or given back, or a
  head stores a new value (not the head that stored it);
- a section's holder (`OPP_SIG_ID`: the section on this signal's side of
  the one found, given back as the train's rear leaves it;
  `ROUTE_CLEARED_TO_SIGNAL`: the first section not yet held;
  `TRAIN_HAS_REQUEST_STOP` with no train: the signal's own);
- time: `CHECK_TIMING_TRIGGER` told no runs again when it will be yes;
  `TRAIN_HAS_REQUEST_STOP` with a train coming runs each step (its next
  stop changes), as approach control does (task 10);
- a switch (`SWITCHSTAND`, also for a `THIS_SIG_NOUPDATE` signal: Open
  Rails' direct link).

The clear-ahead count a script sets is read each time the train extends
its route; signals already cleared stay cleared (Open Rails too).

## Where TSRE differs from Open Rails

- `OPP_SIG_ID_TRAINPATH(SIGFN_NORMAL)`: Open Rails tests for a signal
  facing the other way at a section's end but returns the one facing the
  train's way there (`EndSignals[direction]`, a slip; for the signal's own
  section that is the signal itself). TSRE returns the one facing the
  other way, as the function is described.
- `opp_sig_lr`/`opp_sig_mr` (task 05) looked for the signal facing the
  other way beyond this one; Open Rails looks behind it, over the track
  held for the train (`SONextSignalOpp`). They now share `OPP_SIG_ID`'s
  search. No local route calls them.
- `ROUTE_CLEARED_TO_SIGNAL_CALLON`: Open Rails' call-on test
  (`Train.TestCallOn`: timetable `$callon` at a platform, `$attach`,
  `$pickup`, `$transfer`, `$stable`, pools; in activities, not into a
  platform) is the call-on family's, not run yet. Until then the call-on
  is TSRE's own (task 08, phase 9): toward the train it attaches to or
  picks up.
- `TRAIN_HAS_REQUEST_STOP` is meant for a signal without NORMAL heads
  (Open Rails' manual). Open Rails enables such signals as a train
  reserves their track; TSRE never clears them for a train, so this
  function takes the train holding the track the signal stands on whose
  way passes it. `enabled` itself is unchanged for such heads (see Left).
- An unknown `ORSUBTYPE_` name is a problem (the script is not used, the
  user told), as any unknown name in TSRE; Open Rails warns and takes 0.
- `TRAIN_HAS_REQUEST_STOP` takes any number of arguments (Open Rails'
  manual calls it with none, its code reads two it does not use); the
  others take Open Rails' counts.

## Still not run

`HASHEAD` (the TDB signal-object task), the call-on family
(`TRAINHASCALLON`, `_RESTRICTED`, `_ADVANCED`, `_RESTRICTED_ADVANCED`),
`ALLOW_CLEAR_TO_PARTIAL_ROUTE`, `APPROACH_CONTROL_LOCK_CLAIM`,
`APPROACH_CONTROL_NEXT_STOP` (approach control's route hold, task 10's
Left). A script calling one is still not used.

## Tests

`simulation-core`, synthetic only:

- reading and running: `ORSUBTYPE_` names, the read-only approach
  settings, the composed calls (`NEXT_SIG_LVAR`, `THIS_SIG_HAS...`), the
  problems (an unknown subtype, setting a read-only variable, a wrong
  argument count, `HASHEAD` still not run);
- a 2 km line in Open Rails mode (signals at 500, 1000 (SHUNT), 1500 and
  1700 m, one facing the other way at 300 m, a request stop's signal at
  700 m, two distants, a switchstand before a junction, the train taking
  the second branch), the scripts storing each function's answer in local
  variables (`SimulationCore::scriptLocal`): the idents, subtypes,
  approach settings, `OPP_SIG_ID` dropping the opposite signal once the
  train's rear leaves its section while `_TRAINPATH` keeps it,
  `ROUTE_CLEARED_TO_SIGNAL` turning clear as the route is set, the timing
  trigger answering yes after 30 s, `THIS_SIG_NOUPDATE` keeping a
  distant's first aspect, the switchstand following the switch, a path
  ending inside a section, the clear-ahead count (2 by type, 3 increased,
  1 decreased, unchanged in the legacy mode), the request stop (pick-up
  yes, set-down only no).

## Left

- A real route using them; an Open Rails comparison then.
- `enabled` for heads without NORMAL function: Open Rails enables them as
  a train reserves their track (`TrackCircuitSection.Reserve`), TSRE
  never (found here). Scripts of distants, repeaters
  and info signals reading `enabled` may differ from Open Rails; to be
  checked against the local routes' scripts before changing.
