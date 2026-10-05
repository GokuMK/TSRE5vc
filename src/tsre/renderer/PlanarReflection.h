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
    // Mirror about the plane y = height, column-major.
    static void mirrorMatrix(float height, float *out);

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
