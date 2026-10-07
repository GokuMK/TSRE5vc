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

ViewMode modeOf(const QString &value) {
    return value == "map" ? ViewMode::Map : ViewMode::Scene3D;
}

void applyMode(const QMap<QString, QPushButton *> &buttons, const ToolRegistry *registry,
               ViewMode mode) {
    for (auto it = buttons.constBegin(); it != buttons.constEnd(); ++it) {
        QPushButton *button = it.value();
        if (button == nullptr)
            continue;
        initialise(button);
        const bool allowed = registry != nullptr ? registry->allowed(it.key(), mode)
                                                 : mode == ViewMode::Scene3D;
        button->setProperty(ModeAllowed, allowed);
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
