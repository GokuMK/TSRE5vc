#include <settings/DraftSettingsCatalog.h>

#include <settings/SettingsRegistry.h>

#include <QSet>

namespace {
QString applyModeForAccess(const QString &access) {
    static const QSet<QString> dynamic{
        "hot-cache", "cached", "session-state", "session-safety-state",
        "session-cache", "simulation-cache",
        "action-time", "save-time", "direct", "request-time", "generation-time",
        "allocation-time", "texture-load-time", "rolling-stock-load-time"
    };
    static const QSet<QString> routeReload{
        "startup-default", "route-reload", "load-time", "route-load-time",
        "route-construction", "route-cache", "route-tools-load-time",
        "marker-load-time", "terrain-load-save-time", "object-render-cache"
    };
    static const QSet<QString> rendererRestart{
        "renderer-restart", "renderer-construction"
    };
    if (dynamic.contains(access)) return "dynamic";
    if (routeReload.contains(access)) return "routeReload";
    if (rendererRestart.contains(access)) return "rendererRestart";
    return "applicationRestart";
}

bool add(SettingsRegistry &registry, SettingsDefinition definition,
         const QString &legacyKey, const QString &legacySymbol,
         const QString &owner, bool runtimeCache, const QString &access,
         QString *error) {
    if (!legacyKey.isEmpty())
        definition.legacyFileKey(legacyKey);
    if (!legacySymbol.isEmpty())
        definition.legacyCodeSymbol(legacySymbol);
    definition.implementedBy(owner, runtimeCache, access)
            .applies(applyModeForAccess(access));
    return registry.define(definition, error);
}

QVector<SettingOption> choices(std::initializer_list<std::pair<const char*, const char*> > values) {
    QVector<SettingOption> result;
    for (const auto &value : values)
        result.append({QString::fromUtf8(value.first), QString::fromUtf8(value.second)});
    return result;
}

QVector<SettingOption> numericChoices(
        std::initializer_list<std::pair<int, const char*> > values) {
    QVector<SettingOption> result;
    for (const auto &value : values)
        result.append({value.first, QString::fromUtf8(value.second)});
    return result;
}
}

bool DraftSettingsCatalog::registerDefinitions(SettingsRegistry &registry, QString *error) {
    const QVector<SettingsGroupDefinition> groups = {
        {"system", "System", "Application startup, logging, and general behaviour.", 10,
         {{"startup", "Startup", "Process and application startup behaviour.", 10},
          {"logging", "Logging", "Diagnostic output.", 20},
          {"audio", "Audio", "Sound playback.", 30},
          {"legacy", "Legacy or inactive", "Parsed legacy values with no effective implementation.", 90}}},
        {"profiles", "Paths and startup", "Content locations and startup selection.", 20,
         {{"content", "Content locations", "Directories containing simulator or geographic content.", 10},
          {"route", "Route Editor startup", "Initial route and camera location.", 20}}},
        {"editing", "Editing", "Route editing, saving, cleanup, and object behaviour.", 30,
         {{"saving", "Saving", "Write permissions and serialization behaviour.", 10},
          {"cleanup", "Validation and cleanup", "Repair and cleanup behaviour while loading route data.", 20},
          {"objects", "Object editing", "Placement, deletion, and recent-object behaviour.", 30},
          {"operations", "Startup operations", "One-shot route operations, not ordinary preferences.", 90}}},
        {"camera", "Camera", "Camera movement and navigation controls.", 40,
         {{"view", "View", "Camera projection.", 10},
          {"movement", "Movement", "Camera movement speeds and terrain following.", 20},
          {"input", "Input", "Mouse and keyboard controls.", 30}}},
        {"rendering", "Rendering", "Renderer, texture, shadow, fog, and visibility options.", 50,
         {{"visibility", "Visibility", "Scene radius, distance, and load throttling.", 10},
          {"pipeline", "Renderer pipeline", "Renderer backend selection and diagnostics.", 20},
          {"textures", "Textures", "Texture loading and resolution.", 30},
          {"shadows", "Shadows and anti-aliasing", "Framebuffer and sampling options.", 40},
          {"environment", "Environment", "Fog and sky appearance.", 50},
          {"overlays", "Editor overlays", "Track items and line rendering.", 60}}},
        {"interface", "Interface", "Window layout, colours, HUD, and UI behaviour.", 60,
         {{"routeEditor", "Route Editor", "Route Editor windows and markers.", 10},
          {"consistEditor", "Consist Editor", "Consist Editor layout and preview.", 20},
          {"shapeViewer", "Shape Viewer", "Shape preview appearance.", 30},
          {"hud", "HUD", "Heads-up display.", 40}}},
        {"terrain", "Terrain and geometry", "Terrain, track, coordinate, and geometry options.", 70,
         {{"track", "Track editing", "Track grade, snapping, and procedural rendering.", 10},
          {"terrain", "Terrain", "Terrain loading and seasonal editing.", 20},
          {"serialization", "Geometry serialization", "Compatibility rules used when saving objects.", 30}}},
        {"maps", "Maps and geodata", "Map imagery and geographic data sources.", 80,
         {{"imagery", "Online imagery", "Background-map download and output options.", 10}}},
        {"consist", "Consist Editor", "Rolling-stock and Consist Editor options.", 90,
         {{"rollingStock", "Rolling stock", "ENG and WAG file loading behaviour.", 10}}},
        {"network", "Network", "Server, client, and remote editor options.", 100,
         {{"client", "Route Editor client", "Remote editor connection.", 10},
          {"server", "Route Editor server", "Server authentication.", 20},
          {"simulation", "External simulation", "External locomotive data integration.", 30}}},
        {"advanced", "Advanced", "Specialized and compatibility settings.", 110,
         {{"routeStorage", "Route storage", "World and TrackDB storage implementations.", 10},
          {"compatibility", "Compatibility", "Legacy content compatibility behaviour.", 20}}}
    };
    for (const SettingsGroupDefinition &group : groups) {
        if (!registry.defineGroup(group, error))
            return false;
    }

    int order = 0;
#define ADD(def, oldKey, symbol, owner, runtimeCache, access) \
    do { SettingsDefinition value = (def); value.order = (order += 10); \
         if (!add(registry, value, oldKey, symbol, owner, runtimeCache, access, error)) return false; } while (false)

    ADD(SettingsDefinition::boolean("core.system.consoleOutput", false)
            .withName("Console output").withDescription("Mirror Qt log messages to the process console as well as log.txt.").inGroup("system").inSubgroup("logging"),
        "consoleOutput", "Game::consoleOutput", "Game", false, "startup");
    ADD(SettingsDefinition::boolean("core.system.useWorkingDirectory", false)
            .withName("Use launch working directory (inactive)").withDescription("Legacy option intended to preserve the launch directory. It is parsed only after the working directory has already been selected and currently has no effect.").inGroup("system").inSubgroup("legacy").asAdvanced(),
        "useWorkingDir", "Game::UseWorkingDir", "Application startup", false, "obsolete");
    ADD(SettingsDefinition::boolean("core.system.systemTheme", false)
            .withName("Use system theme").withDescription("Use the operating-system palette instead of applying TSRE's dark application palette.").inGroup("system").inSubgroup("startup"),
        "systemTheme", "Game::systemTheme", "Game", false, "startup");
    ADD(SettingsDefinition::boolean("core.interface.routeEditor.startMaximized", false)
            .withName("Start Route Editor maximized").withDescription("Open the Route Editor maximized. Despite the legacy name, this does not enter a borderless full-screen mode.").inGroup("interface").inSubgroup("routeEditor"),
        "fullscreen", "Game::fullscreen", "RouteEditorWindow", false, "startup");
    ADD(SettingsDefinition::boolean("core.system.warningBox", false)
            .withName("Safety warning box (inactive)").withDescription("Legacy Consist Editor warning toggle. The only call site is commented out, so this setting currently has no effect.").inGroup("system").inSubgroup("legacy").asAdvanced(),
        "warningBox", "Game::warningBox", "Consist Editor", false, "obsolete");
    ADD(SettingsDefinition::integer("core.system.fpsLimit", 0)
            .withName("Frame-rate limit").withDescription("Set the Route Editor render timer interval to 1000 divided by this value. Zero uses the built-in 15 ms interval (about 67 updates per second).").withRange(0, 1000, 1).withUnit("fps").inGroup("system").inSubgroup("startup"),
        "fpsLimit", "Game::fpsLimit", "RouteEditorGLWidget", false, "startup");
    ADD(SettingsDefinition::boolean("core.system.soundEnabled", false)
            .withName("Enable route sounds").withDescription("Load and update route, rolling-stock, and ambient sounds in the Route Editor.").inGroup("system").inSubgroup("audio"),
        "soundEnabled", "Game::soundEnabled", "SoundManager", true, "route-reload");

    order = 0;
    ADD(SettingsDefinition::string("core.paths.gameRoot", "", SettingType::Directory)
            .withName("Train Simulator root").withDescription("Root directory containing GLOBAL, ROUTES, and TRAINS. Editors can replace it while selecting content.").inGroup("profiles").inSubgroup("content"),
        "gameRoot", "Game::root", "Content session", false, "startup-default");
    ADD(SettingsDefinition::string("core.paths.geoData", "", SettingType::Directory)
            .withName("Geographic data directory").withDescription("Directory read by the HGT and TIFF geographic-data tools.").inGroup("profiles").inSubgroup("content"),
        "geoPath", "Game::geoPath", "GeoTools", false, "direct");
    ADD(SettingsDefinition::string("core.startup.route", "")
            .withName("Startup route").withDescription("Default route directory opened at startup; route selection can replace it for the current session.").inGroup("profiles").inSubgroup("route"),
        "routeName", "Game::route", "Route selection", false, "startup-default");
    ADD(SettingsDefinition::boolean("core.startup.useTilePosition", false)
            .withName("Use startup tile position").withDescription("Start the Route Editor camera at the configured tile coordinates. When disabled, use the route's normal starting position.").inGroup("profiles").inSubgroup("route"),
        "", "", "Route Editor startup", false, "startup");
    ADD(SettingsDefinition::integer("core.startup.tileX", 0)
            .withName("Startup tile X").withDescription("World tile X coordinate used for the initial Route Editor camera position when both startup tile coordinates are provided.").withRange(-32768, 32767, 1).inGroup("profiles").inSubgroup("route"),
        "startTileX", "Game::startTileX", "Game", false, "startup");
    ADD(SettingsDefinition::integer("core.startup.tileZ", 0)
            .withName("Startup tile Z").withDescription("World tile Z coordinate used for the initial Route Editor camera position; the legacy file calls it startTileY.").withRange(-32768, 32767, 1).inGroup("profiles").inSubgroup("route"),
        "startTileY", "Game::startTileY", "Game", false, "startup");
    ADD(SettingsDefinition::string("core.startup.season", "")
            .withName("Content season").withDescription("Select the seasonal texture variant used while loading route terrain and objects.").inGroup("profiles").inSubgroup("route").asAdvanced(),
        "season", "Game::season", "Route content session", true, "route-reload");

    order = 0;
    ADD(SettingsDefinition::boolean("core.editing.createMissingRoute", false)
            .withName("Create missing startup route").withDescription("Create a new route when the configured startup route directory does not exist.").inGroup("editing").inSubgroup("operations"),
        "createNewIfNotExist", "Game::createNewRoutes", "Route", false, "startup");
    ADD(SettingsDefinition::boolean("core.editing.writeEnabled", true)
            .withName("Enable route writes").withDescription("Master permission for saving route, terrain, world, and related files.").inGroup("editing").inSubgroup("saving"),
        "writeEnabled", "Game::writeEnabled", "Route", true, "hot-cache");
    ADD(SettingsDefinition::boolean("core.editing.writeTrackDatabase", true)
            .withName("Write track databases").withDescription("Recommended: enabled. Disabling TrackDB and RoadDB writes is intended only for special recovery or inspection work and is unsafe during normal editing: other route files may still be saved, leaving the route databases inconsistent with them. Route validation can temporarily suspend database writes after a serious error.").inGroup("editing").inSubgroup("saving").asAdvanced(),
        "writeTDB", "Game::writeTDB", "Route/TDB", true, "session-safety-state");
    ADD(SettingsDefinition::boolean("core.editing.deleteTrackWatermarks", false)
            .withName("Omit track watermarks when saving").withDescription("Do not serialize legacy track watermark objects into world files.").inGroup("editing").inSubgroup("cleanup").asAdvanced(),
        "deleteTrWatermarks", "Game::deleteTrWatermarks", "TrWatermarkObj", false, "save-time");
    ADD(SettingsDefinition::boolean("core.editing.deleteViewDbSpheres", false)
            .withName("Omit ViewDB spheres when saving").withDescription("Do not serialize legacy ViewDB sphere records into world files.").inGroup("editing").inSubgroup("cleanup").asAdvanced(),
        "deleteViewDbSpheres", "Game::deleteViewDbSpheres", "Tile", false, "save-time");
    ADD(SettingsDefinition::boolean("core.editing.leaveTrackShapeAfterDelete", false)
            .withName("Keep track shape after deleting database item").withDescription("Leave the visible world track object after deleting its TrackDB entry.").inGroup("editing").inSubgroup("objects"),
        "leaveTrackShapeAfterDelete", "Game::leaveTrackShapeAfterDelete", "RouteEditorGLWidget", false, "action-time");
    ADD(SettingsDefinition::boolean("core.editing.autoFix", false)
            .withName("Automatically fix supported errors").withDescription("Permit Tile and TrackDB validation to apply supported repairs while loading route data.").inGroup("editing").inSubgroup("cleanup").asAdvanced(),
        "autoFix", "Game::autoFix", "Tile/TDB", false, "load-time");
    ADD(SettingsDefinition::integer("core.editing.recentItemLimit", 11)
            .withName("Recent placement item limit").withDescription("Maximum number of recently used object-placement items shown by Route Editor object tools.").withRange(0, 100, 1).inGroup("editing").inSubgroup("objects"),
        "numRecentItems", "Game::numRecentItems", "ObjTools", false, "ui-construction");
    ADD(SettingsDefinition::boolean("core.editing.sortTileObjects", true)
            .withName("Sort tile objects when saving").withDescription("Write world objects grouped by detail level instead of retaining their current in-memory order.").inGroup("editing").inSubgroup("saving").asAdvanced(),
        "sortTileObjects", "Game::sortTileObjects", "Tile", false, "save-time");
    ADD(SettingsDefinition::string("core.editing.objectsToRemove", "", SettingType::StringList)
            .withName("Object types to remove while validating").withDescription("Colon-separated legacy world-object type names removed by Tile error checking.").inGroup("editing").inSubgroup("cleanup").asAdvanced(),
        "objectsToRemove", "Game::objectsToRemove", "Tile", false, "load-time");

    order = 0;
    ADD(SettingsDefinition::floating("core.camera.fieldOfView", 55.0)
            .withName("Field of view").withDescription("Vertical perspective field of view used by Route Editor cameras.").withRange(1, 179, 1).withUnit("degrees").inGroup("camera").inSubgroup("view"),
        "cameraFov", "Game::cameraFov", "Camera", true, "cached");
    ADD(SettingsDefinition::floating("core.camera.speed.minimum", 1.0)
            .withName("Slow movement speed").withDescription("Camera translation speed selected while the slow-movement modifier is held.").withRange(0.01, 1000, 0.1).inGroup("camera").inSubgroup("movement"),
        "cameraSpeedMin", "Game::cameraSpeedMin", "Camera", true, "cached");
    ADD(SettingsDefinition::floating("core.camera.speed.standard", 3.0)
            .withName("Standard movement speed").withDescription("Normal camera translation speed.").withRange(0.01, 1000, 0.1).inGroup("camera").inSubgroup("movement"),
        "cameraSpeedStd", "Game::cameraSpeedStd", "Camera", true, "cached");
    ADD(SettingsDefinition::floating("core.camera.speed.maximum", 40.0)
            .withName("Fast movement speed").withDescription("Camera translation speed selected while the fast-movement modifier is held.").withRange(0.01, 5000, 1).inGroup("camera").inSubgroup("movement"),
        "cameraSpeedMax", "Game::cameraSpeedMax", "Camera", true, "cached");
    ADD(SettingsDefinition::floating("core.camera.mouseSensitivity", 1.0)
            .withName("Mouse look sensitivity").withDescription("Multiplier applied to mouse-driven camera rotation.").withRange(0.01, 10, 0.05).inGroup("camera").inSubgroup("input"),
        "mouseSpeed", "Game::mouseSpeed", "Camera", true, "cached");
    ADD(SettingsDefinition::boolean("core.camera.stickToTerrain", false)
            .withName("Stick camera to terrain").withDescription("Keep the free camera above the terrain surface while it moves.").inGroup("camera").inSubgroup("movement"),
        "cameraStickToTerrain", "Game::cameraStickToTerrain", "Camera", true, "cached");
    ADD(SettingsDefinition::boolean("core.camera.useNumericKeypad", false)
            .withName("Use numeric keypad controls").withDescription("Enable the Route Editor's numeric-keypad camera bindings.").inGroup("camera").inSubgroup("input"),
        "usenNumPad", "Game::usenNumPad", "Camera", true, "cached");

    order = 0;
    ADD(SettingsDefinition::integer("core.rendering.tileRadius", 2)
            .withName("Tile rendering radius").withDescription("Number of full 2048 m world tiles loaded and rendered outward from the camera tile. The visible square is (2 x radius + 1) tiles wide; very large debug values can require extreme memory and processing time.").withRange(0, 128, 1).withUnit("tiles").inGroup("rendering").inSubgroup("visibility"),
        "tileLod", "Game::tileLod", "Renderer", true, "cached");
    ADD(SettingsDefinition::floating("core.rendering.objectLodDistance", 3000.0)
            .withName("Object visibility distance").withDescription("Far clipping and culling distance for ordinary route objects and terrain patches.").withRange(100, 200000, 100).withUnit("m").inGroup("rendering").inSubgroup("visibility"),
        "objectLod", "Game::objectLod", "RouteEditor renderer", true, "hot-cache");
    ADD(SettingsDefinition::integer("core.rendering.objectLoading.targetTokens", 10)
            .withName("Object loading budget target").withDescription("Target token level for incremental shape loading. Rendering replenishes two tokens per update when the current token count is below this value.").withRange(0, 10000, 1).inGroup("rendering").inSubgroup("visibility").applies("dynamic").asAdvanced(),
        "maxObjLag", "Game::maxObjLag", "Shape loading scheduler", true, "hot-cache");
    ADD(SettingsDefinition::integer("core.rendering.objectLoading.initialTokens", 1000)
            .withName("Initial object loading tokens").withDescription("Initial value of the separate runtime token counter consumed while shapes load. Changing this setting at runtime resets the counter; normal counter consumption and refilling never modify the saved setting.").withRange(0, 100000, 2).inGroup("rendering").inSubgroup("visibility").applies("dynamic").asAdvanced(),
        "allowObjLag", "Game::allowObjLag", "Shape loading scheduler", true, "session-state");
    ADD(SettingsDefinition::string("core.rendering.pipeline", "legacy", SettingType::Enum)
            .withName("Renderer pipeline").withDescription("Select the renderer implementation used when the Route Editor starts.")
            .withOptions(choices({{"legacy", "Legacy"}, {"gather", "Gather"}, {"validation", "Validation"}})).inGroup("rendering").inSubgroup("pipeline"),
        "rendererPipeline", "Game::requestedRendererPipeline", "Renderer", true, "cached");
    ADD(SettingsDefinition::boolean("core.rendering.pipelineHotSwap", true)
            .withName("Allow renderer hot swap").withDescription("Allow the Route Editor pipeline command to switch renderer implementations while running.").inGroup("rendering").inSubgroup("pipeline").asAdvanced(),
        "rendererPipelineHotSwap", "Game::rendererPipelineHotSwap", "Renderer", true, "cached");
    ADD(SettingsDefinition::boolean("core.rendering.threadedTextureLoading", true)
            .withName("Threaded texture loading").withDescription("Allow ACE and DDS libraries to load textures on worker threads.").inGroup("rendering").inSubgroup("textures"),
        "textureLoaderThreaded", "Game::textureLoaderThreaded", "AceLib/DdsLib", true, "startup-cache");
    ADD(SettingsDefinition::enumeration("core.rendering.textureDownscaleFactor", 1)
            .withName("Texture downscale factor").withDescription("Divide loaded ACE texture width and height by this factor; 1 preserves the source dimensions.")
            .withOptions(numericChoices({{1, "Original size"}, {2, "1/2"}, {4, "1/4"}, {8, "1/8"}, {16, "1/16"}})).inGroup("rendering").inSubgroup("textures"),
        "textureQuality", "Game::textureQuality", "AceLib", true, "texture-load-time");
    ADD(SettingsDefinition::enumeration("core.rendering.antiAliasingSamples", 0)
            .withName("Anti-aliasing samples").withDescription("OpenGL multisample count requested before the application and rendering contexts are created; support depends on the graphics driver.")
            .withOptions(numericChoices({{0, "Disabled"}, {2, "2x"}, {4, "4x"}, {8, "8x"}, {16, "16x"}})).inGroup("rendering").inSubgroup("shadows"),
        "AASamples", "Game::AASamples", "Renderer", false, "startup");
    ADD(SettingsDefinition::boolean("core.rendering.shadows.enabled", true)
            .withName("Enable shadows").withDescription("Render Route Editor shadow passes. The legacy integer was only tested as greater than zero; Consist Editor and Shape Viewer disable shadows for their sessions.").inGroup("rendering").inSubgroup("shadows"),
        "shadowsEnabled", "Game::shadowsEnabled", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::enumeration("core.rendering.shadow.primaryMapSize", 2048)
            .withName("Near shadow-map size").withDescription("Width and height of the near shadow depth texture created when the Route Editor renderer initializes.")
            .withOptions(numericChoices({{256, "256 px"}, {512, "512 px"}, {1024, "1024 px"}, {2048, "2048 px"}, {4096, "4096 px"}, {8192, "8192 px"}, {16384, "16384 px"}})).withUnit("px").inGroup("rendering").inSubgroup("shadows"),
        "shadowMapSize", "Game::shadowMapSize", "RouteEditorGLWidget", false, "renderer-restart");
    ADD(SettingsDefinition::enumeration("core.rendering.shadow.distantMapSize", 1024)
            .withName("Distant shadow-map size").withDescription("Width and height of the distant shadow depth texture created when the Route Editor renderer initializes.")
            .withOptions(numericChoices({{256, "256 px"}, {512, "512 px"}, {1024, "1024 px"}, {2048, "2048 px"}, {4096, "4096 px"}, {8192, "8192 px"}, {16384, "16384 px"}})).withUnit("px").inGroup("rendering").inSubgroup("shadows"),
        "shadowLowMapSize", "Game::shadowLowMapSize", "RouteEditorGLWidget", false, "renderer-restart");
    ADD(SettingsDefinition::integer("core.rendering.defaultLineWidth", 1)
            .withName("Default line width").withDescription("OpenGL width used for editor line primitives when an item does not request a custom width.").withRange(1, 16, 1).withUnit("px").inGroup("rendering").inSubgroup("overlays"),
        "oglDefaultLineWidth", "Game::oglDefaultLineWidth", "OpenGL renderer", true, "hot-cache");
    ADD(SettingsDefinition::floating("core.rendering.fogDensity", 0.7)
            .withName("Fog density").withDescription("Fog density copied into each OpenGL scene helper when it is constructed.").withRange(0, 1, 0.01).inGroup("rendering").inSubgroup("environment"),
        "fogDensity", "Game::fogDensity", "GLUU", false, "renderer-construction");
    ADD(SettingsDefinition::string("core.rendering.fogColor", "#E6F8FF", SettingType::Color)
            .withName("Fog colour").withDescription("Colour used for distance fog and copied into the scene renderer.").inGroup("rendering").inSubgroup("environment"),
        "fogColor", "Game::fogColor", "GLUU", false, "renderer-construction");
    ADD(SettingsDefinition::string("core.rendering.skyColor", "#E6F8FF", SettingType::Color)
            .withName("Sky colour").withDescription("Base clear colour copied into the scene renderer.").inGroup("rendering").inSubgroup("environment"),
        "skyColor", "Game::skyColor", "GLUU", false, "renderer-construction");
    ADD(SettingsDefinition::boolean("core.rendering.renderTrackItems", false)
            .withName("Render TrackDB items").withDescription("Display TrackDB and RoadDB interactive-item markers in the Route Editor scene.").inGroup("rendering").inSubgroup("overlays"),
        "renderTrItems", "Game::renderTrItems", "RouteEditor", true, "hot-cache");

    order = 0;
    ADD(SettingsDefinition::boolean("core.interface.hideTools", false)
            .withName("Hide Route Editor tools").withDescription("Hide the Route Editor's auxiliary tool windows after its saved layout is created.").inGroup("interface").inSubgroup("routeEditor"),
        "toolsHidden", "Game::toolsHidden", "RouteEditor", false, "startup");
    ADD(SettingsDefinition::string("core.interface.mainWindowLayout", "PWT")
            .withName("Route Editor window layout").withDescription("Compact startup code: P enables Properties, T enables Tools, and W denotes the main world view.").inGroup("interface").inSubgroup("routeEditor"),
        "mainWindowLayout", "Game::mainWindowLayout", "RouteEditor", false, "startup");
    ADD(SettingsDefinition::string("core.interface.consistWindowLayout", "C1")
            .withName("Consist Editor window layout").withDescription("Compact startup code controlling the C, 1, 2, and U Consist Editor panels.").inGroup("interface").inSubgroup("consistEditor"),
        "ceWindowLayout", "Game::ceWindowLayout", "ConEditor", false, "startup");
    ADD(SettingsDefinition::string("core.interface.consistBackground", "", SettingType::Color).withNullDefault()
            .withName("Consist preview background").withDescription("Background colour applied to the Consist Editor's consist preview when its window is created.").inGroup("interface").inSubgroup("consistEditor"),
        "colorConView", "Game::colorConView", "ConEditor", false, "startup");
    ADD(SettingsDefinition::string("core.interface.shapeBackground", "", SettingType::Color).withNullDefault()
            .withName("Shape preview background").withDescription("Background colour applied to shape preview widgets in the Consist Editor and Shape Viewer.").inGroup("interface").inSubgroup("shapeViewer"),
        "colorShapeView", "Game::colorShapeView", "ShapeViewer", false, "startup");
    ADD(SettingsDefinition::boolean("core.interface.hud.enabled", false)
            .withName("Enable Route Editor HUD").withDescription("Display the Route Editor heads-up overlay.").inGroup("interface").inSubgroup("hud"),
        "hudEnabled", "Game::hudEnabled", "HUD", true, "cached");
    ADD(SettingsDefinition::floating("core.interface.hud.scale", 1.0)
            .withName("HUD scale").withDescription("Scale multiplier used to build the Route Editor HUD projection.").withRange(0.25, 4, 0.05).inGroup("interface").inSubgroup("hud"),
        "hudScale", "Game::hudScale", "HUD", true, "cached");
    ADD(SettingsDefinition::boolean("core.interface.hud.showEditorFps", false)
            .withName("Show editor FPS").withDescription("Display Route Editor frame-rate diagnostics in the HUD.").inGroup("interface").inSubgroup("hud"),
        "editorFpsHudEnabled", "Game::editorFpsHudEnabled", "HUD", true, "cached");
    ADD(SettingsDefinition::boolean("core.interface.markerLines", false)
            .withName("Show route marker lines").withDescription("Create line geometry connecting imported route-marker points.").inGroup("interface").inSubgroup("routeEditor"),
        "markerLines", "Game::markerLines", "Route markers", false, "marker-load-time");

    order = 0;
    ADD(SettingsDefinition::enumeration("core.track.defaultGradeFormat", "permille")
            .withName("Default track grade format").withDescription("Initial grade input format in track, dynamic-track, and ruler properties.")
            .withOptions(choices({{"permille", "Permille"}, {"percent", "Percent"}, {"oneInX", "1 in X"}, {"angle", "Angle"}})).inGroup("terrain").inSubgroup("track"),
        "defaultElevationBox", "Game::DefaultElevationBox", "Track property panels", false, "ui-construction");
    ADD(SettingsDefinition::floating("core.editing.defaultMoveStep", 0.25)
            .withName("Default movement step").withDescription("Initial translation increment and live-track quantization grid used by Route Editor controls.").withRange(0.001, 1000, 0.01).withUnit("m").inGroup("terrain").inSubgroup("track"),
        "defaultMoveStep", "Game::DefaultMoveStep", "RouteEditorGLWidget/track properties", true, "session-cache");
    ADD(SettingsDefinition::floating("core.track.maximumElevationPermille", 700.0)
            .withName("Maximum absolute track grade").withDescription("Clamp track and dynamic-track property input to this absolute grade, expressed in per mille.").withRange(0, 1000, 1).withUnit("permille").inGroup("terrain").inSubgroup("track"),
        "trackElevationMaxPm", "Game::trackElevationMaxPm", "Track property panels", false, "action-time");
    ADD(SettingsDefinition::boolean("core.track.snapRotationOnly", false)
            .withName("Snap rotation only").withDescription("When snapping compatible objects, align rotation without translating the placed object to the snap point.").inGroup("terrain").inSubgroup("track"),
        "snapableOnlyRot", "Game::snapableOnlyRot", "Route", true, "route-cache");
    ADD(SettingsDefinition::string("core.track.proceduralMode", "Disabled", SettingType::Enum)
            .withName("Procedural track mode").withDescription("Disabled always uses fallback geometry; Enabled uses requested procedural templates when available; Forced also falls back to DefaultTrack.")
            .withOptions(choices({{"Disabled", "Disabled"}, {"Enabled", "Enabled"}, {"Forced", "Forced"}})).inGroup("terrain").inSubgroup("track"),
        "proceduralTracks", "Game::proceduralTracks", "ProceduralTrackPolicy", true, "object-render-cache");
    ADD(SettingsDefinition::boolean("core.terrain.seasonalEditing", false)
            .withName("Edit seasonal terrain files").withDescription("When a content season is selected, load and save terrain texture and raw files in that season's directories.").inGroup("terrain").inSubgroup("terrain").asAdvanced(),
        "seasonalEditing", "Game::seasonalEditing", "Terrain", true, "terrain-load-save-time");
    ADD(SettingsDefinition::boolean("core.terrain.loadAllWorldFiles", false)
            .withName("Preload all world files").withDescription("Load every route world tile during route initialization instead of only the normal working set; server mode forces this on.").inGroup("terrain").inSubgroup("terrain").asAdvanced(),
        "loadAllWFiles", "Game::loadAllWFiles", "Route", true, "route-load-time");
    ADD(SettingsDefinition::boolean("core.geometry.positiveQuaternionsOnly", false)
            .withName("Write positive quaternions only").withDescription("Normalize object rotations to the positive equivalent quaternion while serializing world objects.").inGroup("terrain").inSubgroup("serialization").asAdvanced(),
        "useOnlyPositiveQuaternions", "Game::useOnlyPositiveQuaternions", "WorldObj", false, "save-time");

    order = 0;
    ADD(SettingsDefinition::string("core.maps.imageryUrl", "")
            .withName("Imagery URL template").withDescription("HTTP URL template used by map tools. Supported placeholders are {lat}, {lon}, {zoom}, {res}, and {apikey}. The API key is resolved from the separate secret setting; advanced templates may reference another secret directly as {secret:ID}.").inGroup("maps").inSubgroup("imagery"),
        "imageMapsUrl", "Game::imageMapsUrl", "MapDataUrlImage", false, "request-time");
    ADD(SettingsDefinition::string("core.maps.imageryApiKey", "maps.imageryApiKey", SettingType::Secret)
            .withName("Imagery API key").withDescription("Reference to the provider API key in profile-local secrets.json. Its secret value replaces {apikey} in the imagery URL template.").inGroup("maps").inSubgroup("imagery"),
        "", "", "MapDataUrlImage", false, "request-time");
    ADD(SettingsDefinition::integer("core.maps.imageResolution", 4096)
            .withName("Generated map resolution").withDescription("Width and height of the composite terrain-map image created after downloaded map tiles are assembled.").withRange(256, 16384, 256).withUnit("px").inGroup("maps").inSubgroup("imagery"),
        "mapImageResolution", "Game::mapImageResolution", "MapWindow", false, "generation-time");

    order = 0;
    ADD(SettingsDefinition::boolean("core.consist.preferOpenRailsEng", true)
            .withName("Load OpenRails ENG overrides").withDescription("For each locomotive, first try its ENG file and include paths under the trainset OpenRails subdirectory.").inGroup("consist").inSubgroup("rollingStock"),
        "ortsEngEnable", "Game::ortsEngEnable", "Eng", false, "rolling-stock-load-time");

    order = 0;
    ADD(SettingsDefinition::string("core.network.clientLogin", "")
            .withName("Route Editor client login").withDescription("Connection template in username[:password]@host[:port] form. Insert protected parts with {secret:ID}, for example user:{secret:network.clientPassword}@server:port. A non-empty value starts remote-client mode.").inGroup("network").inSubgroup("client"),
        "serverLogin", "Game::serverLogin", "RouteEditorClient", false, "startup");
    ADD(SettingsDefinition::string("core.network.serverAuthenticationMode", "", SettingType::Enum)
            .withName("Server authentication mode").withDescription("Select no authentication or validate users against users.txt in the server working directory.")
            .withOptions(choices({{"", "None"}, {"file", "users.txt file"}})).inGroup("network").inSubgroup("server"),
        "serverAuth", "Game::serverAuth", "RouteEditorServer", false, "server-startup");
    ADD(SettingsDefinition::boolean("core.network.playerMode", false)
            .withName("Player mode").withDescription("Hide Route Editor editing UI, suppress the 3D placement pointer, and skip the interactive out-of-sync TrackSection repair prompt.").inGroup("network").inSubgroup("client"),
        "playerMode", "Game::playerMode", "RouteEditor", true, "session-cache");
    ADD(SettingsDefinition::boolean("core.network.useNetworkEngine", false)
            .withName("Use external locomotive data").withDescription("Read locomotive speed from TrainNetworkEng and publish elevation, path distance, and geographic position to it during simulation updates.").inGroup("network").inSubgroup("simulation"),
        "useNetworkEng", "Game::useNetworkEng", "Eng/TrainNetworkEng", true, "simulation-cache");

    order = 0;
    ADD(SettingsDefinition::boolean("core.advanced.useQuadTree", true)
            .withName("Use quadtree terrain library").withDescription("Construct TerrainLibQt instead of the legacy TerrainLibSimple implementation when loading a route; remote-client mode forces this on.").inGroup("advanced").inSubgroup("routeStorage").asAdvanced(),
        "useQuadTree", "Game::useQuadTree", "Route", false, "route-construction");
    ADD(SettingsDefinition::boolean("core.advanced.useEmptyTrackItems", true)
            .withName("Reuse empty TrackDB item slots").withDescription("Allocate new TrackDB items into existing emptyitem slots before extending the TrackDB item array.").inGroup("advanced").inSubgroup("routeStorage").asAdvanced(),
        "useTdbEmptyItems", "Game::useTdbEmptyItems", "TDB", true, "allocation-time");
    ADD(SettingsDefinition::boolean("core.advanced.ignoreMissingGlobalShapes", false)
            .withName("Hide missing GLOBAL track shapes").withDescription("Filter TrackSection shapes whose files are absent from GLOBAL/SHAPES out of Route Editor placement lists.").inGroup("advanced").inSubgroup("compatibility").asAdvanced(),
        "ignoreMissingGlobalShapes", "Game::ignoreMissingGlobalShapes", "ObjTools", false, "route-tools-load-time");

#undef ADD
    return true;
}
