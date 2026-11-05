# Shape lights: lamps on S shapes

A **shape light** is a point or spot light that an MSTS shape (`.s`) carries
in its `.sd` file: a street lamp's light, a platform canopy's lamps, a
lantern. TSRE draws it as it draws a glTF model's lights
(`KHR_lights_punctual`): it lights the ground, walls, trains and the mist
around it, moves with the part of the shape it hangs on, and adapts to the
daylight as lamps do.

The example is TSRE's own (section 7).

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
| `Cone` | A spot's cone: the inner and outer angles from its axis, in degrees (full light inside the inner, none outside the outer). Up to 90. | `0 45` |
| `ShapeHierarchy` | The matrix it hangs on, by name; it moves with it (an animated part's light moves too). | The shape's root. |
| `Position` | Its place in metres, in that matrix's space. | `0 0 0` |
| `Rotation` | Its turn in degrees about x, then y, then z, as a display's square. | No turn. |
| `QDirection` | Or its turn as a quaternion `x y z w`, as a W file's object or a glTF node has it. | |
| `Radius` | The lamp's size in metres: it softens surfaces very close to the lamp, and is the disc's radius. | `0.05` |
| `Name` | Its name, shown in the Shape Viewer. | None |

Give one of `Color3F` and `ColorRGBA`, and one of `Rotation` and
`QDirection`; with both, the later in the block is used, with a message.

## 3. Placing and aiming

- Find the matrix names in the Shape Viewer's **Hierarchy** window. The
  shape's lights are listed there under the matrices they hang on (lights on
  the root at the bottom), with their type and strength.
- A light shines along **-z** of its matrix, turned by `Rotation` or
  `QDirection`, as a glTF light along its node's -Z. Straight down is
  `Rotation ( -90 0 0 )`; level, along +z, is `Rotation ( 0 180 0 )`.
- A point light shines all round; its turn only aims its disc.
- From a glTF model: a light node's `translation` is the `Position`, its
  `rotation` the `QDirection` as it is, its light's `color`, `intensity`
  and `range` the same values, and its `spot` angles (radians) in degrees.
  Such a light lights the scene exactly as the glTF light does.

## 4. Strength and reach

Lamps are rated in candela here, as in glTF. The example's street lamp
shines 60 cd down from a 6 m pole. Blender's glTF exporter writes a point
or spot light of P watts as P x 683 / (4 pi), about 54 cd a watt: a 5 W
lamp in Blender is 272 cd.

A light reaches as far as its strength carries it, up to TSRE's default
reach; `Range` can only shorten that. For a lamp that should light farther
(a floodlit yard), set the object's light reach in the route editor's
properties of the placed object.

## 5. Messages

TSRE tells the shape's author once a shape, in the messages window:

- a block left out: no `Intensity` above 0, a `Type` other than `Point` or
  `Spot`;
- both of a pair given (the later used), a `ColorRGBA` that is not eight
  hex digits (white used), a `Cone` out of range (the nearest used);
- a `ShapeHierarchy` the shape has not (the light hangs on its root).

## 6. Open Rails

Open Rails skips `ESD_TSRELight` blocks without a message: the shape works
there unchanged, without the lights. Saving a material in TSRE's Shape
Viewer keeps them.

## 7. Example

[Shape light example](../examples/shape-lights/README.md): a street lamp's
light as an S shape's `.sd` light, beside the same lamp as a glTF model.

## 8. Known limits

- No shadows from lamps (as glTF lights in TSRE).
- Lights are always on: switching by the time of day, flashing, or by a
  car's ENG light conditions (for headlights) come later.
- No directional lights (the sun lights the scene).
