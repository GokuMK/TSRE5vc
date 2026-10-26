/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef UIPROJECTION_H
#define UIPROJECTION_H

// Matrices of the UI drawers (UiDrawList), column-major.
namespace UiProjection {

// Pixels from the top left of a width x height view to OpenGL's clip space.
inline void pixels(float *out, int width, int height) {
    for (int i = 0; i < 16; ++i)
        out[i] = 0.0f;
    out[0] = 2.0f / float(width);
    out[5] = -2.0f / float(height);
    out[10] = -1.0f;
    out[12] = -1.0f;
    out[13] = 1.0f;
    out[15] = 1.0f;
}

// a * b; b null is identity.
inline void multiply(float *out, const float *a, const float *b) {
    if (b == nullptr) {
        for (int i = 0; i < 16; ++i)
            out[i] = a[i];
        return;
    }
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += a[k * 4 + row] * b[column * 4 + k];
            out[column * 4 + row] = sum;
        }
}

}

#endif // UIPROJECTION_H
