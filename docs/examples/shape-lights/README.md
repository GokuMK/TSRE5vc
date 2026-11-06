# Shape light example

The example of the [shape lights guide](../../guides/shape-lights.md): the
street lamp TSRE's tests use, its light as an S shape's `.sd` light. The
files are in `tests/renderer/lights`:

| File | What it is |
| --- | --- |
| `tsre_street_lamp.gltf` | The lamp as a glTF model: a 6 m pole, an arm, a head with a warm emissive lens and a spot light shining down (`KHR_lights_punctual`). Made by `make_street_lamp.py`. |
| `tsre_street_lamp_dark.gltf` | The same lamp without its light. |
| `tsre_street_lamp_light.s`, `.sd` | A light-only S shape: two 1 cm quads hidden in the lamp's head, and the glTF light as an `ESD_TSRELight`. Placed with the dark lamp, it lights the scene as the glTF lamp does. Made by `make_sd_lamp_light.py` from the glTF lamp. |

The `.sd`'s light, the glTF light's values in the S shape's coordinates
(MSTS shapes are left-handed, drawn with x mirrored): the light node's
translation with x negated as `Position`, its rotation with y and z negated
as `QDirection`, its spot angles of 0.6 and 1.05 radians in degrees:

```
ESD_TSRELight (
	Name ( "Street light" )
	Type ( Spot )
	Position ( -1.25 5.84 0 )
	QDirection ( -0.70710678 0.00000000 0.00000000 0.70710678 )
	Color3F ( 1 0.82 0.6 )
	Intensity ( 60 )
	Range ( 30 )
	Cone ( 34.3775 60.1606 )
)
```

`QDirection ( -0.7071 0 0 0.7071 )` turns the light from -z to straight
down; `Rotation ( -90 0 0 )` is the same. (A turn about x alone is the same
mirrored or not.)

For a locomotive's headlights switched by its ENG file, see the guide's
section 5: its two `.sd` lights and two ENG `Light` blocks (condensed) are
those of TSRE's check on a Dash 9 (USA2's EveGrain at 20:30).

On a shape of your own, put the block in its `.sd`, find the lamp's place
in the Shape Viewer (the matrix it hangs on in the Hierarchy window, the
place from the shape's coordinates), and check it in the route editor's 3D
view at night (time of day on, an evening time).

To see both lamps in a route, `lamp-root.sh` makes a game root of links in
which a route finds these files in its `SHAPES` folder, and
`lamp-views.json` (the glTF lamps) and `sd-lamp-views.json` (dark lamps
with the light-only shape) place 14 of them for the capture harness, as
the guide's verification did.
