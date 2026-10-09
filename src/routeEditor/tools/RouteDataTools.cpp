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
#include <tsre/geo/TerrainOverlays.h>
#include <QApplication>
#include <QGuiApplication>
#include <QPointer>
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
        // One statement per text: lupdate takes a //% text only right before its call.
        //% "populated, tile present"
        const QString tilePresent = qtTrId("route.editor.quad.tree.tool.label.populated.present");
        //% "populated, tile missing"
        const QString tileMissing = qtTrId("route.editor.quad.tree.tool.label.populated.missing");
        //% "not populated"
        const QString notPopulated = qtTrId("route.editor.quad.tree.tool.label.empty");
        menu.addSection(
            //% "Quad %1 km, %2: %3"
            qtTrId("route.editor.quad.tree.tool.label.quad").arg(quad.level * 2).arg(quad.name)
                .arg(quad.populated ? (exists ? tilePresent : tileMissing) : notPopulated));
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
    if (Game::terrainLib == nullptr)
        return nullptr;
    int x = ctx.tileX();
    int z = ctx.tileZ();
    float posx = ctx.pointer()[0];
    float posz = ctx.pointer()[2];
    Game::check_coords(x, z, posx, posz);
    Terrain *terrain = Game::terrainLib->getTerrainByXY(x, z);
    return terrain != nullptr && terrain->loaded ? terrain : nullptr;
}

// The terrain tile's overlay image (F3 Terrain Tile Overlay and Terrain Tile
// Texture): one tool, its actions in the context menu for the tile under the
// pointer; the panel's buttons choose the one a left click runs.
class TerrainOverlayTool : public EditorTool {
public:
    TerrainOverlayTool() : EditorTool("terrainOverlayTool", ViewMode::Scene3D | ViewMode::Map) {
        setDefaultAction("show");
    }

    QString title() const override {
        //% "Terrain Tile Overlay"
        return qtTrId("route.editor.overlay.tool.section.overlay");
    }

    bool press(ToolContext &ctx, const ToolMouse &) override {
        run(ctx, defaultAction());
        return true;
    }

    std::vector<ToolAction> actions(ToolContext &ctx) override {
        // The tile's state when it is loaded; actions load it when it is not.
        Terrain *terrain = terrainAtPointer(ctx);
        bool shown = false, image = true;
        if (terrain != nullptr) {
            int x, z;
            terrain->getLowCornerTileXY(x, z);
            image = TerrainOverlays::has(x, z);
            shown = terrain->showBlob;
        }
        const QString overlay = title();
        //% "Terrain Tile Texture"
        const QString texture = qtTrId("route.editor.overlay.tool.section.texture");
        ToolAction show{"show",
            //% "Show Loaded Overlay"
            qtTrId("route.editor.overlay.tool.action.show"), overlay};
        show.checkable = true;
        show.checked = shown;
        ToolAction make{"makeTexture",
            //% "Make from Overlay"
            qtTrId("route.editor.overlay.tool.action.make.texture"), texture};
        make.enabled = image;
        ToolAction osm{"osm",
            //% "Create from OSM"
            qtTrId("route.editor.overlay.tool.action.osm"), overlay};
        osm.enabled = !TerrainOverlays::osmBusy();
        ToolAction save{"save",
            //% "Save Overlay to Disk"
            qtTrId("route.editor.overlay.tool.action.save"), overlay};
        save.enabled = image;
        return {show,
                osm,
                {"imagery",
                 //% "Create from Imagery"
                 qtTrId("route.editor.overlay.tool.action.imagery"), overlay},
                save,
                make,
                {"removeTexture",
                 //% "Remove Overlay Texture"
                 qtTrId("route.editor.overlay.tool.action.remove.texture"), texture}};
    }

    void run(ToolContext &ctx, const QString &action) override {
        // The map completes the tile first; at any zoom.
        if (!ctx.prepareTerrainTile())
            return;
        if (action == QLatin1String("show")) {
            Game::terrainLib->setTileBlob(ctx.tileX(), ctx.tileZ(), ctx.pointer());
        } else if (action == QLatin1String("osm")) {
            createFromOsm(ctx);
        } else if (action == QLatin1String("imagery")) {
            Terrain *terrain = terrainAtPointer(ctx);
            if (terrain == nullptr)
                return;
            int x, z;
            terrain->getLowCornerTileXY(x, z);
            const QImage *before = TerrainOverlays::image(TerrainOverlays::key(x, z));
            const qint64 previous = before != nullptr ? before->cacheKey() : 0;
            ctx.openImageryWindow(terrain);
            // Applied in the window: shown at once.
            const QImage *after = TerrainOverlays::image(TerrainOverlays::key(x, z));
            if (after != nullptr && after->cacheKey() != previous)
                show(ctx, x, z);
        } else if (action == QLatin1String("save")) {
            Terrain *terrain = terrainAtPointer(ctx);
            if (terrain == nullptr)
                return;
            int x, z;
            terrain->getLowCornerTileXY(x, z);
            QString error;
            if (!TerrainOverlays::saveToDisk(x, z, error))
                QMessageBox::warning(ctx.view(), title(), error);
        } else if (action == QLatin1String("makeTexture")) {
            Game::terrainLib->makeTextureFromMap(ctx.tileX(), ctx.tileZ(), ctx.pointer());
        } else if (action == QLatin1String("removeTexture")) {
            Game::terrainLib->removeTileTextureFromMap(ctx.tileX(), ctx.tileZ(), ctx.pointer());
        }
    }

private:
    // Shows the overlay of the tile under the pointer, if its low corner is x, z.
    static void show(ToolContext &ctx, int x, int z) {
        if (Terrain *terrain = terrainAtPointer(ctx)) {
            int tx, tz;
            terrain->getLowCornerTileXY(tx, tz);
            if (tx == x && tz == z)
                terrain->showBlob = true;
        }
        ctx.terrainChanged();
    }

    // Draws the tile's OSM data (local files, else the web) and shows it.
    static void createFromOsm(ToolContext &ctx) {
        Terrain *terrain = terrainAtPointer(ctx);
        if (terrain == nullptr)
            return;
        int x, z;
        terrain->getLowCornerTileXY(x, z);
        const int size = terrain->getSampleCount() * terrain->getSampleSize();
        QPointer<QWidget> view = ctx.view();
        ToolContext *context = &ctx;
        // The pointer may move before the web answers: the tile is kept.
        const int pointerTile[2] = {ctx.tileX(), ctx.tileZ()};
        const float pointer[3] = {ctx.pointer()[0], ctx.pointer()[1], ctx.pointer()[2]};
        QString error;
        QGuiApplication::setOverrideCursor(Qt::BusyCursor);
        const bool started = TerrainOverlays::createFromOsm(x, z, size,
            [view, context, x, z, pointerTile, pointer](bool ok, const QString &message) {
                QGuiApplication::restoreOverrideCursor();
                if (view.isNull())
                    return;
                if (!ok) {
                    QMessageBox::warning(view,
                        //% "Create from OSM"
                        qtTrId("route.editor.overlay.tool.action.osm.title"), message);
                    return;
                }
                int tx = pointerTile[0], tz = pointerTile[1];
                float px = pointer[0], pz = pointer[2];
                Game::check_coords(tx, tz, px, pz);
                if (Terrain *tile = Game::terrainLib->getTerrainByXY(tx, tz)) {
                    int cx, cz;
                    tile->getLowCornerTileXY(cx, cz);
                    if (tile->loaded && cx == x && cz == z)
                        tile->showBlob = true;
                }
                context->terrainChanged();
            }, error);
        if (!started) {
            QGuiApplication::restoreOverrideCursor();
            QMessageBox::warning(ctx.view(), qtTrId("route.editor.overlay.tool.action.osm.title"), error);
        }
    }
};

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
    tools.push_back(std::make_unique<TerrainOverlayTool>());
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
