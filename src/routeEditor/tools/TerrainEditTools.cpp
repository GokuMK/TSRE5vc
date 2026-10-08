/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "TerrainEditTools.h"
#include "ToolContext.h"
#include <QAction>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <cmath>
#include <functional>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/gui/GuiFunct.h>
#include <tsre/texture/Brush.h>
#include <tsre/texture/TexLib.h>
#include <tsre/world/Route.h>
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/world/TerrainMaterialMap.h>

namespace {

// The texture tools also work in map mode (task editor 04); height, water
// and gaps make little sense on a flat map and stay 3D only.
const ViewModes TextureModes = ViewMode::Scene3D | ViewMode::Map;

// A tool whose click runs one command at the pointer.
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

// Brushes with a direction (raise or lower, show or hide): Z flips it, and
// the context menu sets it.
class DirectedBrushTool : public EditorTool {
public:
    using EditorTool::EditorTool;

    bool key(ToolContext &ctx, QKeyEvent *event) override {
        if (event->key() != Qt::Key_Z || ctx.controlDown())
            return false;
        ctx.brush()->direction = -ctx.brush()->direction;
        ctx.message("brushDirection", ctx.brush()->direction == 1 ? "+" : "-");
        return true;
    }

    void contextMenu(ToolContext &ctx, QMenu &menu) override {
        QMenu *direction = menu.addMenu(directionTitle());
        const bool up = ctx.brush()->direction + 1;
        QAction *upAction = GuiFunct::newMenuCheckAction(upText(), direction, up);
        QAction *downAction = GuiFunct::newMenuCheckAction(downText(), direction, !up);
        QObject::connect(upAction, &QAction::triggered, [&ctx] {
            ctx.brush()->direction = 1;
            ctx.message("brushDirection", "+");
        });
        QObject::connect(downAction, &QAction::triggered, [&ctx] {
            ctx.brush()->direction = -1;
            ctx.message("brushDirection", "-");
        });
        direction->addAction(upAction);
        direction->addAction(downAction);
    }

protected:
    virtual QString directionTitle() const = 0;
    virtual QString upText() const = 0;
    virtual QString downText() const = 0;
};

class HeightTool : public DirectedBrushTool {
public:
    HeightTool() : DirectedBrushTool("heightTool") {}

    bool press(ToolContext &ctx, const ToolMouse &) override {
        ctx.currentRoute()->paintHeightMap(ctx.brush(), ctx.tileX(), ctx.tileZ(), ctx.pointer());
        return true;
    }
    void drag(ToolContext &ctx, const ToolMouse &mouse) override {
        if (mouse.moved())
            ctx.currentRoute()->paintHeightMap(ctx.brush(), ctx.tileX(), ctx.tileZ(), ctx.pointer());
    }

protected:
    QString directionTitle() const override {
        //% "Paint Direction"
        return qtTrId("route.editor.route.editor.glwidget.group.paint.direction");
    }
    QString upText() const override {
        //% "Up"
        return qtTrId("route.editor.route.editor.glwidget.text.up");
    }
    QString downText() const override {
        //% "Down"
        return qtTrId("route.editor.route.editor.glwidget.text.down");
    }
};

class WaterTool : public DirectedBrushTool {
public:
    WaterTool() : DirectedBrushTool("waterTerrTool") {}

    bool press(ToolContext &ctx, const ToolMouse &) override {
        toggle(ctx);
        return true;
    }
    void drag(ToolContext &ctx, const ToolMouse &mouse) override {
        if (mouse.moved())
            toggle(ctx);
    }

protected:
    QString directionTitle() const override {
        //% "Water"
        return qtTrId("route.editor.route.editor.glwidget.group.water");
    }
    QString upText() const override {
        //% "Show"
        return qtTrId("route.editor.route.editor.glwidget.text.show");
    }
    QString downText() const override {
        //% "Hide"
        return qtTrId("route.editor.route.editor.glwidget.text.hide");
    }

private:
    static void toggle(ToolContext &ctx) {
        Game::terrainLib->toggleWaterDraw(ctx.tileX(), ctx.tileZ(), ctx.pointer(),
                                          ctx.brush()->direction);
    }
};

class GapsTool : public DirectedBrushTool {
public:
    GapsTool() : DirectedBrushTool("gapsTerrainTool") {}

    bool press(ToolContext &ctx, const ToolMouse &) override {
        Game::terrainLib->toggleGaps(ctx.tileX(), ctx.tileZ(), ctx.pointer(), ctx.brush()->direction);
        return true;
    }

protected:
    QString directionTitle() const override {
        //% "Gaps"
        return qtTrId("route.editor.route.editor.glwidget.group.gaps");
    }
    QString upText() const override {
        //% "Show"
        return qtTrId("route.editor.route.editor.glwidget.text.show.2");
    }
    QString downText() const override {
        //% "Hide"
        return qtTrId("route.editor.route.editor.glwidget.text.hide.2");
    }
};

// Colour and texture painting; Control paints along the nearest track,
// Shift around the nearest object.
class PaintTool : public EditorTool {
public:
    explicit PaintTool(const QString &id) : EditorTool(id, TextureModes) {}
    bool editsByDragging() const override { return true; }

    bool press(ToolContext &ctx, const ToolMouse &) override {
        if (!ctx.prepareTerrainEdit(false))
            return true;
        if (ctx.controlDown())
            ctx.currentRoute()->setTerrainTextureToTrack(ctx.tileX(), ctx.tileZ(), ctx.pointer(),
                                                         ctx.brush(), 0);
        else if (ctx.shiftDown())
            ctx.currentRoute()->setTerrainTextureToObj(ctx.tileX(), ctx.tileZ(), ctx.pointer(),
                                                       ctx.brush(), nullptr);
        else
            Game::terrainLib->paintTexture(ctx.brush(), ctx.tileX(), ctx.tileZ(), ctx.pointer());
        return true;
    }
    void drag(ToolContext &ctx, const ToolMouse &mouse) override {
        if (mouse.moved() && ctx.prepareTerrainEdit(false))
            Game::terrainLib->paintTexture(ctx.brush(), ctx.tileX(), ctx.tileZ(), ctx.pointer());
    }

    void contextMenu(ToolContext &ctx, QMenu &menu) override {
        QMenu *autoPaint = menu.addMenu(
            //% "Auto Paint"
            qtTrId("route.editor.route.editor.glwidget.group.auto.paint"));
        QObject::connect(autoPaint->addAction(
            //% "&Selected Object"
            qtTrId("route.editor.route.editor.glwidget.action.selected.object")),
            &QAction::triggered, [&ctx] {
                ctx.currentRoute()->setTerrainTextureToObj(ctx.tileX(), ctx.tileZ(), ctx.pointer(),
                        ctx.brush(), static_cast<WorldObj *>(ctx.selected()));
            });
        QObject::connect(autoPaint->addAction(
            //% "&Nearest Object"
            qtTrId("route.editor.route.editor.glwidget.action.nearest.object")),
            &QAction::triggered, [&ctx] {
                ctx.currentRoute()->setTerrainTextureToObj(ctx.tileX(), ctx.tileZ(), ctx.pointer(),
                                                           ctx.brush(), nullptr);
            });
        QObject::connect(autoPaint->addAction(
            //% "&Nearest Track or Road"
            qtTrId("route.editor.route.editor.glwidget.action.nearest.track.road")),
            &QAction::triggered, [&ctx] {
                ctx.currentRoute()->setTerrainTextureToTrack(ctx.tileX(), ctx.tileZ(), ctx.pointer(),
                                                             ctx.brush(), 0);
            });
        QObject::connect(autoPaint->addAction(
            //% "&Nearest TDB/RDB Vector"
            qtTrId("route.editor.route.editor.glwidget.action.nearest.tdb.rdb.vector")),
            &QAction::triggered, [&ctx] {
                ctx.currentRoute()->setTerrainTextureToTrack(ctx.tileX(), ctx.tileZ(), ctx.pointer(),
                                                             ctx.brush(), 1);
            });
    }
};

class ProceduralPaintTool : public EditorTool {
public:
    ProceduralPaintTool(const QString &id, int operation, bool paintsWhileDragging)
        : EditorTool(id, TextureModes), operation(operation),
          paintsWhileDragging(paintsWhileDragging) {}
    bool editsByDragging() const override { return paintsWhileDragging; }

    bool press(ToolContext &ctx, const ToolMouse &) override {
        if (!ctx.prepareTerrainEdit(true))
            return true;
        Game::terrainLib->paintProceduralTexture(ctx.brush(), ctx.tileX(), ctx.tileZ(), ctx.pointer(),
                                                 operation);
        return true;
    }
    void drag(ToolContext &ctx, const ToolMouse &mouse) override {
        if (!paintsWhileDragging || !mouse.moved() || !ctx.prepareTerrainEdit(true))
            return;
        Undo::StateBeginIfNotExist();
        Game::terrainLib->paintProceduralTexture(ctx.brush(), ctx.tileX(), ctx.tileZ(), ctx.pointer());
    }

private:
    int operation;
    bool paintsWhileDragging;
};

class PickTextureTool : public EditorTool {
public:
    PickTextureTool() : EditorTool("pickTerrainTexTool", TextureModes) {}

    bool press(ToolContext &ctx, const ToolMouse &) override {
        if (!ctx.prepareTerrainEdit(false))
            return true;
        const int textureId = Game::terrainLib->getTexture(ctx.tileX(), ctx.tileZ(), ctx.pointer());
        ctx.reportTextureId(textureId);
        int x = ctx.tileX(), z = ctx.tileZ();
        float px = ctx.pointer()[0], pz = ctx.pointer()[2];
        Game::check_coords(x, z, px, pz);
        Terrain *terrain = Game::terrainLib->getTerrainByXY(x, z);
        if (terrain && textureId >= 0)
            terrain->rememberProceduralSource(ctx.brush(), x, z, px, pz);
        if (textureId >= 0)
            ctx.reportMaterialPicked();
        // Procedural picking acquires a source ref; static picking borrows
        // the patch's ref. The toolbar now retains its own selection/history.
        if (terrain && terrain->usesProceduralMaterial() && textureId >= 0)
            TexLib::delRef(textureId);
        return true;
    }
};

class ProceduralTileTool : public EditorTool {
public:
    ProceduralTileTool(const QString &id, bool enable)
        : EditorTool(id, TextureModes), enable(enable) {}

    bool press(ToolContext &ctx, const ToolMouse &) override {
        if (!ctx.prepareTerrainEdit(false))
            return true;
        int x = ctx.tileX(), z = ctx.tileZ();
        float px = ctx.pointer()[0], pz = ctx.pointer()[2];
        Game::check_coords(x, z, px, pz);
        Terrain *terrain = Game::terrainLib->getTerrainByXY(x, z);
        QString error;
        bool restore = false, cancelled = false;
        if (terrain && terrain->loaded && enable && !terrain->usesProceduralMaterial()
                && terrain->hasSavedProceduralMap()) {
            const auto answer = QMessageBox::question(ctx.view(),
                QCoreApplication::translate("RouteEditorGLWidget", "Restore procedural map"),
                QCoreApplication::translate("RouteEditorGLWidget",
                    "An existing procedural material map was found for this tile. Restore it?\n\n"
                    "Painted regions will be preserved, but random materials may be assigned if the original material mapping is missing.\n\n"
                    "Choose No to fill the whole tile with the selected material instead."),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
            restore = answer == QMessageBox::Yes;
            cancelled = answer == QMessageBox::Cancel;
        }
        Brush *brush = ctx.brush();
        if (!cancelled && terrain && terrain->loaded
                && !terrain->setProceduralMaterial(enable, error,
                                                   brush ? brush->terrainMaterialUid : 0, restore))
            QMessageBox::warning(ctx.view(),
                //% "Procedural terrain"
                qtTrId("route.editor.route.editor.glwidget.dialog.title.procedural.terrain"), error);
        return true;
    }

private:
    bool enable;
};

// Sets whole patch textures; the brush sets their orientation.
class PutTextureTool : public EditorTool {
public:
    PutTextureTool() : EditorTool("putTerrainTexTool", TextureModes) {}
    bool editsByDragging() const override { return true; }

    bool press(ToolContext &ctx, const ToolMouse &) override {
        if (ctx.prepareTerrainEdit(false))
            put(ctx);
        return true;
    }
    void drag(ToolContext &ctx, const ToolMouse &) override {
        // A patch is 32 m wide: one texture a patch the pointer crosses.
        if ((std::fabs(last[0] - ctx.pointer()[0]) > 32 || std::fabs(last[2] - ctx.pointer()[2]) > 32)
                && ctx.prepareTerrainEdit(false))
            put(ctx);
    }

    void contextMenu(ToolContext &ctx, QMenu &menu) override {
        QMenu *orientation = menu.addMenu(
            //% "Default"
            qtTrId("route.editor.route.editor.glwidget.group.default"));
        Brush *brush = ctx.brush();
        struct Entry { QString text; Brush::Transformation transformation; };
        const Entry entries[] = {
            {//% "&Random"
             qtTrId("route.editor.route.editor.glwidget.action.random"), brush->RANDOM},
            {//% "&Present"
             qtTrId("route.editor.route.editor.glwidget.action.present"), brush->PRESENT},
            {//% "&Rotate 0°"
             qtTrId("route.editor.route.editor.glwidget.action.rotate.0"), brush->ROT0},
            {//% "&Rotate 90°"
             qtTrId("route.editor.route.editor.glwidget.action.rotate.90"), brush->ROT90},
            {//% "&Rotate 180°"
             qtTrId("route.editor.route.editor.glwidget.action.rotate.180"), brush->ROT180},
            {//% "&Rotate 270°"
             qtTrId("route.editor.route.editor.glwidget.action.rotate.270"), brush->ROT270}};
        for (const Entry &entry : entries) {
            QAction *action = GuiFunct::newMenuCheckAction(entry.text, orientation,
                                                           brush->texTransformation == entry.transformation);
            const Brush::Transformation transformation = entry.transformation;
            QObject::connect(action, &QAction::triggered,
                             [&ctx, transformation] { ctx.brush()->texTransformation = transformation; });
            orientation->addAction(action);
        }
    }

private:
    void put(ToolContext &ctx) {
        Game::terrainLib->setTerrainTexture(ctx.brush(), ctx.tileX(), ctx.tileZ(), ctx.pointer());
        std::copy(ctx.pointer(), ctx.pointer() + 3, last);
    }
    float last[3] = {0, 0, 0};
};

}

namespace TerrainEditTools {

std::vector<std::unique_ptr<EditorTool>> create() {
    std::vector<std::unique_ptr<EditorTool>> tools;
    tools.push_back(std::make_unique<HeightTool>());
    tools.push_back(std::make_unique<WaterTool>());
    tools.push_back(std::make_unique<GapsTool>());
    tools.push_back(std::make_unique<PaintTool>("paintToolColor"));
    tools.push_back(std::make_unique<PaintTool>("paintToolTexture"));
    tools.push_back(std::make_unique<ProceduralPaintTool>(
            "proceduralPaintTextureTool", TerrainMaterialMap::TexturePaint, true));
    tools.push_back(std::make_unique<ProceduralPaintTool>(
            "proceduralFillPatchTool", TerrainMaterialMap::FillPatch, false));
    tools.push_back(std::make_unique<ProceduralPaintTool>(
            "proceduralFillTool", TerrainMaterialMap::FloodFill, false));
    tools.push_back(std::make_unique<PickTextureTool>());
    tools.push_back(std::make_unique<ProceduralTileTool>("proceduralTileEnableTool", true));
    tools.push_back(std::make_unique<ProceduralTileTool>("proceduralTileDisableTool", false));
    tools.push_back(std::make_unique<PutTextureTool>());
    auto click = [&tools](const char *id, std::function<void(ToolContext &)> command,
                          ViewModes modes = ViewMode::Scene3D) {
        tools.push_back(std::make_unique<ClickTool>(id, std::move(command), modes));
    };
    click("drawTerrTool", [](ToolContext &ctx) {
        Game::terrainLib->toggleDraw(ctx.tileX(), ctx.tileZ(), ctx.pointer());
    });
    click("waterHeightTileTool", [](ToolContext &ctx) {
        Game::terrainLib->setWaterLevelGui(ctx.tileX(), ctx.tileZ(), ctx.pointer());
    });
    click("fixedTileTool", [](ToolContext &ctx) {
        Game::terrainLib->setFixedTileHeight(ctx.brush(), ctx.tileX(), ctx.tileZ(), ctx.pointer());
    });
    click("lockTexTool", [](ToolContext &ctx) {
        if (ctx.prepareTerrainEdit(false))
            Game::terrainLib->lockTexture(ctx.brush(), ctx.tileX(), ctx.tileZ(), ctx.pointer());
    }, TextureModes);
    click("makeTileTextureTool", [](ToolContext &ctx) {
        Game::terrainLib->makeTextureFromMap(ctx.tileX(), ctx.tileZ(), ctx.pointer());
    });
    click("removeTileTextureTool", [](ToolContext &ctx) {
        Game::terrainLib->removeTileTextureFromMap(ctx.tileX(), ctx.tileZ(), ctx.pointer());
    });
    return tools;
}

}
