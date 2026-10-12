# Task 29 - Auto Exposure

Status: in progress (2026-10-13, `feature/clouds`).

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
- Signals: a constant between their day and night look (the ratio of a
  signal to a lamp is not physical today; to calibrate).
- Display-designed parts are compensated by 1 / E so they look as today:
  unlit shading (editor overlays, MSTS unlit parts), the colour of
  emissive lenses.

### Exposure

E = exposure setting x (1 / s(h)) x correction.

- Correction: (Lref / L)^k, limited to a few stops each way. L is the
  log-average luminance of the view (scene light, before the exposure),
  measured on the GPU (64 x 64, then 1 x 1) and read back a frame or two
  later. Lref is the log-average an ordinary outdoor scene has at that
  hour: s(h) x c x (sun + sky light of that hour), c calibrated on
  ordinary views. An ordinary scene needs no correction: night stays
  night by construction. A tunnel by day (L far under Lref) brightens a
  few stops, partially (k < 1), and its portal glows; a station full of
  lamps at night darkens a little.
- Adaptation over time in log space: faster towards a brighter picture
  than towards a darker one, as the eye. Still captures, camera jumps and
  large jumps of the time of day take the target at once.
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
