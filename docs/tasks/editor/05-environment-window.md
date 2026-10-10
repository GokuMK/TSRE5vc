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

- `EnvironmentWindow` in `src/routeEditor`, a floating tool window as
  `NaviWindow` (user, 2026-10-09): shown and hidden from the View menu and
  a shortcut, remembers its place.
- Each control writes a **session value** of its setting
  (`setSessionValue`); the window listens to `runtimeSettingsChanged`, so it
  shows changes made elsewhere (settings window, profile reload).
- A control whose value differs from the profile is marked; per control and
  for the whole window, **Reset** clears the session values (back to the
  profile). **Save to profile** writes the current values into the profile
  (`setValue`, `save`) for those who want them kept (user: if it fits
  nicely; it does, the settings manager does both).
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
- Size: the real sun is 0.53 degrees across; MSTS and Open Rails draw it at
  a few degrees. With the bloom, the real size plus its halo may already
  look like theirs (user, 2026-10-09): start a little above the real size,
  keep it a setting (`core.rendering.sky.sunSize`, degrees), and fix the
  default from captures the user approves.
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
- Setting `core.rendering.sky.moon` (on); a test suite checks positions and
  phases against published values (full and new moons of 2026).
- In the first version (user, 2026-10-09).

### 6. Moonlight (bonus, user, 2026-10-09)

At night the moon lights the scene a little, by where it is and how full:

- When the sun is below the horizon and the moon above it, the scene's
  directional light comes from the moon: `Daylight` takes the moon's
  elevation and lit fraction and gives a cool, weak diffuse light, scaled
  by the lit fraction and fading in over the moon's first degrees above
  the horizon. Full moon high up: a few percent of the day's diffuse light
  (to be tuned by eye; real moonlight is far weaker than the eye's
  adaptation makes it look).
- The light's direction (`Game::sunLightDirection`, the direction shaders
  light with) then points from the moon; the sun disc keeps its own sun
  direction.
- **No moon shadows for now** (user, 2026-10-09): nice, but too expensive
  for so faint a light. The code must not rule them out: the light's
  direction and the shadow direction stay separate inputs, and shadows are
  simply not drawn under moonlight. A later option: only one shadow map for
  the moon, for example the middle one.
- At twilight the sun's light fades out before the moon's fades in, so
  there is no jump in direction while both are visible.

### 7. Later steps (future work)

- **Weather**: rain and snow (particles around the camera), wind (vegetation
  and smoke later), cloud layers on the sky, overcast light and fog.
- **ENV files**: choose the route's ENV file for a season and weather from
  the `.trk` (or any in `ENVFILES`), and read its sky layers, satellites
  and fog into the window's values; editing them is a later question.
- **Season**: switching the season in the window needs seasonal textures
  reloaded (shapes, terrain); a separate step.
- Stars at night; clock time with the route's time zone; time running
  (time-lapse) with a speed control (user: later).

## Decisions (user, 2026-10-09)

| Topic | Decision |
|---|---|
| Window | Floating, like the navigation window. |
| Save to profile | Yes, if it fits nicely. |
| Sun size | A little bigger than real; real size plus bloom may equal the others' 2 degrees. Needs visual approval. |
| Moon | In the first version; moonlight at night by its position as a bonus. |
| Time running | Later. |

## Steps

1. Window with time, date, sky, fog and lights as session values; sky and
   fog colours and fog density made live; Reset and Save to profile.
2. Sun disc on both renderers.
3. Moon position, phase and disc; suite against published values.
4. Moonlight at night.
5. Later: weather, ENV files, season, stars, clock time.

## Verification

- A suite for the window's session values: a change reaches the runtime
  setting and leaves the profile file unchanged; Reset gives the profile
  value back; Save to profile writes it.
- `time-of-day` suite extended: the moon's position and phase.
- Captures at sunrise, noon, sunset and night on both renderers, with the
  sun and moon in view (the sun's size approved by the user), and a full
  and a new moon night for moonlight; the settings window's values unchanged after using
  the environment window.

## Implementation status (2026-10-09)

Steps 1 to 3 (user: "go with 1, 2 and 3"):

- **Window**: `EnvironmentWindow` (Window > Environment Window), floating,
  hidden at start, laid out as the tool panels (F1, F2; user, 2026-10-09):
  headings in the accent colour, compact rows; every number has a field of
  one width and a slider over its range. Time: time of day, solar
  time (field and slider), date ("Today"), sun, moon, their size.
  Environment: fog colour, fog density. Rendering: local lights, bloom,
  exposure. Sky colour was left out (user: not that useful; the setting
  still applies live). Under the time rows, a read-out of the sun's and
  moon's elevation, the sun's bearing and the moon's lit share (from
  `RouteEditorGLWidget::environmentInfo`). Each control sets a session
  value; changed rows are bold and have a reset button; Reset all; Save to
  profile writes the values (`setValue`, `save`, `applyProfileToRuntime`)
  and clears the session values.
- **Live sky and fog**: `skyColor`, `fogColor`, `fogDensity` are `hot-cache`
  now; the editor takes them on the next frame (into `GLUU` and as the day
  colours of time of day).
- **Sun and moon**: `SkySatellites`, discs 1500 m out in the sky layer (in
  front of 2000 m MSTS skydomes, no fog there), drawn from the camera's
  position. Sun: where time of day puts it, or where the fixed light comes
  from; warm near the horizon; hidden once below it. Moon (time of day
  only): `MoonPosition` (Meeus' main terms, mean parallax), its lit part a
  mesh between the limb and the terminator ellipse, turned towards the sun;
  a dark side slightly lighter than the night sky at night. QRhi: emissive
  (sun 12, moon 1 by day to 3 at night times the colour), the sun's glow at
  full strength (`RenderItem::Pbr::GLOW_FULL`, replacing the signal light
  flag by a glow choice), the moon's following the lamps' scale. OpenGL:
  plain discs.
- **Settings**: `core.rendering.sky.sun` (on), `.moon` (on), `.sunSize`
  (both discs, 0.2 to 5 degrees). Default 1.2 (user, 2026-10-09): 0.8 looks
  right with the default bloom, but too small with bloom lowered or off;
  1.2 sits between, a little large with bloom and a little small without,
  fine in both.
- **Shared**: `DiscMesh` (the signal lights' disc, now shared with the sky).
- **Tests**: `environment-window` (session values, Reset, Save to profile
  on a temporary copy of the profile, live fog density); `time-of-day` with
  the moon against 2026 events (lunar eclipse of 3 March, solar eclipse of
  12 August, full, new and quarter moons, the full moon in the south at
  midnight); `settings` catalogue count 110.
- **Translations**: entries added by hand (en, pl). The `lupdate` target
  would also rewrite both files in its own layout (the files were last
  written by another tool; about 27,000 changed lines) and remove 35
  entries no code uses any more, so it was left for a commit of its own.
- **Not yet checked**: captures of the sun and moon on both renderers; the
  sun's size and brightness need the user's approval.
