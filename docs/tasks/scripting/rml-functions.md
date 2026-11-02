# TSRE's functions for RmlUi documents

A list of the functions TSRE gives RmlUi documents' data expressions, and
their status. They are the middle ground between a plain document (data
binding) and Lua (the user, 2026-11-02): an author formats and picks
values in the markup, with no script.

Started with the displays task (`02-displays.md`, step 10): display
documents have them; the HUD's and the game UI's documents may take the
same ones later.

## How they work
- **Two call forms,** both RmlUi's: a pipe, the value first,
  `{{ row.time | format_time('h:mm AP') }}`, and a call,
  `{{ format_date(date, 'dd.MM.yyyy') }}`. Pipes chain:
  `{{ row.time | add_minutes(row.delay) | minutes_until(clock) }}`.
- **C++:** each is a C++ function registered on the document's data model
  (`DataModelConstructor::RegisterTransformFunc`), run by RmlUi's
  expression evaluator; no Lua. A display has an RmlUi context of its own,
  and the type register (the functions' home) is the context's.
- **Only their arguments:** RmlUi works an expression out again only when
  a data variable in it changed, so a function reading the clock behind
  the expression's back would go stale (`cycle(4, …)` alone was never
  drawn again, found in step 10's captures). The clock is an argument:
  `clock` (seconds after midnight), `clock | cycle(4, …)`,
  `minutes_until(clock)`.
- **When they run:** on each redraw of the document (TSRE marks all its
  data changed then), once per expression using them. A call is a short
  string or number conversion (microseconds; not measured); the redraw's
  layout and drawing cost far more.
- **Empty in, empty out:** an empty value (a field a train has not)
  gives empty text, so missing data prints nothing odd. `default` is the
  exception.
- **Wrong arguments:** empty text and one message in Errors and Messages
  (the document's reporting).
- **Names:** lower case with `_`, like RmlUi's own; none may be a data
  name of the documents (`date`, `time`, `number`, `line`, `distance`,
  …) or one of RmlUi's built-ins.
- **Languages:** names of days and months and decimal commas follow a
  language argument (`'pl'`, `'en'`, …), English without it: a display
  belongs to the route's country, not to the player's interface language.

## RmlUi's own (already there)

| Function | Does |
| --- | --- |
| `to_upper`, `to_lower` | Letter case. |
| `format(decimals, trim)` | A number with that many decimals (`trim`: drop trailing zeros). |
| `round` | A number rounded. |

Already possible without a function: a list's first rows
(`data-if="i < 5"`), a platform's rows only
(`data-if="row.platform == '2'"`), a fallback
(`a != '' ? a : b`), joining (`'0' + day`).

## TSRE's

| Function | Input | Does | Example | Status |
| --- | --- | --- | --- | --- |
| `format_time(pattern)` | A time (`HH:mm`, `HH:mm:ss`) or seconds after midnight | The time in a Qt time pattern. | `{{ row.time \| format_time('h:mm AP') }}` → `2:25 PM` | done (step 10) |
| `format_date(pattern, language)` | `date` (`yyyy-MM-dd`) | The date in a Qt date pattern; names of days and months in the language. | `{{ date \| format_date('dd.MM.yyyy') }}` → `02.11.2026`; `format_date('dddd', 'pl')` → `poniedziałek` | done (step 10) |
| `pad(width, fill)` | Text or a number | Filled on the left to `width` (`fill` `'0'` by default). | `{{ day \| pad(2) }}` → `02` | done (step 10) |
| `default(text)` | Text | `text` when the input is empty. | `{{ row.platform \| default('—') }}` | done (step 10) |
| `add_minutes(n)` | A time | The time `n` minutes later (empty `n`: 0), past midnight too. | `{{ row.time \| add_minutes(row.delay) }}` → the expected time | done (step 10) |
| `minutes_until(clock)` | A time | Minutes from the clock to it (negative when passed). | `{{ row.time \| minutes_until(clock) }} min` → `3 min` | done (step 10) |
| `cycle(period, a, b, …)` | The clock (a number) | One of the values in turn, each `period` seconds. The Lua example's PL/EN line without Lua; also `data-if="clock \| cycle(4, true, false)"`. The display must redraw often enough. | `{{ clock \| cycle(4, 'Odjazdy', 'Departures') }}` | done (step 10) |
| `has(word)` | Words separated by spaces | True when they hold the word: a car's tags (task 03, `car.tags`). | `data-if="car.tags \| has('bike')"` | done (task 03) |
| `truncate(length, end)` | Text | Cut to `length` characters, `end` added when cut. A fixed-width LED line. | `{{ row.id \| truncate(16, '…') }}` | proposed |
| `replace(from, to)` | Text | Every `from` replaced. | `{{ row.id \| replace('_', ' ') }}` | proposed |
| `part(separator, index)` | Text | The text's `index`-th part (from 0). For names that follow one pattern; data rows are better. | `{{ row.id \| part('_', 0) }}` → `0846` | proposed |
| `ascii` | Text | Letters without diacritics, for displays whose real ones have none. | `{{ row.destination \| ascii }}` → `Czestochowa` | proposed |
| `length_in(unit, decimals)` | Metres | In `km`, `mi`, `ft` or `yd`. | `{{ train.distance \| length_in('km', 1) }}` → `1.2` | proposed |
| `format_number(decimals, language)` | A number | With the language's decimal mark. | `{{ 1.26 \| format_number(1, 'pl') }}` → `1,3` | proposed |

The first batch is in (displays step 10, `src/tsre/ui/UiDocumentFunctions.cpp`,
checked by the `displays` suite); the others when a document needs them.

## Not functions
- **Lists another way** (sorted otherwise, merged, filtered by a rule
  more than `data-if` can say): a function returns one value. A new
  `Input` or Lua.
- **State across redraws** (a ticker's position, a count): Lua.
