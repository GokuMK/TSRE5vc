/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */


#include <cmath>
#include <algorithm>

#include <tsre/renderer/OpenGL3Renderer.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderStats.h>
#include <tsre/math3d/GLMatrix.h>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLVertexArrayObject>
#include <tsre/ogl/GLUU.h>
#include <tsre/ogl/ScopedTerrainDecal.h>
#include <tsre/Game.h>
#ifndef __APPLE__
#include <GL/gl.h>
#else
#include <OpenGL/gl.h>
#endif

namespace {

struct DetailStateCache {
    QVector3D remap;
    float scale = -1.0f;
    unsigned int texture = 0;
};

void applyItemState(GLUU *gluu, QOpenGLFunctions *f, RenderItem *item,
                    quint32 selectionId, DetailStateCache &detail){
    if(item->normalsEnabled)
        gluu->enableNormals();
    else
        gluu->disableNormals();

    gluu->setBrightness(item->brightness);

    gluu->setSelectionId(selectionId);
    if (detail.remap != item->terrainTextureRemap) {
        gluu->currentShader->setUniformValue(gluu->currentShader->terrainTextureRemap, item->terrainTextureRemap);
        detail.remap=item->terrainTextureRemap;
    }
    const float detailScale = item->texturesEnabled && !selectionId
            ? item->secondTexScale : 0.0f;
    if (detailScale != 0.0f && detail.texture != item->secondTexAddr) {
        f->glActiveTexture(GL_TEXTURE1);
        f->glBindTexture(GL_TEXTURE_2D, item->secondTexAddr);
        f->glActiveTexture(GL_TEXTURE0);
        detail.texture = item->secondTexAddr;
    }
    if (detail.scale != detailScale) {
        gluu->currentShader->setUniformValue(gluu->currentShader->shaderSecondTexEnabled, detailScale);
        detail.scale = detailScale;
    }
    if(item->texturesEnabled){
        gluu->enableTextures();
        gluu->bindTexture(f, item->texAddr);
    } else {
        gluu->disableTextures(item->colorX, item->colorY, item->colorZ, item->colorA);
    }
}

unsigned int getItemDrawType(const RenderItem *item){
    if(item->itemType == RenderItem::Points)
        return GL_POINTS;
    if(item->itemType == 0)
        return GL_TRIANGLES;
    return item->itemType;
}

struct TerrainStateCache {
    bool valid = false;
    bool paged = false;
    QOpenGLBuffer *params = NULL;
    int verticesPerPatch = 0;
    int patchSide = 0;
    float sampleSpacing = 0.0f;
    bool applyGaps = false;
    bool mapPass = false;
};

void applyTerrainState(GLUU *gluu, RenderItem *item,
                       TerrainStateCache &cache){
    if(gluu == NULL || gluu->currentShader == NULL || item == NULL)
        return;
    Shader *shader = gluu->currentShader;
    if(!cache.valid || cache.paged != item->terrainPaged)
        shader->setUniformValue(shader->terrainPaged, item->terrainPaged ? 1 : 0);
    if(!item->terrainPaged){
        cache.valid = true;
        cache.paged = false;
        cache.params = NULL;
        return;
    }
    if(!cache.valid || !cache.paged
            || cache.verticesPerPatch != item->terrainVerticesPerPatch)
        shader->setUniformValue(shader->terrainVerticesPerPatch,
                                item->terrainVerticesPerPatch);
    if(!cache.valid || !cache.paged
            || cache.patchSide != item->terrainPatchSide)
        shader->setUniformValue(shader->terrainPatchSide,
                                item->terrainPatchSide);
    if(!cache.valid || !cache.paged
            || cache.sampleSpacing != item->terrainSampleSpacing)
        shader->setUniformValue(shader->terrainSampleSpacing,
                                item->terrainSampleSpacing);
    if(!cache.valid || !cache.paged || cache.applyGaps != item->terrainApplyGaps)
        shader->setUniformValue(shader->terrainApplyGaps,
                                item->terrainApplyGaps ? 1 : 0);
    if(!cache.valid || !cache.paged || cache.mapPass != item->terrainMapPass)
        shader->setUniformValue(shader->terrainMapPass,
                                item->terrainMapPass ? 1 : 0);
    if(!cache.valid || !cache.paged || cache.params != item->terrainParamsBuffer)
        QOpenGLContext::currentContext()->extraFunctions()->glBindBufferBase(
                    GL_UNIFORM_BUFFER, 0,
                    item->terrainParamsBuffer == NULL ? 0
                    : item->terrainParamsBuffer->bufferId());
    cache.valid = true;
    cache.paged = true;
    cache.params = item->terrainParamsBuffer;
    cache.verticesPerPatch = item->terrainVerticesPerPatch;
    cache.patchSide = item->terrainPatchSide;
    cache.sampleSpacing = item->terrainSampleSpacing;
    cache.applyGaps = item->terrainApplyGaps;
    cache.mapPass = item->terrainMapPass;
}

void setMatrixUniform(GLUU *gluu, int uniform, const float *matrix){
    gluu->currentShader->setUniformValue(
                uniform, *reinterpret_cast<const float(*)[4][4]>(matrix));
}

void drawItem(QOpenGLFunctions *f, RenderItem *item, quint32 selectionId,
              RenderStats::Category category){
    RenderStats::countDraw(category, getItemDrawType(item), item->vertCount);
    ScopedTerrainDecal decalState(f, item->terrainDecal && selectionId == 0);
    if(item->indexed){
        QOpenGLContext::currentContext()->extraFunctions()->glDrawElementsBaseVertex(
                    getItemDrawType(item), item->vertCount, item->indexType,
                    reinterpret_cast<void*>(static_cast<quintptr>(item->indexOffset)),
                    item->baseVertex);
    } else {
        f->glDrawArrays(getItemDrawType(item), item->vertOffset, item->vertCount);
    }
}

// Line width and polygon mode set for one packet and restored afterwards.
class PacketRasterState {
public:
    PacketRasterState(QOpenGLFunctions *f, const RenderItem *item)
        : f(f),
          lineWidth(item->lineWidth > 0 && item->lineWidth != Game::oglDefaultLineWidth),
          wireframe(item->polygonMode != 0) {
        if(lineWidth)
            f->glLineWidth(item->lineWidth);
        if(wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }
    ~PacketRasterState() {
        if(wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        if(lineWidth)
            f->glLineWidth(Game::oglDefaultLineWidth);
    }
private:
    QOpenGLFunctions *f;
    bool lineWidth;
    bool wireframe;
};

}

OpenGL3Renderer::OpenGL3Renderer() {
    objStrMatrix = new float[16];
    Mat4::identity(objStrMatrix);
}

OpenGL3Renderer::~OpenGL3Renderer() {
    clearQueues();
    delete[] objStrMatrix;
}

quint32 OpenGL3Renderer::captureMatrix(const float *matrix){
    const quint32 index = static_cast<quint32>(frameMatrices.size() / 16);
    frameMatrices.insert(frameMatrices.end(), matrix, matrix + 16);
    return index;
}

const float *OpenGL3Renderer::frameMatrix(quint32 index) const{
    return frameMatrices.data() + index * 16;
}

void OpenGL3Renderer::pushItem(RenderItem* r, float* mvmatrix){
    if(r == NULL)
        return;

    RenderItem *queuedItem = r;
    if(r->shared){
        queuedItem = new RenderItem(*r);
        queuedItem->shared = false;
    }
    ownedItems.push_back(queuedItem);

    DrawInstance instance;
    instance.packet = queuedItem;
    instance.matrix = captureMatrix(mvmatrix != NULL ? mvmatrix : mvMatrix);
    instance.selectionId = queuedItem->selectionId;
    instance.order = nextOrder++;
    instance.category = RenderStats::category();
    orderedItems.push_back(instance);

    if(RenderStats::inFrame()){
        RenderStats::current().queuedItems++;
        RenderStats::current().categories[instance.category].items++;
    }
}

void OpenGL3Renderer::queuePacket(RenderItem *packet, const float *matrix,
                                  quint32 selectionId){
    DrawInstance instance;
    instance.packet = packet;
    instance.matrix = captureMatrix(matrix);
    instance.selectionId = selectionId != 0 ? selectionId : packet->selectionId;
    instance.order = nextOrder++;
    instance.category = RenderStats::category();
    packets.push_back(instance);
    queuedPackets++;
    if(RenderStats::inFrame()){
        RenderStats::current().groupedInstances++;
        RenderStats::current().categories[instance.category].items++;
    }
}

void OpenGL3Renderer::pushPackets(const QVector<RenderItem*> &items, quint32 selectionId){
    for(RenderItem *packet : items){
        if(packet != NULL)
            queuePacket(packet, mvMatrix, selectionId);
    }
}

void OpenGL3Renderer::pushItemsVNTA(QVector<RenderItem*>& r, float* mvmatrix){
    for(RenderItem *packet : r){
        if(packet != NULL)
            queuePacket(packet, mvmatrix != NULL ? mvmatrix : mvMatrix, 0);
    }
}

void OpenGL3Renderer::pushItemVNTA(RenderItem* r, float* mvmatrix){
    if(r != NULL)
        queuePacket(r, mvmatrix != NULL ? mvmatrix : mvMatrix, 0);
}

// Orders packet instances by the first submission of their texture, then of
// their packet, then by submission. The order is stable across runs, unlike
// ordering by pointer or hash.
void OpenGL3Renderer::sortPackets(){
    if(!groupByTexture || packets.size() < 2)
        return;
    const size_t count = packets.size();
    packetOrder.resize(count);
    for(size_t i = 0; i < count; ++i)
        packetOrder[i] = static_cast<quint32>(i);

    std::sort(packetOrder.begin(), packetOrder.end(), [this](quint32 a, quint32 b){
        if(packets[a].packet != packets[b].packet)
            return packets[a].packet < packets[b].packet;
        return packets[a].order < packets[b].order;
    });
    for(size_t i = 0; i < count; ){
        const quint32 first = packets[packetOrder[i]].order;
        size_t j = i;
        for(; j < count && packets[packetOrder[j]].packet == packets[packetOrder[i]].packet; ++j)
            packets[packetOrder[j]].packetRank = first;
        i = j;
    }

    std::sort(packetOrder.begin(), packetOrder.end(), [this](quint32 a, quint32 b){
        if(packets[a].packet->texAddr != packets[b].packet->texAddr)
            return packets[a].packet->texAddr < packets[b].packet->texAddr;
        return packets[a].order < packets[b].order;
    });
    for(size_t i = 0; i < count; ){
        const quint32 first = packets[packetOrder[i]].order;
        const unsigned int texture = packets[packetOrder[i]].packet->texAddr;
        size_t j = i;
        for(; j < count && packets[packetOrder[j]].packet->texAddr == texture; ++j)
            packets[packetOrder[j]].textureRank = first;
        i = j;
    }

    std::sort(packets.begin(), packets.end(), [](const DrawInstance &a, const DrawInstance &b){
        if(a.textureRank != b.textureRank)
            return a.textureRank < b.textureRank;
        if(a.packetRank != b.packetRank)
            return a.packetRank < b.packetRank;
        return a.order < b.order;
    });
}

void OpenGL3Renderer::clearQueues(){
    for(RenderItem *item : ownedItems)
        delete item;
    ownedItems.clear();
    orderedItems.clear();
    queuedPackets -= static_cast<int>(packets.size());
    packets.clear();
    frameMatrices.clear();
    nextOrder = 0;
}

void OpenGL3Renderer::resetFrame(){
    clearQueues();
    Renderer::resetFrame();
}

void OpenGL3Renderer::renderFrame(){
    GLUU *gluu = GLUU::get();
    QOpenGLContext *context = QOpenGLContext::currentContext();
    f = context != NULL ? context->functions() : NULL;

    if(gluu == NULL || f == NULL || gluu->currentShader == NULL){
        clearQueues();
        Renderer::renderFrame();
        return;
    }
    TerrainStateCache terrainState;
    DetailStateCache detailState;
    f->glActiveTexture(GL_TEXTURE0);

    if(RenderStats::inFrame() && (!orderedItems.empty() || !packets.empty()))
        RenderStats::current().flushes++;

    // Frame-owned items keep their submission order.
    for(const DrawInstance &instance : orderedItems){
        RenderItem *item = instance.packet;
        if(item->VAO == NULL)
            continue;
        applyItemState(gluu, f, item, instance.selectionId, detailState);
        applyTerrainState(gluu, item, terrainState);
        if(item->msMatrix != NULL)
            setMatrixUniform(gluu, gluu->currentShader->msMatrixUniform, item->msMatrix);
        setMatrixUniform(gluu, gluu->currentShader->mvMatrixUniform, frameMatrix(instance.matrix));
        PacketRasterState raster(f, item);
        QOpenGLVertexArrayObject::Binder vaoBinder(item->VAO);
        drawItem(f, item, instance.selectionId,
                 static_cast<RenderStats::Category>(instance.category));
    }

    // Keep defaults predictable for the packet pass.
    gluu->setBrightness(1.0f);
    gluu->enableTextures();
    gluu->enableNormals();

    sortPackets();
    RenderItem *currentPacket = NULL;
    quint32 currentSelection = 0;
    unsigned int currentTexture = 0;
    bool textureGroupOpen = false;
    for(size_t i = 0; i < packets.size(); ){
        RenderItem *item = packets[i].packet;
        size_t end = i;
        while(end < packets.size() && packets[end].packet == item)
            ++end;
        if(item->VAO == NULL){
            i = end;
            continue;
        }
        if(!textureGroupOpen || item->texAddr != currentTexture){
            currentTexture = item->texAddr;
            textureGroupOpen = true;
            if(RenderStats::inFrame())
                RenderStats::current().textureGroups++;
        }
        if(RenderStats::inFrame())
            RenderStats::current().groupedPackets++;

        applyItemState(gluu, f, item, packets[i].selectionId, detailState);
        currentPacket = item;
        currentSelection = packets[i].selectionId;
        applyTerrainState(gluu, item, terrainState);
        if(item->msMatrix != NULL)
            setMatrixUniform(gluu, gluu->currentShader->msMatrixUniform, item->msMatrix);
        PacketRasterState raster(f, item);
        QOpenGLVertexArrayObject::Binder vaoBinder(item->VAO);
        for(; i < end; ++i){
            const DrawInstance &instance = packets[i];
            if(instance.selectionId != currentSelection){
                gluu->setSelectionId(instance.selectionId);
                currentSelection = instance.selectionId;
            }
            setMatrixUniform(gluu, gluu->currentShader->mvMatrixUniform,
                             frameMatrix(instance.matrix));
            drawItem(f, currentPacket, instance.selectionId,
                     static_cast<RenderStats::Category>(instance.category));
        }
    }

    gluu->currentShader->setUniformValue(gluu->currentShader->shaderSecondTexEnabled, 0.0f);
    gluu->currentShader->setUniformValue(gluu->currentShader->terrainTextureRemap, QVector3D());
    clearQueues();
    Renderer::renderFrame();

    gluu->setBrightness(1.0f);
    gluu->enableTextures();
    gluu->enableNormals();
    gluu->currentShader->setUniformValue(gluu->currentShader->terrainPaged, 0);
    context->extraFunctions()->glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0);
}
