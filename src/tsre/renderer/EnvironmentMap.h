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

#include <QByteArray>
#include <QVector>
#include <memory>

class Renderer;

// A cube map of the surroundings of one point, for reflections. A route view
// renders its faces from the camera position, a few faces per frame; the
// Shape Viewer fills it with a procedural warehouse interior. Faces follow the
// OpenGL cube map order and orientation: +X, -X, +Y, -Y, +Z, -Z.
// The cube itself lives in the renderer's backend (Storage); this class
// schedules the faces and makes the warehouse.
class EnvironmentMap {
public:
    static constexpr int FaceCount = 6;
    // Texture unit the scene shaders sample it from.
    static constexpr int TextureUnit = 10;

    // The cube and its prefiltered copy in one backend.
    class Storage {
    public:
        virtual ~Storage() = default;
        // Whether the cubes exist at this face size.
        virtual bool ready(int faceSize) const = 0;
        // Creates the cube (with mipmaps) and the prefiltered cube of levels
        // levels at faceSize texels.
        virtual bool create(int faceSize, int levels) = 0;
        // Draws from now on go to the face, cleared to clearColor.
        virtual void beginFace(int face, const float *clearColor) = 0;
        // Ends face drawing: rebuilds the cube's mipmaps, then convolves the
        // given faces into the prefiltered cube.
        virtual void endFaces(const QVector<int> &prefilterFaces) = 0;
        // Replaces every face with RGBA rows, first row at the face's top.
        virtual void uploadFaces(const QByteArray *faces) = 0;
        // Lets the scene shaders sample the prefiltered cube, or the raw cube
        // until the prefiltered one is made; unbind stops them.
        virtual void bind(bool prefiltered) = 0;
        virtual void unbind() = 0;
        // Draws the raw faces unfolded as a cross (4 x 3 cells) with its
        // lower-left corner at x, y of the view.
        virtual void drawPreview(int x, int y, int cellSize) = 0;
        virtual void release() = 0;
        // OpenGL texture names, for tests; 0 elsewhere.
        virtual unsigned int texture() const { return 0; }
        virtual unsigned int prefilteredTexture() const { return 0; }
    };
    // The OpenGL storage (GL thread only; its objects belong to the context
    // current when it creates them).
    static Storage *createOpenGlStorage();
    // The prefilter and preview shaders, in the shaders330 dialect, sharing
    // one full-screen vertex shader (a four-vertex triangle strip).
    static const char *fullScreenVertexShader();
    static const char *prefilterFragmentShader();
    static const char *previewFragmentShader();
    // One warehouse face as RGBA rows, first row at the top of the face in
    // the cube's frame.
    static QByteArray warehouseFace(int face, int size);

    // Without a renderer the cube is kept in OpenGL.
    explicit EnvironmentMap(Renderer *renderer = nullptr);
    ~EnvironmentMap();
    EnvironmentMap(const EnvironmentMap &) = delete;
    EnvironmentMap &operator=(const EnvironmentMap &) = delete;

    // Creates the cube at a face size, or recreates it at another one.
    // False when the backend cannot.
    bool ensure(int faceSize);
    int faceSize() const { return size; }
    unsigned int texture() const { return storage ? storage->texture() : 0; }
    unsigned int prefilteredTexture() const { return storage ? storage->prefilteredTexture() : 0; }
    // Levels of the prefiltered cube the scene shaders sample: level i holds
    // reflections for roughness i / (levels - 1). 0 without a cube.
    int levels() const;
    static int levelsFor(int faceSize);
    // Whether every face has content (rendered or filled) since ensure().
    bool complete() const { return facesReady == FaceCount; }

    // View matrix of a face seen from eye, and the square 90-degree
    // projection all faces use.
    static void faceView(int face, const float *eye, float *view);
    static void faceProjection(float nearPlane, float farPlane, float *projection);

    // The next count faces in round-robin order.
    QVector<int> nextFaces(int count);
    // Draws from now on go to the face, cleared to clearColor.
    void beginFace(int face, const float *clearColor);
    // Ends face rendering and rebuilds the mipmaps and the prefiltered cube.
    // The renderer's target is undefined afterwards: bind one.
    void endFaces();

    // Fills every face with a procedural warehouse interior: grey brick
    // walls with high windows, a concrete floor and a dark ceiling with lamps.
    bool fillWarehouse(int faceSize);

    // Lets the scene shaders sample the cube on TextureUnit.
    void bind();
    // Stops them, so rendering into the cube cannot sample it.
    void unbind();
    // Draws the rendered faces unfolded as a cross (4 x 3 cells of cellSize
    // pixels) with its lower-left corner at x, y of the view.
    void drawPreview(int x, int y, int cellSize);

    // Releases the cube; the OpenGL storage needs its context current.
    void release();

private:
    Renderer *renderer = nullptr;
    std::unique_ptr<Storage> storage;
    // Faces drawn since the last endFaces(), prefiltered there.
    QVector<int> pendingFaces;
    // Whether every face has been prefiltered at least once.
    bool prefilteredOnce = false;
    int size = 0;
    int nextFace = 0;
    int facesReady = 0;
    bool faceUpdated[FaceCount] = {};
    void resetFaces();
};

#endif // ENVIRONMENTMAP_H
