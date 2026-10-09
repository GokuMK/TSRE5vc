/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MAPVIEW_H
#define MAPVIEW_H

// A point on the map's ground: a tile in the editor's (camera's) convention and
// metres in it.
struct MapGroundPoint {
    int tileX = 0;
    int tileZ = 0;
    float x = 0.0f;
    float z = 0.0f;
};

// The Route Editor's map mode view (task editor 04): a top-down orthographic
// view of a centre on the ground, a scale and a heading. Coordinates are
// the editor's: tiles of 2048 m with the camera's tile convention (pozT),
// positions in a tile from -1024 to 1024 metres, x east, z south.
class MapView {
public:
    // Heading of the screen's up direction, as a camera heading: up points
    // along (sin h, cos h) in x and z. Pi is north up.
    static constexpr float NorthUp = 3.14159265f;
    // The eye's height and the planes of the projection: everything between
    // -10 km and +10 km is drawn.
    static constexpr float EyeHeight = 10000.0f;
    static constexpr float NearPlane = 1.0f;
    static constexpr float FarPlane = 20000.0f;
    static constexpr float MinMetresPerPixel = 0.02f;
    static constexpr float MaxMetresPerPixel = 500.0f;

    int tileX = 0;
    int tileZ = 0;
    // The centre in its tile.
    float x = 0.0f;
    float z = 0.0f;
    float metresPerPixel = 2.0f;
    float heading = NorthUp;
    // The viewport in pixels.
    int width = 1;
    int height = 1;

    // Screen directions on the ground: right and up, unit vectors in x and
    // z.
    void right(float &rx, float &rz) const;
    void up(float &ux, float &uz) const;
    // The ground under a screen point (pixels, y down), in metres from the
    // centre's tile origin.
    void groundAt(float px, float py, float &gx, float &gz) const;
    // The screen point of a ground position given relative to the centre's
    // tile origin.
    void screenAt(float gx, float gz, float &px, float &py) const;
    // Moves the view so the ground follows the mouse by a drag in pixels.
    void pan(float dxPixels, float dyPixels);
    // Scales by factor (more than 1 zooms out), keeping the ground under the
    // screen point in place.
    void zoomAt(float px, float py, float factor);
    // Turns the view by radians about the screen centre.
    void rotate(float radians);
    // Keeps the centre inside its tile, moving to the next tile as the
    // camera does.
    void normalize();
    // The tiles the view shows, with margin tiles more around, in the
    // camera's tile convention.
    void visibleTiles(int &minX, int &maxX, int &minZ, int &maxZ, int margin = 0) const;
    // View and orthographic projection matrices (column-major).
    void viewMatrix(float *out) const;
    void projection(float *out) const;
};

#endif
