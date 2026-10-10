/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "EditorTool.h"
#include <QAction>
#include <QMenu>

void EditorTool::contextMenu(ToolContext &ctx, QMenu &menu) {
    QString section;
    bool first = true;
    for (const ToolAction &action : actions(ctx)) {
        if (first || action.section != section) {
            if (!action.section.isEmpty())
                menu.addSection(action.section);
            section = action.section;
            first = false;
        }
        QAction *item = menu.addAction(action.text);
        item->setEnabled(action.enabled);
        item->setCheckable(action.checkable);
        item->setChecked(action.checked);
        const QString id = action.id;
        QObject::connect(item, &QAction::triggered, [this, &ctx, id] { run(ctx, id); });
    }
}
