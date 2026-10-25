/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef UIRENDERINTERFACE_H
#define UIRENDERINTERFACE_H

#include <RmlUi/Core/RenderInterface.h>
#include <tsre/renderer/UiDrawList.h>

namespace Ui {

// RmlUi's drawing for TSRE's renderers (task scripting 01): what a context
// renders goes into a UiDrawList, which the view hands to its renderer
// (Renderer::drawUi). Images load through Qt (PNG, JPEG and the other
// formats it reads). Clip masks, layers, filters and shaders are not
// supported yet: RmlUi draws without them (no rounded or rotated clipping,
// blur, drop shadows or gradients).
class RenderInterface : public Rml::RenderInterface {
public:
    // A new frame of the given size in pixels: the last frame's draws gone.
    void beginFrame(int width, int height);
    UiDrawList &list() { return drawList; }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;
    void SetTransform(const Rml::Matrix4f *transform) override;

private:
    UiDrawList drawList;
    bool scissor = false;
    int scissorRect[4] = {0, 0, 0, 0};
    bool transformed = false;
    float transform[16];
};

}

#endif // UIRENDERINTERFACE_H
