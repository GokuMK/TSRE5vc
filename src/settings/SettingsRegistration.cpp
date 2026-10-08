#include <settings/SettingsRegistration.h>

#include <settings/SettingsRegistry.h>
#include <tsre/geo/ElevationSource.h>
#include <tsre/geo/ImagerySource.h>
#include <QDebug>

#include <QSet>
#include <QCoreApplication>
#include <QMap>

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

namespace {
bool registerCoreDefinitions(SettingsRegistry &registry, QString *error) {
    const QVector<SettingsGroupDefinition> groups = {
        {"system",
         //% "System"
         QT_TRID_NOOP("settings.group.system.name"),
         //% "Application startup, logging, and general behaviour."
         QT_TRID_NOOP("settings.group.system.description"), 10,
         {{"startup",
           //% "Startup"
           QT_TRID_NOOP("settings.group.system.subgroup.startup.name"),
           //% "Process and application startup behaviour."
           QT_TRID_NOOP("settings.group.system.subgroup.startup.description"), 10},
          {"logging",
           //% "Logging"
           QT_TRID_NOOP("settings.group.system.subgroup.logging.name"),
           //% "Diagnostic output."
           QT_TRID_NOOP("settings.group.system.subgroup.logging.description"), 20},
          {"audio",
           //% "Audio"
           QT_TRID_NOOP("settings.group.system.subgroup.audio.name"),
           //% "Sound playback."
           QT_TRID_NOOP("settings.group.system.subgroup.audio.description"), 30},
          {"legacy",
           //% "Legacy or inactive"
           QT_TRID_NOOP("settings.group.system.subgroup.legacy.name"),
           //% "Parsed legacy values with no effective implementation."
           QT_TRID_NOOP("settings.group.system.subgroup.legacy.description"), 90}}},
        {"content",
         //% "Content"
         QT_TRID_NOOP("settings.group.content.name"),
         //% "Content locations, route startup, loading, saving, and recovery."
         QT_TRID_NOOP("settings.group.content.description"), 20,
         {{"locations",
           //% "Locations"
           QT_TRID_NOOP("settings.group.content.subgroup.locations.name"),
           //% "Directories containing simulator or geographic content."
           QT_TRID_NOOP("settings.group.content.subgroup.locations.description"), 10},
          {"routeStartup",
           //% "Route startup"
           QT_TRID_NOOP("settings.group.content.subgroup.route.startup.name"),
           //% "Initial route and camera location."
           QT_TRID_NOOP("settings.group.content.subgroup.route.startup.description"), 20},
          {"loading",
           //% "Loading"
           QT_TRID_NOOP("settings.group.content.subgroup.loading.name"),
           //% "Content loading and format compatibility."
           QT_TRID_NOOP("settings.group.content.subgroup.loading.description"), 30},
          {"savingRecovery",
           //% "Saving and recovery"
           QT_TRID_NOOP("settings.group.content.subgroup.saving.recovery.name"),
           //% "Advanced write, validation, cleanup, and recovery policy."
           QT_TRID_NOOP("settings.group.content.subgroup.saving.recovery.description"), 40}}},
        {"editing",
         //% "Editing"
         QT_TRID_NOOP("settings.group.editing.name"),
         //% "Track and world-object editing behaviour."
         QT_TRID_NOOP("settings.group.editing.description"), 30,
         {{"track",
           //% "Track editing"
           QT_TRID_NOOP("settings.group.editing.subgroup.track.name"),
           //% "Track grade, snapping, movement, and procedural rendering."
           QT_TRID_NOOP("settings.group.editing.subgroup.track.description"), 10},
          {"objects",
           //% "Object editing"
           QT_TRID_NOOP("settings.group.editing.subgroup.objects.name"),
           //% "Placement and deletion behaviour."
           QT_TRID_NOOP("settings.group.editing.subgroup.objects.description"), 20}}},
        {"camera",
         //% "Camera"
         QT_TRID_NOOP("settings.group.camera.name"),
         //% "Camera movement and navigation controls."
         QT_TRID_NOOP("settings.group.camera.description"), 40,
         {{"view",
           //% "View"
           QT_TRID_NOOP("settings.group.camera.subgroup.view.name"),
           //% "Camera projection."
           QT_TRID_NOOP("settings.group.camera.subgroup.view.description"), 10},
          {"movement",
           //% "Movement"
           QT_TRID_NOOP("settings.group.camera.subgroup.movement.name"),
           //% "Camera movement speeds and terrain following."
           QT_TRID_NOOP("settings.group.camera.subgroup.movement.description"), 20},
          {"input",
           //% "Input"
           QT_TRID_NOOP("settings.group.camera.subgroup.input.name"),
           //% "Mouse and keyboard controls."
           QT_TRID_NOOP("settings.group.camera.subgroup.input.description"), 30}}},
        {"rendering",
         //% "Rendering"
         QT_TRID_NOOP("settings.group.rendering.name"),
         //% "Renderer, texture, shadow, fog, and visibility options."
         QT_TRID_NOOP("settings.group.rendering.description"), 50,
         {{"performance",
           //% "Performance"
           QT_TRID_NOOP("settings.group.rendering.subgroup.performance.name"),
           //% "Frame timing and rendering performance limits."
           QT_TRID_NOOP("settings.group.rendering.subgroup.performance.description"), 5},
          {"visibility",
           //% "Visibility"
           QT_TRID_NOOP("settings.group.rendering.subgroup.visibility.name"),
           //% "Scene radius, distance, and load throttling."
           QT_TRID_NOOP("settings.group.rendering.subgroup.visibility.description"), 10},
          {"pipeline",
           //% "Renderer pipeline"
           QT_TRID_NOOP("settings.group.rendering.subgroup.pipeline.name"),
           //% "Renderer backend selection and diagnostics."
           QT_TRID_NOOP("settings.group.rendering.subgroup.pipeline.description"), 20},
          {"textures",
           //% "Textures"
           QT_TRID_NOOP("settings.group.rendering.subgroup.textures.name"),
           //% "Texture loading and resolution."
           QT_TRID_NOOP("settings.group.rendering.subgroup.textures.description"), 30},
          {"shadows",
           //% "Shadows and anti-aliasing"
           QT_TRID_NOOP("settings.group.rendering.subgroup.shadows.name"),
           //% "Framebuffer and sampling options."
           QT_TRID_NOOP("settings.group.rendering.subgroup.shadows.description"), 40},
          {"reflections",
           //% "Reflections"
           QT_TRID_NOOP("settings.group.rendering.subgroup.reflections.name"),
           //% "Environment cube map rendered around the camera for reflections."
           QT_TRID_NOOP("settings.group.rendering.subgroup.reflections.description"), 45},
          {"water",
           //% "Water"
           QT_TRID_NOOP("settings.group.rendering.subgroup.water.name"),
           //% "How route water surfaces are drawn."
           QT_TRID_NOOP("settings.group.rendering.subgroup.water.description"), 47},
          {"lights",
           //% "Local lights"
           QT_TRID_NOOP("settings.group.rendering.subgroup.lights.name"),
           //% "Light from lamps and glowing surfaces in the scene (QRhi renderer)."
           QT_TRID_NOOP("settings.group.rendering.subgroup.lights.description"), 48},
          {"image",
           //% "Image"
           QT_TRID_NOOP("settings.group.rendering.subgroup.image.name"),
           //% "Exposure, tone mapping and glow of the final image (QRhi renderer)."
           QT_TRID_NOOP("settings.group.rendering.subgroup.image.description"), 49},
          {"environment",
           //% "Environment"
           QT_TRID_NOOP("settings.group.rendering.subgroup.environment.name"),
           //% "Fog and sky appearance."
           QT_TRID_NOOP("settings.group.rendering.subgroup.environment.description"), 50},
          {"overlays",
           //% "Editor overlays"
           QT_TRID_NOOP("settings.group.rendering.subgroup.overlays.name"),
           //% "Track items and line rendering."
           QT_TRID_NOOP("settings.group.rendering.subgroup.overlays.description"), 60}}},
        {"interface",
         //% "Interface"
         QT_TRID_NOOP("settings.group.interface.name"),
         //% "Window layout, colours, HUD, and UI behaviour."
         QT_TRID_NOOP("settings.group.interface.description"), 60,
         {{"appearance",
           //% "Appearance"
           QT_TRID_NOOP("settings.group.interface.subgroup.appearance.name"),
           //% "Shared theme and accent appearance."
           QT_TRID_NOOP("settings.group.interface.subgroup.appearance.description"), 5},
          {"routeEditor",
           //% "Route Editor"
           QT_TRID_NOOP("settings.group.interface.subgroup.route.editor.name"),
           //% "Route Editor windows and markers."
           QT_TRID_NOOP("settings.group.interface.subgroup.route.editor.description"), 10},
          {"consistEditor",
           //% "Consist Editor"
           QT_TRID_NOOP("settings.group.interface.subgroup.consist.editor.name"),
           //% "Consist Editor layout and preview."
           QT_TRID_NOOP("settings.group.interface.subgroup.consist.editor.description"), 20},
          {"shapeViewer",
           //% "Shape Viewer"
           QT_TRID_NOOP("settings.group.interface.subgroup.shape.viewer.name"),
           //% "Shape preview appearance."
           QT_TRID_NOOP("settings.group.interface.subgroup.shape.viewer.description"), 30},
          {"hud",
           //% "HUD"
           QT_TRID_NOOP("settings.group.interface.subgroup.hud.name"),
           //% "Heads-up display."
           QT_TRID_NOOP("settings.group.interface.subgroup.hud.description"), 40}}},
        {"terrain",
         //% "Terrain"
         QT_TRID_NOOP("settings.group.terrain.name"),
         //% "Terrain and seasonal editing options."
         QT_TRID_NOOP("settings.group.terrain.description"), 70,
         {{"terrain",
           //% "Static materials"
           QT_TRID_NOOP("settings.group.terrain.subgroup.terrain.name"),
           //% "Terrain loading and seasonal editing."
           QT_TRID_NOOP("settings.group.terrain.subgroup.terrain.description"), 20},
          {"proceduralMaterials",
           //% "Procedural materials"
           QT_TRID_NOOP("settings.group.terrain.subgroup.procedural.materials.name"),
           //% "Procedural terrain display, output quality and diagnostics."
           QT_TRID_NOOP("settings.group.terrain.subgroup.procedural.materials.description"), 25},
          {"serialization",
           //% "Geometry serialization"
           QT_TRID_NOOP("settings.group.terrain.subgroup.serialization.name"),
           //% "Compatibility rules used when saving objects."
           QT_TRID_NOOP("settings.group.terrain.subgroup.serialization.description"), 30}}},
        {"maps",
         //% "Maps and geodata"
         QT_TRID_NOOP("settings.group.maps.name"),
         //% "Map imagery and geographic data sources."
         QT_TRID_NOOP("settings.group.maps.description"), 80,
         {{"geodata",
           //% "Geodata"
           QT_TRID_NOOP("settings.group.maps.subgroup.geodata.name"),
           //% "Local elevation and geographic-data sources."
           QT_TRID_NOOP("settings.group.maps.subgroup.geodata.description"), 5},
          {"imagery",
           //% "Online imagery"
           QT_TRID_NOOP("settings.group.maps.subgroup.imagery.name"),
           //% "Background-map download and output options."
           QT_TRID_NOOP("settings.group.maps.subgroup.imagery.description"), 10}}},
        {"network",
         //% "Network"
         QT_TRID_NOOP("settings.group.network.name"),
         //% "Server, client, and remote editor options."
         QT_TRID_NOOP("settings.group.network.description"), 100,
         {{"client",
           //% "Route Editor client"
           QT_TRID_NOOP("settings.group.network.subgroup.client.name"),
           //% "Remote editor connection."
           QT_TRID_NOOP("settings.group.network.subgroup.client.description"), 10},
          {"server",
           //% "Route Editor server"
           QT_TRID_NOOP("settings.group.network.subgroup.server.name"),
           //% "Server authentication."
           QT_TRID_NOOP("settings.group.network.subgroup.server.description"), 20}}},
        {"advanced",
         //% "Advanced"
         QT_TRID_NOOP("settings.group.advanced.name"),
         //% "Specialized and compatibility settings."
         QT_TRID_NOOP("settings.group.advanced.description"), 110,
         {{"routeStorage",
           //% "Route storage"
           QT_TRID_NOOP("settings.group.advanced.subgroup.route.storage.name"),
           //% "World and TrackDB storage implementations."
           QT_TRID_NOOP("settings.group.advanced.subgroup.route.storage.description"), 10},
          {"simulation",
           //% "Experimental simulation"
           QT_TRID_NOOP("settings.group.advanced.subgroup.simulation.name"),
           //% "Experimental player and external simulation integration."
           QT_TRID_NOOP("settings.group.advanced.subgroup.simulation.description"), 20},
          {"safetyRecovery",
           //% "Route safety and recovery"
           QT_TRID_NOOP("settings.group.advanced.subgroup.safety.recovery.name"),
           //% "Dangerous route write and destructive validation controls."
           QT_TRID_NOOP("settings.group.advanced.subgroup.safety.recovery.description"), 30},
          {"compatibility",
           //% "Compatibility"
           QT_TRID_NOOP("settings.group.advanced.subgroup.compatibility.name"),
           //% "Legacy content compatibility behaviour."
           QT_TRID_NOOP("settings.group.advanced.subgroup.compatibility.description"), 40}}}
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
            .withNameId(
                //% "Console output"
                QT_TRID_NOOP("settings.core.system.console.output.name")).withDescriptionId(
                //% "Mirror Qt log messages to the process console as well as log.txt."
                QT_TRID_NOOP("settings.core.system.console.output.description")).inGroup("system").inSubgroup("logging"),
        "consoleOutput", "Game::consoleOutput", "Game", false, "startup");
    ADD(SettingsDefinition::boolean("core.system.systemTheme", false)
            .withNameId(
                //% "Use system theme"
                QT_TRID_NOOP("settings.core.system.system.theme.name")).withDescriptionId(
                //% "Use the operating-system palette instead of TSRE's built-in dark theme. The configurable accent colour is used only with this system-theme mode."
                QT_TRID_NOOP("settings.core.system.system.theme.description")).inGroup("interface").inSubgroup("appearance"),
        "systemTheme", "Game::systemTheme", "Game", false, "startup");
    ADD(SettingsDefinition::enumeration("core.interface.language", "system")
            .withNameId(
                //% "Interface language"
                QT_TRID_NOOP("settings.core.interface.language.name")).withDescriptionId(
                //% "Language used by TSRE's graphical interface. System uses the first supported operating-system UI language. Requires an application restart."
                QT_TRID_NOOP("settings.core.interface.language.description")).withOptions(choices({{"system",
                //% "System / Automatic"
                QT_TRID_NOOP("settings.core.interface.language.option.system")}, {"en",
                //% "English"
                QT_TRID_NOOP("settings.core.interface.language.option.en")}, {"pl",
                //% "Polski"
                QT_TRID_NOOP("settings.core.interface.language.option.pl")}})).inGroup("interface").inSubgroup("appearance"),
        "", "", "TranslationManager", false, "startup");
    ADD(SettingsDefinition::boolean("core.interface.routeEditor.startMaximized", false)
            .withNameId(
                //% "Start Route Editor maximized"
                QT_TRID_NOOP("settings.core.interface.route.editor.start.maximized.name")).withDescriptionId(
                //% "Open the Route Editor maximized. Despite the legacy name, this does not enter a borderless full-screen mode."
                QT_TRID_NOOP("settings.core.interface.route.editor.start.maximized.description")).inGroup("interface").inSubgroup("routeEditor"),
        "fullscreen", "Game::fullscreen", "RouteEditorWindow", false, "startup");
    ADD(SettingsDefinition::integer("core.system.fpsLimit", 0)
            .withNameId(
                //% "Frame-rate limit"
                QT_TRID_NOOP("settings.core.system.fps.limit.name")).withDescriptionId(
                //% "Set the Route Editor render timer interval to 1000 divided by this value. Zero uses the built-in 15 ms interval (about 67 updates per second)."
                QT_TRID_NOOP("settings.core.system.fps.limit.description")).withRange(0, 1000, 1).withUnit("fps").inGroup("rendering").inSubgroup("performance"),
        "fpsLimit", "Game::fpsLimit", "RouteEditorGLWidget", false, "startup");
    ADD(SettingsDefinition::boolean("core.system.soundEnabled", false)
            .withNameId(
                //% "Enable route sounds"
                QT_TRID_NOOP("settings.core.system.sound.enabled.name")).withDescriptionId(
                //% "Load and update route, rolling-stock, and ambient sounds in the Route Editor."
                QT_TRID_NOOP("settings.core.system.sound.enabled.description")).inGroup("system").inSubgroup("audio"),
        "soundEnabled", "Game::soundEnabled", "SoundManager", true, "route-reload");

    order = 0;
    ADD(SettingsDefinition::string("core.paths.gameRoot", "", SettingType::Directory)
            .withNameId(
                //% "Train Simulator root"
                QT_TRID_NOOP("settings.core.paths.game.root.name")).withDescriptionId(
                //% "Root directory containing GLOBAL, ROUTES, and TRAINS. Editors can replace it while selecting content."
                QT_TRID_NOOP("settings.core.paths.game.root.description")).inGroup("content").inSubgroup("locations"),
        "gameRoot", "Game::root", "Content session", false, "startup-default");
    ADD(SettingsDefinition::string("core.paths.geoData", "", SettingType::Directory)
            .withNameId(
                //% "Geodata and elevation cache directory"
                QT_TRID_NOOP("settings.core.paths.geo.data.name")).withDescriptionId(
                //% "Geodata root: user-managed elevation products in their catalogue directories; downloaded elevation and imagery data in cache/."
                QT_TRID_NOOP("settings.core.paths.geo.data.description")).inGroup("maps").inSubgroup("geodata"),
        "geoPath", "Game::geoPath", "GeoTools", false, "direct");
    ADD(SettingsDefinition::string("core.paths.osmData", "", SettingType::Directory)
            .withNameId(
                //% "OpenStreetMap data directory"
                QT_TRID_NOOP("settings.core.paths.osm.data.name")).withDescriptionId(
                //% "Downloaded .osm.pbf extracts, for example from Geofabrik. TSRE converts each file once into a spatially sorted copy (.tsre.osm.pbf) next to it. Subdirectories are not read."
                QT_TRID_NOOP("settings.core.paths.osm.data.description")).inGroup("maps").inSubgroup("geodata"),
        "", "", "OSM data", false, "direct");
    ADD(SettingsDefinition::string("geo.osm.originalAfterConversion", "keep", SettingType::Enum)
            .withNameId(
                //% "Downloaded OSM file after conversion"
                QT_TRID_NOOP("settings.geo.osm.original.after.conversion.name")).withDescriptionId(
                //% "Keep the downloaded .osm.pbf next to its converted copy, or delete it once the conversion has finished. The converted copy has the same content and is used for everything; deleting saves about half of the space."
                QT_TRID_NOOP("settings.geo.osm.original.after.conversion.description"))
            .withOptions(choices({{"keep",
                //% "Keep both files"
                QT_TRID_NOOP("settings.geo.osm.original.after.conversion.option.keep")}, {"delete",
                //% "Delete the download"
                QT_TRID_NOOP("settings.geo.osm.original.after.conversion.option.delete")}}))
            .inGroup("maps").inSubgroup("geodata"),
        "", "", "OSM data", false, "action-time");
    QString elevationCatalogueError;
    const auto elevationCatalogue = Elevation::datasets(elevationCatalogueError);
    const QString defaultElevationSource = Elevation::defaultFileSourceId(elevationCatalogue);
    const QString defaultElevationFallback = Elevation::defaultFallbackSourceId(elevationCatalogue);
    if (!elevationCatalogueError.isEmpty()) qWarning().noquote() << elevationCatalogueError;
    ADD(SettingsDefinition::string("geo.elevation.source", defaultElevationSource, SettingType::Enum)
            .withNameId(
                //% "Terrain elevation source"
                QT_TRID_NOOP("settings.geo.elevation.source.name")).withDescriptionId(
                //% "Source for manual and automatic terrain elevation. Missing coverage uses the selected fallback."
                QT_TRID_NOOP("settings.geo.elevation.source.description"))
            .withOptionsProvider([] {
                QVector<SettingOption> result;
                QString error;
                for (const auto &dataset : Elevation::datasets(error))
                    result.push_back({dataset.id, {}, dataset.name});
                if (!error.isEmpty()) qWarning().noquote() << error;
                return result;
            }).asReference()
            .inGroup("maps").inSubgroup("geodata"),
        "", "", "Terrain elevation", false, "generation-time");
    ADD(SettingsDefinition::string("geo.elevation.fallback", defaultElevationFallback, SettingType::Enum)
            .withNameId(
                //% "Terrain elevation fallback"
                QT_TRID_NOOP("settings.geo.elevation.fallback.name")).withDescriptionId(
                //% "Approved secondary source used for missing coverage or NoData. If both sources fail, elevation generation remains unresolved."
                QT_TRID_NOOP("settings.geo.elevation.fallback.description"))
            .withOptionsProvider([] {
                QVector<SettingOption> result;
                QString error;
                for (const auto &dataset : Elevation::datasets(error))
                    if (dataset.fallbackApproved)
                        result.push_back({dataset.id, {}, dataset.name});
                if (!error.isEmpty()) qWarning().noquote() << error;
                return result;
            }).asReference()
            .inGroup("maps").inSubgroup("geodata"),
        "", "", "Terrain elevation", false, "generation-time");
    QString imageryCatalogueError;
    const auto imageryCatalogue = Imagery::datasets(imageryCatalogueError);
    const QString defaultImagerySource = Imagery::defaultDetailedSourceId(imageryCatalogue);
    if (!imageryCatalogueError.isEmpty()) qWarning().noquote() << imageryCatalogueError;
    ADD(SettingsDefinition::string("geo.imagery.source", defaultImagerySource, SettingType::Enum)
            .withNameId(
                //% "Terrain imagery source"
                QT_TRID_NOOP("settings.geo.imagery.source.name")).withDescriptionId(
                //% "Source used by Load Imagery for detailed terrain. Distant terrain uses an approved source without replacing this saved choice."
                QT_TRID_NOOP("settings.geo.imagery.source.description"))
            .withOptionsProvider([] {
                QVector<SettingOption> result;
                QString error;
                for (const auto &dataset : Imagery::datasets(error))
                    result.push_back({dataset.id, {}, dataset.name});
                if (!error.isEmpty()) qWarning().noquote() << error;
                return result;
            }).asReference()
            .inGroup("maps").inSubgroup("imagery"),
        "", "", "Terrain imagery", false, "generation-time");
    ADD(SettingsDefinition::string("core.startup.route", "")
            .withNameId(
                //% "Startup route"
                QT_TRID_NOOP("settings.core.startup.route.name")).withDescriptionId(
                //% "Default route directory opened at startup; route selection can replace it for the current session."
                QT_TRID_NOOP("settings.core.startup.route.description")).inGroup("content").inSubgroup("routeStartup"),
        "routeName", "Game::route", "Route selection", false, "startup-default");
    ADD(SettingsDefinition::boolean("core.startup.useTilePosition", false)
            .withNameId(
                //% "Use startup tile position"
                QT_TRID_NOOP("settings.core.startup.use.tile.position.name")).withDescriptionId(
                //% "Start the Route Editor camera at the configured tile coordinates. When disabled, use the route's normal starting position."
                QT_TRID_NOOP("settings.core.startup.use.tile.position.description")).inGroup("content").inSubgroup("routeStartup"),
        "", "", "Route Editor startup", false, "startup");
    ADD(SettingsDefinition::integer("core.startup.tileX", 0)
            .withNameId(
                //% "Startup tile X"
                QT_TRID_NOOP("settings.core.startup.tile.x.name")).withDescriptionId(
                //% "World tile X coordinate used for the initial Route Editor camera position when both startup tile coordinates are provided."
                QT_TRID_NOOP("settings.core.startup.tile.x.description")).withRange(-32768, 32767, 1).inGroup("content").inSubgroup("routeStartup"),
        "startTileX", "Game::startTileX", "Route Editor startup", false, "startup");
    ADD(SettingsDefinition::integer("core.startup.tileZ", 0)
            .withNameId(
                //% "Startup tile Z"
                QT_TRID_NOOP("settings.core.startup.tile.z.name")).withDescriptionId(
                //% "World tile Z coordinate used for the initial Route Editor camera position; the legacy file calls it startTileY."
                QT_TRID_NOOP("settings.core.startup.tile.z.description")).withRange(-32768, 32767, 1).inGroup("content").inSubgroup("routeStartup"),
        "startTileY", "Game::startTileY", "Route Editor startup", false, "startup");
    ADD(SettingsDefinition::string("core.startup.camera", "", SettingType::String)
            .withNameId(
                //% "Startup camera"
                QT_TRID_NOOP("settings.core.startup.camera.name")).withDescriptionId(
                //% "Initial Route Editor camera as tileX,tileZ,x,y,z,yaw,pitch: tile and position as in the navigation window, angles in degrees (yaw and pitch may be left out). Tools > Copy Camera Position copies the current one. Overrides the startup tile and the route's starting position; empty to leave them."
                QT_TRID_NOOP("settings.core.startup.camera.description")).inGroup("content").inSubgroup("routeStartup"),
        "", "", "Route Editor startup", false, "startup");
    ADD(SettingsDefinition::string("core.startup.season", "", SettingType::Enum)
            .withNameId(
                //% "Content season"
                QT_TRID_NOOP("settings.core.startup.season.name")).withDescriptionId(
                //% "Static/procedural terrain and transfers share seasonal texture fallback, including rain and snow-free Winter. Shapes retain their existing alternative-texture flags; rain textures for shapes are not implemented. Reload the route after changing season."
                QT_TRID_NOOP("settings.core.startup.season.description"))
            .withOptions(choices({{"",
                //% "Default"
                QT_TRID_NOOP("settings.core.startup.season.option.default")},
                {"SpringClear",
                //% "Spring Clear"
                QT_TRID_NOOP("settings.core.startup.season.option.spring.clear")}, {"SpringRain",
                //% "Spring Rain"
                QT_TRID_NOOP("settings.core.startup.season.option.spring.rain")}, {"SpringSnow",
                //% "Spring Snow"
                QT_TRID_NOOP("settings.core.startup.season.option.spring.snow")},
                {"SummerClear",
                //% "Summer Clear"
                QT_TRID_NOOP("settings.core.startup.season.option.summer.clear")}, {"SummerRain",
                //% "Summer Rain"
                QT_TRID_NOOP("settings.core.startup.season.option.summer.rain")}, {"SummerSnow",
                //% "Summer Snow"
                QT_TRID_NOOP("settings.core.startup.season.option.summer.snow")},
                {"AutumnClear",
                //% "Autumn Clear"
                QT_TRID_NOOP("settings.core.startup.season.option.autumn.clear")}, {"AutumnRain",
                //% "Autumn Rain"
                QT_TRID_NOOP("settings.core.startup.season.option.autumn.rain")}, {"AutumnSnow",
                //% "Autumn Snow"
                QT_TRID_NOOP("settings.core.startup.season.option.autumn.snow")},
                {"WinterClear",
                //% "Winter Clear"
                QT_TRID_NOOP("settings.core.startup.season.option.winter.clear")}, {"WinterRain",
                //% "Winter Rain"
                QT_TRID_NOOP("settings.core.startup.season.option.winter.rain")}, {"WinterSnow",
                //% "Winter Snow"
                QT_TRID_NOOP("settings.core.startup.season.option.winter.snow")}}))
            .inGroup("content").inSubgroup("routeStartup").applies("routeReload").asAdvanced(),
        "season", "Game::season", "Route content session", true, "route-reload");

    order = 0;
    ADD(SettingsDefinition::boolean("core.startup.createMissingRoute", false)
            .withNameId(
                //% "Create missing startup route"
                QT_TRID_NOOP("settings.core.startup.create.missing.route.name")).withDescriptionId(
                //% "Create a new route when the configured startup route directory does not exist."
                QT_TRID_NOOP("settings.core.startup.create.missing.route.description")).inGroup("content").inSubgroup("routeStartup"),
        "createNewIfNotExist", "Game::createNewRoutes", "Route", false, "startup");
    ADD(SettingsDefinition::boolean("core.route.saving.enabled", true)
            .withNameId(
                //% "Enable route writes"
                QT_TRID_NOOP("settings.core.route.saving.enabled.name")).withDescriptionId(
                //% "Master permission for saving route, terrain, world, and related files."
                QT_TRID_NOOP("settings.core.route.saving.enabled.description")).inGroup("advanced").inSubgroup("safetyRecovery").asAdvanced(),
        "writeEnabled", "Game::writeEnabled", "Route", true, "hot-cache");
    ADD(SettingsDefinition::boolean("core.route.saving.trackDatabase", true)
            .withNameId(
                //% "Write track databases"
                QT_TRID_NOOP("settings.core.route.saving.track.database.name")).withDescriptionId(
                //% "Recommended: enabled. Disabling TrackDB and RoadDB writes is intended only for special recovery or inspection work and is unsafe during normal editing: other route files may still be saved, leaving the route databases inconsistent with them. Route validation can temporarily suspend database writes after a serious error."
                QT_TRID_NOOP("settings.core.route.saving.track.database.description")).inGroup("advanced").inSubgroup("safetyRecovery").asAdvanced(),
        "writeTDB", "Game::writeTDB", "Route/TDB", true, "session-safety-state");
    ADD(SettingsDefinition::boolean("core.editing.deleteTrackWatermarks", false)
            .withNameId(
                //% "Omit track watermarks when saving"
                QT_TRID_NOOP("settings.core.editing.delete.track.watermarks.name")).withDescriptionId(
                //% "Do not serialize legacy track watermark objects into world files. Intended only for route diagnostics or recovery."
                QT_TRID_NOOP("settings.core.editing.delete.track.watermarks.description")).inGroup("advanced").inSubgroup("safetyRecovery").asAdvanced(),
        "deleteTrWatermarks", "Game::deleteTrWatermarks", "TrWatermarkObj", false, "save-time");
    ADD(SettingsDefinition::boolean("core.editing.deleteViewDbSpheres", false)
            .withNameId(
                //% "Omit ViewDB spheres when saving"
                QT_TRID_NOOP("settings.core.editing.delete.view.db.spheres.name")).withDescriptionId(
                //% "Do not serialize legacy ViewDB sphere records into world files. Intended only for route diagnostics or recovery."
                QT_TRID_NOOP("settings.core.editing.delete.view.db.spheres.description")).inGroup("advanced").inSubgroup("safetyRecovery").asAdvanced(),
        "deleteViewDbSpheres", "Game::deleteViewDbSpheres", "Tile", false, "save-time");
    ADD(SettingsDefinition::boolean("core.editing.leaveTrackShapeAfterDelete", false)
            .withNameId(
                //% "Keep track shape after deleting database item"
                QT_TRID_NOOP("settings.core.editing.leave.track.shape.after.delete.name")).withDescriptionId(
                //% "Leave the visible world track object after deleting its TrackDB entry."
                QT_TRID_NOOP("settings.core.editing.leave.track.shape.after.delete.description")).inGroup("editing").inSubgroup("objects"),
        "leaveTrackShapeAfterDelete", "Game::leaveTrackShapeAfterDelete", "RouteEditorGLWidget", false, "action-time");
    ADD(SettingsDefinition::boolean("core.route.validation.autoFix", false)
            .withNameId(
                //% "Automatically fix supported errors"
                QT_TRID_NOOP("settings.core.route.validation.auto.fix.name")).withDescriptionId(
                //% "Permit Tile and TrackDB validation to apply supported repairs while loading route data."
                QT_TRID_NOOP("settings.core.route.validation.auto.fix.description")).inGroup("content").inSubgroup("savingRecovery").asAdvanced(),
        "autoFix", "Game::autoFix", "Tile/TDB", false, "load-time");
    ADD(SettingsDefinition::string("core.interface.routeEditor.mapPalette", "light")
            .withNameId(
                //% "Map mode colours"
                QT_TRID_NOOP("settings.core.interface.route.editor.map.palette.name")).withDescriptionId(
                //% "Colour palette of the Route Editor's map mode: light, dark, or the name of a palette file (name.json) in the map-palettes folder of the configuration directory."
                QT_TRID_NOOP("settings.core.interface.route.editor.map.palette.description")).inGroup("interface").inSubgroup("routeEditor"),
        "mapPalette", "Game::mapPalette", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::integer("core.interface.routeEditor.recentItemLimit", 11)
            .withNameId(
                //% "Recent placement item limit"
                QT_TRID_NOOP("settings.core.interface.route.editor.recent.item.limit.name")).withDescriptionId(
                //% "Maximum number of recently used object-placement items shown by Route Editor object tools."
                QT_TRID_NOOP("settings.core.interface.route.editor.recent.item.limit.description")).withRange(0, 100, 1).inGroup("interface").inSubgroup("routeEditor"),
        "numRecentItems", "Game::numRecentItems", "ObjTools", false, "ui-construction");
    ADD(SettingsDefinition::boolean("core.editing.sortTileObjects", true)
            .withNameId(
                //% "Sort tile objects when saving"
                QT_TRID_NOOP("settings.core.editing.sort.tile.objects.name")).withDescriptionId(
                //% "Write world objects grouped by detail level. Disabling this is intended only for diagnostics or examination of old route files."
                QT_TRID_NOOP("settings.core.editing.sort.tile.objects.description")).inGroup("advanced").inSubgroup("safetyRecovery").asAdvanced(),
        "sortTileObjects", "Game::sortTileObjects", "Tile", false, "save-time");
    ADD(SettingsDefinition::stringList("core.editing.objectsToRemove")
            .withNameId(
                //% "Object types to remove while validating"
                QT_TRID_NOOP("settings.core.editing.objects.to.remove.name")).withDescriptionId(
                //% "Colon-separated legacy world-object type names removed by Tile error checking. This is destructive and intended only for controlled route recovery work."
                QT_TRID_NOOP("settings.core.editing.objects.to.remove.description")).inGroup("advanced").inSubgroup("safetyRecovery").asAdvanced(),
        "objectsToRemove", "Game::objectsToRemove", "Tile", false, "load-time");

    order = 0;
    ADD(SettingsDefinition::floating("core.camera.fieldOfView", 55.0)
            .withNameId(
                //% "Field of view"
                QT_TRID_NOOP("settings.core.camera.field.of.view.name")).withDescriptionId(
                //% "Vertical perspective field of view used by Route Editor cameras."
                QT_TRID_NOOP("settings.core.camera.field.of.view.description")).withRange(1, 179, 1).withUnit("degrees").inGroup("camera").inSubgroup("view"),
        "cameraFov", "Game::cameraFov", "Camera", true, "cached");
    ADD(SettingsDefinition::floating("core.camera.speed.minimum", 1.0)
            .withNameId(
                //% "Slow movement speed"
                QT_TRID_NOOP("settings.core.camera.speed.minimum.name")).withDescriptionId(
                //% "Camera translation speed selected while the slow-movement modifier is held."
                QT_TRID_NOOP("settings.core.camera.speed.minimum.description")).withRange(0.01, 1000, 0.1).inGroup("camera").inSubgroup("movement"),
        "cameraSpeedMin", "Game::cameraSpeedMin", "Camera", true, "cached");
    ADD(SettingsDefinition::floating("core.camera.speed.standard", 3.0)
            .withNameId(
                //% "Standard movement speed"
                QT_TRID_NOOP("settings.core.camera.speed.standard.name")).withDescriptionId(
                //% "Normal camera translation speed."
                QT_TRID_NOOP("settings.core.camera.speed.standard.description")).withRange(0.01, 1000, 0.1).inGroup("camera").inSubgroup("movement"),
        "cameraSpeedStd", "Game::cameraSpeedStd", "Camera", true, "cached");
    ADD(SettingsDefinition::floating("core.camera.speed.maximum", 40.0)
            .withNameId(
                //% "Fast movement speed"
                QT_TRID_NOOP("settings.core.camera.speed.maximum.name")).withDescriptionId(
                //% "Camera translation speed selected while the fast-movement modifier is held."
                QT_TRID_NOOP("settings.core.camera.speed.maximum.description")).withRange(0.01, 5000, 1).inGroup("camera").inSubgroup("movement"),
        "cameraSpeedMax", "Game::cameraSpeedMax", "Camera", true, "cached");
    ADD(SettingsDefinition::floating("core.camera.mouseSensitivity", 1.0)
            .withNameId(
                //% "Mouse look sensitivity"
                QT_TRID_NOOP("settings.core.camera.mouse.sensitivity.name")).withDescriptionId(
                //% "Multiplier applied to mouse-driven camera rotation."
                QT_TRID_NOOP("settings.core.camera.mouse.sensitivity.description")).withRange(0.01, 10, 0.05).inGroup("camera").inSubgroup("input"),
        "mouseSpeed", "Game::mouseSpeed", "Camera", true, "cached");
    ADD(SettingsDefinition::boolean("core.camera.stickToTerrain", false)
            .withNameId(
                //% "Stick camera to terrain"
                QT_TRID_NOOP("settings.core.camera.stick.to.terrain.name")).withDescriptionId(
                //% "Keep the free camera above the terrain surface while it moves."
                QT_TRID_NOOP("settings.core.camera.stick.to.terrain.description")).inGroup("camera").inSubgroup("movement"),
        "cameraStickToTerrain", "Game::cameraStickToTerrain", "Camera", true, "cached");
    ADD(SettingsDefinition::boolean("core.camera.useNumericKeypad", false)
            .withNameId(
                //% "Use numeric keypad controls"
                QT_TRID_NOOP("settings.core.camera.use.numeric.keypad.name")).withDescriptionId(
                //% "Enable the Route Editor's numeric-keypad camera bindings."
                QT_TRID_NOOP("settings.core.camera.use.numeric.keypad.description")).inGroup("camera").inSubgroup("input"),
        "usenNumPad", "Game::usenNumPad", "Camera", true, "cached");

    order = 0;
    ADD(SettingsDefinition::integer("core.rendering.tileRadius", 2)
            .withNameId(
                //% "Tile rendering radius"
                QT_TRID_NOOP("settings.core.rendering.tile.radius.name")).withDescriptionId(
                //% "Number of full 2048 m world tiles loaded and rendered outward from the camera tile. The visible square is (2 x radius + 1) tiles wide; very large debug values can require extreme memory and processing time."
                QT_TRID_NOOP("settings.core.rendering.tile.radius.description")).withRange(0, 128, 1).withUnit("tiles").inGroup("rendering").inSubgroup("visibility"),
        "tileLod", "Game::tileLod", "Renderer", true, "cached");
    ADD(SettingsDefinition::floating("core.rendering.objectLodDistance", 3000.0)
            .withNameId(
                //% "Object visibility distance"
                QT_TRID_NOOP("settings.core.rendering.object.lod.distance.name")).withDescriptionId(
                //% "Far clipping and culling distance for ordinary route objects and terrain patches."
                QT_TRID_NOOP("settings.core.rendering.object.lod.distance.description")).withRange(100, 200000, 100).withUnit("m").inGroup("rendering").inSubgroup("visibility"),
        "objectLod", "Game::objectLod", "RouteEditor renderer", true, "hot-cache");
    ADD(SettingsDefinition::integer("core.rendering.objectLoading.targetTokens", 10)
            .withNameId(
                //% "Object loading budget target"
                QT_TRID_NOOP("settings.core.rendering.object.loading.target.tokens.name")).withDescriptionId(
                //% "Pace of loading on the main thread: forests, transfers, and shapes when threaded shape loading is off. Each load takes two tokens; rendering adds two per update up to this level."
                QT_TRID_NOOP("settings.core.rendering.object.loading.target.tokens.description")).withRange(0, 10000, 1).inGroup("rendering").inSubgroup("visibility").applies("dynamic").asAdvanced(),
        "maxObjLag", "Game::maxObjLag", "Shape loading scheduler", true, "hot-cache");
    ADD(SettingsDefinition::boolean("core.rendering.threadedShapeLoading", true)
            .withNameId(
                //% "Threaded shape loading"
                QT_TRID_NOOP("settings.core.rendering.threaded.shape.loading.name")).withDescriptionId(
                //% "Load shapes (MSTS and glTF) on worker threads. Off: shapes load on the main thread, paced by the object loading budget. Either way, the first view of a route and the view after a camera jump are shown with their shapes loaded. Applies after a restart."
                QT_TRID_NOOP("settings.core.rendering.threaded.shape.loading.description")).inGroup("rendering").inSubgroup("visibility"),
        "", "", "ShapeLoader", false, "startup");
    ADD(SettingsDefinition::integer("core.rendering.objectLoading.parallelShapes", 4)
            .withNameId(
                //% "Shapes loaded at once"
                QT_TRID_NOOP("settings.core.rendering.object.loading.parallel.shapes.name")).withDescriptionId(
                //% "Shapes loading at once on worker threads when threaded shape loading is on. Applies after a restart."
                QT_TRID_NOOP("settings.core.rendering.object.loading.parallel.shapes.description")).withRange(1, 32, 1).inGroup("rendering").inSubgroup("visibility").asAdvanced(),
        "", "", "ShapeLoader", false, "startup");
    ADD(SettingsDefinition::string("core.rendering.terrainMesh", "paged", SettingType::Enum)
            .withNameId(
                //% "Terrain mesh backend"
                QT_TRID_NOOP("settings.core.rendering.terrain.mesh.name")).withDescriptionId(
                //% "Select the precomputed legacy terrain mesh or the experimental GPU-oriented paged terrain mesh. A route reload is required."
                QT_TRID_NOOP("settings.core.rendering.terrain.mesh.description"))
            .withOptions(choices({{"legacy",
                //% "Precomputed / Legacy"
                QT_TRID_NOOP("settings.core.rendering.terrain.mesh.option.legacy")}, {"paged",
                //% "On GPU / Experimental"
                QT_TRID_NOOP("settings.core.rendering.terrain.mesh.option.paged")}})).inGroup("rendering").inSubgroup("pipeline").applies("routeReload").asAdvanced(),
        "terrainMesh", "Game::terrainMeshMode", "Terrain renderer", true, "route-reload");
    ADD(SettingsDefinition::string("core.rendering.backend", "opengl", SettingType::Enum)
            .withNameId(
                //% "Renderer"
                QT_TRID_NOOP("settings.core.rendering.backend.name")).withDescriptionId(
                //% "The OpenGL renderer, or the experimental QRhi renderer (Vulkan, Metal, Direct3D or OpenGL through Qt). Applies after a restart."
                QT_TRID_NOOP("settings.core.rendering.backend.description"))
            .withOptions(choices({{"opengl",
                //% "OpenGL"
                QT_TRID_NOOP("settings.core.rendering.backend.option.opengl")}, {"qrhi",
                //% "QRhi / Experimental"
                QT_TRID_NOOP("settings.core.rendering.backend.option.qrhi")}})).inGroup("rendering").inSubgroup("pipeline").asAdvanced(),
        "renderBackend", "Game::renderBackend", "RouteEditorGLWidget", false, "renderer-restart");
    ADD(SettingsDefinition::string("core.rendering.rhiApi", "auto", SettingType::Enum)
            .withNameId(
                //% "QRhi graphics API"
                QT_TRID_NOOP("settings.core.rendering.rhi.api.name")).withDescriptionId(
                //% "Graphics API of the QRhi renderer. Automatic uses Vulkan; without Vulkan the editor uses the OpenGL renderer. OpenGL runs the QRhi renderer on OpenGL, slower than the OpenGL renderer for now. Applies after a restart."
                QT_TRID_NOOP("settings.core.rendering.rhi.api.description"))
            .withOptions(choices({{"auto",
                //% "Automatic"
                QT_TRID_NOOP("settings.core.rendering.rhi.api.option.auto")}, {"vulkan",
                //% "Vulkan"
                QT_TRID_NOOP("settings.core.rendering.rhi.api.option.vulkan")}, {"opengl",
                //% "OpenGL"
                QT_TRID_NOOP("settings.core.rendering.rhi.api.option.opengl")}})).inGroup("rendering").inSubgroup("pipeline").asAdvanced(),
        "rhiApi", "Game::rhiApi", "RouteEditorGLWidget", false, "renderer-restart");
    ADD(SettingsDefinition::boolean("core.rendering.threadedTextureLoading", true)
            .withNameId(
                //% "Threaded texture loading"
                QT_TRID_NOOP("settings.core.rendering.threaded.texture.loading.name")).withDescriptionId(
                //% "Allow ACE and DDS libraries to load textures on worker threads."
                QT_TRID_NOOP("settings.core.rendering.threaded.texture.loading.description")).inGroup("rendering").inSubgroup("textures"),
        "textureLoaderThreaded", "Game::textureLoaderThreaded", "AceLib/DdsLib", true, "startup-cache");
    ADD(SettingsDefinition::enumeration("core.rendering.textureDownscaleFactor", 1)
            .withNameId(
                //% "Texture downscale factor"
                QT_TRID_NOOP("settings.core.rendering.texture.downscale.factor.name")).withDescriptionId(
                //% "Divide loaded ACE texture width and height by this factor; 1 preserves the source dimensions."
                QT_TRID_NOOP("settings.core.rendering.texture.downscale.factor.description"))
            .withOptions(numericChoices({{1,
                //% "Original size"
                QT_TRID_NOOP("settings.core.rendering.texture.downscale.factor.option.1")}, {2,
                //% "1/2"
                QT_TRID_NOOP("settings.core.rendering.texture.downscale.factor.option.2")}, {4,
                //% "1/4"
                QT_TRID_NOOP("settings.core.rendering.texture.downscale.factor.option.4")}, {8,
                //% "1/8"
                QT_TRID_NOOP("settings.core.rendering.texture.downscale.factor.option.8")}, {16,
                //% "1/16"
                QT_TRID_NOOP("settings.core.rendering.texture.downscale.factor.option.16")}})).inGroup("rendering").inSubgroup("textures"),
        "textureQuality", "Game::textureQuality", "AceLib", true, "texture-load-time");
    ADD(SettingsDefinition::enumeration("core.rendering.antiAliasingSamples", 0)
            .withNameId(
                //% "Anti-aliasing samples"
                QT_TRID_NOOP("settings.core.rendering.anti.aliasing.samples.name")).withDescriptionId(
                //% "OpenGL multisample count requested before the application and rendering contexts are created; support depends on the graphics driver."
                QT_TRID_NOOP("settings.core.rendering.anti.aliasing.samples.description"))
            .withOptions(numericChoices({{0,
                //% "Disabled"
                QT_TRID_NOOP("settings.core.rendering.anti.aliasing.samples.option.0")}, {2,
                //% "2x"
                QT_TRID_NOOP("settings.core.rendering.anti.aliasing.samples.option.2")}, {4,
                //% "4x"
                QT_TRID_NOOP("settings.core.rendering.anti.aliasing.samples.option.4")}, {8,
                //% "8x"
                QT_TRID_NOOP("settings.core.rendering.anti.aliasing.samples.option.8")}, {16,
                //% "16x"
                QT_TRID_NOOP("settings.core.rendering.anti.aliasing.samples.option.16")}})).inGroup("rendering").inSubgroup("shadows"),
        "AASamples", "Game::AASamples", "Renderer", false, "startup");
    ADD(SettingsDefinition::boolean("core.rendering.shadows.enabled", true)
            .withNameId(
                //% "Enable shadows"
                QT_TRID_NOOP("settings.core.rendering.shadows.enabled.name")).withDescriptionId(
                //% "Render Route Editor shadow passes. The legacy integer was only tested as greater than zero; Consist Editor and Shape Viewer disable shadows for their sessions."
                QT_TRID_NOOP("settings.core.rendering.shadows.enabled.description")).inGroup("rendering").inSubgroup("shadows"),
        "shadowsEnabled", "Game::shadowsEnabled", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::enumeration("core.rendering.shadow.primaryMapSize", 2048)
            .withNameId(
                //% "Near shadow-map size"
                QT_TRID_NOOP("settings.core.rendering.shadow.primary.map.size.name")).withDescriptionId(
                //% "Width and height of the near shadow depth texture created when the Route Editor renderer initializes."
                QT_TRID_NOOP("settings.core.rendering.shadow.primary.map.size.description"))
            .withOptions(numericChoices({{256,
                //% "256 px"
                QT_TRID_NOOP("settings.core.rendering.shadow.primary.map.size.option.256")}, {512,
                //% "512 px"
                QT_TRID_NOOP("settings.core.rendering.shadow.primary.map.size.option.512")}, {1024,
                //% "1024 px"
                QT_TRID_NOOP("settings.core.rendering.shadow.primary.map.size.option.1024")}, {2048,
                //% "2048 px"
                QT_TRID_NOOP("settings.core.rendering.shadow.primary.map.size.option.2048")}, {4096,
                //% "4096 px"
                QT_TRID_NOOP("settings.core.rendering.shadow.primary.map.size.option.4096")}, {8192,
                //% "8192 px"
                QT_TRID_NOOP("settings.core.rendering.shadow.primary.map.size.option.8192")}, {16384,
                //% "16384 px"
                QT_TRID_NOOP("settings.core.rendering.shadow.primary.map.size.option.16384")}})).withUnit("px").inGroup("rendering").inSubgroup("shadows"),
        "shadowMapSize", "Game::shadowMapSize", "RouteEditorGLWidget", false, "renderer-restart");
    ADD(SettingsDefinition::enumeration("core.rendering.shadow.distantMapSize", 1024)
            .withNameId(
                //% "Distant shadow-map size"
                QT_TRID_NOOP("settings.core.rendering.shadow.distant.map.size.name")).withDescriptionId(
                //% "Width and height of the distant shadow depth texture created when the Route Editor renderer initializes."
                QT_TRID_NOOP("settings.core.rendering.shadow.distant.map.size.description"))
            .withOptions(numericChoices({{256,
                //% "256 px"
                QT_TRID_NOOP("settings.core.rendering.shadow.distant.map.size.option.256")}, {512,
                //% "512 px"
                QT_TRID_NOOP("settings.core.rendering.shadow.distant.map.size.option.512")}, {1024,
                //% "1024 px"
                QT_TRID_NOOP("settings.core.rendering.shadow.distant.map.size.option.1024")}, {2048,
                //% "2048 px"
                QT_TRID_NOOP("settings.core.rendering.shadow.distant.map.size.option.2048")}, {4096,
                //% "4096 px"
                QT_TRID_NOOP("settings.core.rendering.shadow.distant.map.size.option.4096")}, {8192,
                //% "8192 px"
                QT_TRID_NOOP("settings.core.rendering.shadow.distant.map.size.option.8192")}, {16384,
                //% "16384 px"
                QT_TRID_NOOP("settings.core.rendering.shadow.distant.map.size.option.16384")}})).withUnit("px").inGroup("rendering").inSubgroup("shadows"),
        "shadowLowMapSize", "Game::shadowLowMapSize", "RouteEditorGLWidget", false, "renderer-restart");
    ADD(SettingsDefinition::enumeration("core.rendering.blendedParts", 1)
            .withNameId(
                //% "Blended shape parts"
                QT_TRID_NOOP("settings.core.rendering.blended.parts.name")).withDescriptionId(
                //% "MSTS shapes mark many opaque parts as blended. Draw such parts with the opaque ones when their texture has no partly transparent texels, so they sort correctly and receive ambient occlusion."
                QT_TRID_NOOP("settings.core.rendering.blended.parts.description"))
            .withOptions(numericChoices({{0,
                //% "As marked in the shape"
                QT_TRID_NOOP("settings.core.rendering.blended.parts.option.0")}, {1,
                //% "Opaque when the texture is opaque"
                QT_TRID_NOOP("settings.core.rendering.blended.parts.option.1")}, {2,
                //% "Also alpha-tested when the texture alpha is on/off"
                QT_TRID_NOOP("settings.core.rendering.blended.parts.option.2")}})).inGroup("rendering").inSubgroup("pipeline"),
        "blendedParts", "Game::blendedParts", "RenderItem::drawSurface", true, "hot-cache");
    ADD(SettingsDefinition::boolean("core.rendering.environmentMap.enabled", false)
            .withNameId(
                //% "Render environment map"
                QT_TRID_NOOP("settings.core.rendering.environment.map.enabled.name")).withDescriptionId(
                //% "Render a low-resolution cube map of the surroundings from the Route Editor camera, for reflections. The Shape Viewer always uses a fixed warehouse interior."
                QT_TRID_NOOP("settings.core.rendering.environment.map.enabled.description")).inGroup("rendering").inSubgroup("reflections"),
        "environmentMap", "Game::environmentMapEnabled", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::enumeration("core.rendering.environmentMap.faceSize", 256)
            .withNameId(
                //% "Environment map face size"
                QT_TRID_NOOP("settings.core.rendering.environment.map.face.size.name")).withDescriptionId(
                //% "Width and height of each of the six cube faces."
                QT_TRID_NOOP("settings.core.rendering.environment.map.face.size.description"))
            .withOptions(numericChoices({{128,
                //% "128 px"
                QT_TRID_NOOP("settings.core.rendering.environment.map.face.size.option.128")}, {256,
                //% "256 px"
                QT_TRID_NOOP("settings.core.rendering.environment.map.face.size.option.256")}})).withUnit("px").inGroup("rendering").inSubgroup("reflections"),
        "environmentMapSize", "Game::environmentMapSize", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::enumeration("core.rendering.environmentMap.facesPerFrame", 1)
            .withNameId(
                //% "Environment map faces per frame"
                QT_TRID_NOOP("settings.core.rendering.environment.map.faces.per.frame.name")).withDescriptionId(
                //% "Cube faces rendered each frame, in turn; six refreshes the whole cube every frame."
                QT_TRID_NOOP("settings.core.rendering.environment.map.faces.per.frame.description"))
            .withOptions(numericChoices({{1,
                //% "1"
                QT_TRID_NOOP("settings.core.rendering.environment.map.faces.per.frame.option.1")}, {2,
                //% "2"
                QT_TRID_NOOP("settings.core.rendering.environment.map.faces.per.frame.option.2")}, {3,
                //% "3"
                QT_TRID_NOOP("settings.core.rendering.environment.map.faces.per.frame.option.3")}, {6,
                //% "6"
                QT_TRID_NOOP("settings.core.rendering.environment.map.faces.per.frame.option.6")}})).inGroup("rendering").inSubgroup("reflections"),
        "environmentMapFacesPerFrame", "Game::environmentMapFacesPerFrame", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::floating("core.rendering.environmentMap.objectDistance", 300)
            .withNameId(
                //% "Environment map object distance"
                QT_TRID_NOOP("settings.core.rendering.environment.map.object.distance.name")).withDescriptionId(
                //% "Objects farther than this from the camera, and objects too small to cover a cube texel, are left out of the cube. Terrain and sky are always drawn."
                QT_TRID_NOOP("settings.core.rendering.environment.map.object.distance.description")).withRange(25, 2000, 25).withUnit("m").inGroup("rendering").inSubgroup("reflections"),
        "environmentMapObjectDistance", "Game::environmentMapObjectDistance", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::boolean("core.rendering.environmentMap.preview", false)
            .withNameId(
                //% "Show environment map"
                QT_TRID_NOOP("settings.core.rendering.environment.map.preview.name")).withDescriptionId(
                //% "Diagnostic: draw the cube faces unfolded in the lower-left corner of the Route Editor and Shape Viewer."
                QT_TRID_NOOP("settings.core.rendering.environment.map.preview.description")).inGroup("rendering").inSubgroup("reflections"),
        "environmentMapPreview", "Game::environmentMapPreview", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::boolean("core.rendering.water.shaded", true)
            .withNameId(
                //% "Shaded water"
                QT_TRID_NOOP("settings.core.rendering.water.shaded.name")).withDescriptionId(
                //% "Draw water as one surface with waves, reflections and sun glints, coloured by the route's water textures. When off, the water layers of the route's ENV file are drawn as stacked textures."
                QT_TRID_NOOP("settings.core.rendering.water.shaded.description")).inGroup("rendering").inSubgroup("water"),
        "waterShaded", "Game::waterShaded", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::boolean("core.rendering.water.reflection", true)
            .withNameId(
                //% "Water reflects the scene"
                QT_TRID_NOOP("settings.core.rendering.water.reflection.name")).withDescriptionId(
                //% "Shaded water mirrors the terrain, objects and sky around it, from an extra half-resolution view drawn when water is in sight. When off, water reflects the environment map or a sky gradient only."
                QT_TRID_NOOP("settings.core.rendering.water.reflection.description")).inGroup("rendering").inSubgroup("water"),
        "waterReflection", "Game::waterReflection", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::boolean("core.rendering.localLights.enabled", true)
            .withNameId(
                //% "Local lights"
                QT_TRID_NOOP("settings.core.rendering.local.lights.enabled.name")).withDescriptionId(
                //% "glTF lamps and emissive surfaces light the scene around them. QRhi renderer only."
                QT_TRID_NOOP("settings.core.rendering.local.lights.enabled.description")).inGroup("rendering").inSubgroup("lights"),
        "localLightsEnabled", "Game::localLightsEnabled", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::enumeration("core.rendering.ambientOcclusion", 0)
            .withNameId(
                //% "Ambient occlusion"
                QT_TRID_NOOP("settings.core.rendering.ambient.occlusion.name")).withDescriptionId(
                //% "Darkens ambient light in corners, under objects and where surfaces meet, computed from the view's depth (QRhi renderer). Higher quality costs more time per frame."
                QT_TRID_NOOP("settings.core.rendering.ambient.occlusion.description"))
            .withOptions(numericChoices({{0,
                //% "Off"
                QT_TRID_NOOP("settings.core.rendering.ambient.occlusion.option.off")}, {1,
                //% "Low"
                QT_TRID_NOOP("settings.core.rendering.ambient.occlusion.option.low")}, {2,
                //% "Medium"
                QT_TRID_NOOP("settings.core.rendering.ambient.occlusion.option.medium")}, {3,
                //% "High"
                QT_TRID_NOOP("settings.core.rendering.ambient.occlusion.option.high")}})).inGroup("rendering").inSubgroup("lights"),
        "ambientOcclusionQuality", "Game::ambientOcclusionQuality", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::enumeration("core.rendering.toneMapping", 0)
            .withNameId(
                //% "Tone mapping"
                QT_TRID_NOOP("settings.core.rendering.tone.mapping.name")).withDescriptionId(
                //% "How light brighter than white (lamps, glowing surfaces, sun glints) is fitted to the screen. Off clips it as before; Soft shoulder keeps the usual look and only rolls off highlights; ACES and AgX are filmic curves that also change mid-tones. QRhi renderer."
                QT_TRID_NOOP("settings.core.rendering.tone.mapping.description"))
            .withOptions(numericChoices({{0,
                //% "Off"
                QT_TRID_NOOP("settings.core.rendering.tone.mapping.option.off")}, {1,
                //% "Soft shoulder"
                QT_TRID_NOOP("settings.core.rendering.tone.mapping.option.soft")}, {2,
                //% "ACES"
                QT_TRID_NOOP("settings.core.rendering.tone.mapping.option.aces")}, {3,
                //% "AgX"
                QT_TRID_NOOP("settings.core.rendering.tone.mapping.option.agx")}})).inGroup("rendering").inSubgroup("image"),
        "toneMapping", "Game::toneMapping", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::floating("core.rendering.exposure", 0.0)
            .withNameId(
                //% "Exposure"
                QT_TRID_NOOP("settings.core.rendering.exposure.name")).withDescriptionId(
                //% "Brightens (positive) or darkens (negative) the final image, in stops: +1 doubles the light. QRhi renderer."
                QT_TRID_NOOP("settings.core.rendering.exposure.description")).withRange(-4, 4, 0.25).withUnit("EV").inGroup("rendering").inSubgroup("image"),
        "exposure", "Game::exposure", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::floating("core.rendering.bloom", 0.0)
            .withNameId(
                //% "Glow (bloom)"
                QT_TRID_NOOP("settings.core.rendering.bloom.name")).withDescriptionId(
                //% "Strength of the halo around light-emitting surfaces such as lamps and signal or train lights. Only emitted light glows, never surfaces that are merely bright. 0 turns it off. QRhi renderer."
                QT_TRID_NOOP("settings.core.rendering.bloom.description")).withRange(0, 4, 0.05).inGroup("rendering").inSubgroup("image"),
        "bloomStrength", "Game::bloomStrength", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::floating("core.rendering.localLights.exposure", 1.0)
            .withNameId(
                //% "Lamp brightness"
                QT_TRID_NOOP("settings.core.rendering.local.lights.exposure.name")).withDescriptionId(
                //% "Scale of glTF lamp intensities. At 1 a lamp of 1 candela lights a white surface 1 m away to 1/pi of the sun's light, as in the Khronos sample viewer."
                QT_TRID_NOOP("settings.core.rendering.local.lights.exposure.description")).withRange(0, 100, 0.1).inGroup("rendering").inSubgroup("lights"),
        "localLightsExposure", "Game::localLightsExposure", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::floating("core.rendering.localLights.emissiveGain", 1.0)
            .withNameId(
                //% "Glow brightness"
                QT_TRID_NOOP("settings.core.rendering.local.lights.emissive.gain.name")).withDescriptionId(
                //% "Scale of the light emissive surfaces cast. At 1 a surface lights its surroundings as a lamp of its brightness and size would."
                QT_TRID_NOOP("settings.core.rendering.local.lights.emissive.gain.description")).withRange(0, 1000, 1).inGroup("rendering").inSubgroup("lights"),
        "localLightsEmissiveGain", "Game::localLightsEmissiveGain", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::integer("core.rendering.defaultLineWidth", 1)
            .withNameId(
                //% "Default line width"
                QT_TRID_NOOP("settings.core.rendering.default.line.width.name")).withDescriptionId(
                //% "OpenGL width used for editor line primitives when an item does not request a custom width."
                QT_TRID_NOOP("settings.core.rendering.default.line.width.description")).withRange(1, 16, 1).withUnit("px").inGroup("rendering").inSubgroup("overlays"),
        "oglDefaultLineWidth", "Game::oglDefaultLineWidth", "OpenGL renderer", true, "hot-cache");
    ADD(SettingsDefinition::floating("core.rendering.fogDensity", 0.7)
            .withNameId(
                //% "Fog density"
                QT_TRID_NOOP("settings.core.rendering.fog.density.name")).withDescriptionId(
                //% "Fog density copied into each OpenGL scene helper when it is constructed."
                QT_TRID_NOOP("settings.core.rendering.fog.density.description")).withRange(0, 1, 0.01).inGroup("rendering").inSubgroup("environment"),
        "fogDensity", "Game::fogDensity", "GLUU", false, "renderer-construction");
    ADD(SettingsDefinition::string("core.rendering.fogColor", "#E6F8FF", SettingType::Color)
            .withNameId(
                //% "Fog colour"
                QT_TRID_NOOP("settings.core.rendering.fog.color.name")).withDescriptionId(
                //% "Colour used for distance fog and copied into the scene renderer."
                QT_TRID_NOOP("settings.core.rendering.fog.color.description")).inGroup("rendering").inSubgroup("environment"),
        "fogColor", "Game::fogColor", "GLUU", false, "renderer-construction");
    ADD(SettingsDefinition::string("core.rendering.skyColor", "#E6F8FF", SettingType::Color)
            .withNameId(
                //% "Sky colour"
                QT_TRID_NOOP("settings.core.rendering.sky.color.name")).withDescriptionId(
                //% "Base clear colour copied into the scene renderer."
                QT_TRID_NOOP("settings.core.rendering.sky.color.description")).inGroup("rendering").inSubgroup("environment"),
        "skyColor", "Game::skyColor", "GLUU", false, "renderer-construction");
    ADD(SettingsDefinition::boolean("core.rendering.timeOfDay.enabled", false)
            .withNameId(
                //% "Time of day"
                QT_TRID_NOOP("settings.core.rendering.time.of.day.enabled.name")).withDescriptionId(
                //% "Light the Route Editor as the sun would at the camera's latitude and longitude at the time and date below: sun direction, shadows, light and sky colours. When off, the editor uses its fixed light."
                QT_TRID_NOOP("settings.core.rendering.time.of.day.enabled.description")).inGroup("rendering").inSubgroup("environment"),
        "timeOfDayEnabled", "Game::timeOfDayEnabled", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::floating("core.rendering.timeOfDay.time", 12.0)
            .withNameId(
                //% "Time"
                QT_TRID_NOOP("settings.core.rendering.time.of.day.time.name")).withDescriptionId(
                //% "Local mean solar time in hours: 12 is when the sun is highest on average, whatever the route's time zone."
                QT_TRID_NOOP("settings.core.rendering.time.of.day.time.description")).withRange(0, 24, 0.25).withUnit("h").inGroup("rendering").inSubgroup("environment"),
        "timeOfDayHours", "Game::timeOfDayHours", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::string("core.rendering.timeOfDay.date", "2026-06-21", SettingType::String)
            .withNameId(
                //% "Date"
                QT_TRID_NOOP("settings.core.rendering.time.of.day.date.name")).withDescriptionId(
                //% "Date for the sun's path, as yyyy-MM-dd."
                QT_TRID_NOOP("settings.core.rendering.time.of.day.date.description")).inGroup("rendering").inSubgroup("environment"),
        "timeOfDayDate", "Game::timeOfDayDate", "RouteEditorGLWidget", true, "hot-cache");
    ADD(SettingsDefinition::boolean("core.rendering.renderTrackItems", false)
            .withNameId(
                //% "Render TrackDB items"
                QT_TRID_NOOP("settings.core.rendering.render.track.items.name")).withDescriptionId(
                //% "Display TrackDB and RoadDB interactive-item markers in the Route Editor scene."
                QT_TRID_NOOP("settings.core.rendering.render.track.items.description")).inGroup("rendering").inSubgroup("overlays"),
        "renderTrItems", "Game::renderTrItems", "RouteEditor", true, "hot-cache");

    order = 0;
    ADD(SettingsDefinition::boolean("core.interface.hideTools", false)
            .withNameId(
                //% "Hide Route Editor tools"
                QT_TRID_NOOP("settings.core.interface.hide.tools.name")).withDescriptionId(
                //% "Hide the Route Editor's auxiliary tool windows after its saved layout is created."
                QT_TRID_NOOP("settings.core.interface.hide.tools.description")).inGroup("interface").inSubgroup("routeEditor"),
        "toolsHidden", "Game::toolsHidden", "RouteEditor", false, "startup");
    ADD(SettingsDefinition::string("core.interface.accentColor", "#770000", SettingType::Color)
            .withNameId(
                //% "System-theme accent colour"
                QT_TRID_NOOP("settings.core.interface.accent.color.name")).withDescriptionId(
                //% "Used only when Use system theme is enabled. Controls interface headings and selections, with contrast adjusted against light or dark system palettes. TSRE's built-in dark theme retains its own coordinated accent colours."
                QT_TRID_NOOP("settings.core.interface.accent.color.description")).inGroup("interface").inSubgroup("appearance"),
        "", "Game::StyleMainLabel", "Application theme", true, "startup");
    ADD(SettingsDefinition::string("core.interface.mainWindowLayout", "PWT")
            .withNameId(
                //% "Route Editor window layout"
                QT_TRID_NOOP("settings.core.interface.main.window.layout.name")).withDescriptionId(
                //% "Compact startup code: P enables Properties, T enables Tools, and W denotes the main world view."
                QT_TRID_NOOP("settings.core.interface.main.window.layout.description")).inGroup("interface").inSubgroup("routeEditor"),
        "mainWindowLayout", "Game::mainWindowLayout", "RouteEditor", false, "startup");
    ADD(SettingsDefinition::string("core.interface.consistWindowLayout", "C1")
            .withNameId(
                //% "Consist Editor window layout"
                QT_TRID_NOOP("settings.core.interface.consist.window.layout.name")).withDescriptionId(
                //% "Compact startup code controlling the C, 1, 2, and U Consist Editor panels."
                QT_TRID_NOOP("settings.core.interface.consist.window.layout.description")).inGroup("interface").inSubgroup("consistEditor"),
        "ceWindowLayout", "Game::ceWindowLayout", "ConEditor", false, "startup");
    ADD(SettingsDefinition::string("core.interface.consistBackground", "", SettingType::Color).withNullDefault()
            .withNameId(
                //% "Consist preview background"
                QT_TRID_NOOP("settings.core.interface.consist.background.name")).withDescriptionId(
                //% "Background colour applied to the Consist Editor's consist preview when its window is created."
                QT_TRID_NOOP("settings.core.interface.consist.background.description")).inGroup("interface").inSubgroup("consistEditor"),
        "colorConView", "Game::colorConView", "ConEditor", false, "startup");
    ADD(SettingsDefinition::string("core.interface.shapeBackground", "", SettingType::Color).withNullDefault()
            .withNameId(
                //% "Shape preview background"
                QT_TRID_NOOP("settings.core.interface.shape.background.name")).withDescriptionId(
                //% "Background colour applied to shape preview widgets in the Consist Editor and Shape Viewer."
                QT_TRID_NOOP("settings.core.interface.shape.background.description")).inGroup("interface").inSubgroup("shapeViewer"),
        "colorShapeView", "Game::colorShapeView", "ShapeViewer", false, "startup");
    ADD(SettingsDefinition::boolean("core.interface.hud.enabled", false)
            .withNameId(
                //% "Enable Route Editor HUD"
                QT_TRID_NOOP("settings.core.interface.hud.enabled.name")).withDescriptionId(
                //% "Display the Route Editor heads-up overlay."
                QT_TRID_NOOP("settings.core.interface.hud.enabled.description")).inGroup("interface").inSubgroup("hud"),
        "hudEnabled", "Game::hudEnabled", "HUD", true, "cached");
    ADD(SettingsDefinition::floating("core.interface.hud.scale", 1.0)
            .withNameId(
                //% "HUD scale"
                QT_TRID_NOOP("settings.core.interface.hud.scale.name")).withDescriptionId(
                //% "Scale multiplier used to build the Route Editor HUD projection."
                QT_TRID_NOOP("settings.core.interface.hud.scale.description")).withRange(0.25, 4, 0.05).inGroup("interface").inSubgroup("hud"),
        "hudScale", "Game::hudScale", "HUD", true, "cached");
    ADD(SettingsDefinition::boolean("core.interface.hud.showEditorFps", false)
            .withNameId(
                //% "Show editor FPS"
                QT_TRID_NOOP("settings.core.interface.hud.show.editor.fps.name")).withDescriptionId(
                //% "Display Route Editor frame-rate diagnostics in the HUD."
                QT_TRID_NOOP("settings.core.interface.hud.show.editor.fps.description")).inGroup("interface").inSubgroup("hud"),
        "editorFpsHudEnabled", "Game::editorFpsHudEnabled", "HUD", true, "cached");
    ADD(SettingsDefinition::boolean("core.interface.markerLines", false)
            .withNameId(
                //% "Show route marker lines"
                QT_TRID_NOOP("settings.core.interface.marker.lines.name")).withDescriptionId(
                //% "Create line geometry connecting imported route-marker points."
                QT_TRID_NOOP("settings.core.interface.marker.lines.description")).inGroup("interface").inSubgroup("routeEditor"),
        "markerLines", "Game::markerLines", "Route markers", false, "marker-load-time");

    order = 0;
    ADD(SettingsDefinition::enumeration("core.track.defaultGradeFormat", "permille")
            .withNameId(
                //% "Default track grade format"
                QT_TRID_NOOP("settings.core.track.default.grade.format.name")).withDescriptionId(
                //% "Initial grade input format in track, dynamic-track, and ruler properties."
                QT_TRID_NOOP("settings.core.track.default.grade.format.description"))
            .withOptions(choices({{"permille",
                //% "Permille"
                QT_TRID_NOOP("settings.core.track.default.grade.format.option.permille")}, {"percent",
                //% "Percent"
                QT_TRID_NOOP("settings.core.track.default.grade.format.option.percent")}, {"oneInX",
                //% "1 in X"
                QT_TRID_NOOP("settings.core.track.default.grade.format.option.one.in.x")}, {"angle",
                //% "Angle"
                QT_TRID_NOOP("settings.core.track.default.grade.format.option.angle")}})).inGroup("editing").inSubgroup("track"),
        "defaultElevationBox", "Game::DefaultElevationBox", "Track property panels", false, "ui-construction");
    ADD(SettingsDefinition::floating("core.editing.defaultMoveStep", 0.25)
            .withNameId(
                //% "Default movement step"
                QT_TRID_NOOP("settings.core.editing.default.move.step.name")).withDescriptionId(
                //% "Initial translation increment and live-track quantization grid used by Route Editor controls."
                QT_TRID_NOOP("settings.core.editing.default.move.step.description")).withRange(0.001, 1000, 0.01).withUnit("m").inGroup("editing").inSubgroup("track"),
        "defaultMoveStep", "Game::DefaultMoveStep", "RouteEditorGLWidget/track properties", true, "session-cache");
    ADD(SettingsDefinition::floating("core.track.maximumElevationPermille", 700.0)
            .withNameId(
                //% "Maximum absolute track grade"
                QT_TRID_NOOP("settings.core.track.maximum.elevation.permille.name")).withDescriptionId(
                //% "Clamp track and dynamic-track property input to this absolute grade, expressed in per mille."
                QT_TRID_NOOP("settings.core.track.maximum.elevation.permille.description")).withRange(0, 1000, 1).withUnit("permille").inGroup("editing").inSubgroup("track"),
        "trackElevationMaxPm", "Game::trackElevationMaxPm", "Track property panels", false, "action-time");
    ADD(SettingsDefinition::boolean("core.track.snapRotationOnly", false)
            .withNameId(
                //% "Default to rotation-only snapping"
                QT_TRID_NOOP("settings.core.track.snap.rotation.only.name")).withDescriptionId(
                //% "Initial state of the Auto Placement 'Only Rot' option for each newly loaded route. Panel changes affect only the current route session and do not update this profile default."
                QT_TRID_NOOP("settings.core.track.snap.rotation.only.description")).inGroup("editing").inSubgroup("objects"),
        "snapableOnlyRot", "Game::snapableOnlyRot", "Route", true, "route-cache");
    ADD(SettingsDefinition::floating("core.track.snapRadius", 20.0)
            .withNameId(
                //% "Default placement snap radius"
                QT_TRID_NOOP("settings.core.track.snap.radius.name")).withDescriptionId(
                //% "Initial maximum snapping distance used by Auto Placement for each newly loaded route. Panel changes affect only the current route session and do not update this profile default."
                QT_TRID_NOOP("settings.core.track.snap.radius.description")).withRange(1, 999, 1).withUnit("m").inGroup("editing").inSubgroup("objects"),
        "", "Game::snapableRadius", "Route/ObjTools", true, "route-cache");
    ADD(SettingsDefinition::string("core.track.proceduralMode", "Enabled", SettingType::Enum)
            .withNameId(
                //% "Procedural track mode"
                QT_TRID_NOOP("settings.core.track.procedural.mode.name")).withDescriptionId(
                //% "Disabled always uses fallback geometry; Enabled uses requested procedural templates when available; Forced also falls back to DefaultTrack."
                QT_TRID_NOOP("settings.core.track.procedural.mode.description"))
            .withOptions(choices({{"Disabled",
                //% "Disabled"
                QT_TRID_NOOP("settings.core.track.procedural.mode.option.disabled")}, {"Enabled",
                //% "Enabled"
                QT_TRID_NOOP("settings.core.track.procedural.mode.option.enabled")}, {"Forced",
                //% "Forced"
                QT_TRID_NOOP("settings.core.track.procedural.mode.option.forced")}})).inGroup("editing").inSubgroup("track"),
        "proceduralTracks", "Game::proceduralTracks", "ProceduralTrackPolicy", true, "object-render-cache");
    ADD(SettingsDefinition::boolean("core.terrain.seasonalEditing", false)
            .withNameId(
                //% "Edit seasonal terrain files"
                QT_TRID_NOOP("settings.core.terrain.seasonal.editing.name")).withDescriptionId(
                //% "When a content season is selected, load and save terrain texture and raw files in that season's directories."
                QT_TRID_NOOP("settings.core.terrain.seasonal.editing.description")).inGroup("terrain").inSubgroup("terrain").asAdvanced(),
        "seasonalEditing", "Game::seasonalEditing", "Terrain", true, "terrain-load-save-time");
    ADD(SettingsDefinition::boolean("core.terrain.procedural.enabled", true)
            .withNameId(
                //% "Enable procedural materials"
                QT_TRID_NOOP("settings.core.terrain.procedural.enabled.name")).withDescriptionId(
                //% "Generate and display procedural terrain materials. When disabled, use saved static textures without loading or rewriting procedural maps or bakes. Procedural texture tools are unavailable; saved material data is preserved. Requires an application restart."
                QT_TRID_NOOP("settings.core.terrain.procedural.enabled.description")).inGroup("terrain").inSubgroup("proceduralMaterials"),
        "", "", "TerrainMaterialMap", true, "startup");
    ADD(SettingsDefinition::floating("core.terrain.procedural.detailDistance", 2048)
            .withNameId(
                //% "Detailed texture distance"
                QT_TRID_NOOP("settings.core.terrain.procedural.detail.distance.name")).withDescriptionId(
                //% "Horizontal camera-to-patch-centre distance for detailed procedural textures. Beyond this distance use the saved tile bake when available. Independent of terrain geometry and object draw distances; applies immediately."
                QT_TRID_NOOP("settings.core.terrain.procedural.detail.distance.description")).withRange(0, 200000, 128).withUnit("m").inGroup("terrain").inSubgroup("proceduralMaterials"),
        "", "", "TerrainMaterialMap", true, "hot-cache");
    ADD(SettingsDefinition::enumeration("core.terrain.procedural.patchTextureSize", 512)
            .withNameId(
                //% "Patch texture size"
                QT_TRID_NOOP("settings.core.terrain.procedural.patch.texture.size.name")).withDescriptionId(
                //% "Width and height of each generated detailed patch texture. Higher values increase generation time and memory use. Does not resize the painted material-ID map. Requires an application restart."
                QT_TRID_NOOP("settings.core.terrain.procedural.patch.texture.size.description")).withOptions(numericChoices({{128,
                //% "128 x 128"
                QT_TRID_NOOP("settings.core.terrain.procedural.patch.texture.size.option.128")},{256,
                //% "256 x 256"
                QT_TRID_NOOP("settings.core.terrain.procedural.patch.texture.size.option.256")},{512,
                //% "512 x 512"
                QT_TRID_NOOP("settings.core.terrain.procedural.patch.texture.size.option.512")},{1024,
                //% "1024 x 1024"
                QT_TRID_NOOP("settings.core.terrain.procedural.patch.texture.size.option.1024")},{2048,
                //% "2048 x 2048"
                QT_TRID_NOOP("settings.core.terrain.procedural.patch.texture.size.option.2048")}})).withUnit("px").inGroup("terrain").inSubgroup("proceduralMaterials"),
        "", "", "TerrainMaterialMap", true, "startup");
    ADD(SettingsDefinition::enumeration("core.terrain.procedural.bakedTextureSize", 1024)
            .withNameId(
                //% "Baked tile texture size"
                QT_TRID_NOOP("settings.core.terrain.procedural.baked.texture.size.name")).withDescriptionId(
                //% "Width and height of the whole-tile DXT1 fallback texture. Existing bakes remain visible; a changed size is generated on the next tile save after restarting. Does not resize the material-ID map."
                QT_TRID_NOOP("settings.core.terrain.procedural.baked.texture.size.description")).withOptions(numericChoices({{256,
                //% "256 x 256"
                QT_TRID_NOOP("settings.core.terrain.procedural.baked.texture.size.option.256")},{512,
                //% "512 x 512"
                QT_TRID_NOOP("settings.core.terrain.procedural.baked.texture.size.option.512")},{1024,
                //% "1024 x 1024"
                QT_TRID_NOOP("settings.core.terrain.procedural.baked.texture.size.option.1024")},{2048,
                //% "2048 x 2048"
                QT_TRID_NOOP("settings.core.terrain.procedural.baked.texture.size.option.2048")}})).withUnit("px").inGroup("terrain").inSubgroup("proceduralMaterials"),
        "", "", "TerrainMaterialMap", true, "startup");
    ADD(SettingsDefinition::boolean("core.terrain.procedural.validateBakeInputs", false)
            .withNameId(
                //% "Validate baked texture inputs"
                QT_TRID_NOOP("settings.core.terrain.procedural.validate.bake.inputs.name")).withDescriptionId(
                //% "Diagnostic/repair mode: hash the entire material-ID map and generation/source metadata on load and save. A mismatch requests a full rebake on save, but keeps displaying the existing fallback. Can cause pauses; leave off for normal editing. Requires an application restart."
                QT_TRID_NOOP("settings.core.terrain.procedural.validate.bake.inputs.description")).inGroup("terrain").inSubgroup("proceduralMaterials").asAdvanced(),
        "", "", "TerrainMaterialMap", true, "startup");
    ADD(SettingsDefinition::boolean("core.route.loading.preloadAllWorldFiles", false)
            .withNameId(
                //% "Preload all world files"
                QT_TRID_NOOP("settings.core.route.loading.preload.all.world.files.name")).withDescriptionId(
                //% "Load every route world tile during route initialization instead of only the normal working set; server mode forces this on."
                QT_TRID_NOOP("settings.core.route.loading.preload.all.world.files.description")).inGroup("content").inSubgroup("loading").asAdvanced(),
        "loadAllWFiles", "Game::loadAllWFiles", "Route", true, "route-load-time");
    ADD(SettingsDefinition::boolean("core.geometry.positiveQuaternionsOnly", false)
            .withNameId(
                //% "Canonicalize quaternion signs"
                QT_TRID_NOOP("settings.core.geometry.positive.quaternions.only.name")).withDescriptionId(
                //% "Convert object rotations to the equivalent representation with a non-negative W component. Existing affected objects are marked as modified when loaded. Intended for route compatibility or migration."
                QT_TRID_NOOP("settings.core.geometry.positive.quaternions.only.description")).inGroup("content").inSubgroup("savingRecovery").asAdvanced(),
        "useOnlyPositiveQuaternions", "Game::useOnlyPositiveQuaternions", "WorldObj", false, "route-load-and-save-time");

    order = 0;
    ADD(SettingsDefinition::integer("core.maps.imageResolution", 4096)
            .withNameId(
                //% "Generated map resolution"
                QT_TRID_NOOP("settings.core.maps.image.resolution.name")).withDescriptionId(
                //% "Width and height of the composite terrain-map image created after downloaded map tiles are assembled."
                QT_TRID_NOOP("settings.core.maps.image.resolution.description")).withRange(256, 16384, 256).withUnit("px").inGroup("maps").inSubgroup("imagery"),
        "mapImageResolution", "Game::mapImageResolution", "MapWindow", false, "generation-time");

    order = 0;
    ADD(SettingsDefinition::boolean("core.content.loading.preferOpenRailsEng", true)
            .withNameId(
                //% "Load OpenRails ENG overrides"
                QT_TRID_NOOP("settings.core.content.loading.prefer.open.rails.eng.name")).withDescriptionId(
                //% "For each locomotive, first try its ENG file and include paths under the trainset OpenRails subdirectory."
                QT_TRID_NOOP("settings.core.content.loading.prefer.open.rails.eng.description")).inGroup("content").inSubgroup("loading"),
        "ortsEngEnable", "Game::ortsEngEnable", "Eng", false, "rolling-stock-load-time");

    order = 0;
    ADD(SettingsDefinition::string("core.network.clientLogin", "")
            .withNameId(
                //% "Route Editor client login"
                QT_TRID_NOOP("settings.core.network.client.login.name")).withDescriptionId(
                //% "Connection template in username[:password]@host[:port] form. Insert protected parts with {secret:ID}, for example user:{secret:network.clientPassword}@server:port. A non-empty value starts remote-client mode."
                QT_TRID_NOOP("settings.core.network.client.login.description")).inGroup("network").inSubgroup("client"),
        "serverLogin", "Game::serverLogin", "RouteEditorClient", false, "startup");
    ADD(SettingsDefinition::string("core.network.serverAuthenticationMode", "", SettingType::Enum)
            .withNameId(
                //% "Server authentication mode"
                QT_TRID_NOOP("settings.core.network.server.authentication.mode.name")).withDescriptionId(
                //% "Select no authentication or validate users against users.txt in the server working directory."
                QT_TRID_NOOP("settings.core.network.server.authentication.mode.description"))
            .withOptions(choices({{"",
                //% "None"
                QT_TRID_NOOP("settings.core.network.server.authentication.mode.option.default")}, {"file",
                //% "users.txt file"
                QT_TRID_NOOP("settings.core.network.server.authentication.mode.option.file")}})).inGroup("network").inSubgroup("server"),
        "serverAuth", "Game::serverAuth", "RouteEditorServer", false, "server-startup");
    ADD(SettingsDefinition::boolean("core.network.playerMode", false)
            .withNameId(
                //% "Player mode"
                QT_TRID_NOOP("settings.core.network.player.mode.name")).withDescriptionId(
                //% "Experimental mode that hides Route Editor editing UI, suppresses the 3D placement pointer, and skips the interactive out-of-sync TrackSection repair prompt."
                QT_TRID_NOOP("settings.core.network.player.mode.description")).inGroup("advanced").inSubgroup("simulation").asAdvanced(),
        "playerMode", "Game::playerMode", "RouteEditor", true, "startup");
    ADD(SettingsDefinition::boolean("core.network.useNetworkEngine", false)
            .withNameId(
                //% "Use external locomotive data"
                QT_TRID_NOOP("settings.core.network.use.network.engine.name")).withDescriptionId(
                //% "Experimental integration that reads locomotive speed from TrainNetworkEng and publishes elevation, path distance, and geographic position during simulation updates."
                QT_TRID_NOOP("settings.core.network.use.network.engine.description")).inGroup("advanced").inSubgroup("simulation").asAdvanced(),
        "useNetworkEng", "Game::useNetworkEng", "Eng/TrainNetworkEng", true, "simulation-cache");

    order = 0;
    ADD(SettingsDefinition::boolean("core.advanced.useQuadTree", true)
            .withNameId(
                //% "Use saved terrain QuadTree"
                QT_TRID_NOOP("settings.core.advanced.use.quad.tree.name")).withDescriptionId(
                //% "Use the route's saved terrain index. When disabled, reconstruct a temporary index from terrain descriptors; use Errors and Messages > Fix to adopt it for ordinary saving. Remote clients always use the server's index."
                QT_TRID_NOOP("settings.core.advanced.use.quad.tree.description")).inGroup("advanced").inSubgroup("routeStorage").asAdvanced(),
        "useQuadTree", "Game::useQuadTree", "Route", false, "route-construction");
    ADD(SettingsDefinition::boolean("core.advanced.useEmptyTrackItems", true)
            .withNameId(
                //% "Reuse empty TrackDB item slots"
                QT_TRID_NOOP("settings.core.advanced.use.empty.track.items.name")).withDescriptionId(
                //% "Allocate new TrackDB items into existing emptyitem slots before extending the TrackDB item array."
                QT_TRID_NOOP("settings.core.advanced.use.empty.track.items.description")).inGroup("advanced").inSubgroup("routeStorage").asAdvanced(),
        "useTdbEmptyItems", "Game::useTdbEmptyItems", "TDB", true, "allocation-time");
    ADD(SettingsDefinition::boolean("core.advanced.ignoreMissingGlobalShapes", false)
            .withNameId(
                //% "Hide missing GLOBAL track shapes"
                QT_TRID_NOOP("settings.core.advanced.ignore.missing.global.shapes.name")).withDescriptionId(
                //% "Filter TrackSection shapes whose files are absent from GLOBAL/SHAPES out of Route Editor placement lists."
                QT_TRID_NOOP("settings.core.advanced.ignore.missing.global.shapes.description")).inGroup("advanced").inSubgroup("compatibility").asAdvanced(),
        "ignoreMissingGlobalShapes", "Game::ignoreMissingGlobalShapes", "ObjTools", false, "route-tools-load-time");

#undef ADD
    return true;
}

QMap<QString, SettingsRegistration::Provider> &extensionProviders() {
    static QMap<QString, SettingsRegistration::Provider> providers;
    return providers;
}
}

bool SettingsRegistration::addProvider(const QString &id,
                                       const Provider &provider,
                                       QString *error) {
    const QString normalized = id.trimmed();
    if (normalized.isEmpty() || !provider) {
        if (error) *error = "Settings provider requires an ID and callback.";
        return false;
    }
    if (extensionProviders().contains(normalized)) {
        if (error) *error = QString("Duplicate settings provider: %1").arg(normalized);
        return false;
    }
    extensionProviders().insert(normalized, provider);
    return true;
}

bool SettingsRegistration::registerAll(SettingsRegistry &registry, QString *error) {
    if (!registerCoreDefinitions(registry, error))
        return false;
    for (auto it = extensionProviders().constBegin();
         it != extensionProviders().constEnd(); ++it) {
        if (!it.value()(registry, error)) {
            if (error && error->isEmpty())
                *error = QString("Settings provider failed: %1").arg(it.key());
            return false;
        }
    }
    return true;
}
