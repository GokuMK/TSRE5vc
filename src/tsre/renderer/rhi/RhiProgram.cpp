/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "RhiProgram.h"
#include "RhiShaderSource.h"
#include <QList>

void reflect(RhiProgram &program) {
    const QShader *stages[2] = {&program.source->vertex, &program.source->fragment};
    QHash<int, RhiProgram::Sampler> samplers;
    for (const QShader *stage : stages) {
        const QShaderDescription description = stage->description();
        for (const QShaderDescription::UniformBlock &block : description.uniformBlocks()) {
            if (block.binding == RhiShaderSource::TerrainPatchBlockBinding) {
                program.terrainPatches = true;
                continue;
            }
            if (block.binding != RhiShaderSource::UniformBlockBinding)
                continue;
            program.blockSize = std::max(program.blockSize, block.size);
            for (const QShaderDescription::BlockVariable &member : block.members) {
                RhiProgram::Member entry;
                entry.offset = member.offset;
                entry.size = member.size;
                entry.arrayStride = member.arrayStride;
                // Reflection leaves the stride of plain arrays at 0; std140
                // spreads the elements evenly over the size.
                int elements = 1;
                for (int dimension : member.arrayDims)
                    elements *= dimension;
                if (entry.arrayStride == 0 && !member.arrayDims.isEmpty() && elements > 0)
                    entry.arrayStride = member.size / elements;
                program.members.insert(member.name, entry);
            }
        }
        for (const QShaderDescription::InOutVariable &sampler : description.combinedImageSamplers()) {
            RhiProgram::Sampler entry;
            entry.binding = sampler.binding;
            switch (sampler.type) {
            case QShaderDescription::Sampler2DArray: entry.type = 1; break;
            case QShaderDescription::SamplerCube: entry.type = 2; break;
            case QShaderDescription::Sampler2D:
                entry.type = 0;
                break;
            default:
                entry.type = 0;
                break;
            }
            // A comparison sampler shows up as Sampler2D with the shadow flag
            // in its name only through the declared names; the shadow maps
            // use bindings 2, 3 and 9.
            if (sampler.name == "shadow0" || sampler.name == "shadow1" || sampler.name == "shadow2")
                entry.type = 3;
            samplers.insert(entry.binding, entry);
        }
    }
    QList<int> bindings = samplers.keys();
    std::sort(bindings.begin(), bindings.end());
    for (int binding : bindings)
        program.samplers.push_back(samplers.value(binding));
    program.inputs = program.source->vertex.description().inputVariables();
    program.block.assign(size_t(std::max(program.blockSize, 16)), 0);
}

