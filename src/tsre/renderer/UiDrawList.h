/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef UIDRAWLIST_H
#define UIDRAWLIST_H

#include <QImage>
#include <QtGlobal>
#include <unordered_map>
#include <vector>

// A frame of 2D user interface for the renderers (RmlUi's documents, task
// scripting 01), drawn over the finished view: after tone mapping, in pixels
// from the view's top left, colours with premultiplied alpha, in the order
// of `draws`. Geometry and textures stay between frames under ids unique in
// the program; a renderer keeps its own copies of them by id and frees them
// when they are released.
struct UiDrawList {
    // As RmlUi's vertex: 20 bytes.
    struct Vertex {
        float x, y;
        quint8 rgba[4];
        float u, v;
    };
    struct Geometry {
        std::vector<Vertex> vertices;
        std::vector<quint32> indices;
    };
    struct Draw {
        quint64 geometry = 0;
        // 0: untextured.
        quint64 texture = 0;
        float translation[2] = {0.0f, 0.0f};
        // Applied after the translation, column-major; identity when not
        // transformed.
        bool transformed = false;
        float transform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        // Pixels outside are not drawn; x, y from the top left.
        bool scissor = false;
        int scissorRect[4] = {0, 0, 0, 0};
    };

    int width = 0;
    int height = 0;
    std::unordered_map<quint64, Geometry> geometries;
    // QImage::Format_RGBA8888_Premultiplied.
    std::unordered_map<quint64, QImage> textures;
    // TexLib's textures (the documents' images: ACE, DDS, PNG...) by their
    // TexLib ids: straight alpha, premultiplied when drawn; draws of one
    // TexLib has not loaded yet are left out.
    std::unordered_map<quint64, int> libraryTextures;
    // Released since a renderer last drew the list.
    std::vector<quint64> releasedGeometries;
    std::vector<quint64> releasedTextures;
    std::vector<Draw> draws;
};

#endif // UIDRAWLIST_H
