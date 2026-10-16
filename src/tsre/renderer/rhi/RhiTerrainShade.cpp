/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// The terrain's shade of a low sun or moon past the shadow maps
// (TerrainShade): its map of tops as a texture, and where it lies in the
// view, for the lit shaders, the volumetric light shafts and the fog.

#include "RhiRenderer.h"
#include "RhiProgram.h"
#include <QFloat16>
#include <tsre/environment/TerrainShade.h>

void RhiRenderer::setTerrainShade(const TerrainShade &shade) {
    terrainShade = shade;
}

void RhiRenderer::releaseTerrainShade() {
    delete shadeResources.texture;
    shadeResources = ShadeResources();
}

bool RhiRenderer::terrainShadeReady() const {
    return shadeResources.filled && terrainShade.map != nullptr && terrainShade.size > 0;
}

void RhiRenderer::prepareTerrainShade(QRhiResourceUpdateBatch *updates) {
    ShadeResources &s = shadeResources;
    const TerrainShade &t = terrainShade;
    if (t.map == nullptr || t.size <= 0 || t.map->size() != size_t(t.size) * t.size || s.revision == t.revision)
        return;
    const QSize size(t.size, t.size);
    if (s.texture == nullptr || s.texture->pixelSize() != size) {
        delete s.texture;
        // Half floats from the reference: a few decimetres near the camera's
        // ground, a metre or two on mountains 2 km above it.
        const auto format = rhi->isTextureFormatSupported(QRhiTexture::R16F) ? QRhiTexture::R16F
                                                                               : QRhiTexture::R32F;
        s.texture = rhi->newTexture(format, size);
        if (!s.texture->create()) {
            delete s.texture;
            s = ShadeResources();
            return;
        }
    }
    QByteArray bytes;
    if (s.texture->format() == QRhiTexture::R16F) {
        bytes.resize(int(t.map->size() * sizeof(qfloat16)));
        qfloat16 *out = reinterpret_cast<qfloat16 *>(bytes.data());
        for (size_t i = 0; i < t.map->size(); ++i)
            out[i] = qfloat16(std::clamp((*t.map)[i] - t.reference, -60000.0f, 60000.0f));
    } else {
        bytes.resize(int(t.map->size() * sizeof(float)));
        float *out = reinterpret_cast<float *>(bytes.data());
        for (size_t i = 0; i < t.map->size(); ++i)
            out[i] = (*t.map)[i] - t.reference;
    }
    updates->uploadTexture(s.texture, QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(bytes)));
    s.revision = t.revision;
    s.reference = t.reference;
    s.filled = true;
}

void RhiRenderer::terrainShadePlace(float place[4], float params[4]) const {
    const TerrainShade &t = terrainShade;
    const bool on = terrainShadeReady();
    place[0] = viewPosition[0] + float(t.west - t.cameraEast);
    place[1] = viewPosition[2] - float(t.north - t.cameraNorth);
    place[2] = std::max(float(t.size) * t.cell, 1.0f);
    place[3] = viewPosition[1] - sky.cameraHeight;
    params[0] = on ? 1.0f : 0.0f;
    params[1] = shadeResources.reference;
    params[2] = 1.0f / ::TerrainShade::Band;
    params[3] = 0.0f;
}
