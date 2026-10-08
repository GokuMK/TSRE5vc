/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "RouteDataTools.h"
#include "ToolContext.h"
#include <functional>
#include <tsre/Game.h>
#include <tsre/world/Route.h>
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainLib.h>

namespace {

class ClickTool : public EditorTool {
public:
    ClickTool(const QString &id, std::function<void(ToolContext &)> command,
              ViewModes modes = ViewMode::Scene3D)
        : EditorTool(id, modes), command(std::move(command)) {}

    bool press(ToolContext &ctx, const ToolMouse &) override {
        command(ctx);
        return true;
    }

private:
    std::function<void(ToolContext &)> command;
};

// The loaded terrain tile under the pointer, or null.
Terrain *terrainAtPointer(ToolContext &ctx) {
    int x = ctx.tileX();
    int z = ctx.tileZ();
    float posx = ctx.pointer()[0];
    float posz = ctx.pointer()[2];
    Game::check_coords(x, z, posx, posz);
    Terrain *terrain = Game::terrainLib->getTerrainByXY(x, z);
    return terrain != nullptr && terrain->loaded ? terrain : nullptr;
}

}

namespace RouteDataTools {

std::vector<std::unique_ptr<EditorTool>> create() {
    std::vector<std::unique_ptr<EditorTool>> tools;
    auto click = [&tools](const char *id, std::function<void(ToolContext &)> command,
                          ViewModes modes = ViewMode::Scene3D) {
        tools.push_back(std::make_unique<ClickTool>(id, std::move(command), modes));
    };
    // The activity tools find the nearest track to the pointer, which works
    // on the map too, where the pointer has no height.
    const ViewModes bothModes = ViewMode::Scene3D | ViewMode::Map;
    auto useHeight = [](ToolContext &ctx) { return ctx.viewMode() == ViewMode::Scene3D; };
    // The geo tools act on the tile under the pointer, which the map
    // completes first; they work at any zoom, the tile picked by its
    // square or border.
    click("mapTileShowTool", [](ToolContext &ctx) {
        if (ctx.prepareTerrainTile())
            Game::terrainLib->setTileBlob(ctx.tileX(), ctx.tileZ(), ctx.pointer());
    }, bothModes);
    click("mapTileLoadTool", [](ToolContext &ctx) {
        if (!ctx.prepareTerrainTile())
            return;
        if (Terrain *terrain = terrainAtPointer(ctx))
            ctx.openMapTileWindow(terrain);
    }, bothModes);
    click("imageryTileLoadTool", [](ToolContext &ctx) {
        if (!ctx.prepareTerrainTile())
            return;
        if (Terrain *terrain = terrainAtPointer(ctx))
            ctx.openImageryWindow(terrain);
    }, bothModes);
    click("heightTileLoadTool", [](ToolContext &ctx) {
        if (ctx.prepareTerrainTile())
            Game::terrainLib->setHeightFromGeoGui(ctx.tileX(), ctx.tileZ(), ctx.pointer());
    }, bothModes);
    click("actNewLooseConsistTool", [useHeight](ToolContext &ctx) {
        ctx.currentRoute()->actNewLooseConsist(ctx.tileX(), ctx.tileZ(), ctx.pointer(),
                                               useHeight(ctx));
        ctx.message("refreshActivityTools");
    }, bothModes);
    click("actNewSpeedZoneTool", [useHeight](ToolContext &ctx) {
        ctx.currentRoute()->actNewNewSpeedZone(ctx.tileX(), ctx.tileZ(), ctx.pointer(),
                                               useHeight(ctx));
        ctx.message("refreshActivityTools");
    }, bothModes);
    click("pickNewEventLocationTool", [useHeight](ToolContext &ctx) {
        ctx.currentRoute()->actPickNewEventLocation(ctx.tileX(), ctx.tileZ(), ctx.pointer(),
                                                    useHeight(ctx));
        ctx.activateTool("");
    }, bothModes);
    return tools;
}

}
