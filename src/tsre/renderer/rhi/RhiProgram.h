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
#include <unordered_map>
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
    // Members by name address (see member()); offset -1: no such member.
    std::unordered_map<const char *, Member> byAddress;
    int blockSize = 0;
    std::vector<char> block;
    // 0 2D, 1 2D array, 2 cube, 3 2D shadow (depth compare).
    struct Sampler {
        int binding = 0;
        int type = 0;
    };
    std::vector<Sampler> samplers;
    QVector<QShaderDescription::InOutVariable> inputs;
    // A resource set the pipelines are created against.
    QRhiShaderResourceBindings *layout = nullptr;

    bool valid() const { return source != nullptr && source->valid(); }
    // The setters take names that are string literals: a member is found by
    // the name's address after the first lookup by text (about 15 lookups a
    // draw; hashing the names cost about 4 % of the frame's CPU time).
    const Member *member(const char *name) {
        auto cached = byAddress.find(name);
        if (cached == byAddress.end()) {
            Member found;
            found.offset = -1;
            auto byName = members.constFind(QByteArray::fromRawData(name, int(std::strlen(name))));
            if (byName != members.constEnd())
                found = *byName;
            cached = byAddress.emplace(name, found).first;
        }
        return cached->second.offset >= 0 ? &cached->second : nullptr;
    }
    void set(const char *name, const void *data, int bytes) {
        const Member *found = member(name);
        if (found == nullptr)
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
        const Member *found = member(name);
        if (found == nullptr || found->arrayStride == 0)
            return;
        for (int i = 0; i < count && (i + 1) * found->arrayStride <= found->size; ++i)
            std::memcpy(block.data() + found->offset + i * found->arrayStride, values + i * 3, 12);
    }
};

// Fills a program's members, samplers and inputs from its shaders.
void reflect(RhiProgram &program);

// Bakes a Vulkan-style GLSL 440 shader held in the code (full-screen passes)
// for the QRhi's backend; invalid, with a warning, when it does not compile.
QShader bakeInline(const char *source, QShader::Stage stage, QRhi *rhi);
// A full-screen triangle (three vertices, no inputs) passing texture
// coordinates `uv` at location 0, whose rows match the target's texture
// rows on every backend.
QByteArray fullScreenVertex(QRhi *rhi);

#endif
