# Display examples

The example documents of the [displays guide](../../guides/displays.md)
([po polsku](../../guides/displays.pl.md)). They were made for shapes of
OR_PSK's Prudnik route, hence the folders' names; the folders follow a game
folder's: a `DISPLAYS` folder's files go into the route's or the trainset's
own, the pictures into the route's `TEXTURES`. Each document begins with a
note on what it shows and how.

The shapes and their `.sd` files are not copied here; each document's `.sd`
blocks are below. A block goes inside the `.sd`'s `shape ( … )` block.

| File | Kind | Shows |
| --- | --- | --- |
| `ROUTES/PRUDNIK/DISPLAYS/odjazdy.rml` | Texture, paper; Station | A station's departures: time, delay, destination, train-type pictogram, the train's fields, platform. |
| `ROUTES/PRUDNIK/DISPLAYS/przyjazdy.rml` | Texture, paper; Station | The same for arrivals, with the origin. |
| `ROUTES/PRUDNIK/DISPLAYS/peron_lcd.rml` | Squares, glass; Platform | The platform's next train (`trains[0]`: leaving, or ending here), minutes until it leaves or arrives (`add_minutes`, `minutes_until`), its cars as pictograms with their numbers (`row.cars`, coloured by `has`), an analog clock, the date (`format_date`). |
| `ROUTES/PRUDNIK/TEXTURES/rodzaj_pociagu_*.png` | Pictures | The pictograms for `#tsre.extra.kind`: `fast`, `regional`, `aglo`. |
| `TRAINS/TRAINSET/KS_EN75/DISPLAYS/en75_wyswietlacze.rml` | Texture, glass; Train | An EMU's front and side bands: line or category and number, destination, the stops still ahead (`train.stops`). |
| `TRAINS/TRAINSET/PKP_Z2B-OR/DISPLAYS/z2_drzwi.rml` | Squares, glass; Car | A coach's panels on the body and on door leaves, with the coach's number and class (`car.number`, `car.tags \| has`); the bottom line in Polish, then English (`cycle`). |
| `TRAINS/TRAINSET/PKP_Z2B-OR/DISPLAYS/z2_drzwi_lua.rml`, `z2_drzwi.lua` | Squares, glass; Train | The same with Lua, without the number. |

The documents show the trains' fields, a timetable's `#tsre.<field>` rows
(`category`, `number`, `name`, `operator`, `line`, `origin`,
`destination`), one key of the user's own data, `#tsre.extra.kind`, and
the coaches' numbers and tags of `#tsre.cars` (guide, section 7). The
timetable's IC 108 and IC 109, whose consists have the Z2B coaches, have
rows such as:

```
#tsre.cars;…;[-] [21 B] [22 B] [23 B] [24 B bike] [25 B] [26 B] [27 B wheelchair] [28 WR] [29 A];…
```

## The `.sd` blocks

`ROUTES/PRUDNIK/SHAPES/ORB_Tpo4.sd` (the departures and arrivals board):

```
ESD_TSREDisplay (
    Document ( "odjazdy.rml" )
    Image ( "ORB_Tpo_ODJAZDY.ACE" )
    Resolution ( 512 1024 )
    Surface ( Paper )
    RefreshRate ( 0.2 )
    Input ( Station )
)
ESD_TSREDisplay (
    Document ( "przyjazdy.rml" )
    Image ( "ORB_Tpo_PRZYJAZDY.ACE" )
    Resolution ( 512 1024 )
    Surface ( Paper )
    RefreshRate ( 0.2 )
    Input ( Station )
)
```

`ROUTES/PRUDNIK/SHAPES/wyswietlacz_zegar_wiszacy_1.sd` and
`wyswietlacz_peron_zegar_wiszacy.sd` (a hanging clock with a screen on each
side):

```
ESD_TSREDisplay (
    Document ( "peron_lcd.rml" )
    ShapeHierarchy ( "MAIN" )
    Position ( -0.207 0.414 0.001 )
    Rotation ( 8.4 -90 0 )
    Size ( 1.389 0.67 0 )
    Resolution ( 1040 500 )
    Surface ( Glass )
    RefreshRate ( 1 )
    Input ( Platform )
)
ESD_TSREDisplay (
    Document ( "peron_lcd.rml" )
    ShapeHierarchy ( "MAIN" )
    Position ( 0.211 0.415 0.001 )
    Rotation ( 8.3 90 0 )
    Size ( 1.389 0.667 0 )
    Resolution ( 1040 500 )
    Surface ( Glass )
    RefreshRate ( 1 )
    Input ( Platform )
)
```

`TRAINS/TRAINSET/KS_EN75/KS_EN75_A.sd` and `KS_EN75_B.sd`:

```
ESD_TSREDisplay (
    Document ( "en75_wyswietlacze.rml" )
    Image ( "EN75_wyswietlacz.ace" )
    Resolution ( 1024 1024 )
    Surface ( Glass )
    RefreshRate ( 0.2 )
    Input ( Train )
)
```

`TRAINS/TRAINSET/PKP_Z2B-OR/PKPIC_B11mnouz-70082.sd`: two squares on the
body, two on door leaves, `Input ( Car )`: the coach's own document.
`PKPIC_B11mnouz-70085.sd` has the same four with
`Document ( "z2_drzwi_lua.rml" )` and `Input ( Train )`.

```
ESD_TSREDisplay (
    Document ( "z2_drzwi.rml" )
    ShapeHierarchy ( "MAIN" )
    Position ( 1.44 1.587 10.057 )
    Rotation ( 0 90 0 )
    Size ( 0.78 0.21 0 )
    Resolution ( 624 168 )
    Surface ( Glass )
    RefreshRate ( 1 )
    Input ( Car )
)
ESD_TSREDisplay (
    Document ( "z2_drzwi.rml" )
    ShapeHierarchy ( "MAIN" )
    Position ( -1.44 1.587 -10.056 )
    Rotation ( 0 -90 0 )
    Size ( 0.78 0.21 0 )
    Resolution ( 624 168 )
    Surface ( Glass )
    RefreshRate ( 1 )
    Input ( Car )
)
ESD_TSREDisplay (
    Document ( "z2_drzwi.rml" )
    ShapeHierarchy ( "DOOR_B" )
    Position ( 0.076 -0.625 0 )
    Rotation ( 0 90 0 )
    Size ( 0.5 0.135 0 )
    Resolution ( 624 168 )
    Surface ( Glass )
    RefreshRate ( 1 )
    Input ( Car )
)
ESD_TSREDisplay (
    Document ( "z2_drzwi.rml" )
    ShapeHierarchy ( "DOOR_D" )
    Position ( -0.076 -0.625 0 )
    Rotation ( 0 -90 0 )
    Size ( 0.5 0.135 0 )
    Resolution ( 624 168 )
    Surface ( Glass )
    RefreshRate ( 1 )
    Input ( Car )
)
```
