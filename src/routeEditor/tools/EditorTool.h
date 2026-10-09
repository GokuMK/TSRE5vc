/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef EDITORTOOL_H
#define EDITORTOOL_H

#include <QFlags>
#include <QPointF>
#include <QString>

class QKeyEvent;
class QMenu;
class ToolContext;

// How the Route Editor view shows the route (task editor 04): the 3D scene,
// or the top-down map.
enum class ViewMode : unsigned char {
    Scene3D = 1,
    Map = 2
};
Q_DECLARE_FLAGS(ViewModes, ViewMode)
Q_DECLARE_OPERATORS_FOR_FLAGS(ViewModes)

// A mouse event as tools see it, in device pixels.
struct ToolMouse {
    QPointF position;
    // The position of the previous mouse event.
    QPointF last;
    bool moved() const { return position != last; }
    float dx() const { return float(position.x() - last.x()); }
};

// A Route Editor tool: what the left mouse button, the wheel and the keys do
// while it is active. The view calls the active tool; a tool reaches the
// editor only through the ToolContext. Tools are identified by the names
// the tool panels send (enableTool).
class EditorTool {
public:
    explicit EditorTool(QString id, ViewModes modes = ViewMode::Scene3D)
        : toolId(std::move(id)), toolModes(modes) {}
    virtual ~EditorTool() = default;

    const QString &id() const { return toolId; }
    ViewModes modes() const { return toolModes; }
    bool supports(ViewMode mode) const { return toolModes.testFlag(mode); }
    // Whether a left drag edits with the tool (painting) instead of moving
    // the map, in map mode; the right button then moves the map.
    virtual bool editsByDragging() const { return false; }

    virtual void activate(ToolContext &) {}
    virtual void deactivate(ToolContext &) {}
    // Left button pressed. An undo state is open; the view ends it when the
    // button is released. False ends the press here: no drag follows.
    virtual bool press(ToolContext &, const ToolMouse &) { return true; }
    // The mouse moved with the left button down after a press the tool
    // received.
    virtual void drag(ToolContext &, const ToolMouse &) {}
    // Wheel turned by steps (a notch is 1.2); true when the tool used it.
    virtual bool wheel(ToolContext &, float) { return false; }
    // A key pressed; true when the tool used it.
    virtual bool key(ToolContext &, QKeyEvent *) { return false; }
    // Adds the tool's section to the view's context menu.
    virtual void contextMenu(ToolContext &, QMenu &) {}

private:
    QString toolId;
    ViewModes toolModes;
};

#endif
