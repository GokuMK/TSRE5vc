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
#include <QAction>
#include <QMenu>
#include <QMessageBox>

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

// Edits the quadtree of the terrain being edited (F3 Edit Quad Tree): a
// left click does nothing; the context menu acts on the quad under the
// pointer, taken when the menu opens.
class QuadTreeTool : public EditorTool {
public:
    // The largest quad populated or given a tile: 32 km.
    static constexpr int MaxTileLevel = 16;

    QuadTreeTool() : EditorTool("quadTreeTool", ViewMode::Scene3D | ViewMode::Map) {}

    bool press(ToolContext &, const ToolMouse &) override { return true; }

    void contextMenu(ToolContext &ctx, QMenu &menu) override {
        QuadTree *tree = Game::terrainLib != nullptr ? Game::terrainLib->currentTree() : nullptr;
        if (tree == nullptr)
            return;
        int x = ctx.tileX(), z = ctx.tileZ();
        float px = ctx.pointer()[0], pz = ctx.pointer()[2];
        Game::check_coords(x, z, px, pz);
        // Tree coordinates count tiles northwards.
        const QuadTree::Quad quad = tree->quadAt(x, -z);
        const bool exists = Game::terrainLib->quadTileExists(quad);
        QWidget *view = ctx.view();
        menu.addSection(
            //% "Quad %1 km, %2: %3"
            qtTrId("route.editor.quad.tree.tool.label.quad").arg(quad.level * 2).arg(quad.name)
                .arg(quad.populated
                     ? (exists
                        //% "populated, tile present"
                        ? qtTrId("route.editor.quad.tree.tool.label.populated.present")
                        //% "populated, tile missing"
                        : qtTrId("route.editor.quad.tree.tool.label.populated.missing"))
                     //% "not populated"
                     : qtTrId("route.editor.quad.tree.tool.label.empty")));
        const bool writable = Game::writeEnabled;
        QAction *split = menu.addAction(
            //% "Split Quad"
            qtTrId("route.editor.quad.tree.tool.action.split"));
        split->setEnabled(writable && quad.level >= 2);
        QObject::connect(split, &QAction::triggered, [tree, quad] {
            tree->splitQuad(quad.x, quad.y, quad.level);
        });
        QAction *toggle = menu.addAction(
            //% "Toggle Populated"
            qtTrId("route.editor.quad.tree.tool.action.toggle.populated"));
        toggle->setEnabled(writable && quad.level <= MaxTileLevel);
        QObject::connect(toggle, &QAction::triggered, [tree, quad] {
            tree->setPopulated(quad.x, quad.y, quad.level, !quad.populated);
        });
        QAction *create = menu.addAction(
            //% "Create Tile"
            qtTrId("route.editor.quad.tree.tool.action.create.tile"));
        create->setEnabled(writable && quad.level <= MaxTileLevel);
        QObject::connect(create, &QAction::triggered, [view, quad, exists] {
            if (exists && QMessageBox::question(view,
                    //% "Create Tile"
                    qtTrId("route.editor.quad.tree.tool.dialog.create.title"),
                    //% "The tile %1 exists. Replace it with an empty tile?"
                    qtTrId("route.editor.quad.tree.tool.dialog.create.override").arg(quad.name))
                    != QMessageBox::Yes)
                return;
            QString error;
            if (!Game::terrainLib->createQuadTile(quad, exists, error))
                QMessageBox::warning(view, qtTrId("route.editor.quad.tree.tool.dialog.create.title"),
                                     error);
        });
        QAction *remove = menu.addAction(
            //% "Delete Tile"
            qtTrId("route.editor.quad.tree.tool.action.delete.tile"));
        remove->setEnabled(writable && (quad.populated || exists));
        QObject::connect(remove, &QAction::triggered, [view, quad] {
            if (QMessageBox::question(view,
                    //% "Delete Tile"
                    qtTrId("route.editor.quad.tree.tool.dialog.delete.title"),
                    //% "Delete the tile %1 and its files? This cannot be undone."
                    qtTrId("route.editor.quad.tree.tool.dialog.delete.question").arg(quad.name))
                    != QMessageBox::Yes)
                return;
            QString error;
            if (!Game::terrainLib->deleteQuadTile(quad, error))
                QMessageBox::warning(view, qtTrId("route.editor.quad.tree.tool.dialog.delete.title"),
                                     error);
        });
    }
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
    tools.push_back(std::make_unique<QuadTreeTool>());
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
