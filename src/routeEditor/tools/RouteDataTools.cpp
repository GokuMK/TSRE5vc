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
    ClickTool(const QString &id, std::function<void(ToolContext &)> command)
        : EditorTool(id), command(std::move(command)) {}

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
    auto click = [&tools](const char *id, std::function<void(ToolContext &)> command) {
        tools.push_back(std::make_unique<ClickTool>(id, std::move(command)));
    };
    click("mapTileShowTool", [](ToolContext &ctx) {
        Game::terrainLib->setTileBlob(ctx.tileX(), ctx.tileZ(), ctx.pointer());
    });
    click("mapTileLoadTool", [](ToolContext &ctx) {
        if (Terrain *terrain = terrainAtPointer(ctx))
            ctx.openMapTileWindow(terrain);
    });
    click("imageryTileLoadTool", [](ToolContext &ctx) {
        if (Terrain *terrain = terrainAtPointer(ctx))
            ctx.openImageryWindow(terrain);
    });
    click("heightTileLoadTool", [](ToolContext &ctx) {
        Game::terrainLib->setHeightFromGeoGui(ctx.tileX(), ctx.tileZ(), ctx.pointer());
    });
    click("actNewLooseConsistTool", [](ToolContext &ctx) {
        ctx.currentRoute()->actNewLooseConsist(ctx.tileX(), ctx.tileZ(), ctx.pointer());
        ctx.message("refreshActivityTools");
    });
    click("actNewSpeedZoneTool", [](ToolContext &ctx) {
        ctx.currentRoute()->actNewNewSpeedZone(ctx.tileX(), ctx.tileZ(), ctx.pointer());
        ctx.message("refreshActivityTools");
    });
    click("pickNewEventLocationTool", [](ToolContext &ctx) {
        ctx.currentRoute()->actPickNewEventLocation(ctx.tileX(), ctx.tileZ(), ctx.pointer());
        ctx.activateTool("");
    });
    return tools;
}

}
