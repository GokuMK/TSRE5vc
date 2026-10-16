/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/map/FogMapLayer.h>

#include <tsre/environment/FogFloor.h>
#include <tsre/map/MapPalette.h>
#include <tsre/map/MapView.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>

#include <QImage>
#include <QOpenGLFunctions>
#include <algorithm>
#include <vector>

FogMapLayer::FogMapLayer() = default;

FogMapLayer::~FogMapLayer() = default;

void FogMapLayer::pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette,
                                  const FogFloor &floor, const WeatherFog::State &fog) {
    if (!floor.valid())
        return;
    constexpr int N = FogFloor::Size;
    const QColor wanted = palette.background.lightnessF() > 0.5 ? QColor(120, 160, 200) : QColor(225, 235, 248);
    if (wanted != colour) {
        colour = wanted;
        dirty = true;
    }
    if (dirty) {
        dirty = false;
        // Where the ground lies under the fog's top: faint for a few metres
        // of fog, most opaque from 30 m.
        QImage pixels(N, N, QImage::Format_RGBA8888);
        any = false;
        for (int row = 0; row < N; ++row) {
            uchar *line = pixels.scanLine(row);
            for (int column = 0; column < N; ++column) {
                const FogFloor::Sample s = floor.cell(column, row);
                float alpha = 0.0f;
                if (s.known && fog.amount > 0.0f) {
                    const float thickness = FogFloor::top(s, fog.depth, fog.pooling) - std::max(s.ground, s.floor + fog.base);
                    alpha = fog.amount * std::clamp(thickness / 30.0f, 0.0f, 1.0f) * 0.8f;
                }
                const int a = int(alpha * 255.0f + 0.5f);
                any = any || a > 0;
                uchar *p = line + column * 4;
                p[0] = uchar(colour.red());
                p[1] = uchar(colour.green());
                p[2] = uchar(colour.blue());
                p[3] = uchar(a);
            }
        }
        if (!texture) {
            texture = new Texture(N, N, 32);
            texture->pathid = QStringLiteral("tsre-map-fog-%1.:atlas").arg(quintptr(this), 0, 16);
            textureId = TexLib::addTex(texture);
            texture->GLTextures();
        }
        texture->updateRegion(0, 0, N, N, pixels.constBits());
    }
    if (!any)
        return;
    if (!image)
        image = std::make_unique<OglObj>();
    // The map's corners relative to the view's tile (terrain z runs south).
    const float x0 = float(floor.west() - view.tileX * 2048.0);
    const float z0 = float(-floor.north() - view.tileZ * 2048.0);
    const float x1 = x0 + float(N * FogFloor::CellSize), z1 = z0 + float(N * FogFloor::CellSize);
    const float corners[4][4] = {{x0, z0, 0.0f, 0.0f}, {x1, z0, 1.0f, 0.0f}, {x1, z1, 1.0f, 1.0f},
                                 {x0, z1, 0.0f, 1.0f}};
    std::vector<float> vertices;
    for (int c : {0, 3, 2, 0, 2, 1})
        vertices.insert(vertices.end(), {corners[c][0], Height, corners[c][1], corners[c][2], corners[c][3], 0.0f});
    image->setMaterialTextureId(textureId);
    image->setOpacity(0.99f);
    image->init(vertices.data(), int(vertices.size()), RenderItem::VT, GL_TRIANGLES);
    image->pushRenderItem(queue);
}
