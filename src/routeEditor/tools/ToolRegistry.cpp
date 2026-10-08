/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ToolRegistry.h"
#include "ObjectTools.h"
#include "TerrainEditTools.h"
#include "RouteDataTools.h"

ToolRegistry::ToolRegistry() {
    for (std::unique_ptr<EditorTool> &tool : ObjectTools::create())
        add(std::move(tool));
    for (std::unique_ptr<EditorTool> &tool : TerrainEditTools::create())
        add(std::move(tool));
    for (std::unique_ptr<EditorTool> &tool : RouteDataTools::create())
        add(std::move(tool));
}

void ToolRegistry::add(std::unique_ptr<EditorTool> tool) {
    byId.insert(tool->id(), tool.get());
    tools.push_back(std::move(tool));
}

EditorTool *ToolRegistry::find(const QString &id) const {
    return byId.value(id, nullptr);
}

bool ToolRegistry::allowed(const QString &id, ViewMode mode) const {
    if (id.isEmpty())
        return true;
    const EditorTool *tool = find(id);
    return tool != nullptr ? tool->supports(mode) : mode == ViewMode::Scene3D;
}

QStringList ToolRegistry::ids() const {
    QStringList list;
    for (const std::unique_ptr<EditorTool> &tool : tools)
        list.append(tool->id());
    return list;
}
