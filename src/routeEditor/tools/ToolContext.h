/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TOOLCONTEXT_H
#define TOOLCONTEXT_H

#include <QString>
#include "EditorTool.h"

class Brush;
class GameObj;
class Route;
class TelepoleObj;
class QWidget;

// What the Route Editor view offers its tools. Tools read and change the
// editor through it, not through the view or Game, so tool code builds
// without the view's header.
class ToolContext {
public:
    virtual ~ToolContext() = default;

    // How the select tool works on the selected object (keys R, T, Y and
    // the context menu).
    enum class ObjectEdit { Select, Rotate, Translate, Resize };

    // The view: parent of dialogs and receiver of the menu actions that
    // call its slots.
    virtual QWidget *view() = 0;
    virtual ViewMode viewMode() const = 0;
    virtual Route *currentRoute() const = 0;
    // The camera tile; pointer() is relative to it.
    virtual int tileX() const = 0;
    virtual int tileZ() const = 0;
    // The 3D pointer, as the route's functions take it.
    virtual float *pointer() = 0;
    // The camera's heading (radians), for moving objects with the keys.
    virtual float cameraHeading() const = 0;
    virtual Brush *brush() = 0;

    virtual GameObj *selected() const = 0;
    virtual void select(GameObj *object) = 0;
    // The object a later undo or a key such as Z returns to.
    virtual void setLastSelected(GameObj *object) = 0;
    // Picks the object under the mouse with the next frame.
    virtual void requestSelectionPass() = 0;
    virtual ObjectEdit objectEdit() const = 0;
    virtual void setObjectEdit(ObjectEdit edit) = 0;
    // Whether the pointer stays on the terrain or also lands on objects.
    virtual bool pointerSticksToTerrain() const = 0;
    virtual void setPointerSticksToTerrain(bool terrainOnly) = 0;

    virtual bool shiftDown() const = 0;
    virtual bool controlDown() const = 0;
    // Step of key and wheel moves (Control and Alt change it).
    virtual float keyMoveStep() const = 0;
    virtual void resetKeyMoveStep() = 0;

    // Placement: the rotation and elevation new objects get, whether
    // placed track objects join the track database, and where the last
    // object was placed.
    virtual float *placementRotation() = 0;
    virtual float placementElevation() const = 0;
    virtual bool autoAddToTrackDb() const = 0;
    virtual void resetPlacementRotation() = 0;
    virtual void rememberPlacement() = 0;

    // Commands of the view the tools share with its menus.
    virtual void terrainToSelected() = 0;
    virtual void selectedPositionToTerrain() = 0;
    virtual void selectedRotationToTerrain() = 0;
    virtual void pickPlacementFromSelected() = 0;
    virtual void pickPlacementRotation() = 0;
    virtual void pickPlacementRotationAndElevation() = 0;
    virtual bool placeContinuousFlex(float *rotation) = 0;
    virtual bool placeContinuousRulerPoint(const float *rotation) = 0;
    virtual void startTelepole(TelepoleObj *telepole) = 0;

    virtual void activateTool(const QString &id) = 0;
    // Messages to the panels (RouteEditorGLWidget::sendMsg).
    virtual void message(const QString &name) = 0;
    virtual void message(const QString &name, const QString &value) = 0;
    // Dynamic track points for the flex properties (flexData).
    virtual void sendFlexData() = 0;
};

#endif
