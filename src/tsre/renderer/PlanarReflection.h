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

#include <memory>
#include <vector>

class Renderer;

// The scene mirrored in a horizontal water plane, drawn into a mipmapped
// texture that water samples at its screen position. The texture lives in
// the renderer's backend (Storage).
class PlanarReflection {
public:
    // Texture unit the water program reads the reflection from (terrain
    // material units elsewhere).
    static const int TextureUnit = 6;

    // The mipmapped colour target in one backend.
    class Storage {
    public:
        virtual ~Storage() = default;
        virtual bool ready(int width, int height) const = 0;
        virtual bool create(int width, int height) = 0;
        // Draws from now on go to the target, cleared to clearColor.
        virtual void begin(const float *clearColor) = 0;
        // Ends drawing and builds the mipmaps; the renderer's target is
        // undefined afterwards.
        virtual void end() = 0;
        // Lets the water program sample it, or stops it.
        virtual void bind() = 0;
        virtual void unbind() = 0;
        virtual void release() = 0;
    };
    // The OpenGL storage (GL thread only).
    static Storage *createOpenGlStorage();

    // Without a renderer the target is kept in OpenGL.
    explicit PlanarReflection(Renderer *renderer = nullptr);
    PlanarReflection(const PlanarReflection &) = delete;
    PlanarReflection &operator=(const PlanarReflection &) = delete;
    ~PlanarReflection();

    // Creates or resizes the target.
    bool ensure(int width, int height);
    // Draws from now on go to the target, cleared to clearColor.
    void begin(const float *clearColor);
    // Builds the mipmaps; bind the renderer's target again afterwards.
    void end();
    void bind();
    void unbind();
    int width() const { return targetWidth; }
    int height() const { return targetHeight; }
    int levels() const;
    // Plane (unit normal pointing up, and offset: n . p + d = 0) fitted to
    // the bounding sphere centres of water patches (x, y, z, radius each),
    // nearer patches weighing more. Only patches within LocalRadius of the
    // eye count, unless none is that near. A sloping river gets a tilted
    // plane; the tilt is limited to 10%. False without patches.
    static constexpr double LocalRadius = 400.0;
    static bool fitPlane(const std::vector<float> &spheres, const float *eye, float *plane);
    // Mirror about a plane, column-major.
    static void mirrorMatrix(const float *plane, float *out);

private:
    Renderer *renderer = nullptr;
    std::unique_ptr<Storage> storage;
    int targetWidth = 0;
    int targetHeight = 0;
};

#endif
