# Task 03 - Car data: car numbers, a train's cars, Car displays

Status: steps 1 to 5 done on `feature/displays` (2026-11-02), not merged;
for the user's acceptance. Design written the same day from the user's
decisions, the go given ("If nothing else to agree, proceed with
implementation"). It builds on task 02, not merged yet.

The user (2026-11-02), after task 02: a car's own data on displays, first
of all its number, the coach number passengers look for with a seat
reservation ("wagon 7"), as door panels show it. The consist's UiDs give
it; a timetable may override them per train. A train's display gets the
basic data of all its cars; a car's display, its own as well.

## TSRE today

- **Train displays share one document per train.** `Input ( Train )`
  gives a document the train's data (`train.*`); the document key holds the
  train (`RouteDisplays::bind`), so every car of the train shows the same
  picture. A car's own data is not given (task 02's open items).
- **Consist files number their cars.** Each car has `UiD ( n )`; TSRE
  reads it (`Consist::EngItem::uid`) and writes it back, and gives a new car
  `NextWagonUID`. The consist editor renumbers: Reverse sets every car's
  UiD to its position, Up and Down shift two cars' UiDs by one
  (`Consist::reverse`, `moveLeftSelected`, `moveRightSelected`). A standing
  consist an activity assembles from several is numbered by position
  (`Activity.cpp`; it stays).
- **The simulation names cars by their UiD** for the activities' wagon
  lists: `Vehicle::id` is the consist's number × 65536 + the UiD (the
  player's train consist 0, a loose consist its object's id); an AI train's
  cars have none (`NoCar`). Work orders match cars by it. A UiD is below
  65536.
- **UiDs are free in practice.** OR_PSK (2026-11-02, read only): of 291
  consists, 241 are not numbered 0, 1, 2… in order
  (`PSK_IC_Matejko_Przemysl_Szczecin`'s coaches 8 to 13), and some repeat a
  number (`PSK_IC131_Batory`: the loco and three coaches UiD 1); they run in
  Open Rails.
- **Timetable rows:** `#tsre.<field>` (TSRE's train fields) and
  `#tsre.extra.<key>` (the user's data, task 02 step 6); another `#tsre.`
  row is reported and skipped. Open Rails only warns about rows it does not
  know. It splits cells on the file's separator (`;` in PSK_2026, `,` or a
  tab in others) and does not take quotes.
- **A document's data is copied in on each redraw** (`DisplayDocument::
  fill`) and every variable marked changed (`DirtyAllVariables`); at most
  `core.rendering.displayRedraws` (2) documents are redrawn a frame.
- **RmlUi 6.3's expressions take a variable index**
  (`train.cars[car.index].number`; `DataExpression.cpp`).

## Design (the user's decisions, 2026-11-02)

### A car's number

- **The consist's UiD is the car's number** (the user: "number order is
  completely free to use, so Consist could support coach numbers this way,
  even without redesign"). No new consist format.
- **A timetable overrides it**, per train (`#tsre.cars`, below). On a
  failure, the consist's numbers, and a message.
- **The consist editor keeps UiDs** on Reverse, Up and Down, lets a car's
  number be typed, and warns of a repeated one (work orders match cars by
  UiD; two coaches would show one number).
- Content whose UiDs mean nothing shows them: its author's choice.
- The number travels with its car: coupling and cutting cars off keep it.

### The `#tsre.cars` row

The user's rough idea was `[E-e][1-a-b-c][2-a-c-d]`; agreed, with spaces in
a bracket and words for tags. The Gedania train (bocznica.eu's composition
of ECE 57000/1, the user's example):

```
#tsre.cars ; [-] [272 A] [271 WR] [270 B wheelchair conductor] [269 B] [268 B] [267 B bike]
```

- **One bracket per vehicle,** in the train's order from its front as the
  timetable builds it (its consists in order, a `$reverse`d one the other
  way round). The loco has its bracket.
- **The first word is the car's number as shown:** any text without spaces
  or brackets (the UK's coach letters too); `-` none (a loco, an EMU's
  vehicles); `*` the consist's UiD (to give tags only).
- **The rest are tags,** passed to documents as they are; TSRE does not
  know them. The guide suggests UIC letters for class and type (`A`, `B`,
  `AB`, `WR`, `WL`, `BC`) and words for features (`bike`, `wheelchair`,
  `quiet`, `family`). Authors may use their own, as with
  `#tsre.extra.kind`.
- **The brackets must match the vehicles.** Another count, or text outside
  brackets: the consist's numbers and no tags, and a message. A strict count
  shows a mistake instead of numbers shifted along the train.
- No `;`, `,`, tab or quotes are needed: the row reads in Open Rails as any
  `#tsre` row (warned about, ignored).
- Left out on purpose: seats, a vehicle's series, cars running part of the
  route (Open Rails timetables join and split trains for that).

### Inputs

| Input | Gets | One document per | Changes |
| --- | --- | --- | --- |
| `Train` | the train's data + `train.cars` (basic data of each car) | train (shared) | rarely: the timetable, a coupling |
| `Car` | the above + `car` (its own entry) | car | rarely |
| `Eng` (reserved) | the above + the vehicle's full definition and simulation values | car | every simulation step |

- **`train.cars` stays basic** (the user: "train.cars hold only
  timetable/consist info, maybe some very basic eng/wag data"): each car's
  `index` (from 0, the train's front), `number`, `tags`, `kind` (`engine`,
  `carriage`, `freight`, `tender`) and `name` (its file's name). No car's
  full data in the list: a car's document would copy every car's, a cost
  growing with the square of the train's length.
- **`car`** is the car's own entry (the same fields). Its own full data
  waits for `Eng`.
- **A `Car` document is the car's own:** its key holds the train and the
  car's index. Its data: the train's part, built once per train a frame,
  and its car.
- **A car without a train** (a standing train: a loose consist, cars cut
  off): `car` only, `train` empty. The Shape Viewer: sample data.
- **`Eng`** is read and reported as not supported yet; the display shows
  the clock only.

### Documents

- `train.cars`: `<div data-for="c : train.cars">{{ c.number }}</div>`;
  `car.number`, `car.tags`, `car.index`, `car.kind`, `car.name`.
- **`has(tag)`**, a new function: `{{ car.tags | has('bike') }}`, true when
  the words (`car.tags`, a text of words separated by spaces) hold it.

### Editing

- **Timetable window, Train data:** a Cars table, one line per vehicle of
  the train as the timetable builds it: the vehicle's name, its number (the
  consist's greyed when not given), its tags. It writes the row, so the
  count is always right. A row whose count does not match is shown and
  reported.
- **Consist editor:** each car's number in the units list (`7 · name`), a
  Number field for the chosen car, a repeated number marked.

### Not in this task

- `Eng`'s data: the vehicle's definition and simulation values (cab
  displays).
- Data read on demand and changed variables marked per group (with `Eng`
  and the RmlUi thread, the scripting roadmap).
- Seats, cars running part of the route.
- An activity's own numbers: its trains take the consists' (an activity's
  train data is editor task 07's open item).

## Steps

1. **Cars in the simulation:** each vehicle's number and tags, from its
   consist's UiD (every train, standing ones too); `#tsre.cars` read and
   written (`OrTimetable`), parsed (`TrainCars.h`), applied to a timetable's
   train (its count checked, a warning on a mismatch). Tests:
   `timetable-read`, `timetable-trains`.
2. **Displays:** `Car` and `Eng` read (`.sd`), `Eng` reported;
   `train.cars`; `Car` documents per car (a car's index from its train, a
   standing train's cars); `has`; the Shape Viewer's sample. Tests:
   `displays`.
3. **Consist editor:** UiDs kept on Reverse, Up and Down; a car's number
   typed; a repeated one marked. A test.
4. **Timetable window:** the Cars table. Test: `timetable-ui`.
5. **Examples and docs:** the coach's panel shows its number (`Input (
   Car )`); `docs/examples/displays`, OR_PSK (the coach's `.sd`, PSK_2026's
   `#tsre.cars` for its trains); the guide (English, Polish, HTML),
   `rml-functions.md` (`has`), the roadmap.

## Steps 1 to 5 done (2026-11-02)

- **The simulation's cars** (`Sim::Vehicle::number`, `tags`): each
  vehicle's number its consist's UiD (`VehicleOf`, every train, loose
  consists, standing trains), travelling with the car through coupling and
  cutting off. `TrainCar` (`src/tsre/trains/TrainCars.h`) reads and writes
  a `#tsre.cars` cell; `OrTimetable` reads the row (its word in any case;
  a train's cell that does not read reported), keeps each train's cell
  (`Train::cars`), writes the row back (empty too), `setTrainCars`.
  `Sim::GiveTrainCars` gives a timetable train's vehicles their numbers and
  tags (`*` keeps the consist's); another count or a cell not read leaves
  them, and the run's warning says so ("T2: #tsre.cars has 1 cars, the
  train 2; its consists' numbers are shown"; Check Activity lists it).
  `Sim::TimetableTrainCars` lists a timetable train's vehicles from its
  consist files' Engine and Wagon entries (names and UiDs, a `$reverse`d
  consist the other way round) without loading them.
- **Displays:** `Input ( Car )` and `Input ( Eng )` read; `Eng` reported
  ("its display's Input ( Eng ) is not supported yet; it shows the clock
  only"). `train.cars` (`index`, `number`, `tags`, `kind`, `name`: the
  file's name through EngLib, else the Wagon block's) from the frame's
  vehicles now, else the plan's. A `Car` document's key holds the train,
  the car's place and a standing train's (`Eng::setTrain(train, car,
  standing)`, given by `SimulationController` to running and standing
  cars); its data the train's, made once a frame as a Train display's, and
  its car (`RouteDisplays::carData`, `standingCar`); a standing train's
  car its car only. `has` (`UiDocumentFunctions.cpp`). The Shape Viewer's
  sample: four cars, the second the Car display's.
- **Consist editor:** Reverse, Up and Down keep each car's UiD (they
  renumbered by position); a new car gets a number above every car's (a
  file's `NextWagonUID` may be behind them: IC5610's 2 with UiDs to 14);
  `Consist::setNumber` (0 to 65535, `NextWagonUID` after it),
  `numberRepeated`. The units list shows each car's number
  (`7 · name`), a repeated one in red with a tooltip; a **Number** field
  for the chosen car (`conUnitNumber`).
- **Timetable window:** Train data's **Cars** table (`orTrainCars`): a
  line a vehicle (`TimetableTrainCars`), the consist's number greyed, the
  timetable's black; a number typed (`-` none) or tags typed write the row
  for all the cars, an undo step each; a number cleared is the consist's
  again; a row that does not read, or of another count, reported in the
  panel's problems.
- **Examples and OR_PSK** (backups in scratchpad
  `displays/orpsk-backup/task03`): `z2_drzwi.rml` shows the coach's number
  and class on the right (`car.number`, `car.tags | has('A')`, `'B'`), the
  docs' copy the same; `PKPIC_B11mnouz-70082.sd`'s four squares
  `Input ( Car )` (70085's Lua example stays `Train`); PSK_2026's
  `#tsre.cars` row after `#tsre.extra.kind`, for IC 109 (column 165, its
  consist repeating UiD 1) and IC 108 (column 166):
  `[-] [21 B] [22 B] [23 B] [24 B bike] [25 B] [26 B] [27 B wheelchair]
  [28 WR] [29 A]`. Seen at Zawiercie (QRhi, auto exposure, PSK_2026 at
  16:06:40 and 16:07:45; scratchpad `displays/task03/cars.sh`): IC 108's
  coaches 21 and 26 each their own number and class 2, the door leaf's
  panel too, the door open.
- **Docs:** the guide (both languages, HTML): `Input ( Car )`, `Eng`,
  `train.cars`, `car.*`, `has`, `#tsre.cars` and the UiDs, the consist
  editor's Number, the Cars table; the examples' README;
  `rml-functions.md`.
- **Tests:** `timetable-read` (the cell read and written, cells not read,
  the row read, reported, written back, typed; a timetable train's cars
  from its consist files; cars given numbers and tags; a consist's
  numbers kept by Reverse, Up and Down, set, repeated, written),
  `timetable-trains` (T1's numbers and tags from the row, T2's count
  warned, the consists' UiDs), `displays` (the inputs read; a train's cars
  from its plan and from a frame, a Car display's car, a standing train's
  car; a Car document drawn: `car.number`, `has`,
  `train.cars[car.index]`), `timetable-ui` (the Cars table: the consist's
  numbers, a number and tags typed, cleared, undo; a row of another count
  reported).

