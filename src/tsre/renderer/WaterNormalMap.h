/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef WATERNORMALMAP_H
#define WATERNORMALMAP_H

#include <vector>

class QOpenGLContext;
class QOpenGLFunctions;

// Tileable wave slopes for shaded water: a sum of waves in random
// directions, generated once and kept as a mipmapped texture in the context
// that created it.
class WaterNormalMap {
public:
    static const int Size = 256;
    // RGBA8 texels: the x and z slopes (0.5 is flat), scaled so the largest
    // is 1, and their squares, so mipmaps keep the slope variance.
    static std::vector<unsigned char> generate(int size);

    WaterNormalMap() = default;
    WaterNormalMap(const WaterNormalMap &) = delete;
    WaterNormalMap &operator=(const WaterNormalMap &) = delete;
    // Deletes the texture when its context is current.
    ~WaterNormalMap();
    // Binds the map on the given unit, creating it in the current context
    // first; leaves unit 0 active.
    bool bind(QOpenGLFunctions *f, int unit);

private:
    unsigned int texture = 0;
    QOpenGLContext *context = nullptr;
};

#endif
