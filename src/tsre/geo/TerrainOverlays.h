/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TERRAINOVERLAYS_H
#define TERRAINOVERLAYS_H

#include <QImage>
#include <QString>
#include <functional>

// Terrain tile overlays (F3 Terrain Tile Overlay): one image a terrain tile, made
// from OSM data or imagery, shown over the tile (Terrain::showBlob) and the source
// of its map texture (Make from Overlay). Images are opaque; how much of the terrain
// shows through is the opacity, applied when drawing.
//
// Tiles are keyed by their low corner (Terrain::getLowCornerTileXY): x * 10000 + z.
// The tile's overlay texture is the TexLib texture "<key>.:maptex" (MapLib).
namespace TerrainOverlays {

int key(int x, int z);
// The tile's image, or null.
const QImage *image(int key);
bool has(int x, int z);
// Stores the tile's image, opaque (RGB888), and reloads its overlay texture.
void set(int x, int z, const QImage &image);
// ROUTES/<route>/TERRAIN_MAPS/<key>.png
QString diskPath(int x, int z);
bool loadFromDisk(int x, int z);
bool saveToDisk(int x, int z, QString &error);

// Draws the tile's OSM data at core.maps.imageResolution, in the map palette's
// styles (light or dark, as the map's OSM layer), and stores it: from the OSM
// directory (offering to convert its downloads), else from the OSM web API.
// done(ok, error) runs when the image is stored or the data failed: at once for
// local data, after the requests for the web. False (with error) when another
// web request is still running.
bool createFromOsm(int x, int z, int tileSize, std::function<void(bool ok, const QString &error)> done,
                   QString &error);
bool osmBusy();

// How opaque overlays are drawn, 0 to 1 (F3 Opacity; for the session). 1: the
// overlay replaces the terrain's own textures; below: the terrain is drawn and the
// overlay over it.
// TODO(renderer): the overlay's alpha is not multiplied by this yet. Terrain draws
// the overlay above the terrain below 1 (as translucent overlays were drawn), but
// the shaders draw it opaque; see docs/tasks/editor/04-map-mode.md, "Terrain tile
// overlay opacity".
float opacity();
void setOpacity(float value);

}

#endif
