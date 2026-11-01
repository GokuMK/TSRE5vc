/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef UIDISPLAY_H
#define UIDISPLAY_H

#include <QString>
#include <memory>

class Renderer;
struct UiDrawList;

namespace Rml {
class Context;
}

namespace Ui {

// A document drawn into a texture, for a shape to show (task scripting 02):
// an RmlUi context of its own, the texture's size in pixels, drawn by a
// renderer into a texture its items can use
// (RenderItem::Material::textureObject). GUI thread only, as all of RmlUi.
class Display {
public:
    // Starts RmlUi (TSRE's UI folder) when it is not yet.
    Display(int width, int height);
    // The texture stays: release() it with its renderer first.
    ~Display();
    Display(const Display &) = delete;
    Display &operator=(const Display &) = delete;

    // Null when RmlUi did not start. Its data models are made here before
    // the document loads.
    Rml::Context *context() const;
    // The document from a file, shown; false when it does not load.
    bool load(const QString &path);
    // The document's scripts and bindings brought up to date.
    void update();
    // The document rendered into its draw list (what draw() hands the
    // renderer).
    UiDrawList &render();
    // Rendered, then drawn into its texture by the renderer, made on the
    // first draw: the texture, 0 when the renderer cannot.
    unsigned int draw(Renderer &renderer);
    unsigned int texture() const;
    // The texture freed by the renderer that made it.
    void release(Renderer &renderer);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}

#endif // UIDISPLAY_H
