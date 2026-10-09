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
#include <vector>

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

// One thing a tool can do where the pointer is (a tool with one subject and
// several actions, such as a terrain tile's overlay). The context menu lists a
// tool's actions; panel buttons choose the one a left click runs, by naming the
// tool as "tool:action" (enableTool).
struct ToolAction {
    QString id;
    QString text;
    // The menu section it is listed under; actions of one section stay together.
    QString section;
    bool enabled = true;
    bool checkable = false;
    bool checked = false;
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

    // The tool of a name that may carry an action ("tool:action"), and the action.
    static QString idOf(const QString &name) { return name.section(QLatin1Char(':'), 0, 0); }
    static QString actionOf(const QString &name) { return name.section(QLatin1Char(':'), 1); }

    const QString &id() const { return toolId; }
    // The heading of its context menu section; empty uses the id.
    virtual QString title() const { return QString(); }
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
    // Adds the tool's section to the view's context menu: by default its
    // actions, under their sections, each running its action.
    virtual void contextMenu(ToolContext &ctx, QMenu &menu);

    // The actions at the pointer, with their state (asked when the menu opens);
    // none for a tool with one action.
    virtual std::vector<ToolAction> actions(ToolContext &) { return {}; }
    // Runs an action at the pointer.
    virtual void run(ToolContext &, const QString &) {}
    // The action a left click runs, set from the "tool:action" name a panel
    // button enables.
    const QString &defaultAction() const { return clickAction; }
    void setDefaultAction(const QString &action) { clickAction = action; }

private:
    QString toolId;
    QString clickAction;
    ViewModes toolModes;
};

#endif
