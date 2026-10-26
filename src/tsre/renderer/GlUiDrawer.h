/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef GLUIDRAWER_H
#define GLUIDRAWER_H

#include <QPointer>
#include <QtGlobal>
#include <functional>
#include <unordered_map>

class QOpenGLContext;
class QOpenGLShaderProgram;
struct UiDrawList;

// The OpenGL renderer's 2D user interface (task scripting 01): a UiDrawList
// drawn into the bound framebuffer with one small program, premultiplied
// alpha, no depth, a scissor rectangle per draw. Geometry and textures are
// kept by their ids until the list releases them. The GL state it changes
// is put back.
class GlUiDrawer {
public:
    // The GL texture of a TexLib texture, false while it is not loaded (the
    // renderer's own lookup).
    using LibraryTexture = std::function<bool(int textureId, unsigned int &texture)>;

    ~GlUiDrawer();
    void draw(UiDrawList &list, const LibraryTexture &library);
    // Frees the GL objects; with the context they were made in current.
    void release();

private:
    struct Geometry {
        unsigned int array = 0;
        unsigned int vertices = 0;
        unsigned int indices = 0;
        int count = 0;
    };
    QPointer<QOpenGLContext> context;
    QOpenGLShaderProgram *program = nullptr;
    int transformUniform = -1;
    int translationUniform = -1;
    int premultiplyUniform = -1;
    unsigned int white = 0;
    std::unordered_map<quint64, Geometry> geometries;
    std::unordered_map<quint64, unsigned int> textures;

    bool ensureProgram();
    const Geometry *geometryFor(UiDrawList &list, quint64 id);
    unsigned int textureFor(UiDrawList &list, quint64 id);
    void freeReleased(UiDrawList &list);
};

#endif // GLUIDRAWER_H
