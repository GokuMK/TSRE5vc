/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "WaterNormalMap.h"
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <algorithm>
#include <cmath>
#include <random>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

std::vector<unsigned char> WaterNormalMap::generate(int size) {
    // Waves with whole cycles across the map, so it tiles. Wave numbers are
    // spread evenly in log scale over 3-48 cycles; slope amplitude falls
    // with the wave number, as on wind-driven water.
    const int waveCount = 64;
    const double twoPi = 6.283185307179586;
    std::mt19937 random(20261005u);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::vector<double> slopeX(size_t(size) * size, 0.0), slopeZ(size_t(size) * size, 0.0);
    std::vector<double> cosX(size), sinX(size), cosZ(size), sinZ(size);
    for (int w = 0; w < waveCount; ++w) {
        const double radius = 3.0 * std::pow(16.0, (w + unit(random)) / waveCount);
        const double angle = twoPi * unit(random);
        const double phase = twoPi * unit(random);
        const int a = int(std::lround(radius * std::cos(angle)));
        const int b = int(std::lround(radius * std::sin(angle)));
        if (a == 0 && b == 0)
            continue;
        const double length = std::sqrt(double(a * a + b * b));
        const double amplitude = std::pow(length, -0.8);
        // cos(ax + bz + phase) from per-row and per-column tables.
        for (int i = 0; i < size; ++i) {
            cosX[i] = std::cos(twoPi * a * i / size);
            sinX[i] = std::sin(twoPi * a * i / size);
            cosZ[i] = std::cos(twoPi * b * i / size + phase);
            sinZ[i] = std::sin(twoPi * b * i / size + phase);
        }
        const double sx = amplitude * a / length, sz = amplitude * b / length;
        for (int z = 0; z < size; ++z)
            for (int x = 0; x < size; ++x) {
                const double c = cosX[x] * cosZ[z] - sinX[x] * sinZ[z];
                slopeX[size_t(z) * size + x] += sx * c;
                slopeZ[size_t(z) * size + x] += sz * c;
            }
    }
    double largest = 1e-9;
    for (size_t i = 0; i < slopeX.size(); ++i)
        largest = std::max(largest, std::max(std::abs(slopeX[i]), std::abs(slopeZ[i])));
    std::vector<unsigned char> texels(slopeX.size() * 4);
    auto byte = [](double v) {
        return static_cast<unsigned char>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
    };
    for (size_t i = 0; i < slopeX.size(); ++i) {
        const double x = slopeX[i] / largest, z = slopeZ[i] / largest;
        texels[i * 4 + 0] = byte(0.5 + 0.5 * x);
        texels[i * 4 + 1] = byte(0.5 + 0.5 * z);
        texels[i * 4 + 2] = byte(x * x);
        texels[i * 4 + 3] = byte(z * z);
    }
    return texels;
}

WaterNormalMap::~WaterNormalMap() {
    QOpenGLContext *current = QOpenGLContext::currentContext();
    if (texture != 0 && current != nullptr && current == context)
        current->functions()->glDeleteTextures(1, &texture);
}

bool WaterNormalMap::bind(QOpenGLFunctions *f, int unit) {
    QOpenGLContext *current = QOpenGLContext::currentContext();
    if (current == nullptr)
        return false;
    if (current != context) {
        // A texture of another context cannot be used or deleted here.
        texture = 0;
        context = current;
    }
    f->glActiveTexture(GL_TEXTURE0 + unit);
    if (texture == 0) {
        static const std::vector<unsigned char> texels = generate(Size);
        f->glGenTextures(1, &texture);
        f->glBindTexture(GL_TEXTURE_2D, texture);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Size, Size, 0, GL_RGBA,
                        GL_UNSIGNED_BYTE, texels.data());
        f->glGenerateMipmap(GL_TEXTURE_2D);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // Grazing views keep the waves sharper with anisotropic filtering.
        if (current->hasExtension("GL_EXT_texture_filter_anisotropic")
                || current->hasExtension("GL_ARB_texture_filter_anisotropic"))
            f->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
    } else {
        f->glBindTexture(GL_TEXTURE_2D, texture);
    }
    f->glActiveTexture(GL_TEXTURE0);
    return true;
}
