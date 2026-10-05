/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef PLANARREFLECTION_H
#define PLANARREFLECTION_H

#include <vector>

class QOpenGLContext;

// The scene mirrored in a horizontal water plane, drawn into a mipmapped
// texture that water samples at its screen position. Owned by the widget
// whose context created it.
class PlanarReflection {
public:
    // Texture unit the water program reads the reflection from (terrain
    // material units elsewhere).
    static const int TextureUnit = 6;

    PlanarReflection() = default;
    PlanarReflection(const PlanarReflection &) = delete;
    PlanarReflection &operator=(const PlanarReflection &) = delete;
    ~PlanarReflection();

    // Creates or resizes the target in the current context.
    bool ensure(int width, int height);
    // Binds the target, sets the viewport and clears it.
    void begin(const float *clearColor);
    // Builds the mipmaps and binds restoreFramebuffer again.
    void end(unsigned int restoreFramebuffer);
    void bind() const;
    static void unbind();
    int width() const { return targetWidth; }
    int height() const { return targetHeight; }
    int levels() const;
    // Plane (unit normal pointing up, and offset: n . p + d = 0) fitted to
    // the bounding sphere centres of water patches (x, y, z, radius each),
    // nearer patches weighing more. A sloping river gets a tilted plane;
    // the tilt is limited to 10%. False without patches.
    static bool fitPlane(const std::vector<float> &spheres, const float *eye, float *plane);
    // Mirror about a plane, column-major.
    static void mirrorMatrix(const float *plane, float *out);

private:
    void release();
    QOpenGLContext *context = nullptr;
    unsigned int texture = 0;
    unsigned int depth = 0;
    unsigned int framebuffer = 0;
    int targetWidth = 0;
    int targetHeight = 0;
};

#endif
