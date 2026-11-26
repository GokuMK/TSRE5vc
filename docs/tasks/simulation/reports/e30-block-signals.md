# E30's block signals: TSRE, Open Rails and a real line

Task simulation 09, 2026-11-26. The user's question: E30's largest aspect
difference against Open Rails is its block signals showing CLEAR_2 in
TSRE where Open Rails shows CLEAR_1 or APPROACH_2. How serious is that in
reality, and what would those signals show on a real Polish line?

In short: on automatic block signals TSRE shows what a real line shows;
Open Rails' more restrictive aspects come from how it clears signals for
a train. Where a station's signal is involved, the difference is when
each simulator clears the station's route, a dispatching question.

## The real signals

Polish 4-aspect automatic line block (SBL czterostawna), [Ie-1][ie1] and
its readings ([PKM 03][pkm], [Józwik 2016][jozwik], [a letter on
gov.pl][letter]):

| Aspect | Light | On an automatic block signal |
|---|---|---|
| S1 | red | its block occupied |
| S5 | orange | the next signal shows S1: one block free |
| S3 | flashing green | two blocks free (the next shows S5) |
| S2 | green | three or more blocks free |

The signals show what is on the track (track circuits or axle
counters): nobody clears them for a train. One reading is disputed: the
last block signal before a station's entry signal at S5 shows S2 or S3
depending on how the rule is read ([Józwik][jozwik], [letter][letter]).

## The content

E30-Podkarpacie's block signals are `PKP_SBL_4st`, `PKP_SBL_4st_w18` and
`PKP_BLOK_SBL` (`OPENRAILS/sigcfg.dat`, scripts in `sigscr_TS0001.dat`):

| Aspect | Polish | Speed (sigcfg) |
|---|---|---|
| STOP_AND_PROCEED | S1 | 20 km/h |
| APPROACH_2 | S5 | 100 km/h |
| CLEAR_1 | S3 | 160 km/h |
| CLEAR_2 | S2 | none |

The script (`PKP_SBL_4st`, TomSka 2008): cleared for a train, S1 with its
block occupied, S5 before a signal at stop, S3 before a signal at S5 when
the signal has the USER1 option (the 4-aspect ones), else S2. Not cleared
for a train it is dark (S1 with its block occupied) and reads as stop to
the signal before it. All flags `OR_NOSPEEDREDUCTION`: Open Rails' AI
does not creep up to them, the speed applies past the signal.

So the script is the real rule as long as the signals ahead are cleared.

## What Open Rails does

Traced in Open Rails (the local trace build, below) on E30's timetable
from 04:45 to 07:50. Open Rails clears the block signals ahead of a train
one at a time as it comes up, though the type asks for four
(`SignalNumClearAhead ( 4 )`); a signal not yet cleared reads as stop, so
the one before it shows S5 and the one before that S3. TOW 335000
Przeworsk-Łańcut, every block free all along:

| Time | Open Rails | Signal 682 | TSRE | Real line |
|---|---|---|---|---|
| 04:46:58 | 682 cleared, 680 not yet | S5 | S2 | S2 |
| 04:47:54 | 680 cleared, 675 not yet | S5 | S2 | S2 |
| 04:49:03 | 675 cleared | S2 | S2 | S2 |

TSRE clears four ahead (the type's count), so its block signals show
their blocks as a real line does. Why Open Rails clears fewer is not
traced into its code (`propagateRequest`, `ReqNumClearAhead`); the roadmap's
JAPAN1 item (the signal after next uncleared, APPROACH_3 against
CLEAR_2, task 05) is likely the same behaviour.

## The samples

The block-signal samples of that run (202 signals traced; TSRE's events
paired with Open Rails' dispatcher samples, `scripts/orts-compare-aspects.py`):
1530 paired, 981 the same. Of the 549 that differ, what a real line shows was
worked out from Open Rails' own block occupancy at the moment (block
signals by the rule above, stations' signals as Open Rails set them):

| Situation | Samples | Shows the real aspect |
|---|---|---|
| Block signals ahead only; Open Rails had not yet cleared the next ones | 253 | TSRE |
| Block signals ahead only; an occupied block | 3 | Open Rails |
| A station's signal ahead cleared in Open Rails; a block signal before it not yet | 96 | TSRE |
| A station's signal one or two ahead still at stop in Open Rails, cleared in TSRE | 126 | the dispatcher's timing |
| Signals not traced | 71 | – |

The station cases do not test the block signal rule: TSRE clears up to
four signals ahead, a station's entry signal included, Open Rails later.
When a real dispatcher sets that route is not modelled by either.

## The comparison with Open Rails' clearing window set aside

`scripts/orts-compare-aspects.py --trace` (a second run, every signal
TSRE's trains see traced, 579): a sample where Open Rails is more
restrictive is set aside when the first signal at stop ahead of it in
Open Rails is not cleared there: on plain track with its block clear
(automatic block signals), or a station's signal (a junction in its
block: Open Rails reads no block for those while not cleared). A signal
cleared, its block clear and not held, but at stop is followed past: E30's
`PKP_BLOK_SBL` passes a stop on from the signal after it.

| E30, 04:45 to 07:50 | Samples | The same |
|---|---|---|
| All | 5015 | 3102 (62 %) |
| Without 739 before automatic block signals | 4276 | 73 % |
| Without 493 before stations' signals too | 3783 | 82 % |

Of the 681 differences left, 640 are stations' signals: Open Rails CLEAR_2
where TSRE shows APPROACH_2 (169), RESTRICTING where TSRE shows STOP (91),
APPROACH_2 where TSRE shows CLEAR_2 (84), STOP or RESTRICTING where TSRE
shows CLEAR_2 (110). These are TSRE's real differences to look at next.

## Impact

On a real line a spurious S5 tells the driver to be ready to stop at the
next signal, and a spurious S3 that only two blocks are free; both make a
driver brake for nothing. In the simulation, Open Rails' trains are held
to 100 km/h past such an S5, so they run slower than a real train would
there and TSRE's trains run ahead of Open Rails' in the comparisons. The
aspect comparison's 67 % on E30 counts these as differences.

## Decisions (the user, 2026-11-26)

- TSRE keeps its block signals as they are.
- The aspect comparison sets aside the samples where Open Rails is more
  restrictive only because a signal ahead is not yet cleared there
  (`scripts/orts-compare-aspects.py --trace`).
- Rules that depend on the content (follow Open Rails, or show automatic
  block signals and stations' routes more as a real line does) may later
  be chosen per route, perhaps in sigcfg (roadmap's open items).

## How it was done

- Open Rails trace build: `../openrails-wz-trace` (branch
  `wz-signal-trace` of the Open Rails fork, local, never pushed): prints
  `TSRETRACE` lines to the Open Rails log for the signals listed in
  `TSRE_OR_TRACE_SIGNALS` (TDB items): each head's aspect, the train it is
  cleared for, its next NORMAL signal, its route, block state and whether
  it has a fixed route, when any changes; also for trains
  (`TSRE_OR_TRACE_TRAINS`). It loads CMK_E30's wagons (include separators
  turned to `/`). Build: `dotnet build Source/RunActivity/RunActivity.csproj
  -c Release`, then copy `Program/Content/*.mgfx` from the fork's own
  `Program`.
- The run: `scripts/orts-compare-aspects.py --trace-items <TSRE events>`
  gives the signals to trace; `ORTS_PROGRAM=../openrails-wz-trace/Program
  TSRE_OR_TRACE_SIGNALS=<items> scripts/orts-timetable-run.sh ~/ORTS/CMK_E30
  E30-PODKARPACIE <work> "TT E30 Poranek 2.0.timetable-or" "[pl] TLK 31100
  poc SAN Przemysl-Warszawa" 07:50`; then `scripts/orts-compare-aspects.py
  <TSRE events> <samples> --trace <work>/logs/OpenRailsLog.txt`.
- Open Rails' own runs vary (task 09): numbers from one run.

[ie1]: https://www.plk-sa.pl/files/public/user_upload/pdf/Akty_prawne_i_przepisy/Instrukcje/Wydruk/Ie/Ie-1_wersja_obowiazujaca_od_2024-10-14_WCAG.pdf
[pkm]: https://www.pkm-sa.pl/wp-content/uploads/2020/04/PKM-03-15.05.2020.pdf
[jozwik]: https://yadda.icm.edu.pl/baztech/element/bwmeta1.element.baztech-13f44993-d1c6-4250-b4bd-c14833e7f700/c/jozwik_wybrane_110_2016.pdf
[letter]: https://www.gov.pl/attachment/5e2dafb8-0762-4dc6-be2c-33722f0a0e76
