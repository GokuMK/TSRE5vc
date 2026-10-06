/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RHIPROGRAM_H
#define RHIPROGRAM_H

#include "RhiContext.h"
#include <QHash>
#include <QVector>
#include <algorithm>
#include <cstring>
#include <vector>
#include <rhi/qshaderdescription.h>

// A program variant: its baked shaders and what their reflection says about
// the uniform block, the samplers and the vertex inputs.
struct RhiProgram {
    enum Kind {MAIN, TERRAIN, UNLIT, PBR, WATER, SELECTION, SHADOW, KIND_COUNT, OTHER};
    Kind kind = MAIN;
    const RhiContext::Program *source = nullptr;
    struct Member {
        int offset = 0;
        int size = 0;
        int arrayStride = 0;
    };
    QHash<QByteArray, Member> members;
    int blockSize = 0;
    std::vector<char> block;
    // 0 2D, 1 2D array, 2 cube, 3 2D shadow (depth compare).
    struct Sampler {
        int binding = 0;
        int type = 0;
    };
    std::vector<Sampler> samplers;
    bool terrainPatches = false;
    QVector<QShaderDescription::InOutVariable> inputs;
    // A resource set the pipelines are created against.
    QRhiShaderResourceBindings *layout = nullptr;

    bool valid() const { return source != nullptr && source->valid(); }
    void set(const char *name, const void *data, int bytes) {
        auto found = members.constFind(QByteArray::fromRawData(name, int(std::strlen(name))));
        if (found == members.constEnd())
            return;
        std::memcpy(block.data() + found->offset, data, size_t(std::min(bytes, found->size)));
    }
    void setFloat(const char *name, float value) { set(name, &value, 4); }
    void setInt(const char *name, int value) { set(name, &value, 4); }
    void setUint(const char *name, quint32 value) { set(name, &value, 4); }
    void setVec(const char *name, float x, float y, float z = 0.0f, float w = 0.0f) {
        const float v[4] = {x, y, z, w};
        set(name, v, 16);
    }
    void setMat4(const char *name, const float *matrix) { set(name, matrix, 64); }
    // Array of vec3 (std140: one vec4 per element).
    void setVec3Array(const char *name, const float *values, int count) {
        auto found = members.constFind(QByteArray::fromRawData(name, int(std::strlen(name))));
        if (found == members.constEnd() || found->arrayStride == 0)
            return;
        for (int i = 0; i < count && (i + 1) * found->arrayStride <= found->size; ++i)
            std::memcpy(block.data() + found->offset + i * found->arrayStride, values + i * 3, 12);
    }
};

// Fills a program's members, samplers and inputs from its shaders.
void reflect(RhiProgram &program);

#endif
