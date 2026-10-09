# Task 05 - Environment Window, Sun and Moon

## Objective

A tool window in the Route Editor, like the navigation window, to change the
environment of the running editor: time of day, date, sky and fog. Changes
apply at once and last for the session; the profile keeps its values unless
the user saves them there. Today the only way is to edit the profile
settings, which is slow for something tried many times in a session.

The sky gets a sun, and a moon if the cost stays small (it does; see below).

Later steps of the same window: weather (rain, snow, wind, clouds) and the
route's ENV files.

## Current state (2026-10-09)

- **Settings** (profile values; the settings window edits them):
  - `core.rendering.timeOfDay.enabled`, `.time`, `.date`: applied live
    (`hot-cache`). Time is local mean solar time at the camera (task 22).
  - `core.rendering.skyColor`, `core.rendering.fogColor`,
    `core.rendering.fogDensity`: read when the renderer is built
    (`renderer-construction`), so a change needs a restart. Time of day also
    keeps its own copy of the day sky and fog colours
    (`RouteEditorGLWidget`, the saved state handed to `Daylight`).
  - `core.rendering.localLights.enabled`, `core.rendering.bloom`,
    `core.rendering.exposure`: live.
  - `core.startup.season`: chosen when the route loads (seasonal textures).
- **Session values**: `SettingsManager::setSessionValue` sets a value for
  the running editor only (source `ForcedSession`, above the profile and the
  command line) and emits `runtimeSettingsChanged`; `clearSessionValue`
  goes back to the profile's value. The load window and the elevation and
  imagery windows use it already.
- **Sky**: `Skydome` draws the route's sky shape (`skydome.s`) in the sky
  layer. No sun or moon.
- **Sun position**: `SunPosition` (NOAA equations, about 0.01 degrees) and
  `Daylight` (sun, ambient, sky and fog colours, lamp and signal light
  scales by sun elevation) from task 22.
- **ENV files**: `Environment` reads only the water layers of
  `ENVFILES/editor.env`. The route's `.trk` names an ENV file per season
  and weather (`Environment ( SpringClear ( ... ) SpringRain ( ... ) ... )`;
  CMK has twelve). They hold sky layers (clouds), sky satellites (sun and
  moon: rise and set times and positions, colours, scales), fog and
  animated water shaders, none of which is read.

## Design

### 1. The window

- `EnvironmentWindow` in `src/routeEditor`, a tool window as `NaviWindow`:
  shown and hidden from the View menu and a shortcut, remembers its place.
- Each control writes a **session value** of its setting
  (`setSessionValue`); the window listens to `runtimeSettingsChanged`, so it
  shows changes made elsewhere (settings window, profile reload).
- A control whose value differs from the profile is marked; per control and
  for the whole window, **Reset** clears the session values (back to the
  profile). **Save to profile** writes the current values into the profile
  (`setValue`, `save`) for those who want them kept.
- Nothing about the environment is stored in the route.

### 2. Contents of the first version

| Group | Controls | Setting |
|---|---|---|
| Time | on/off; time slider 0-24 h with a field; date picker; "today" | `timeOfDay.enabled`, `.time`, `.date` |
| Sky and fog | sky colour; fog colour; fog density | `skyColor`, `fogColor`, `fogDensity` |
| Lights | local lights on/off; bloom strength; exposure | `localLights.enabled`, `bloom`, `exposure` |
| Sun and moon | show sun; show moon | new settings, below |

A read-out under the time shows the sun's elevation and azimuth at the
camera, and the moon's phase.

### 3. Live sky and fog

Sky colour, fog colour and fog density become live (`hot-cache`):

- `GLUU` takes the new values on `runtimeSettingsChanged` instead of only
  at construction;
- time of day refreshes its saved day colours when they change (today it
  copies them once, when time of day turns on);
- both renderers read them per frame already (frame uniforms), so nothing
  else changes.

### 4. Sun

- A disc in the sky layer in the direction of `Game::sunLightDirection`
  (the sun of time of day; with time of day off, the fixed editor sun),
  drawn after the skydome and before distant terrain, so terrain and
  objects hide it and fog does not.
- Size: the real sun is 0.53 degrees across, small on screen; a few
  degrees, as MSTS and Open Rails draw it, reads better. A setting with a
  default of about 2 degrees.
- Colour from `Daylight`: white high up, orange near the horizon; hidden
  below the horizon.
- QRhi: emissive and bright enough for the bloom to give it a halo (the
  renderer's own bloom, task 24); also in the environment map faces, so it
  shows in reflections. OpenGL: a plain disc.
- Setting `core.rendering.sky.sun` (on).

### 5. Moon

Not much code: a moon position needs a short series (Meeus, "Astronomical
Algorithms" chapter 47, truncated: about 1 degree, plenty for a disc) with
the same date, place and sidereal time as `SunPosition`, about 80 lines.

- Position: ecliptic longitude and latitude from the date, to azimuth and
  elevation at the camera, turned by the grid bearing as the sun is.
- Phase: the angle between sun and moon; drawn as a disc shaded by the
  sun's direction (a sphere lit from the sun, in the shader), so the lit
  side faces the sun as in the real sky; the dark side close to the sky
  colour.
- Shown by night and by day when above the horizon; brighter at night.
  No moonlight on the scene in this task.
- Setting `core.rendering.sky.moon` (on); a test suite checks positions and
  phases against published values (full and new moons of 2026).

### 6. Later steps (future work)

- **Weather**: rain and snow (particles around the camera), wind (vegetation
  and smoke later), cloud layers on the sky, overcast light and fog.
- **ENV files**: choose the route's ENV file for a season and weather from
  the `.trk` (or any in `ENVFILES`), and read its sky layers, satellites
  and fog into the window's values; editing them is a later question.
- **Season**: switching the season in the window needs seasonal textures
  reloaded (shapes, terrain); a separate step.
- Stars at night; clock time with the route's time zone; time running
  (time-lapse) with a speed control; moonlight.

## Decisions for the user

1. **Window**: a floating tool window like the navigation window
   (suggested), or docked in the main window?
2. **Save to profile**: wanted, or session only with Reset?
3. **Sun size**: about 2 degrees across (suggested), the real 0.53, or a
   setting only?
4. **Moon**: in the first version (suggested, small), or later?
5. **Time running** (time-lapse) in the first version, or later?

## Steps

1. Window with time, date, sky, fog and lights as session values; sky and
   fog colours and fog density made live; Reset and Save to profile.
2. Sun disc on both renderers.
3. Moon position, phase and disc; suite against published values.
4. Later: weather, ENV files, season, stars, clock time.

## Verification

- A suite for the window's session values: a change reaches the runtime
  setting and leaves the profile file unchanged; Reset gives the profile
  value back; Save to profile writes it.
- `time-of-day` suite extended: the moon's position and phase.
- Captures at sunrise, noon, sunset and night on both renderers, with the
  sun and moon in view; the settings window's values unchanged after using
  the environment window.
