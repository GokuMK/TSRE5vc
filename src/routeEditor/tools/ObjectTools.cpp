/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ObjectTools.h"
#include "ToolContext.h"
#include <QAction>
#include <QDateTime>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/gui/GuiFunct.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/math3d/Vector2f.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/world/Route.h>
#include <tsre/world/objects/TelepoleObj.h>
#include <tsre/world/objects/WorldObj.h>

namespace {

// Before a fresh placement: the previous object is let go, and joins the
// track database if placed objects do.
void releasePrevious(ToolContext &ctx) {
    GameObj *previous = ctx.selected();
    if (previous == nullptr)
        return;
    previous->unselect();
    if (ctx.autoAddToTrackDb() && previous->typeObj == GameObj::worldobj)
        ctx.currentRoute()->addToTDBIfNotExist(static_cast<WorldObj *>(previous));
}

// Select and place: the selected object moves with the keys and the wheel,
// and the context menu sets the pointer and placement defaults.
class ObjectEditTool : public EditorTool {
public:
    using EditorTool::EditorTool;

    bool wheel(ToolContext &ctx, float steps) override {
        GameObj *object = ctx.selected();
        if (object != nullptr && object->typeObj == GameObj::worldobj) {
            Undo::StateBeginIfNotExist();
            Undo::PushGameObjData(object);
            static_cast<WorldObj *>(object)->translate(0, steps * ctx.keyMoveStep(), 0);
        }
        return true;
    }

    bool key(ToolContext &ctx, QKeyEvent *event) override;

    void contextMenu(ToolContext &ctx, QMenu &menu) override {
        QMenu *pointer = menu.addMenu(
            //% "Pointer"
            qtTrId("route.editor.route.editor.glwidget.group.pointer"));
        QAction *stickTerrain = GuiFunct::newMenuCheckAction(
            //% "&Stick to Terrain"
            qtTrId("route.editor.route.editor.glwidget.action.stick.terrain"), pointer,
            ctx.pointerSticksToTerrain());
        QObject::connect(stickTerrain, &QAction::triggered,
                         [&ctx] { ctx.setPointerSticksToTerrain(true); });
        QAction *stickAll = GuiFunct::newMenuCheckAction(
            //% "&Stick to All"
            qtTrId("route.editor.route.editor.glwidget.action.stick.all"), pointer,
            !ctx.pointerSticksToTerrain());
        QObject::connect(stickAll, &QAction::triggered,
                         [&ctx] { ctx.setPointerSticksToTerrain(false); });
        pointer->addAction(stickTerrain);
        pointer->addAction(stickAll);
        QAction *resetStep = menu.addAction(
            //% "&Reset MoveStep"
            qtTrId("route.editor.route.editor.glwidget.action.reset.move.step"));
        QObject::connect(resetStep, &QAction::triggered, [&ctx] { ctx.resetKeyMoveStep(); });
        QAction *resetRotation = menu.addAction(
            //% "&Reset Rotation"
            qtTrId("route.editor.route.editor.glwidget.action.reset.rotation"));
        QObject::connect(resetRotation, &QAction::triggered, [&ctx] { ctx.resetPlacementRotation(); });
    }

protected:
    // Moves of the selected object by the mouse start after this long, so a
    // click does not shift it.
    static constexpr qint64 DragDelayMs = 200;
    qint64 pressTime = 0;
};

bool ObjectEditTool::key(ToolContext &ctx, QKeyEvent *event) {
    GameObj *object = ctx.selected();
    const ToolContext::ObjectEdit edit = ctx.objectEdit();
    const bool resize = edit == ToolContext::ObjectEdit::Resize;
    const bool rotate = edit == ToolContext::ObjectEdit::Rotate;
    const float step = ctx.keyMoveStep();
    Vector2f a;
    switch (event->key()) {
    case Qt::Key_Up:
        if (Game::usenNumPad)
            break;
        [[fallthrough]];
    case Qt::Key_8:
        Undo::PushGameObjData(object);
        if (resize && object != nullptr) {
            object->resize(step, 0, 0);
        } else if (rotate && object != nullptr) {
            object->rotate(step / 10, 0, 0);
        } else if (object != nullptr) {
            a.y = step;
            a.rotate(-ctx.cameraHeading(), 0);
            object->translate(a.x, 0, a.y);
        }
        break;
    case Qt::Key_Down:
        if (Game::usenNumPad)
            break;
        [[fallthrough]];
    case Qt::Key_2:
        Undo::PushGameObjData(object);
        if (resize && object != nullptr) {
            object->resize(-step, 0, 0);
        } else if (rotate && object != nullptr) {
            object->rotate(-step / 10, 0, 0);
        } else if (object != nullptr) {
            a.y = -step;
            a.rotate(-ctx.cameraHeading(), 0);
            object->translate(a.x, 0, a.y);
        }
        break;
    case Qt::Key_Left:
        if (Game::usenNumPad)
            break;
        [[fallthrough]];
    case Qt::Key_4:
        Undo::PushGameObjData(object);
        if (resize && object != nullptr) {
            object->resize(0, step, 0);
        } else if (rotate && object != nullptr) {
            object->rotate(0, -step / 10, 0);
        } else if (object != nullptr) {
            a.x = step;
            a.rotate(-ctx.cameraHeading(), 0);
            object->translate(a.x, 0, a.y);
        }
        break;
    case Qt::Key_Right:
        if (Game::usenNumPad)
            break;
        [[fallthrough]];
    case Qt::Key_6:
        Undo::PushGameObjData(object);
        if (resize && object != nullptr) {
            object->resize(0, -step, 0);
        } else if (rotate && object != nullptr) {
            object->rotate(0, step / 10, 0);
        } else if (object != nullptr) {
            a.x = -step;
            a.rotate(-ctx.cameraHeading(), 0);
            object->translate(a.x, 0, a.y);
        }
        break;
    case Qt::Key_PageUp:
    case Qt::Key_9:
        Undo::PushGameObjData(object);
        if (resize && object != nullptr) {
            object->resize(0, 0, step);
        } else if (rotate && object != nullptr) {
            object->rotate(0, 0, step / 10);
        } else if (object != nullptr) {
            object->translate(0, step, 0);
        }
        break;
    case Qt::Key_PageDown:
    case Qt::Key_3:
    case Qt::Key_7:
        Undo::PushGameObjData(object);
        if (rotate && object != nullptr) {
            object->rotate(0, 0, -step / 10);
        } else if (resize && object != nullptr) {
            object->resize(0, 0, -step);
        } else if (object != nullptr) {
            object->translate(0, -step, 0);
        }
        break;
    case Qt::Key_F:
        ctx.terrainToSelected();
        break;
    case Qt::Key_H:
        ctx.selectedPositionToTerrain();
        break;
    case Qt::Key_N:
        ctx.selectedRotationToTerrain();
        break;
    case Qt::Key_Delete:
        if (object != nullptr) {
            if (object->typeObj == GameObj::worldobj) {
                ctx.currentRoute()->deleteObj(static_cast<WorldObj *>(object));
                object->unselect();
            }
            if (object->typeObj == GameObj::tritemobj) {
                QMessageBox msgBox;
                msgBox.setWindowTitle(
                    //% "Remove Track Item?"
                    qtTrId("route.editor.route.editor.glwidget.title.remove.track.item"));
                msgBox.setStyleSheet("QLabel{min-width: 300px;}");
                msgBox.setText(
                    //% "Warning!"
                    qtTrId("route.editor.route.editor.glwidget.text.warning"));
                msgBox.setInformativeText(
                    //% "Do you want to remove this track item? It will damage your route if you don't know what you are doing!"
                    qtTrId("route.editor.route.editor.glwidget.message.do.you.want.remove.this.track.item"));
                msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                msgBox.setDefaultButton(QMessageBox::No);
                if (msgBox.exec() == QMessageBox::Yes) {
                    ctx.currentRoute()->deleteTrackItem(static_cast<TRitem *>(object));
                    object->unselect();
                }
            }
            if (object->typeObj == GameObj::activityobj) {
                object->remove();
                ctx.message("refreshActivityTools");
            }
            ctx.select(nullptr);
            ctx.setLastSelected(nullptr);
        }
        break;
    case Qt::Key_C:
        if (object != nullptr) {
            object->unselect();
            if (object->typeObj == GameObj::worldobj) {
                WorldObj *world = static_cast<WorldObj *>(object);
                ctx.select(ctx.currentRoute()->placeObject(world->x, world->y, world->position,
                                                    world->qDirection, 0, world->getRefInfo()));
                if (ctx.selected() != nullptr)
                    ctx.selected()->select();
            }
        }
        break;
    case Qt::Key_P:
        if (ctx.controlDown())
            ctx.pickPlacementFromSelected();
        else if (ctx.shiftDown())
            ctx.pickPlacementRotationAndElevation();
        else
            ctx.pickPlacementRotation();
        break;
    case Qt::Key_Z:
        Undo::StateBegin();
        Undo::PushTrackDB(Game::trackDB, false);
        Undo::PushTrackDB(Game::roadDB, true);
        ctx.currentRoute()->toggleToTDB(static_cast<WorldObj *>(object));
        Undo::StateEnd();
        if (object != nullptr)
            object->unselect();
        ctx.setLastSelected(object);
        ctx.select(nullptr);
        break;
    case Qt::Key_X:
        if (object == nullptr || object->typeObj != WorldObj::worldobj)
            return true;
        ctx.currentRoute()->flipObject(static_cast<WorldObj *>(object));
        if (ctx.placementElevation() != 0)
            object->rotate(ctx.placementElevation(), 0, 0);
        break;
    default:
        return false;
    }
    return true;
}

class SelectTool : public ObjectEditTool {
public:
    SelectTool() : ObjectEditTool("selectTool") {}

    bool press(ToolContext &ctx, const ToolMouse &) override {
        pressTime = QDateTime::currentMSecsSinceEpoch();
        const ToolContext::ObjectEdit edit = ctx.objectEdit();
        if (edit == ToolContext::ObjectEdit::Select)
            ctx.requestSelectionPass();
        GameObj *object = ctx.selected();
        if (object != nullptr && edit == ToolContext::ObjectEdit::Translate
                && object->typeObj == GameObj::worldobj) {
            Undo::PushGameObjData(object);
            float position[3];
            int tileX = ctx.tileX();
            int tileZ = ctx.tileZ();
            ctx.currentRoute()->getPointerPosition(position, tileX, tileZ, ctx.pointer());
            static_cast<WorldObj *>(object)->setPosition(tileX, tileZ, position);
            static_cast<WorldObj *>(object)->setMartix();
        }
        return true;
    }

    void drag(ToolContext &ctx, const ToolMouse &mouse) override {
        GameObj *object = ctx.selected();
        if (object == nullptr)
            return;
        const ToolContext::ObjectEdit edit = ctx.objectEdit();
        if (edit == ToolContext::ObjectEdit::Select
                && QDateTime::currentMSecsSinceEpoch() - pressTime > DragDelayMs) {
            Undo::PushGameObjData(object);
            if (ctx.shiftDown()) {
                object->rotate(0, mouse.dx() * ctx.keyMoveStep() * 0.1, 0);
            } else {
                if (object->typeObj == GameObj::worldobj)
                    ctx.currentRoute()->dragWorldObject(static_cast<WorldObj *>(object), ctx.tileX(),
                                                 ctx.tileZ(), ctx.pointer());
                if (object->typeObj == GameObj::activityobj)
                    object->setPosition(ctx.tileX(), ctx.tileZ(), ctx.pointer());
            }
        }
        if (edit == ToolContext::ObjectEdit::Translate) {
            Undo::PushGameObjData(object);
            object->setPosition(ctx.tileX(), ctx.tileZ(), ctx.pointer());
            object->setMartix();
        }
        if (edit == ToolContext::ObjectEdit::Rotate) {
            Undo::PushGameObjData(object);
            object->rotate(0, mouse.dx() * ctx.keyMoveStep() * 0.1, 0);
        }
    }

    void contextMenu(ToolContext &ctx, QMenu &menu) override {
        const ToolContext::ObjectEdit edit = ctx.objectEdit();
        QMenu *mode = menu.addMenu(
            //% "Mode"
            qtTrId("route.editor.route.editor.glwidget.group.mode"));
        struct Entry { QString text; ToolContext::ObjectEdit edit; };
        const Entry entries[] = {
            {//% "&Select"
             qtTrId("route.editor.route.editor.glwidget.action.select"),
             ToolContext::ObjectEdit::Select},
            {//% "&Rotate"
             qtTrId("route.editor.route.editor.glwidget.action.rotate"),
             ToolContext::ObjectEdit::Rotate},
            {//% "&Translate"
             qtTrId("route.editor.route.editor.glwidget.action.translate"),
             ToolContext::ObjectEdit::Translate},
            {//% "&Custom"
             qtTrId("route.editor.route.editor.glwidget.action.custom"),
             ToolContext::ObjectEdit::Resize}};
        for (const Entry &entry : entries) {
            QAction *action = GuiFunct::newMenuCheckAction(entry.text, mode, edit == entry.edit);
            const ToolContext::ObjectEdit chosen = entry.edit;
            QObject::connect(action, &QAction::triggered, [&ctx, chosen] { ctx.setObjectEdit(chosen); });
            mode->addAction(action);
        }
        ObjectEditTool::contextMenu(ctx, menu);
    }
};

class PlaceTool : public ObjectEditTool {
public:
    PlaceTool() : ObjectEditTool("placeTool") {}

    bool press(ToolContext &ctx, const ToolMouse &) override {
        pressTime = QDateTime::currentMSecsSinceEpoch();
        releasePrevious(ctx);
        Undo::StateBeginIfNotExist();
        ctx.rememberPlacement();
        float *rotation = Quat::create();
        Quat::copy(rotation, ctx.placementRotation());
        WorldObj *placed = ctx.currentRoute()->placeObject(ctx.tileX(), ctx.tileZ(), ctx.pointer(),
                                                    rotation, ctx.placementElevation());
        ctx.select(placed);
        if (placed != nullptr) {
            if (placed->typeID == WorldObj::telepole)
                ctx.startTelepole(static_cast<TelepoleObj *>(placed));
            else
                placed->select();
        }
        return true;
    }

    void drag(ToolContext &ctx, const ToolMouse &mouse) override {
        GameObj *object = ctx.selected();
        if (object == nullptr || QDateTime::currentMSecsSinceEpoch() - pressTime <= DragDelayMs)
            return;
        Undo::PushGameObjData(object);
        if (ctx.shiftDown())
            object->rotate(0, mouse.dx() * ctx.keyMoveStep() * 0.1, 0);
        else
            ctx.currentRoute()->dragWorldObject(static_cast<WorldObj *>(object), ctx.tileX(), ctx.tileZ(),
                                         ctx.pointer());
    }
};

class AutoPlaceSimpleTool : public EditorTool {
public:
    AutoPlaceSimpleTool() : EditorTool("autoPlaceSimpleTool") {}

    bool press(ToolContext &ctx, const ToolMouse &) override {
        releasePrevious(ctx);
        Undo::StateBeginIfNotExist();
        ctx.rememberPlacement();
        int mode = 0;
        if (ctx.controlDown())
            mode = 1;
        if (ctx.shiftDown())
            mode = 2;
        ctx.select(ctx.currentRoute()->autoPlaceObject(ctx.tileX(), ctx.tileZ(), ctx.pointer(), mode));
        if (ctx.selected() != nullptr)
            ctx.selected()->select();
        return true;
    }
};

class SignalLinkTool : public EditorTool {
public:
    SignalLinkTool() : EditorTool("signalLinkTool") {}

    bool press(ToolContext &ctx, const ToolMouse &) override {
        WorldObj *signal = static_cast<WorldObj *>(ctx.selected());
        Undo::PushGameObjData(signal);
        Undo::PushTrackDB(Game::trackDB);
        ctx.currentRoute()->linkSignal(ctx.tileX(), ctx.tileZ(), ctx.pointer(), signal);
        ctx.activateTool("");
        return true;
    }
};

// Sends pointer positions to the dynamic track properties.
class FlexPointTool : public EditorTool {
public:
    FlexPointTool() : EditorTool("FlexTool") {}

    bool press(ToolContext &ctx, const ToolMouse &) override {
        ctx.sendFlexData();
        return true;
    }
};

class ContinuousFlexTool : public EditorTool {
public:
    explicit ContinuousFlexTool(const QString &id) : EditorTool(id) {}

    bool press(ToolContext &ctx, const ToolMouse &) override {
        float rotation[4];
        Quat::copy(rotation, ctx.placementRotation());
        if (!ctx.placeContinuousFlex(rotation))
            Undo::StateCancel();
        return false;
    }
};

class ContinuousRulerTool : public EditorTool {
public:
    ContinuousRulerTool() : EditorTool("continuousRulerTool") {}

    bool press(ToolContext &ctx, const ToolMouse &) override {
        float rotation[4];
        Quat::copy(rotation, ctx.placementRotation());
        if (!ctx.placeContinuousRulerPoint(rotation))
            Undo::StateCancel();
        return false;
    }
};

}

namespace ObjectTools {

std::vector<std::unique_ptr<EditorTool>> create() {
    std::vector<std::unique_ptr<EditorTool>> tools;
    tools.push_back(std::make_unique<SelectTool>());
    tools.push_back(std::make_unique<PlaceTool>());
    tools.push_back(std::make_unique<AutoPlaceSimpleTool>());
    tools.push_back(std::make_unique<SignalLinkTool>());
    tools.push_back(std::make_unique<FlexPointTool>());
    tools.push_back(std::make_unique<ContinuousFlexTool>("continuousFlexTool"));
    tools.push_back(std::make_unique<ContinuousFlexTool>("continuousFlexRoadTool"));
    tools.push_back(std::make_unique<ContinuousRulerTool>());
    return tools;
}

}
