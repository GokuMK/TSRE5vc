/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "MapSelection.h"
#include <QOpenGLFunctions>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderItem.h>

MapSelection::MapSelection() = default;

MapSelection::~MapSelection() = default;

void MapSelection::pushRenderItems(RenderQueue &queue) {
    objects.clear();
    for (auto &entry : shapes) {
        if (entry.second.empty() || entry.first == 0)
            continue;
        auto object = std::make_unique<OglObj>();
        // An object without a material draws nothing; selection ignores
        // the colour.
        object->setMaterial(1.0f, 1.0f, 1.0f);
        object->init(entry.second.data(), int(entry.second.size()), RenderItem::V, GL_TRIANGLES);
        object->pushRenderItem(queue, entry.first);
        objects.push_back(std::move(object));
    }
}

void MapSelection::clear() {
    shapes.clear();
}
