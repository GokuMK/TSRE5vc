/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef UISHAPEDISPLAYS_H
#define UISHAPEDISPLAYS_H

#include <QMatrix4x4>
#include <QSize>
#include <QString>
#include <memory>
#include <vector>

#include <tsre/shape/ShapeDisplays.h>

class ComplexShape;
class RenderQueue;
class RenderItem;

namespace Ui {

class DisplayDocument;

// The displays of a shape's state (task scripting 02): its .sd's blocks
// (ComplexShape::displays), each showing a document's texture through the
// state's texture override (a texture replaced: the whole texture is the
// document) or on a square of its own hung on one of the shape's parts. The
// documents are drawn by their owner: shared by placed objects
// (RouteDisplays), or the Shape Viewer's own. GUI thread only.
class ShapeDisplays {
public:
    // The shape's displays (loaded) and a document for each, in its order
    // (null for none).
    ShapeDisplays(const ComplexShape &shape, std::vector<std::shared_ptr<DisplayDocument>> documents);
    // Its squares retired; the state's overrides stay (release()).
    ~ShapeDisplays();
    ShapeDisplays(const ShapeDisplays &) = delete;
    ShapeDisplays &operator=(const ShapeDisplays &) = delete;

    // A document of its own for each display (the Shape Viewer's).
    static std::vector<std::shared_ptr<DisplayDocument>> ownDocuments(const ComplexShape &shape);
    // A display's document's file (the shape's documents folder, a
    // subfolder of it too; empty without one or outside it) and size: its
    // resolution, else the replaced texture's, else 512 x 256.
    static QString documentPath(const ComplexShape &shape, const ::ShapeDisplays::Display &display);
    static QSize documentSize(const ComplexShape &shape, const ::ShapeDisplays::Display &display);
    // The part a square hangs on, by name (any case); -1 the root.
    static int squarePart(const ComplexShape &shape, const ::ShapeDisplays::Display &display);
    // A loaded shape's displays' problems to its author's messages, once a
    // shape (displayMessage): its blocks left out, a document not found or
    // outside its folder, an Image the shape has not, a ShapeHierarchy it
    // has not (the square on its root).
    static void report(const ComplexShape &shape);
    // A square's transform in the shape's space: its part as the state
    // poses it, then its place, turn (about x, then y, then z) and size. It
    // faces +z (its readers' side).
    static QMatrix4x4 squareMatrix(const ComplexShape &shape, unsigned int stateId, int part,
                                   const ::ShapeDisplays::Display &display);

    const std::vector<::ShapeDisplays::Display> &definitions() const { return blocks; }
    const std::vector<std::shared_ptr<DisplayDocument>> &documents() const { return docs; }

    // Each drawn document's texture given to the state (a texture
    // replaced), again when it changes (made again after it was freed,
    // RouteDisplays::frame; 0 the shape's own): before the shape's own
    // submission, which takes the state's textures as they are then.
    void overrideTextures(ComplexShape &shape, unsigned int stateId);
    // After the shape's own submission (the same transform): the textures
    // given to the state (overrideTextures) and its squares submitted on
    // their parts as the state poses them; the documents marked seen in
    // `frame`, `distance` metres from the camera.
    void push(RenderQueue &queue, ComplexShape &shape, unsigned int stateId, quint32 selectionId,
              unsigned int frame, float distance);
    // The state's overrides cleared and the squares retired.
    void release(ComplexShape &shape, unsigned int stateId);

private:
    std::vector<::ShapeDisplays::Display> blocks;
    std::vector<std::shared_ptr<DisplayDocument>> docs;
    // Per display: the texture given to the state, a square's item (its
    // producer's) and the part it hangs on (-1 the root; -2 not found yet).
    struct Shown {
        unsigned int texture = 0;
        RenderItem *square = nullptr;
        int part = -2;
    };
    std::vector<Shown> shown;
};

}

#endif // UISHAPEDISPLAYS_H
