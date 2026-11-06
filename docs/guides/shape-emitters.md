# Shape emitters: smoke and steam from S shapes

A **shape emitter** gives out smoke or steam from an MSTS shape (`.s`),
defined in its `.sd` file: a house's chimney, a factory's stack, a steam
vent. TSRE draws it with the same smoke and steam as its trains' exhausts:
puffs that leave the nozzle, rise while warm, drift and spread with the
wind, and thin out; steam lingers on a cold damp morning and goes quickly
on a dry afternoon. Every placed object smokes on its own, so ten houses
of one shape never smoke alike.

The examples are TSRE's own (section 8).

## 1. Where it shows

| Where | What you see |
| --- | --- |
| The route editor's 3D view, the simulation (QRhi, `Exhaust` Normal or High in the graphics settings) | The smoke and steam, the simulation running or not. |
| The standalone Shape Viewer (QRhi) | The shown shape's emitters at their own flow (no `Variation`), and an engine's or a consist's exhaust at fixed test values: a preview without the simulation. |
| The OpenGL renderer, the editors' small shape views | None. |

On a route, the placed objects with a shape smoke: static objects,
gantries, collide objects, pickups, signals, speed posts, level crossings
and hazards. Track sections, dynamic track, forests, transfers and car
spawners do not, nor do trains (section 9).

## 2. The `.sd` block

One `ESD_TSREEmitter` block an emitter, inside the `.sd`'s `shape ( … )`
block, beside its other `ESD_` entries:

```
ESD_TSREEmitter (
	Name ( "POT1" )
	Position ( 4.08 7.8 -0.02 )
	Diameter ( 0.22 )
	Kind ( Exhaust )
	Flow ( 0.04 )
	ExitSpeed ( 1.5 )
	Temperature ( 120 )
	Vapour ( 0.2 )
	Soot ( 0.4 )
	Variation ( 0.3 )
)
```

| Value | Meaning | When left out |
| --- | --- | --- |
| `Kind` | Its defaults for the values below (section 4): `Exhaust`, `Stack`, `SafetyValves`, `Heating`, `Steam`. | `Steam` |
| `Flow` | The gas it gives out, m³/s; 0 is off. | The kind's |
| `ExitSpeed` | Its speed at the nozzle, m/s. | The kind's |
| `Temperature` | Its temperature, °C: how fast it rises, how long steam shows. | The kind's |
| `Vapour` | The share of it that is water vapour, 0 to 1; the rest is smoke. | The kind's |
| `Soot` | How dark the smoke is, 0 to 1. | The kind's |
| `Beats` | Puffs a second, a steam stack's beat; 0 for an even flow. | The kind's |
| `ColorRGBA` | The smoke's colour as hex `RRGGBBAA`. | TSRE's own |
| `Diameter` | The nozzle, m. | `0.2` |
| `Variation` | Each placed object's flow within plus or minus this share, the same every time for one object. | `0` |
| `ShapeHierarchy` | The matrix it hangs on, by name; it moves with it. | The shape's root |
| `Position` | Its place in metres, in that matrix's space. | `0 0 0` |
| `Rotation` | Its turn in degrees about x, then y, then z: the gas leaves upwards (+y) before it. | Upwards |
| `QDirection` | Or its turn as a quaternion `x y z w`, as a W file's object has it. | |
| `On` | `0`: off until something switches it on. | `1` |
| `Name` | Its name, shown in the Shape Viewer. | None |

Give one of `Rotation` and `QDirection`; with both, the later in the block
is used, with a message.

## 3. Placing and aiming

- Give the `Position` in the shape's own coordinates, as its vertices: the
  top of the chimney, a little above its rim. An emitter without a
  `ShapeHierarchy` hangs on the shape's root, its first matrix.
- The Shape Viewer's **Hierarchy** window lists the shape's emitters under
  the matrices they hang on, with their kind and flow; the standalone
  Shape Viewer shows their smoke.
- The gas leaves upwards unless turned: a vent in a wall pointing along +z
  is `Rotation ( 90 0 0 )`.

## 4. Values, kinds and size

The kinds' defaults (TSRE's test values for train exhausts):

| Kind | Flow | ExitSpeed | Temperature | Vapour | Soot | Beats |
| --- | --- | --- | --- | --- | --- | --- |
| `Exhaust` (diesel, smoke) | 0.5 | 10 | 300 | 0.1 | 0.4 | 0 |
| `Stack` (steam engine) | 2 | 30 | 150 | 0.8 | 0.3 | 4 |
| `SafetyValves` | 1 | 100 | 200 | 1 | 0 | 0 |
| `Heating` (hoses) | 0.02 | 5 | 110 | 1 | 0 | 0 |
| `Steam` | 0.3 | 40 | 120 | 1 | 0 | 0 |

A house's chimney gives far less than a diesel: `Kind ( Exhaust )` with a
`Flow` of a few hundredths and a slow `ExitSpeed`, as above.

A puff's size is no value of its own: it comes from the gas. A puff is at
least as wide as the nozzle and carries the flow of a thirtieth of a
second (a stack's: of its beat); it grows as it takes in the air around
it, faster from a fast nozzle and in wind. A large stack is a wide
`Diameter` with a large `Flow`.

`Variation` keeps a row of houses from smoking evenly: each placed object
takes its own share of the flow, fixed by its UiD.

## 5. Limits

Scenery's smoke and steam are kept apart from the trains': 1,000 puffs at
once at Normal, 3,000 at High (the trains have 3,000 and 10,000 of their
own). Objects within 2,000 m of the camera smoke; when the scenery's
puffs are full, the nearest emitters keep theirs. Objects smoke whether
they are in view or not, so a plume is there when the camera turns. An
object whose shape draws nothing past its last level
(`ESD_TSREDistanceLevelExtension ( None )`, [Shape lights](shape-lights.md),
section 4) gives out none there.

## 6. Messages

TSRE tells the shape's author once a shape, in the messages window, and
again after the shape is reloaded: a `Kind` it does not know (`Steam`
used), both `Rotation` and `QDirection`, a `ColorRGBA` that is not eight
hex digits, a `ShapeHierarchy` the shape has not (the emitter hangs on
its root).

## 7. Open Rails

Open Rails skips `ESD_TSREEmitter` blocks without a message: the shape
works there unchanged, without the smoke.

## 8. Example

[Shape emitter example](../examples/shape-emitters/README.md): chimney
pots on a stock cottage, and a test shape smoking in a row on a route.

## 9. Known limits

- An emitter on a locomotive's or wagon's shape gives out nothing yet: a
  car smokes from its ENG's emitters only. Binding the shape's emitters to
  the ENG's exhaust comes later.
- No emitters in glTF models yet.
- Emitters are on or off by `On` only: switching by the time of day or
  by events comes later.
- No size value; no smoke on the OpenGL renderer.
