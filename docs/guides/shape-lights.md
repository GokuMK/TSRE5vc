# Shape lights: lamps on S shapes

A **shape light** is a point or spot light that an MSTS shape (`.s`) carries
in its `.sd` file: a street lamp's light, a platform canopy's lamps, a
lantern. TSRE draws it as it draws a glTF model's lights
(`KHR_lights_punctual`): it lights the ground, walls, trains and the mist
around it, moves with the part of the shape it hangs on, and adapts to the
daylight as lamps do.

A light can also be a locomotive's headlight, switched by its ENG file's
lights (section 5). The examples are TSRE's own (section 8).

## 1. Where it shows

| Renderer | What you see |
| --- | --- |
| QRhi with local lights on (the default) | The light lights the scene: pools of light on the ground, lit walls, light in the mist. The lamp itself shows as the shape paints it (a full-bright or night texture on its lens). |
| QRhi with local lights off, and the OpenGL renderer | No light in the scene; a plain disc in the light's colour marks the lamp, as signal lamps and train lamps there. |

By day lamps are dimmed to a few per cent with time of day on (eyes and
cameras adapt to the daylight); with time of day off they shine at full
strength, so they can be placed and checked in the editor's daylight.

## 2. The `.sd` block

One `ESD_TSRELight` block a light, inside the `.sd`'s `shape ( … )` block,
beside its other `ESD_` entries:

```
SIMISA@@@@@@@@@@JINX0t1t______

shape ( lamppost.s
	ESD_Detail_Level ( 0 )
	ESD_Alternative_Texture ( 0 )
	ESD_TSRELight (
		Name ( "LAMP" )
		Type ( Spot )
		ShapeHierarchy ( "MAIN" )
		Position ( 1.25 5.84 0 )
		Rotation ( -90 0 0 )
		Color3F ( 1 0.82 0.6 )
		Intensity ( 60 )
		Range ( 30 )
		Cone ( 35 60 )
	)
)
```

| Value | Meaning | When left out |
| --- | --- | --- |
| `Type` | `Point` (light all round) or `Spot` (a cone). | `Point` |
| `Intensity` | Its strength in candela, as glTF's. | Required: a block without it is left out. |
| `Color3F` | Its colour as three numbers from 0 to 1, red, green, blue, in linear light: glTF's `color`. | White |
| `ColorRGBA` | Or its colour as eight hex digits `RRGGBBAA`, as colour pickers give it; the alpha scales the intensity. | |
| `Range` | Metres: the light ends there. | Where it fades out by its strength. |
| `ViewDistance` | Metres from the camera: farther, the light lights nothing (section 4). | As far as its object is drawn. |
| `Cone` | A spot's cone: the inner and outer angles from its axis, in degrees (full light inside the inner, none outside the outer). Up to 90. | `0 45` |
| `ShapeHierarchy` | The matrix it hangs on, by name; it moves with it (an animated part's light moves too). | The shape's root. |
| `Position` | Its place in metres, in that matrix's space. | `0 0 0` |
| `Rotation` | Its turn in degrees about x, then y, then z, as a display's square. | No turn. |
| `QDirection` | Or its turn as a quaternion `x y z w`, as a W file's object or a glTF node has it. | |
| `Radius` | The lamp's size in metres: it softens surfaces very close to the lamp, and is the disc's radius. | `0.05` |
| `Name` | Its name: shown in the Shape Viewer, and the name an ENG light switches it by (section 5). | None |
| `On` | `1`: it shines unless something switches it; `0`: off until something switches it on (an ENG light bound to it). | `1` |

Give one of `Color3F` and `ColorRGBA`, and one of `Rotation` and
`QDirection`; with both, the later in the block is used, with a message.

## 3. Placing and aiming

- Give the `Position` in the shape's own coordinates, as its vertices and
  an ENG file's light positions have them. A light without a
  `ShapeHierarchy` hangs on the shape's root, its first matrix.
- Find the matrix names in the Shape Viewer's **Hierarchy** window. The
  shape's lights are listed there under the matrices they hang on, with
  their type and strength.
- A light shines along **-z** of its matrix, turned by `Rotation` or
  `QDirection`, as a glTF light along its node's -Z. Straight down is
  `Rotation ( -90 0 0 )`; level, along +z (a locomotive's front), is
  `Rotation ( 0 180 0 )`.
- A point light shines all round; its turn only aims its disc.
- From a glTF model: MSTS shapes are left-handed, drawn with x mirrored, so
  a light node's `translation` is the `Position` with **x negated**, and
  its `rotation` (x y z w) the `QDirection` with **y and z negated** (in a
  `Rotation`, the y and z angles negated); its light's `color`,
  `intensity` and `range` are the same values, its `spot` angles
  (radians) in degrees. Such a light lights the scene exactly as the glTF
  light does.

## 4. Strength and reach

Lamps are rated in candela here, as in glTF. The example's street lamp
shines 60 cd down from a 6 m pole. Blender's glTF exporter writes a point
or spot light of P watts as P x 683 / (4 pi), about 54 cd a watt: a 5 W
lamp in Blender is 272 cd.

A light reaches as far as its strength carries it, up to TSRE's default
reach; `Range` can only shorten that. For a lamp that should light farther
(a floodlit yard), set the object's light reach in the route editor's
properties of the placed object.

### Far away

A light lights the scene as far as its object is drawn: up to the object
view distance, since a shape's last level is drawn on past its own
distance (Open Rails' viewing extension). A lamp seen only close by, such
as a cab's, a coach's or a station building's interior lamp, makes no
difference from afar, yet costs drawing time wherever it is. Two values
limit it:

- `ViewDistance ( m )` in the light's block: farther from the camera, the
  light lights nothing.
- `ESD_TSREDistanceLevelExtension ( … )` in the `.sd`'s `shape ( … )`
  block: what of the whole object is drawn on past the shape's last level
  (its last `dlevel_selection`, scaled by the LOD bias):

  | Value | Past the last level |
  | --- | --- |
  | `None` | Nothing drawn: no lights, no smoke (its emitters), no car lamps or loads. |
  | `MeshOnly` | The last level drawn on, its lights lighting nothing: its `.sd` lights, its glTF emissive surfaces' light, a car's ENG lamps. Glows stay. |
  | `All` | The last level drawn on with its lights. |
  | `Default`, or left out | As `All` (TSRE's default). |

```
shape ( coach.s
	ESD_Detail_Level ( 0 )
	ESD_TSREDistanceLevelExtension ( MeshOnly )
	ESD_TSRELight ( Name ( "SALOON" ) Position ( 0 2.6 0 ) Intensity ( 20 ) ViewDistance ( 150 ) )
)
```

A glTF model takes the same values as extras: `TSRE_viewDistance` (a
number) on a `KHR_lights_punctual` light, and `TSRE_distanceLevelExtension` (a
string) on its scene. Its last level ends only with `MSFT_lod` and a last
`MSFT_screencoverage` above 0; without levels it is never past them.

## 5. Headlights: lights switched by the ENG file

An ENG or WAG file's `Light` can switch a light of the car's shape: its
`ShapeHierarchy` names the shape light (its `Name`; a glTF model's light by
its `extras.OPENRAILS_light_name`, as Open Rails binds it). The ENG light
then says when the light is on (its `Conditions`); the shape light says
what it is (its place, aim, colour, strength, range and cone). The ENG
light's `State` is still written (Open Rails needs one), but its
`Position`, `Azimuth`, `Angle` and `LightColour` are not used for it.

In the `.sd`, the lamps at both ends, off unless the ENG turns them on:

```
ESD_TSRELight (
	Name ( "HEAD_FRONT" )
	Type ( Spot )
	On ( 0 )
	Position ( 0.15 3.2 9.75 )
	Rotation ( -3 180 0 )
	Intensity ( 6000 )
	Range ( 250 )
	Cone ( 4 10 )
)
ESD_TSRELight (
	Name ( "HEAD_REAR" )
	Type ( Spot )
	On ( 0 )
	Position ( 0.01 4.26 -9.65 )
	Rotation ( -3 0 0 )
	Intensity ( 6000 )
	Range ( 250 )
	Cone ( 4 10 )
)
```

In the ENG's `Lights` block, one `Light` for each end, by its `Unit`
condition (2: the train's first car; 5: the first car, running reversed,
its rear end leading):

```
Light (
	Type ( 1 )
	ShapeHierarchy ( "HEAD_FRONT" )
	Conditions ( Headlight ( 3 ) Unit ( 2 ) )
	States ( 1 State ( Duration ( 0 ) LightColour ( ffffffff ) Position ( 0 3.5 18 ) Radius ( 400 ) Angle ( 15 ) ) )
)
Light (
	Type ( 1 )
	ShapeHierarchy ( "HEAD_REAR" )
	Conditions ( Headlight ( 3 ) Unit ( 5 ) )
	States ( 1 State ( Duration ( 0 ) LightColour ( ffffffff ) Position ( 0 3.5 -18 ) Radius ( 400 ) Angle ( 15 ) ) )
)
```

- Several ENG lights may switch one shape light (dim and bright cones):
  it is on while any of them is.
- Each bound light is switched for each car on its own, by that car's
  place in the train and its controls.
- A bound cone gets no copy facing backwards (the copy MSTS and Open Rails
  add to every cone): bind each end as above.
- An ENG that binds a cone draws no headlight beam of its own from its
  other cones (TSRE's beam from the first lit cone, for content without
  bindings): bind every headlight you want.
- Fades and flashing (`FadeIn`, `FadeOut`, `Cycle`) do not apply yet: a
  bound light is switched on and off at once.
- Where no ENG drives the shape (the shape alone in the Shape Viewer, a
  locomotive's shape as scenery), its lights follow their `On`.
- A `ShapeHierarchy` naming a matrix of the shape, not a light, keeps the
  ENG light on the shape's root for now (with a message); placing ENG
  lights on matrices comes later.

## 6. Messages

TSRE tells the shape's author once a shape, in the messages window, and
again after the shape is reloaded:

- a block left out: no `Intensity` above 0, a `Type` other than `Point` or
  `Spot`;
- both of a pair given (the later used), a `ColorRGBA` that is not eight
  hex digits (white used), a `Cone` out of range (the nearest used);
- a `ShapeHierarchy` the shape has not (the light hangs on its root);
- an `ESD_TSREDistanceLevelExtension` (glTF:
  `TSRE_distanceLevelExtension`) that is none of `None`, `MeshOnly`, `All`
  and `Default` (ignored);
- for an ENG: a light's `ShapeHierarchy` naming a matrix (not placed there
  yet) or nothing of the shape, and the cones left without a beam when it
  binds others.

## 7. Open Rails

Open Rails skips `ESD_TSRELight` blocks without a message: the shape works
there unchanged, without the lights. It skips `ESD_TSREDistanceLevelExtension`
alike, and draws a shape's last level on as TSRE's default does. Saving a material in TSRE's Shape
Viewer keeps them. An ENG binding a glTF model's light by its
`OPENRAILS_light_name` binds the same light in Open Rails (which lights
only the first lit cone's).

## 8. Example

[Shape light example](../examples/shape-lights/README.md): a street lamp's
light as an S shape's `.sd` light, beside the same lamp as a glTF model.

## 9. Known limits

- No shadows from lamps (as glTF lights in TSRE).
- Lights switched by ENG lights only: switching by the time of day, by the
  cab view's controls (cab lamps), fades and flashing come later.
- No directional lights (the sun lights the scene).
