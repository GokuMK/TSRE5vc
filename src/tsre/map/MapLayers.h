/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MAPLAYERS_H
#define MAPLAYERS_H

// What the map mode draws (the Map menu). Separate from the 3D view's
// toggles, which live in Game.
enum class MapLayer { Track, Road, Junctions, Ends, Pointer, Count };

struct MapLayers {
    bool visible[int(MapLayer::Count)] = {true, true, true, true, true};
    bool shows(MapLayer layer) const { return visible[int(layer)]; }
    void set(MapLayer layer, bool show) { visible[int(layer)] = show; }
};

#endif
