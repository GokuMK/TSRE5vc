# Task 29 - Auto Exposure

Status: implemented (2026-10-13, `feature/clouds`); Dimmed stays the default.

## Objective

Replace the lamps' daylight scale (task 21) with an exposure that adapts
to the light, as an eye or a camera does: lamps keep one strength all day,
the sun outshines them by day, the view adapts. Night stays night: the
adaptation must not turn a night scene into a dim day.

## Decisions (user, 2026-10-12/13)

- Selectable, with the current behaviour kept: setting "Lamps by day"
  (`core.rendering.lampsByDay`):
  - **Dimmed** (default for now): today's scale, lamps at 3 % with the
    sun 20 degrees up, full at sunset (`Daylight::localLights`).
  - **Full strength**: no compensation, lamps as at night all day.
  - **Auto exposure**: this task. QRhi renderer with tone mapping (the
    view must hold light brighter than white); otherwise as Dimmed.
- The user likes AgX: auto exposure is meant to be used with it.

## Today (before this task)

- Light levels are display values: sun 0.7, the sky's ambient 0.3 by day,
  about 0.04 at night (`Daylight::forElevation`), multiplying the texture
  in display (gamma) space; lamps add in linear light. Lamps and their
  glow are scaled by `localLights(h)`: `DayLocalLights` (0.03) with the
  sun 20 degrees up to 1 at sunset, evenly in log space; signals by
  `DaySignalLights` (0.5) to 1.
- The view holds floats only with tone mapping on. The final pass takes
  the view to linear light, adds bloom, multiplies by the exposure
  setting (stops) and applies the curve.
- The physical sky and the clouds map their light to display colour
  themselves (`Atmosphere::SkyExposure`, the clouds' composite).
- The sky enclosure (task 27) keeps lamps at night strength under cover by
  day; its darkening of the sky's light stays needed with HDR.

## Design

### One light scale

Today, at a sun elevation h, the lamps are scaled by A(h) = `localLights`
and everything else is designed as D(h). The same picture in one scale:
lamps at the constant `DayLocalLights`, everything lit by the sun and the
sky (sun, sky ambient, fog, sky, clouds, shafts) times the scene light
s(h) = `DayLocalLights` / A(h): 1 by day, 0.03 at night. An exposure of
1 / s(h) shows exactly today's picture.

- Gamma-space terms (the light colours, fog, clear colours, the sky's and
  the clouds' display output) are scaled by s^(1/2.2), so linear light
  scales by s.
- Lamps: gathered at `DayLocalLights`, no per-pixel day scale (the
  enclosure's night strength under cover is not needed: the exposure
  finds the tunnel dark). Their glow likewise.
- Signals: they only glow, they light nothing, and they must be seen: they
  keep their Dimmed scale (`signalLights`) and the renderer divides their
  glow by E, so they look as in Dimmed at any exposure. One constant scale
  could not do both: a quarter of Dimmed by day was too weak, four times
  at night glared.
- Display-designed parts are compensated by 1 / E so they look as today:
  unlit shading (editor overlays, MSTS unlit parts, plain-coloured lines
  and markers), the colour of emissive lenses.
- Fog under cover is lit as the ambient is (`skyAmbient(shade)`): the full
  sky colour as haze deep in a tunnel was invisible at today's exposure
  and a bright fog at a tunnel's.

### Exposure

E = exposure setting x 2^e, e = log2(1 / s(h)) + correction.

- L is the log-average luminance of the view (scene light, before the
  exposure), measured on the GPU (64 x 64, then 1 x 1) and read back a
  frame or two later. Lref(h) is the log-average of an ordinary outdoor
  view at that hour (`Daylight::viewLuminanceLog2`, a table measured on
  the CMK lake, plus log2 s(h)).
- The eye's curve (`Daylight::adaptationCurve`): ordinary views from night
  to a high sun give pairs (Lref(h), log2(1 / s(h))), luminance against
  the exposure that shows them as Dimmed. A view takes the exposure of the
  ordinary view as bright as it is: a tunnel by day that of dusk, a
  station lit by its lamps that of the night, at any hour. Below the
  darkest point the night's exposure: no view is shown brighter than the
  eye sees at night, so night stays night by construction (night views
  are Dimmed's). Above the brightest point the exposure falls by 0.75 of
  the excess, at most 2 stops (snow, the sun).
- Views within 1 stop of the hour's ordinary view keep its exposure (L is
  moved up to 1 stop towards Lref first): the adaptation is for tunnels
  and stations, not for a darker field.
- The first attempt, a correction k (Lref(h) - L) around the hour's
  exposure, made a station at dusk ten times brighter than at night: the
  hour's reference, not the light, set the target.
- Adaptation over time in log space: towards a brighter picture in 2.5 s,
  towards a darker one in 0.5 s, as the eye. Camera jumps, the first
  view, jumps of the time of day (over 0.1 h) and of the scene light (over
  half a stop) take the target at once until two measurements of the new
  view, dropping read-backs of the view before.
- Time of day off (the editing light): no auto exposure.

## Steps

1. Setting with Dimmed, Full strength, Auto exposure (Auto as Dimmed
   until step 2).
2. Scene light scale, lamp and glow scales, display compensation.
3. Luminance measurement, read-back, exposure with adaptation.
4. Calibration on CMK (lake by day, sunset, night), the EUROPE1 tunnel by
   day and its portal, the CMK underground station, the CMK hall at night;
   signals.

## Verification

- With the correction fixed at 1, auto exposure gives today's picture
  (Dimmed) at noon, sunset and night.
- Tunnel by day: brighter inside than Dimmed, the portal bright; night
  views about as Dimmed; overlays unchanged.
- Cost: the measurement and read-back, a fraction of a millisecond.

## Results (2026-10-13)

Captures at 1280 x 800, AgX, physical sky, fair cumulus, bloom; Dimmed
against Auto exposure (`build/renderer-autoexposure`).

- CMK lake, 12 hours from 6:00 to 23:00: correction 0.00 at every hour
  (the table is calibrated there); against Dimmed a mean difference of
  0.1 to 3 levels of 255, no pixel over 24. The plain-coloured overlays
  were blown out at night until the untextured branch got the unlit
  compensation.
- EUROPE1 portal, seen from outside (a stone cut in shade): +3.2 stops at
  noon (the cut reads as a day view in shade, the tunnel mouth visible),
  +1.4 at 19:30, Dimmed's picture at 22:00.
- EUROPE1 deep tunnel (enclosed ambient 0.1): the night's exposure by day
  (+5.06 stops), walls faint, the exit glowing; black at night as Dimmed.
  Before the fog fix the haze lit it five stops brighter than its walls.
- CMK underground station (enclosed ambient 0.1): at noon and at 19:30
  the night's exposure, the station lit by its lamps as at night (what
  Dimmed's night strength under cover gave by a rule); at 22:00 equal to
  Dimmed (mean difference 0.02 levels).
- CMK hall: +0.26 stops at noon (the hall is open at its sides, the lamps
  outshone by day), 0 at 19:30, equal to Dimmed at 22:00 (means 15.66 and
  15.66).
- Signals: as Dimmed at 19:30 and 22:00; at noon the whole sky-heavy view
  0.35 stops darker (past the dead zone), the lens with it.
- Lamp ranges follow the exposure (`gatherLights` rangeGain, in half
  stops): gathered at 0.03 they reached about 7 m instead of the 30 m cap,
  and the station at night was darker than Dimmed.
- Suites: rhi-shaders, time-of-day (three new cases), local-lights,
  settings, sky, water-gl, environment-map-gl, editor-tools, map-view,
  token-world pass.

## Exposure offset (2026-10-13)

The user tested auto exposure with AgX: the picture was too dark, the
evening sky above all. Auto exposure now adds an offset to Dimmed's
exposure, by the sun's elevation (`Daylight::exposureBias`): +0.25 stops
with the sun 20 degrees up, rising with the lamps' fade to +1.5 at sunset,
then through twilight to +1.75 with the sun 12 degrees down. Tuned by the
user on CMK in clear weather (camera
-5350,14859,-778.45,278.47,-208.37,82.16,-5.60), in three rounds: +0.25 /
+1, then +1.25 / +1.5, then +1.5 / +1.75. Mean brightness of that view,
without the offset and with it: noon 103 and 109; 19:30 71 and 95 (the
sky 144 and 184); 20:30 16 and 31 (the dusk sky 30 and 59). The night stays
black (the user: not an issue). Overlays, lenses and signals undo it with
the rest of the exposure.

The map view (user: it should not use auto exposure): the renderer kept the
last 3D frame's exposure control, as only the 3D path handed one over. The
map's layers draw unlit, so the exposure cancelled on them (the map was the
same in Dimmed and Auto); what changed its colours was the tone curve, in
every mode (with AgX the pointer's 255,120,0 showed as 212,136,65). The map
now turns auto exposure off and draws a plain image
(`Renderer::setPlainImage`): no tone curve, exposure or bloom; it matches the
map without tone mapping within 2 levels. Back in 3D, the first frame with
auto exposure takes its target at once.

Signals (user: too weak against the sun's and the moon's bloom after the
offset): their glow undid the whole exposure, the offset too. It now takes
the offset as the view does, not the adaptation (no glare in a tunnel by
day), times 2 (`AutoSignalBoost`; 1 was barely visible by day, 3 a
floodlight at night). Red of the CMK signal head against Dimmed: 1.7 times
at 13:00, 2.3 at 19:30, 3.7 at 22:00. The lit lens itself keeps Dimmed's
brightness.

Tried and reverted on the way: the sky weighted a fifth in the measurement
with the table recalibrated, and the offset carried by the adaptation curve.
Recalibrating on the same view cancels any overall gain from the weighting;
it only changes how views compare with the lake (0.18 stops between the
lake and a sunlit town view).

Next: cloudy days. They measure as bright as the lake view or brighter
(overcast lake -4.78 against -5.38, overcast town -5.16), as the lake view
looks towards the sun over a shaded forest and dark water: the reference
is a dim view. To try first: a reference from a well-lit view (sun behind
the camera, 1.3 stops brighter) with a smaller dead zone, watching for the
exposure jumping as the camera turns; if it does, sky weighting against a
well-lit reference.

Tried (2026-10-13) and kept for testing with the weather timelapse, the
tuning paused (`feature/clouds`, commit "WIP: auto exposure for cloudy
days"):

- The daytime table from the user's CMK camera turned away from the sun
  (clear sky): +0.4 stops over the lake at 6:00 and 18:00, +1.0 to +1.4 from
  9:00 to 15:00. The table from sunset down unchanged.
- The dead zone 0.5 stops (the user's first value).
- The curve gives every hour with the sun 20 degrees up the same exposure,
  so it cannot adapt within the day: a daytime metering term besides, 0.75
  of the difference beyond the dead zone, gone from sunset, never above the
  night's exposure; the stronger of the two wins.
- The user's camera at noon, correction and ground brightness before and
  after: clear -0.28 / 0 (56.4 / 62.1), broken 0 / +0.43 (24.2 / 31.0),
  overcast 0 / +0.29 (28.7 / 33.3), rain 0 / +0.44 (28.9 / 35.5). Cloudy
  views measure only about 1 stop under the clear one, their bright sky in
  the measurement: clear ground stays about twice the overcast.
- Turning the camera: clear 0 in every direction; broken +0.38 to +1.46
  (darkest towards the sun, the ground in the clouds' shadow).
- Next: the sky weighted down in the measurement against this reference.

## Later

- The enclosed ambient sets how bright a deep tunnel is by day: with auto
  exposure it is seen at the night's exposure, 0.3 (the default) shows
  grey walls, 0.1 faint ones.
- The dead zone (1 stop) and the darkening above the brightest ordinary
  view (0.75, at most 2 stops) are first values; to tune on routes.
