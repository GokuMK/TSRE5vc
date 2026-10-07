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

// The tool panels' buttons and the view mode (task editor 04). A panel
// keeps working in every mode; only the buttons of tools the mode does not
// support are disabled. The panels get the mode from the view's "viewMode"
// message.
namespace ToolButtons {

// The mode a "viewMode" message's value names ("map" or "3d").
ViewMode modeOf(const QString &value);
// Enables each button whose tool the mode allows. Without a registry
// every tool counts as 3D only.
void applyMode(const QMap<QString, QPushButton *> &buttons, const ToolRegistry *registry,
               ViewMode mode);
// A button's own condition (for example a selection it needs), kept
// together with the mode's: the button is enabled when both allow it.
void setAvailable(QPushButton *button, bool available);

}

#endif
