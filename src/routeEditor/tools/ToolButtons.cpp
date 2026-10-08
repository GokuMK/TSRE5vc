/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ToolButtons.h"
#include "ToolRegistry.h"
#include <QPushButton>
#include <QVariant>

namespace {

const ToolRegistry *tools = nullptr;
const char *const ModeAllowed = "toolModeAllowed";
const char *const Available = "toolAvailable";

void update(QPushButton *button) {
    button->setEnabled(button->property(ModeAllowed).toBool()
                       && button->property(Available).toBool());
}

void initialise(QPushButton *button) {
    if (!button->property(ModeAllowed).isValid())
        button->setProperty(ModeAllowed, true);
    if (!button->property(Available).isValid())
        button->setProperty(Available, button->isEnabled());
}

}

namespace ToolButtons {

void setRegistry(const ToolRegistry *registry) {
    tools = registry;
}

bool allowed(const QString &tool, ViewMode mode) {
    return tools != nullptr ? tools->allowed(tool, mode) : mode == ViewMode::Scene3D;
}

ViewMode modeOf(const QString &value) {
    return value == "map" ? ViewMode::Map : ViewMode::Scene3D;
}

void applyMode(const QMap<QString, QPushButton *> &buttons, ViewMode mode) {
    for (auto it = buttons.constBegin(); it != buttons.constEnd(); ++it) {
        QPushButton *button = it.value();
        if (button == nullptr)
            continue;
        initialise(button);
        // A button may start a tool under another name (its "tool" property).
        const QString tool = button->property("tool").toString();
        button->setProperty(ModeAllowed, allowed(tool.isEmpty() ? it.key() : tool, mode));
        update(button);
    }
}

void setAvailable(QPushButton *button, bool available) {
    if (button == nullptr)
        return;
    initialise(button);
    button->setProperty(Available, available);
    update(button);
}

}
