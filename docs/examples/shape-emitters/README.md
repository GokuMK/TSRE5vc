# Shape emitter examples

The examples of the [shape emitters guide](../../guides/shape-emitters.md).

## Chimney pots on a cottage

MSTS's `ROUTES/EUROPE1/SHAPES/cottage3.s` has two chimney pots on its roof,
their tops at 7.79 m, at x 4.08 and 4.61 (found in the shape's points:
TSRE's shape converter, `--shapeconv -f unicode -c uncompressed`, writes
it as text). Its `.sd` with an emitter on each pot, after its own entries:

```
SIMISA@@@@@@@@@@JINX0t1t______

shape ( cottage3.s
	ESD_Detail_Level ( 0 )
	ESD_Alternative_Texture ( 0 )
	ESD_Bounding_Box ( -5.34707 0 -3.8411 5.34703 7.79261 3.8411 )
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
	ESD_TSREEmitter (
		Name ( "POT2" )
		Position ( 4.61 7.8 -0.02 )
		Diameter ( 0.22 )
		Kind ( Exhaust )
		Flow ( 0.04 )
		ExitSpeed ( 1.5 )
		Temperature ( 120 )
		Vapour ( 0.2 )
		Soot ( 0.4 )
		Variation ( 0.3 )
	)
)
```

Each placed cottage then smokes from both pots, a little more or less than
its neighbours (`Variation`).

## The test shape (`tests/renderer/emitters`)

| File | What it is |
| --- | --- |
| `tsre_smoke_demo.s`, `.sd` | A shape of two 1 cm quads at its foot; its `.sd` a chimney 8 m up (smoke, `Kind ( Exhaust )` with a small flow) and a steam vent 1 m up beside it, both `Variation ( 0.3 )`. Made by `make_smoke_demo.py`. |
| `emitter-views.json` | Six of them in a row on a route for the capture harness, a street view and an aerial one. |
| `emitter-root.sh` | A game root of links in which the route finds the shape, the route left unchanged. |

```bash
tests/renderer/emitters/emitter-root.sh ~/ORTS/MSTS EUROPE1 /tmp/emitroot
build/TSRE5vc --game-root /tmp/emitroot --route EUROPE1 --test \
    --test-suite=renderer-capture --test-cases tests/renderer/emitters/emitter-views.json \
    --test-label winter --set=core.rendering.timeOfDay.enabled=true \
    --set=core.rendering.timeOfDay.date=2026-01-15 --set=core.rendering.timeOfDay.time=8
```
