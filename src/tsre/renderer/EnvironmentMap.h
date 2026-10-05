/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ENVIRONMENTMAP_H
#define ENVIRONMENTMAP_H

#include <QPointer>
#include <QVector>

class QOpenGLContext;
class QOpenGLShaderProgram;

// A cube map of the surroundings of one point, for reflections. A route view
// renders its faces from the camera position, a few faces per frame; the
// Shape Viewer fills it with a procedural warehouse interior. Faces follow the
// OpenGL cube map order and orientation: +X, -X, +Y, -Y, +Z, -Z.
// GL thread only; the objects belong to the context current at ensure().
class EnvironmentMap {
public:
    static constexpr int FaceCount = 6;
    // Texture unit the scene shaders sample it from.
    static constexpr int TextureUnit = 10;

    EnvironmentMap();
    ~EnvironmentMap();
    EnvironmentMap(const EnvironmentMap &) = delete;
    EnvironmentMap &operator=(const EnvironmentMap &) = delete;

    // Creates the cube, its framebuffer and depth buffer, or recreates them
    // at another face size. False without a context or on a framebuffer error.
    bool ensure(int faceSize);
    int faceSize() const { return size; }
    unsigned int texture() const { return cube; }
    unsigned int prefilteredTexture() const { return prefiltered; }
    // Levels of the prefiltered cube the scene shaders sample: level i holds
    // reflections for roughness i / (levels - 1). 0 without a cube.
    int levels() const;
    // Whether every face has content (rendered or filled) since ensure().
    bool complete() const { return facesReady == FaceCount; }

    // View matrix of a face seen from eye, and the square 90-degree
    // projection all faces use.
    static void faceView(int face, const float *eye, float *view);
    static void faceProjection(float nearPlane, float farPlane, float *projection);

    // The next count faces in round-robin order.
    QVector<int> nextFaces(int count);
    // Binds the face's framebuffer and viewport and clears it to clearColor.
    void beginFace(int face, const float *clearColor);
    // Ends face rendering: rebuilds the mipmaps and binds restoreFramebuffer.
    void endFaces(unsigned int restoreFramebuffer);

    // Fills every face with a procedural warehouse interior: grey brick
    // walls with high windows, a concrete floor and a dark ceiling with lamps.
    bool fillWarehouse(int faceSize);

    // Binds the prefiltered cube to TextureUnit and leaves unit 0 active.
    void bind();
    // Unbinds TextureUnit, so rendering into the cube cannot sample it.
    static void unbind();
    // Draws the rendered faces unfolded as a cross (4 x 3 cells of cellSize
    // pixels) with its lower-left corner at x, y of the current framebuffer.
    void drawPreview(int x, int y, int cellSize);

    // Deletes the GL objects; needs their context current.
    void release();

private:
    QPointer<QOpenGLContext> context;
    unsigned int cube = 0;
    unsigned int framebuffer = 0;
    unsigned int depth = 0;
    unsigned int previewArray = 0;
    QOpenGLShaderProgram *preview = nullptr;
    // The cube convolved with the GGX lobe of each level's roughness.
    unsigned int prefiltered = 0;
    QOpenGLShaderProgram *prefilterProgram = nullptr;
    // Faces drawn since the last endFaces(), prefiltered there.
    QVector<int> pendingFaces;
    // Whether every face has been prefiltered at least once.
    bool prefilteredOnce = false;
    // Prefilters the given faces at every level; the raw cube must have its
    // mipmaps. Leaves the framebuffer unbound.
    void prefilter(const QVector<int> &faces);
    bool ensurePrograms();
    int size = 0;
    int nextFace = 0;
    int facesReady = 0;
    bool faceUpdated[FaceCount] = {};
};

#endif // ENVIRONMENTMAP_H
