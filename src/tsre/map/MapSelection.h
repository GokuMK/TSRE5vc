/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MAPSELECTION_H
#define MAPSELECTION_H

#include <QtGlobal>
#include <map>
#include <memory>
#include <vector>

class OglObj;
class RenderQueue;

// The map's selection pass (task editor 04): shapes drawn with the 3D
// view's selection IDs (SelectionIdCodec), so a pick on the map is handled
// as one in 3D. The layers add triangles by ID; the shapes live until the
// next pass, so their buffers outlast the frame that draws them.
class MapSelection {
public:
    // Hit areas are this much larger than what is drawn, around markers.
    static constexpr float MarginPixels = 3.0f;

    MapSelection();
    ~MapSelection();
    // Triangles (x, y, z a vertex) to draw with an ID.
    std::vector<float> &shape(quint32 id) { return shapes[id]; }
    // Builds the shapes and pushes them with their IDs.
    void pushRenderItems(RenderQueue &queue);
    // Drops the shapes for the next pass.
    void clear();
    int count() const { return int(shapes.size()); }

private:
    std::map<quint32, std::vector<float>> shapes;
    std::vector<std::unique_ptr<OglObj>> objects;
};

#endif
