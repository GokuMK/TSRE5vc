# Settings System Phase 2A Audit

Master roadmap: [settings-system-plan.md](settings-system-plan.md).

Status: approved and used by the active Phase 2B runtime integration.

## Method

All 76 branches in `Game::load()` were matched to their default, every active
`Game::` call site, and the code that changes the value after startup. The
approved executable catalogue is registered by
`src/settings/SettingsRegistration.cpp`.
Descriptions, types, ranges, owners, application timing, and the one-level UI
subgroups now reflect those call sites rather than the legacy variable names.

`Keep` below means a hot or shared cached value remains necessary, preferably in
the named owning subsystem rather than automatically in `Game`. `Move` means the
owner can query Settings Manager at the indicated cold boundary. `Split` means a
profile default and mutable session state must not share one variable. `Remove`
means the legacy parser entry has no effective implementation or is an operation
rather than a preference.

Ranges without a code-level clamp are editor validation policy and remain part of
the approval decision. In particular, the practical caps for tile radius, texture
downscaling, framebuffer size, and map output size are protective UI limits, not
limits discovered in the old parser.

## Catalogue and migration matrix

### System

| Legacy key | Candidate key | Subgroup | Actual meaning / timing | Owner and Phase 2B direction |
|---|---|---|---|---|
| `consoleOutput` | `core.system.consoleOutput` | Logging | Mirrors Qt messages to the process console; startup | Logging bootstrap; Move |
| `useWorkingDir` | `core.system.useWorkingDirectory` | Legacy or inactive | Parsed after cwd selection and therefore ineffective | Startup bootstrap; Remove after separate cwd decision |
| `systemTheme` | `core.system.systemTheme` | Appearance | Chooses the system palette instead of TSRE's built-in dark theme and enables the configurable system-theme accent; startup | Application UI bootstrap; Move |
| — | `core.interface.accentColor` | Appearance | Used only with the system theme; controls headings and selections with contrast adjusted from the actual light or dark system palette. The built-in TSRE dark theme retains its coordinated fixed accent | Application theme; native setting, application restart |
| `fullscreen` | `core.interface.routeEditor.startMaximized` | Route Editor | Maximizes Route Editor; it is not full-screen | RouteEditorWindow; Move |
| `warningBox` | `core.system.warningBox` | Legacy or inactive | Only consumer is commented-out Consist Editor code | Consist Editor; Remove |
| `fpsLimit` | `core.system.fpsLimit` | Performance | Uses `1000/value` ms; zero selects the built-in 15 ms interval (about 67 Hz) | RouteEditorGLWidget; Move at widget construction |
| `soundEnabled` | `core.system.soundEnabled` | Audio | Controls sound construction and periodic updates | SoundManager/route session; Keep cached, apply on route reload |

### Content paths and Route Editor startup

| Legacy key | Candidate key | Subgroup | Actual meaning / timing | Owner and Phase 2B direction |
|---|---|---|---|---|
| `gameRoot` | `core.paths.gameRoot` | Content locations | Optional GLOBAL/ROUTES/TRAINS root, replaceable by load windows; generated default is empty | Content session; split profile default from selected session root; development value may use `--game-root` |
| `geoPath` | `core.paths.geoData` | Geodata | Optional directory used by HGT and TIFF tools; generated default is empty | GeoTools; Move/direct lookup; development value may use `--geo-path` |
| `routeName` | `core.startup.route` | Route Editor startup | Optional default route, later replaced by route selection/network load; generated default is empty | Route session; split profile default from selected route; development value may use `--route` |
| — | `core.startup.useTilePosition` | Route Editor startup | Explicitly selects whether startup tile X/Z are active | Route Editor startup; native boolean replaces legacy presence counting |
| `startTileX` | `core.startup.tileX` | Route Editor startup | Initial camera tile X when both coordinates are supplied | Route Editor startup; Move |
| `startTileY` | `core.startup.tileZ` | Route Editor startup | Initial camera tile Z; legacy name says Y | Route Editor startup; Move |
| `season` | `core.startup.season` | Route Editor startup | Seasonal content/texture variant used during loading; retained as a string until Open Rails combinations are audited | Route content session; Keep cached and apply on reload |

### Editing

| Legacy key | Candidate key | Subgroup | Actual meaning / timing | Owner and Phase 2B direction |
|---|---|---|---|---|
| `createNewIfNotExist` | `core.startup.createMissingRoute` | Route startup | Creates a route when the configured startup route is absent | Route startup; Move, possibly replace with explicit UI action |
| `writeEnabled` | `core.route.saving.enabled` | Route safety and recovery | Master gate on route/terrain/world writes; generated default is true | Route save policy; Keep hot cache; Advanced because disabling it is a special recovery mode |
| `writeTDB` | `core.route.saving.trackDatabase` | Route safety and recovery | Recommended true; false is a special recovery/inspection mode that can let other route files diverge from unsaved TrackDB/RoadDB data | TDB save policy; keep configured permission separate from session safety latch; Advanced |
| `deleteTrWatermarks` | `core.editing.deleteTrackWatermarks` | Route safety and recovery | Omits TrWatermarkObj records during serialization | TrWatermarkObj/save policy; Move at save time; Advanced diagnostic/recovery control |
| `deleteViewDbSpheres` | `core.editing.deleteViewDbSpheres` | Route safety and recovery | Omits ViewDB sphere records during Tile serialization | Tile save policy; Move at save time; Advanced diagnostic/recovery control |
| `leaveTrackShapeAfterDelete` | `core.editing.leaveTrackShapeAfterDelete` | Object editing | Keeps world geometry after deleting its TrackDB entry | RouteEditorGLWidget action; Move/action-time lookup |
| `autoFix` | `core.route.validation.autoFix` | Saving and recovery | Allows Tile/TDB load validation to repair supported errors | Validation policy; Move at route load |
| `numRecentItems` | `core.interface.routeEditor.recentItemLimit` | Route Editor | Size and count of recent placement items in ObjTools | ObjTools; Move at UI construction |
| `sortTileObjects` | `core.editing.sortTileObjects` | Route safety and recovery | Writes objects grouped by detail level instead of current order; disabling is useful only for old-route diagnostics | Tile serializer; Move at save time; Advanced diagnostic control |
| `routeMergeString` | — | Startup operations | Immediately runs `route:offsetX:offsetY:offsetZ` merge after load | Removed from profiles; retained as `--route-merge` command-line/`startup-args.txt` operation |
| `objectsToRemove` | `core.editing.objectsToRemove` | Route safety and recovery | Legacy object-type list consumed destructively by Tile error checking | Validation policy; Move at route load; Advanced |

### Camera

| Legacy key | Candidate key | Subgroup | Actual meaning / timing | Owner and Phase 2B direction |
|---|---|---|---|---|
| `cameraFov` | `core.camera.fieldOfView` | View | Vertical perspective FOV read in render projections and copied to cameras | Camera/renderer; Keep hot cache |
| `cameraSpeedMin` | `core.camera.speed.minimum` | Movement | Translation speed under slow modifier | Camera; Keep input cache |
| `cameraSpeedStd` | `core.camera.speed.standard` | Movement | Normal translation speed | Camera; Keep input cache |
| `cameraSpeedMax` | `core.camera.speed.maximum` | Movement | Translation speed under fast modifier | Camera; Keep input cache |
| `mouseSpeed` | `core.camera.mouseSensitivity` | Input | Mouse-look rotation multiplier | Camera input; Keep input cache |
| `cameraStickToTerrain` | `core.camera.stickToTerrain` | Movement | Constrains free camera above terrain and can be toggled at runtime | Camera; Keep session cache |
| `usenNumPad` | `core.camera.useNumericKeypad` | Input | Enables numeric-keypad Route Editor bindings | Camera input; Keep input cache |

### Rendering

| Legacy key | Candidate key | Subgroup | Actual meaning / timing | Owner and Phase 2B direction |
|---|---|---|---|---|
| `tileLod` | `core.rendering.tileRadius` | Visibility | Radius in 2048 m tiles; renders a `(2r+1) x (2r+1)` square, not terrain LOD; range extends to 128 for debug work | Route/Terrain renderer; Keep hot cache |
| `objectLod` | `core.rendering.objectLodDistance` | Visibility | Object/terrain culling distance and ordinary far plane; range extends to 200 km for debug work | Scene renderer; Keep hot cache |
| `maxObjLag` | `core.rendering.objectLoading.targetTokens` | Visibility | Target token level for incremental object loading | Shape loading scheduler; Keep hot cache |
| `allowObjLag` | `core.rendering.objectLoading.initialTokens` | Visibility | Configured initial token count; runtime editing resets the separate mutable counter | Shape loading scheduler; split saved initial value from runtime counter |
| `rendererPipeline` | `core.rendering.pipeline` | Renderer pipeline | Initial/selected renderer backend | Renderer controller; Keep cache |
| `rendererPipelineHotSwap` | `core.rendering.pipelineHotSwap` | Renderer pipeline | Permits the runtime pipeline-cycle command | Renderer controller; Keep cache |
| `gatherLegacyOverlays` | — | Renderer pipeline | Disabled diagnostic command used while testing new renderer modes | Remove from profiles; retain as a disabled command-line/`startup-args.txt` launch command with an explanatory note |
| `textureLoaderThreaded` | `core.rendering.threadedTextureLoading` | Textures | Copied to ACE/DDS libraries; changes loading execution model | AceLib/DdsLib; Keep subsystem startup cache |
| `textureQuality` | `core.rendering.textureDownscaleFactor` | Textures | Power-of-two enum dividing loaded ACE width and height; 1 means original dimensions | AceLib; Keep texture-load cache |
| `AASamples` | `core.rendering.antiAliasingSamples` | Shadows and anti-aliasing | Enum MSAA request (off/2/4/8/16) set before QApplication/OpenGL contexts | Graphics bootstrap; Move/startup-only |
| `shadowsEnabled` | `core.rendering.shadows.enabled` | Shadows and anti-aliasing | Route Editor shadow pass is active when legacy value is greater than zero | RouteEditorGLWidget; modern type is bool; Keep hot cache |
| `shadowMapSize` | `core.rendering.shadow.primaryMapSize` | Shadows and anti-aliasing | Power-of-two enum for near shadow framebuffer dimensions and legacy preset input | RouteEditorGLWidget; Move at renderer construction |
| `shadowLowMapSize` | `core.rendering.shadow.distantMapSize` | Shadows and anti-aliasing | Power-of-two enum for distant shadow framebuffer dimensions and preset input | RouteEditorGLWidget; Move at renderer construction |
| `oglDefaultLineWidth` | `core.rendering.defaultLineWidth` | Editor overlays | Default OpenGL line width for render items | Renderer; Keep hot cache |
| `fogDensity` | `core.rendering.fogDensity` | Environment | Copied into GLUU construction; other editors force zero | GLUU/scene; Keep renderer cache |
| `fogColor` | `core.rendering.fogColor` | Environment | Fog colour copied into scene rendering | GLUU/scene; Keep renderer cache |
| `skyColor` | `core.rendering.skyColor` | Environment | Scene clear-sky colour | GLUU/scene; Keep renderer cache |
| `renderTrItems` | `core.rendering.renderTrackItems` | Editor overlays | Shows TrackDB/RoadDB item markers in Route Editor | Route Editor scene; Keep hot cache |

### Interface

| Legacy key | Candidate key | Subgroup | Actual meaning / timing | Owner and Phase 2B direction |
|---|---|---|---|---|
| `toolsHidden` | `core.interface.hideTools` | Route Editor | Hides auxiliary Route Editor tool windows after layout construction | RouteEditorWindow; Move at startup |
| `mainWindowLayout` | `core.interface.mainWindowLayout` | Route Editor | Compact P/T/W panel startup code | RouteEditorWindow; Move at startup; later replace with structured layout state |
| `ceWindowLayout` | `core.interface.consistWindowLayout` | Consist Editor | Compact C/1/2/U panel startup code | ConEditorWindow; Move at startup |
| `colorConView` | `core.interface.consistBackground` | Consist Editor | Nullable Consist preview override; null uses the widget/application default | ConEditorWindow; Move at construction |
| `colorShapeView` | `core.interface.shapeBackground` | Shape Viewer | Nullable shape preview override; null uses the widget/application default | Preview widgets; Move at construction |
| `hudEnabled` | `core.interface.hud.enabled` | HUD | Enables Route Editor overlay rendering | HUD/renderer; Keep hot cache |
| `hudScale` | `core.interface.hud.scale` | HUD | HUD orthographic projection scale | HUD/renderer; Keep hot cache |
| `editorFpsHudEnabled` | `core.interface.hud.showEditorFps` | HUD | Adds frame diagnostics to HUD | HUD; Keep hot cache |
| `markerLines` | `core.interface.markerLines` | Route Editor | Builds line geometry for imported route markers | Marker loader; Move at marker load/rebuild |

### Terrain and geometry

| Legacy key | Candidate key | Subgroup | Actual meaning / timing | Owner and Phase 2B direction |
|---|---|---|---|---|
| `defaultElevationBox` | `core.track.defaultGradeFormat` | Track editing | Named enum: permille, percent, one-in-X, or angle; legacy integer is migration input only | Track property panels; Move at UI construction |
| `defaultMoveStep` | `core.editing.defaultMoveStep` | Track editing | Initial movement increment and live-track quantization grid | Route Editor/track panels; Keep session cache |
| `trackElevationMaxPm` | `core.track.maximumElevationPermille` | Track editing | Maximum absolute grade accepted by track property panels | Track property panels; Keep action cache |
| `snapableOnlyRot` | `core.track.snapRotationOnly` | Object editing | Startup default for Auto Placement's rotation-only snapping; panel changes remain session-only | Route/ObjTools; Keep route cache |
| — | `core.track.snapRadius` | Object editing | Startup default for Auto Placement's maximum snapping distance; panel changes remain session-only | Route/ObjTools; native setting, keep route cache |
| `proceduralTracks` | `core.track.proceduralMode` | Track editing | Three modes exist: Disabled, Enabled, and Forced; the modern default is Enabled | ProceduralTrackPolicy; Keep render/object cache |
| `seasonalEditing` | `core.terrain.seasonalEditing` | Terrain | Reads/writes seasonal terrain paths when a season is selected | Terrain; Keep route cache |
| `loadAllWFiles` | `core.route.loading.preloadAllWorldFiles` | Loading | Preloads every world tile; server mode forces true | Route loader; Move at route load |
| `useOnlyPositiveQuaternions` | `core.geometry.positiveQuaternionsOnly` | Saving and recovery | Canonicalizes equivalent quaternion signs to a non-negative W component; affected objects are marked modified during loading | WorldObj loading fixes and serializer; Move at route load and save time |

### Maps, shared content loading, network, and advanced storage

| Legacy key | Candidate key | Subgroup | Actual meaning / timing | Owner and Phase 2B direction |
|---|---|---|---|---|
| `imageMapsUrl` | `core.maps.imageryUrl` | Online imagery | HTTP template with `{lat}`, `{lon}`, `{zoom}`, `{res}`, and `{apikey}` | MapDataUrlImage; resolve `{apikey}` from the separate secret at request time |
| — | `core.maps.imageryApiKey` | Online imagery | Native secret reference; its secret value replaces `{apikey}` | MapDataUrlImage; direct request-time lookup, no legacy plaintext field |
| `mapImageResolution` | `core.maps.imageResolution` | Online imagery | Width/height of assembled map output, not each provider request | MapWindow; Move at generation time |
| `ortsEngEnable` | `core.content.loading.preferOpenRailsEng` | Loading | Tries ENG/include files in each trainset OpenRails subdirectory | Shared Eng loader; Move at rolling-stock load |
| `serverLogin` | `core.network.clientLogin` | Route Editor client | Visible `username[:password]@host[:port]` template with protected parts referenced as `{secret:ID}` | Client bootstrap; resolve secrets only at connection time; later split structured fields if useful |
| `serverAuth` | `core.network.serverAuthenticationMode` | Route Editor server | Authentication mode: empty means none, `file` means users.txt | RouteEditorServer; enum, not a secret |
| `playerMode` | `core.network.playerMode` | Experimental simulation | Hides editing tools/pointer and skips the interactive TrackSection repair prompt | RouteEditor/Route validation; Keep session cache; Advanced experimental feature |
| `useNetworkEng` | `core.network.useNetworkEngine` | Experimental simulation | Uses TrainNetworkEng for speed and publishes locomotive telemetry | Eng simulation; Keep simulation cache; Advanced experimental feature |
| `useQuadTree` | `core.advanced.useQuadTree` | Route storage | Selects TerrainLibQt versus TerrainLibSimple; client mode forces true | Route construction; Move at route load |
| `useTdbEmptyItems` | `core.advanced.useEmptyTrackItems` | Route storage | Reuses `emptyitem` TrackDB slots when allocating an ID | TDB allocator; Keep allocator cache |
| `ignoreMissingGlobalShapes` | `core.advanced.ignoreMissingGlobalShapes` | Compatibility | Filters absent GLOBAL track shapes from placement lists | ObjTools; Move at route-tools load |

## Approved Phase 2A decisions

The initial meanings, keys, groups, and subgroups are accepted for the first
implementation, including the corrected `tileRadius`, `textureDownscaleFactor`,
`defaultGradeFormat`, `clientLogin`, and `serverAuthenticationMode` names.

The object-loading scheduler is split into saved configuration and runtime state:

- `objectLoading.targetTokens` remains a dynamically editable target.
- `objectLoading.initialTokens` is a dynamically editable saved value.
- The scheduler owns a separate `currentTokens` counter. Loading and frame-based
  refill change only this counter and never write those changes to the profile.
- Editing `initialTokens` at runtime resets `currentTokens` once. The only expected
  side effect is a temporary loading burst or pause while it refills.

Legacy non-preference fields are handled as follows:

- `useWorkingDir` stays inactive pending the separate working-directory task and
  is not migrated as a functioning preference.
- `warningBox` is removed because its only consumer is commented out.
- `gatherLegacyOverlays` is removed from generated profiles. It is retained as a
  disabled renderer-test command-line/`startup-args.txt` launch command, with a
  note explaining that it currently has no effect.
- `routeMergeString` becomes an explicit one-shot operation/launch command rather
  than a persistent preference.

Generated content-path defaults are empty and portable. Development-specific
values can be supplied without editing a profile through `--game-root`, `--route`,
and `--geo-path`, either on the command line or in `startup-args.txt`. Startup tile
coordinates are guarded by the native
`core.startup.useTilePosition` boolean, so `(0, 0)` remains distinguishable from
“not configured.” `writeEnabled` defaults to true.

The user-facing `apply` field uses only four lifecycle values: `dynamic`,
`routeReload`, `rendererRestart`, and `applicationRestart`. Cold action/save/load
consumers are dynamic: they read the current value when starting, and background
operations snapshot their inputs before leaving the GUI thread. No separate
"next action" state is maintained. Detailed
implementation timing remains in the transitional `implementation.access` field.
`implementation.requiresRuntimeCache` replaces the misleading
`requiresGameMember`: a cached value belongs in the named owner, not necessarily
in `Game`.

`writeTrackDatabase` defaults to true and that enabled state is recommended.
Setting it to false is an advanced, unsafe special-use mode for recovery or
inspection: other route files may still be written while TrackDB/RoadDB are not,
leaving them mutually inconsistent. The runtime safety latch remains separate so
validation can temporarily suspend database writes without silently changing the
user's saved permission.

For the current single imagery provider, `imageryUrl` contains `{apikey}` and
`imageryApiKey` is a `secret` setting whose value is a reference into
`secrets.json`. Advanced templates can instead use the explicit `{secret:ID}`
syntax. Arbitrary placeholders such as `{GOOGLE_KEY}` are not treated as secrets,
so ordinary URL variables remain distinguishable and secret-reference typos can
be validated.

A future multiple-provider schema should store provider objects with an ID, URL
template, `apiKeyRef`, and optional ordinary variables, plus a separate active
provider ID. The same explicit secret syntax can be reused inside those provider
templates without complicating the initial single-provider schema.

String settings may reference protected fragments explicitly as `{secret:ID}`.
The Settings Manager validates IDs containing letters, numbers, `.`, `_`, or `-`
and resolves them only at the consumer boundary; resolved values are never written
back into settings JSON. Missing references report the ID without exposing other
secret data. Route Editor client login therefore remains visible as a connection
template while only its password fragment needs to be secret. A later structured
endpoint/user/password split remains possible.

Part 1 GUI-test profiles require no migration and may be deleted. Fresh profiles
are generated from the approved 76-setting registry; renamed draft aliases and
removed operations are not seeded.
