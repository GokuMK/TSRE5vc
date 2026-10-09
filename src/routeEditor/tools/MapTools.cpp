/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "MapTools.h"
#include "ToolContext.h"
#include <QAction>
#include <QKeyEvent>
#include <QMenu>

namespace {

// Measure Distance: a left drag on the map measures from where it began to the
// pointer. The line stays until the next drag, Escape, Clear Measurement or another
// tool. A click without a drag clears it.
class MapMeasureTool : public EditorTool {
public:
    MapMeasureTool() : EditorTool(MapTools::MeasureToolId, ViewMode::Map) {}
    bool editsByDragging() const override { return true; }

    void deactivate(ToolContext &ctx) override { ctx.clearMapMeasurement(); }

    bool press(ToolContext &ctx, const ToolMouse &) override {
        from = here(ctx);
        ctx.clearMapMeasurement();
        return true;
    }

    void drag(ToolContext &ctx, const ToolMouse &) override {
        ctx.setMapMeasurement(from, here(ctx));
    }

    bool key(ToolContext &ctx, QKeyEvent *event) override {
        if (event->key() != Qt::Key_Escape)
            return false;
        ctx.clearMapMeasurement();
        return true;
    }

    void contextMenu(ToolContext &ctx, QMenu &menu) override {
        QAction *clear = menu.addAction(
            //% "Clear Measurement"
            qtTrId("route.editor.map.measure.clear"));
        QObject::connect(clear, &QAction::triggered, [&ctx] { ctx.clearMapMeasurement(); });
    }

private:
    // The map pointer (on the ground under the mouse), in the camera's tile.
    static MapGroundPoint here(ToolContext &ctx) {
        const float *p = ctx.pointer();
        return MapGroundPoint{ctx.tileX(), ctx.tileZ(), p[0], p[2]};
    }

    MapGroundPoint from;
};

}

namespace MapTools {

std::vector<std::unique_ptr<EditorTool>> create() {
    std::vector<std::unique_ptr<EditorTool>> tools;
    tools.push_back(std::make_unique<MapMeasureTool>());
    return tools;
}

}
