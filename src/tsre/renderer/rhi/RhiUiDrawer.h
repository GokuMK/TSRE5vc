/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RHIUIDRAWER_H
#define RHIUIDRAWER_H

#include <QSize>
#include <QtGlobal>
#include <unordered_map>
#include <vector>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
struct UiDrawList;

// The QRhi renderer's 2D user interface (task scripting 01): a UiDrawList
// drawn into the frame after the view is composed (tone mapped), with one
// small pipeline, premultiplied alpha and a scissor rectangle per draw.
// Geometry and textures are kept by their ids until the list releases them.
class RhiUiDrawer {
public:
    explicit RhiUiDrawer(QRhi *rhi);
    ~RhiUiDrawer();
    // Before the frame's pass: the list's new geometry and textures and the
    // draws' uniforms recorded into `batch`, the pipeline for `pass`.
    void prepare(UiDrawList &list, QRhiResourceUpdateBatch *batch, QRhiRenderPassDescriptor *pass,
                 const QSize &frameSize);
    // Inside the pass: the draws prepared.
    void record(QRhiCommandBuffer *cb);

private:
    struct Geometry {
        QRhiBuffer *vertices = nullptr;
        QRhiBuffer *indices = nullptr;
        quint32 count = 0;
    };
    struct Command {
        const Geometry *geometry = nullptr;
        QRhiShaderResourceBindings *bindings = nullptr;
        quint32 uniformOffset = 0;
        bool scissor = false;
        int scissorRect[4] = {0, 0, 0, 0};
    };

    QRhi *rhi;
    QRhiGraphicsPipeline *pipeline = nullptr;
    QRhiRenderPassDescriptor *pipelinePass = nullptr;
    QRhiShaderResourceBindings *layout = nullptr;
    QRhiBuffer *uniforms = nullptr;
    quint32 uniformStride = 0;
    quint32 uniformCapacity = 0;
    QRhiSampler *sampler = nullptr;
    QRhiTexture *white = nullptr;
    std::unordered_map<quint64, Geometry> geometries;
    std::unordered_map<quint64, QRhiTexture *> textures;
    // One resource set for each texture drawn with (0: white).
    std::unordered_map<quint64, QRhiShaderResourceBindings *> bindings;
    std::vector<Command> commands;
    QSize size;
    // TSRE_UI_LOG: CPU time of prepare() and record(), every 5 s in the log.
    qint64 timed = 0;
    qint64 timedMost = 0;
    qint64 prepared = 0;
    int timedFrames = 0;
    qint64 timedSince = 0;

    bool ensurePipeline(QRhiRenderPassDescriptor *pass, QRhiResourceUpdateBatch *batch);
    void ensureUniforms(quint32 draws);
    const Geometry *geometryFor(UiDrawList &list, quint64 id, QRhiResourceUpdateBatch *batch);
    QRhiShaderResourceBindings *bindingsFor(UiDrawList &list, quint64 texture, QRhiResourceUpdateBatch *batch);
    void freeReleased(UiDrawList &list);
};

#endif // RHIUIDRAWER_H
