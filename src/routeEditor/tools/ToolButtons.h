/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TOOLBUTTONS_H
#define TOOLBUTTONS_H

#include <QMap>
#include <QString>
#include "EditorTool.h"

class QPushButton;
class ToolRegistry;

// The tool buttons of panels and property windows, and the view mode (task
// editor 04). A panel keeps working in every mode; only the buttons of
// tools the mode does not support are disabled. The panels get the mode
// from the view's "viewMode" message.
namespace ToolButtons {

// The editor's tools, set once by the window. Without them every tool
// counts as 3D only.
void setRegistry(const ToolRegistry *registry);
// The mode a "viewMode" message's value names ("map" or "3d").
ViewMode modeOf(const QString &value);
// Whether a tool works in a mode.
bool allowed(const QString &tool, ViewMode mode);
// Enables each button whose tool the mode allows: the tool of its key, or
// of its "tool" property when it starts a tool under another name.
void applyMode(const QMap<QString, QPushButton *> &buttons, ViewMode mode);
// A button's own condition (for example a selection it needs), kept
// together with the mode's: the button is enabled when both allow it.
void setAvailable(QPushButton *button, bool available);

}

#endif
