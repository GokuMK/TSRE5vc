# Explore task 05 - The Explore window in pages: a route, then its activities, timetables or exploring

Status 2026-11-24: built on `feature/explore` the same day (the user's
go: "ok, proceed"), not merged; its questions answered (at the end); see
"As built". Narrow and wide views (at the end) built the same day (the
user's go: "Ok, proceed"). Part of the [explore set](00-task-roadmap.md).
Reworks task 02's three-column window ([02](02-explore-window.md)); keeps its readers, the
run's start, the game menu and the remembered choices, and task 04's
walking ([04](04-walking-runs.md)).

## The user's request (2026-11-24)
> I think that we could do the explore tab better. When explore is
> chosen, a tab opens with route list, route image, description if
> available etc. Three buttons are available to proceed further:
> activity / timetable / explore. Each switches to their own tab, we can
> design with more care and better info. Like in timetable, the table can
> now fill whole tab width. Activity can show more details, briefing etc.
> Review and propose new design.

## The window now (review)
Seen in the captures of tasks 02 and 04 (1280x720, EUROPE1 and PRUDNIK).
The window right of the main menu is about 965 px wide at 1280x720, in
three columns: the routes (20 %), the route's runs (24 %), the run's
details (the rest, about 460 px). Everything a run needs is in that last
column.
- **Timetables:** the table has those 460 px. Names are cut
  (`0446_R46401_KK-O-W`), so is the Engine column (`ORMG PREG EN57-2…`);
  Category, Number and Line are hidden below 16:10. About 17 rows show.
  A train's calls are seen only in walking's Station picker. Its
  `#comment` and `#briefing` are not shown at all, though they are the
  readable names: PSK_2026 has a comment for 294 of its 347 columns
  ("R 46401 KedzierzynKozle-Wroclaw") and a briefing for 179
  ("TLK48261 GANIMEDES Bielsko Biala-Kolobrzeg-Swinoujscie").
- **Activities:** the facts, the Day choice, the description and the
  briefing in one scrolled column; two thirds of Low Water's briefing
  are below the fold. The player's stops (read for walking) are not
  shown, nor anything of the traffic.
- **Exploring:** Start at, Heading to, Locomotive and Consist are each a
  picker over the details: one list seen at a time, the others only as
  the value chosen.
- **The route:** its picture and description beside the runs list, in
  the narrow column; none of its facts (miles or kilometres,
  electrified, speed limit, how many activities, paths, stations).
- The routes column stays while a run's details are chosen, also on a
  root with one route (OR_PSK_NEW).
- The window's button and the menu's item are both "Explore".

## Design
The Explore window keeps its place right of the main menu (question 1)
and shows one page at a time.

### 1. The route page (the window's first page)
```
┌─────────────────────────────────────────────────────────────────────┐
│ Routes                  Settle & Carlisle Line                      │
│  ebias1                 ┌──────────────────────────────────┐        │
│  FOREST                 │                                  │        │
│  Hisatsu Line           │      the route's picture         │        │
│  Innsbruck - St. Anton  │      (the TRK's Graphic)         │        │
│  ...                    │                                  │        │
│ ▌Settle & Carlisle Line └──────────────────────────────────┘        │
│  slarge                 Miles · not electrified · 105 mph           │
│  ...                    10 activities · 18 stations                 │
│                         Location: Northwestern England              │
│                         Route length: 72 miles (116 km) ...          │
│                         (the description, scrolled)                 │
│                                                                     │
│     [ Activities  10 ]    [ Timetables ]    [ Explore the route ]   │
└─────────────────────────────────────────────────────────────────────┘
```
- **The routes list** as now.
- **The chosen route:** its name, its picture larger than now, a line of
  facts from its TRK (miles or kilometres, electrified and its line
  voltage, the speed limit in the route's units), a line of counts (its
  activities, timetable sets, player paths, and its stations once the
  worker has read them, as walking reads them now), and its description,
  scrolled.
- **Three buttons:** Activities, Timetables, Explore the route, each with
  its count; greyed when the route has none (EUROPE1 has no timetables,
  PRUDNIK no activities). Each opens its page.

### The run pages: what they share
- **The whole window:** no routes column, no runs column.
- **A heading:** `‹ Settle & Carlisle Line · Activities`. The route's name
  is a button back to the route page (as Esc is).
- **The bottom bar**, in two rows:
  ```
  [ Drive | Walk ]   Station [ Raszowa  05:39 ]   Time [ 05:34 ][ min ]
  Day [ 21 June ][ month ]   Summer   Weather [ Clear ]        [ Start ]
  ```
  Drive or Walk, walking's Station and Time (task 04), the day, the
  season it gives, the weather, and the button that starts the run.
  Each page shows the parts it uses (an activity: its own season and
  weather as facts, the time only when walking). The button is named
  **Start** (question 2), not Explore as the menu's item.

### 2. The activities page
```
┌ ‹ Settle & Carlisle Line · Activities ──────────────────────────────┐
│ Activity               Difficulty Time │ Local Service              │
│  After the Storm       Medium     1:20 │ 09:00 · Spring · Clear ·   │
│  Home Stretch          Medium     0:20 │ Easy · 2 h 10 min          │
│  ITR Settle - Carlisle Easy            │ (the description)          │
│ ▌Local Service         Easy       2:10 │ ── Briefing ────────────── │
│  Low Water             Hard       2:10 │ (scrolled, its own box)    │
│  Medical Emergency     Medium     0:45 │ ── Your train ──────────── │
│  ...                                   │ Flying Scotsman, 10 pass.  │
│                                        │ Carlisle to Settle         │
│                                        │ (its stops and times)      │
│                                        │ 12 other trains            │
├────────────────────────────────────────┴────────────────────────────┤
│ (the bottom bar)                                                    │
└─────────────────────────────────────────────────────────────────────┘
```
- **The list** (about a third): name, difficulty, duration.
- **The details** (the rest): the name; a line of facts (start time,
  season, weather, difficulty, duration); the description as its lead;
  the briefing in a box of its own, scrolled; **your train:** its
  consist and engine, its path's start and end, its stops with their
  times (the player's timetable, read now for walking, its stations by
  the route's platform items); **the traffic:** how many other trains.

### 3. The timetables page
```
┌ ‹ Polskie_Szlaki_Kolejowe · Timetables ─────────────────────────────┐
│ Set [ TT PSK 2026 · 296 trains ]  Filter [         ]  [Drivable only]│
│ Train               Starts Ends  From       To          Cat Number Line Engine        Stops │
│  0446_R46401_KK-O-W 04:46  05:57 Kedzierzyn Dabrowa N.  ... 46401  ...  EN57-2023     13    │
│ ▌0518_R46403_R-KK-O 05:18  06:23 KK-Azoty   Opole Gl    ... 46403  ...  EN57AL-1687   11    │
│  ...  (the whole width; about 14 rows at 1280x720)                  │
├─────────────────────────────────────────────────────────────────────┤
│ R 46403 · PSK_PREG_EN57AL-1687 · path REG_Rac_KKIV8_OII4            │
│ KK-Azoty 05:20 · Kedzierzyn-Kozle 05:29 · Raszowa 05:39 · ... · Opole Gl 06:23 │
├─────────────────────────────────────────────────────────────────────┤
│ (the bottom bar)                                                    │
└─────────────────────────────────────────────────────────────────────┘
```
- **The set:** a picker in the page's heading when the route has more
  than one (question 4, the user: "a picker in the timetable tab"), with
  its trains' count; the filter and Drivable only
  beside it.
- **The table** the window's width: Train, Starts, Ends (its last call),
  From, To, Category, Number and Line (when the set has them, whatever
  the view's shape), Engine, Stops. Sorting and the filter as now;
  walking's No train first, as now.
- **The chosen train** under the table: its comment (and briefing when
  it says more), its consist and path, and its calls in time order on a
  line (stops and passes, the passes dimmed), scrolled sideways when
  long.

### 4. The explore page
Proposed after the user's answer to question 3 ("difficult to decide,
propose something then we iterate"): a first version to try and change.
```
┌ ‹ Settle & Carlisle Line · Explore the route ───────────────────────┐
│ Path    Filter [        ]          │ Train   Filter [        ]      │
│ Start at         Heading to        │ Consist               Engine   │
│  Appleby         Carlisle          │  2 x class 50, 12 ca… Class 50 │
│  Armathwaite St. Carlisle Station  │ ▌Flying Scotsman, 10… Scotsman │
│ ▌Carlisle        Settle            │  ...                           │
│  Carlisle        Settle            │                                │
│  Garsdale        Armathwaite       │                                │
│  ...                               │                                │
│ Local Service.pat: Carlisle to     │ Scotsman · (its cars' count)   │
│ Settle                             │                                │
├────────────────────────────────────┴────────────────────────────────┤
│ (the bottom bar)                                                    │
└─────────────────────────────────────────────────────────────────────┘
```
- **Two lists, each with a filter**, in place of the four pickers (Start
  at, Heading to, Locomotive, Consist):
  - **Path:** one row a player path, Start at and Heading to in two
    columns, sorted by its start and end; the filter finds text in
    either or in its file's name. Under the list: the chosen path's file
    and its ends.
  - **Train:** one row a consist with its lead engine, sorted by the
    engine and the consist's name; the filter finds text in either.
    Under the list: the engine and how many cars.
- **Why not one list per choice:** on PSK, steps barely narrow anything.
  PRUDNIK's 314 player paths have 217 different starts, and OR_PSK_NEW's
  313 consists have about 220 different lead engines. A start then
  holds one or two paths, and an engine one or two consists, so four
  lists would be four long lists. Typing "Nysa" or "EN57" into a filter
  narrows them at once. EUROPE1 (12 player paths, 99 consists with an
  engine) reads the same way.
- **Walking:** No train first in the paths (task 04); with it, the train
  list is greyed.
- **Keys:** Left and Right between the two lists and the bottom bar; Up
  and Down in a list; Up from a list's first row, or a click, goes to
  its filter. Enter on a path goes to the train list, on a consist to
  Start.
- "Reading consists..." in the train list while the worker reads them,
  as now.
- The time, day and weather in the bottom bar. The picker stays for those
  small choices (hour, minutes, day, month, weather) and for walking's
  station.

### Keys
- **The route page:** Up and Down in the routes; Enter or Right on a
  route goes to its first button that is not greyed; Left and Right
  between the buttons; Enter on one opens its page. Left from the routes,
  or Esc, goes back to the menu's item (as now).
- **A run page:** Left and Right across its columns (the lists, the
  details, the bottom bar); Up and Down in a list; Enter on an activity,
  a drivable train or a consist goes to Start. Esc, or Left from the
  first column, goes back to the route page, its button for that page
  focused. The picker as now (Esc closes it).

### Remembered
`explore.json` keeps the page too. Back from a run (the game menu's Main
menu): the page it was started from, as it was (as now). The first time
Explore opens after the app starts (question 5): the route page, the
last route chosen and the last page's button focused; Enter opens that
page with its last choices made, Enter again starts the run.

### What changes in the code
- **`explore-menu.rml`, `explore.rcss`:** the window's three columns become
  the four pages (`data-if` on a `page` value of the model), the heading
  and the bottom bar shared by the run pages.
- **`Explore::Panel`:** a page in place of the runs column (the
  activities, the sets and Explore the route no longer one list); the
  keys' columns per page; the explore page's two filtered lists in place
  of its four pickers. The choices, `choice()`, `go()` and walking stay.
- **`Explore::Lists`:** the route's facts from the TRK it reads already;
  the route's counts when it is chosen (its activities and sets are read
  then now; its player paths; its stations on the worker, as walking
  reads them); an activity's other trains (its `Traffic_Definition`); a
  timetable train's comment, briefing, consist, path, end time and
  stops; a consist's cars counted (its reader reads the file already).
- **Not changed:** starting the run, walking, the loading screen, the
  game menu, the run.

## Steps
1. The readers' new values (`Explore::Lists`).
2. The pages in the document, the route page, the shared heading and
   bottom bar.
3. The activities page.
4. The timetables page.
5. The explore page.
6. The keys per page, the page remembered.
7. Checks and documents.

## Checks
- **`explore-window`** (content-free): the pages and their keys: a
  route's buttons (greyed with none), Enter into a page, Esc and Left
  back to the route page and its button, then to the menu.
- **`explore-lists`** on content: EUROPE1, the route's facts and counts,
  Local Service's 12 other trains and its stops' stations; PRUDNIK, the
  table's new columns (0518_R46403_R-KK-O: ends 06:23 at Opole Gl, 11
  calls), its comment and consist.
- **`explore-start`** unchanged (it chooses through the panel's API):
  every run starts as before, walking too.
- **Live, Xvfb at 1280x720 and 1920x1080:** EUROPE1 (the route page, Local
  Service, exploring on the two lists), PRUDNIK (the table full width, a
  train's calls), keys only.
- Before a merge: `all` built, ctest, the app suites.

## Questions
Answers (the user, 2026-11-24):
1. **The main menu beside the run pages:** "keep for now". Folding it,
   a page about 1220 px wide in place of 965 px at 1280x720, can come
   later.
2. **The button that starts a run:** "Start", "ok".
3. **The explore page:** "difficult to decide, propose something then
   we iterate": proposed above, two lists with filters, to try and
   change.
4. **A route with more than one timetable set:** "a picker in the
   timetable tab": in the timetables page's heading.
5. **The first opening after the app starts** (asked again, the first
   wording was not clear): the route page with the last route chosen
   and the last page's button focused, one Enter from that page; not
   straight into that page. The user: "a".

## As built (2026-11-24)
- **The pages** as designed: the route page (routes; picture, facts,
  counts, description; Activities, Timetables, Explore the route with
  their counts, greyed at 0), then a run page with its heading
  (`< Settle & Carlisle Line  Activities`, the route's name a button
  back) and the bar under it in two rows; Start in place of Explore.
- **The activities page:** the list (name, difficulty, duration); the
  details: the facts line, the description, the briefing in a box (left
  out when it repeats the description), your train (its consist and
  engine, its path's ends, its stops with their times), the other
  trains. One change: the details scroll as one (not the briefing apart):
  a page's text has nothing to focus, so **Page Up and Page Down scroll
  it** (and the route page's description).
- **The timetables page:** the tools row (the set's picker only when the
  route has more than one, else its name; the trains' count; the filter;
  Drivable only), the table (Train, Starts, Ends, From, To, Category,
  Number and Line when the set has them on any view, Engine, Stops; the
  filter finds the comment too), the chosen train under it: its comment
  (else its name), its briefing when it says more, its consist and path,
  its calls in time order (a stop's arrival, its first's departure;
  passes dimmed), each call wrapping whole.
- **The explore page:** the two filtered lists as proposed; the paths'
  rows got a third, dimmed column, the path's name (seen live: EUROPE1
  has two Carlisle to Settle paths that read the same).
- **Keys:** the route page: Enter on a route goes to the button of the
  page last used there (the remembered run's), else the first not
  greyed; the greyed ones are passed; Left from the buttons to the
  routes. A run page: Left and Right across its lists and the bar, along
  the bar's row inside it (Start its last); Left from the first column,
  or with nothing left of it, back to the route page; Esc too, the
  page's button then focused; Esc again the menu's item. Up from a list's
  first row to its filter, Down from the filter's row to the list. Enter
  on an activity, a train or a consist goes to Start; on a path to the
  trains. A page opens on Start when it holds the run last started (one
  Enter from the route page's button, answer 5), else on its first
  column with something to focus.
- **Found live and fixed:**
  - **Typing into a menu's text field:** the UI screen gave typed text
    only to a field outside a modal menu (`Ui::Screen::keyPress`, its
    `Keys::Text`), so no letter reached the Explore window's filters,
    task 02's timetable filter included. A menu's focused text field now
    takes the text; the keys stay the menu's.
  - **Esc in a filter:** RmlUi's text field stops every key it gets (Esc
    too); the window's key handler listens in the capture phase, before
    it.
  - The page's `.note` class moved the menu's "Later" notes (renamed);
    RmlUi's flex shrank the bar's buttons to nothing (`flex: 0 0 auto`).
- **The values** (`Explore::Lists`): the route's units, electrified and
  line voltage, speed limit from the TRK read already; an activity's
  other trains counted in its parse (Local Service 12); a timetable
  train's comment, briefing, consist, path, end and stops; a consist's
  cars. The route's stations read on the worker when a route is chosen
  (its count), as walking read them.
- **The code:** `Explore::Panel` around pages (task 02's runs list gone;
  `page()`, `openPage()`, `back()`, the filters, `text(id)` for the
  checks); `Explore::Menu`: Esc back a page, Up and Down to a filter,
  Page Up and Down, its key listener in the capture phase, `click()`
  focusing first as a mouse does; `Ui::Screen::keyPress` (typing).
- **Texts:** 28 new (English and Polish), 3 gone (Start time, Duration,
  Locomotive).
- **Checks:**
  - `explore-window` 54 (was 46): the route page's buttons (greyed ones
    passed), Enter into the explore page (no paths: on the bar's Drive),
    the bar's keys, a picker, typing into the paths' filter, Esc and Left
    back to the route page, in again on the page last used;
  - `explore-lists` EUROPE1 29 (the route's facts, Local Service's 12
    other trains, the consists' cars, the pages through the model: the
    route's counts, no timetables page, Local Service's details, back,
    the explore page's filters, a consist's note), PRUDNIK 29
    (0518_R46403_R-KK-O: ends 06:23, 11 stops, "R 46403", its consist,
    path and calls under the table);
  - `explore-start` EUROPE1 73, PRUDNIK 40 (unchanged), `rmlui` 43;
  - `all` built, ctest 11/11, 125 of the 153 app suites (the 28 others
    need content or arguments, as on main), `settings` 306, `keys` 19,
    `server` 172, `simulation-wire` 16.
- **Live, Xvfb at 1280x720, keys only:** EUROPE1: the route page, Local
  Service (Page Down, Esc back), the explore page; PRUDNIK: the route page
  at first opening (Timetables focused), the timetables page opened on
  Start, the 21:03 TLK chosen by keys (its comment readable under the
  table), the explore page with "nysa" and "en57" typed into its filters.
  Screens in the session's scratchpad (live/), not kept.
- **Left:** the route's picture a fixed 400 dp tall; the menu folded beside the run pages
  (question 1) later.

## Narrow and wide views (2026-11-24, built the same day)
The user, on the 4:3 view left unchecked: "For 4:3, the tab window
should just cover menu. Proportions should stay +- the same and design
consistent." On wider views: "maximum should be set. When we have route
background, users will get nice view on the sides."

The view is 1080 dp tall at any size. The window is 960 dp tall, 430 dp
from the left (right of the menu) and 40 dp from the right:

| View | Beside the menu (now) | Over the menu, 40 dp from both edges |
|---|---|---|
| 21:9 (2520 dp wide) | 2050 x 960 | - |
| 16:9 (1920) | **1450 x 960** (1.51) | - |
| 16:10 (1728) | 1258 x 960 (1.31) | 1648 x 960 |
| 3:2 (1620) | 1150 x 960 (1.20) | 1540 x 960 |
| 4:3 (1440) | 970 x 960 (1.01) | **1360 x 960** (1.42) |
| 5:4 (1350) | 880 x 960 (0.92) | 1270 x 960 (1.32) |

- **At most its 16:9 width:** the window at most 1450 dp wide; a wider
  view centres it in the space right of the menu, the route's background
  seen on both sides (21:9: 300 dp more each side). In `explore.rcss`
  alone: `max-width` and auto margins (RmlUi centres an absolutely placed
  box with both `left` and `right` so, its `LayoutDetails::BuildBoxWidth`).
- **Narrow views cover the menu:** at 3:2 and narrower
  (`@media (max-aspect-ratio: 3/2)`) the window goes 40 dp from both
  edges, over the menu, capped and centred the same: 3:2 1450 dp, 4:3
  1360, 5:4 1270. 16:10 stays beside the menu (1258); moved under the
  rule if its screen shows it too tight. The window is then 1258 to 1450
  dp wide and 960 tall on every view: the same pages, no rules of their
  own.
- **Back to the menu when covered:** the menu cannot stay live beside the
  window. Esc on the route page, or Left from its routes, hides the
  window and focuses Explore (still marked current); Enter, Right or a
  click opens it again on the same route and page (as now). Beside the
  menu, as now. The ratio only in the stylesheet: the code asks whether
  the window's box overlaps the menu's items.
- **The mouse:** the menu's items are under the window, so the route
  page's routes heading gets a `< Menu` button, shown only when covered
  (in the same `@media` block), as a run page's `< route`.
- **Checks:** screens at 21:9, 16:9, 16:10, 3:2, 4:3 and 5:4 on PRUDNIK
  (the route, timetables and explore pages); `explore-window` cases at
  4:3 (Esc from the route page hides the window and focuses Explore,
  Right opens it on the same route, `< Menu` shown) and at 16:9 (the
  window beside the menu, no `< Menu`).

### As built
- **`explore.rcss`:** `.window` with `max-width: 1450dp` and auto
  margins; `@media (max-aspect-ratio: 3/2)` puts its left at 40 dp and
  shows the routes' `< Main menu` (the game menu's text, none new). 16:10
  stays beside the menu: its screen fits (the Engine column clips as it
  does at 16:9).
- **Found on the screens:** the window's background is not opaque, so the
  menu's items showed through it at 4:3; they are hidden while a window is
  over them (`#items.under`, TSRE's class).
- **`Explore::Menu`:** whether the window covers the menu is its style's
  `left` (computed while hidden too) left of the items' right edge.
  Esc on the route page, Left from the routes and `< Main menu` send the
  keys to the Explore item; while they are there a window over the menu
  is hidden (and the items shown), and opening it again shows it. Each
  frame the window is put right: a view made wider shows it beside the
  menu again; one made narrower with the keys on the menu hides it. Esc
  on the item hides a window over the menu that no route took the keys
  into (a root without routes). For the checks: `covering()`,
  `windowShown()`, `box(id)` in dp, `shows(id)`.
- **Checks:** `explore-window` 70 (was 54): its walk now at 1280x720
  (16:9; was 1024x768), then the views by resizing the window (the
  resolution setting offers 4:3 and 16:9 only; other screens come by Fill
  screen): 16:9 beside (430 dp to 40 dp from the right, no `< Main menu`,
  the items shown); 21:9 at most 1450 dp, centred right of the menu;
  16:10 beside; 4:3 with the keys on the menu hidden, Right over the menu
  (40 dp from both edges, `< Main menu`, the items hidden), Esc, Left from
  the routes and a click on `< Main menu` back to the menu with the window
  hidden; 16:9 again shows it; 5:4 over the menu; 3:2 over the menu at
  most 1450 dp, centred.
- **Live, Xvfb at each ratio 720 pixels tall, Fill screen, PRUDNIK by
  keys:** the route page, the timetables page and the menu after Esc Esc
  at 21:9, 16:9, 16:10, 3:2, 4:3 and 5:4 (4:3 and 3:2 again after the
  items' fix). Screens in the session's scratchpad (views/), not kept.
