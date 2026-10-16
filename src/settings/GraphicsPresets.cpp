#include <settings/GraphicsPresets.h>

#include <QtGlobal>

namespace {
// The settings every preset sets, with Basic, Extended and Advanced values.
struct PresetRow {
    const char *key;
    QVariant basic;
    QVariant extended;
    QVariant advanced;
};

QVector<PresetRow> presetRows() {
    return {
        {"core.rendering.shadows.enabled", false, true, true},
        {"core.rendering.environmentMap.enabled", false, true, true},
        {"core.rendering.water.shaded", false, true, true},
        {"core.rendering.water.reflection", false, true, true},
        {"core.rendering.water.depth", false, true, true},
        {"core.rendering.localLights.enabled", false, true, true},
        // Dimmed, dimmed, auto exposure.
        {"core.rendering.lampsByDay", 0, 0, 2},
        {"core.rendering.skyEnclosure", false, false, true},
        // Off, off, low.
        {"core.rendering.ambientOcclusion", 0, 0, 1},
        // Off, off, AgX.
        {"core.rendering.toneMapping", 0, 0, 3},
        {"core.rendering.exposure", 0.0, 0.0, 0.0},
        {"core.rendering.bloom", 0.0, 3.0, 2.0},
        // The classic fog's cap; with the physical sky a fifth of the
        // weather's haze, so far mountains stay blue.
        {"core.rendering.fogDensity", 0.7, 0.7, 0.3},
        {"core.rendering.timeOfDay.enabled", true, true, true},
        // Classic, classic, physical.
        {"core.rendering.sky.model", 0, 0, 1},
        // Off, off, high.
        {"core.rendering.clouds", 0, 0, 3},
        // Off, off, volumetric.
        {"core.rendering.lightShafts", 0, 0, 2},
        // Mist and fog: off, off, high.
        {"core.rendering.mist", 0, 0, 2},
    };
}
}

const QVector<GraphicsPreset> &GraphicsPresets::all() {
    static const QVector<GraphicsPreset> presets = [] {
        QVector<GraphicsPreset> result{
            {"openRailsBasic",
             //% "OpenRails Basic"
             QT_TRID_NOOP("settings.graphics.preset.openrails.basic.name"),
             //% "The look of Open Rails: the route's sky and fog, no shadows, reflections or lamps. The sun still moves with the time of day."
             QT_TRID_NOOP("settings.graphics.preset.openrails.basic.description"),
             {}},
            {"openRailsExtended",
             //% "OpenRails Extended"
             QT_TRID_NOOP("settings.graphics.preset.openrails.extended.name"),
             //% "Open Rails with shadows, reflections, shaded water and lamps lighting the scene, dimmed by day; still the route's sky, without tone mapping. A new profile starts with it."
             QT_TRID_NOOP("settings.graphics.preset.openrails.extended.description"),
             {}},
            {"advancedEnvironment",
             //% "Advanced Environment"
             QT_TRID_NOOP("settings.graphics.preset.advanced.environment.name"),
             //% "The sky computed from the atmosphere, volumetric clouds and light shafts from the weather, ambient occlusion, AgX tone mapping and auto exposure, lamps that keep their strength in tunnels. QRhi renderer; costs the most time per frame."
             QT_TRID_NOOP("settings.graphics.preset.advanced.environment.description"),
             {}},
        };
        for (const PresetRow &row : presetRows()) {
            result[0].values.append({QString::fromLatin1(row.key), row.basic});
            result[1].values.append({QString::fromLatin1(row.key), row.extended});
            result[2].values.append({QString::fromLatin1(row.key), row.advanced});
        }
        return result;
    }();
    return presets;
}
