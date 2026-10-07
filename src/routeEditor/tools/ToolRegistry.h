/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TOOLREGISTRY_H
#define TOOLREGISTRY_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>
#include "EditorTool.h"

// The Route Editor's tools by name. A name without a tool (panels send a few
// that need no mouse handling, such as proceduralPickTool) stays valid and
// does nothing, as before tools had classes.
class ToolRegistry {
public:
    // With every tool of the editor.
    ToolRegistry();
    EditorTool *find(const QString &id) const;
    // Whether a tool name may be active in a view mode: no tool, or a tool
    // that supports the mode. Names without a tool count as 3D only.
    bool allowed(const QString &id, ViewMode mode) const;
    QStringList ids() const;

private:
    void add(std::unique_ptr<EditorTool> tool);
    std::vector<std::unique_ptr<EditorTool>> tools;
    QHash<QString, EditorTool *> byId;
};

#endif
