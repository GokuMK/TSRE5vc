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
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>
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

// Resolves a packet's texture handle when drawing. Returns false while the
// texture is not uploaded, failed to load or is disabled by the user.
bool resolveTexture(const RenderItem *item, unsigned int &address){
    if(item->textureId < 0){
        address = item->texAddr;
        return true;
    }
    const auto found = TexLib::mtex.find(item->textureId);
    if(found == TexLib::mtex.end() || found->second == NULL)
        return false;
    Texture *texture = found->second;
    if(!texture->glLoaded && texture->loaded)
        texture->GLTextures();
    if(!texture->glLoaded || texture->tex == NULL)
        return false;
    address = texture->tex[0];
    return TexLib::disabledTextures.value(static_cast<int>(address), 0) != 1;
}

// Grouping key; texture handles and raw addresses do not collide.
quint64 textureKey(const RenderItem *item){
    if(item->textureId >= 0)
        return (quint64(1) << 32) | quint32(item->textureId);
    return item->texAddr;
}

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
    unsigned int address = 0;
    if(item->texturesEnabled && resolveTexture(item, address)){
        gluu->enableTextures();
        gluu->bindTexture(f, address);
    } else if(item->texturesEnabled){
        gluu->disableTextures(1.0f, 0.0f, 1.0f, 1.0f);
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

// A null model-space matrix means identity.
void setModelMatrix(GLUU *gluu, const float *matrix){
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    setMatrixUniform(gluu, gluu->currentShader->msMatrixUniform,
                     matrix != NULL ? matrix : identity);
}

void drawItem(QOpenGLFunctions *f, RenderItem *item, quint32 selectionId,
              RenderStats::Category category, int pass){
    RenderStats::countDraw(category, getItemDrawType(item), item->vertCount);
    RenderStats::countPassDraw(pass);
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

Renderer::RenderPass OpenGL3Renderer::routePass(const RenderItem *packet,
                                                SubmitOrder order) const{
    switch(currentLayer){
    case LAYER_SKY: return PASS_SKY;
    case LAYER_DISTANT: return PASS_DISTANT;
    case LAYER_OVERLAY: return PASS_OVERLAY;
    case LAYER_WATER: return PASS_WATER;
    case LAYER_UI: return PASS_UI;
    case LAYER_SCENE: break;
    }
    if(packet->surface == RenderItem::SURFACE_TERRAIN)
        return PASS_TERRAIN;
    if(order == SUBMIT_ORDERED)
        return PASS_OPAQUE;
    if(packet->surface == RenderItem::SURFACE_ALPHA_TEST)
        return PASS_ALPHA_TEST;
    if(packet->surface == RenderItem::SURFACE_BLENDED)
        return PASS_BLENDED;
    return PASS_OPAQUE;
}

void OpenGL3Renderer::queueInstance(RenderItem *packet, const float *matrix,
                                    quint32 selectionId, SubmitOrder order, bool owned){
    const RenderPass pass = routePass(packet, order);
    DrawInstance instance;
    instance.packet = packet;
    instance.matrix = captureMatrix(matrix);
    instance.selectionId = selectionId != 0 ? selectionId : packet->selectionId;
    instance.order = nextOrder++;
    instance.category = RenderStats::category();
    instance.owned = owned;
    if(pass == PASS_BLENDED && order == SUBMIT_GROUPED){
        // Distance from the camera to the packet origin, for back-to-front order.
        const float *ms = packet->msMatrix;
        const float local[3] = {ms ? ms[12] : 0.0f, ms ? ms[13] : 0.0f, ms ? ms[14] : 0.0f};
        float delta[3];
        for(int i = 0; i < 3; ++i)
            delta[i] = matrix[i] * local[0] + matrix[4 + i] * local[1]
                    + matrix[8 + i] * local[2] + matrix[12 + i] - viewPosition[i];
        instance.distance = delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
    }
    if(order == SUBMIT_ORDERED)
        passes[pass].ordered.push_back(instance);
    else
        passes[pass].grouped.push_back(instance);
    if(!owned)
        queuedPackets++;
    if(RenderStats::inFrame()){
        if(owned)
            RenderStats::current().queuedItems++;
        else
            RenderStats::current().groupedInstances++;
        RenderStats::current().categories[instance.category].items++;
    }
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
    queueInstance(queuedItem, mvmatrix != NULL ? mvmatrix : mvMatrix,
                  queuedItem->selectionId, SUBMIT_ORDERED, true);
}

void OpenGL3Renderer::pushPacket(RenderItem *packet, quint32 selectionId, SubmitOrder order){
    if(packet != NULL)
        queueInstance(packet, mvMatrix, selectionId, order, false);
}

void OpenGL3Renderer::pushPackets(const QVector<RenderItem*> &items, quint32 selectionId){
    for(RenderItem *packet : items){
        if(packet != NULL)
            queueInstance(packet, mvMatrix, selectionId, SUBMIT_GROUPED, false);
    }
}

void OpenGL3Renderer::pushItemsVNTA(QVector<RenderItem*>& r, float* mvmatrix){
    for(RenderItem *packet : r){
        if(packet != NULL)
            queueInstance(packet, mvmatrix != NULL ? mvmatrix : mvMatrix, 0,
                          SUBMIT_GROUPED, false);
    }
}

void OpenGL3Renderer::pushItemVNTA(RenderItem* r, float* mvmatrix){
    if(r != NULL)
        queueInstance(r, mvmatrix != NULL ? mvmatrix : mvMatrix, 0, SUBMIT_GROUPED, false);
}

// Orders instances by the first submission of their texture, then of their
// packet, then by submission. The order is stable across runs, unlike
// ordering by pointer or hash.
void OpenGL3Renderer::sortByTexture(std::vector<DrawInstance> &instances){
    if(!groupByTexture || instances.size() < 2)
        return;
    const size_t count = instances.size();
    sortOrder.resize(count);
    for(size_t i = 0; i < count; ++i)
        sortOrder[i] = static_cast<quint32>(i);

    std::sort(sortOrder.begin(), sortOrder.end(), [&instances](quint32 a, quint32 b){
        if(instances[a].packet != instances[b].packet)
            return instances[a].packet < instances[b].packet;
        return instances[a].order < instances[b].order;
    });
    for(size_t i = 0; i < count; ){
        const quint32 first = instances[sortOrder[i]].order;
        size_t j = i;
        for(; j < count && instances[sortOrder[j]].packet == instances[sortOrder[i]].packet; ++j)
            instances[sortOrder[j]].packetRank = first;
        i = j;
    }

    std::sort(sortOrder.begin(), sortOrder.end(), [&instances](quint32 a, quint32 b){
        const quint64 ka = textureKey(instances[a].packet);
        const quint64 kb = textureKey(instances[b].packet);
        if(ka != kb)
            return ka < kb;
        return instances[a].order < instances[b].order;
    });
    for(size_t i = 0; i < count; ){
        const quint32 first = instances[sortOrder[i]].order;
        const quint64 key = textureKey(instances[sortOrder[i]].packet);
        size_t j = i;
        for(; j < count && textureKey(instances[sortOrder[j]].packet) == key; ++j)
            instances[sortOrder[j]].textureRank = first;
        i = j;
    }

    std::sort(instances.begin(), instances.end(), [](const DrawInstance &a, const DrawInstance &b){
        if(a.textureRank != b.textureRank)
            return a.textureRank < b.textureRank;
        if(a.packetRank != b.packetRank)
            return a.packetRank < b.packetRank;
        return a.order < b.order;
    });
}

void OpenGL3Renderer::sortBackToFront(std::vector<DrawInstance> &instances){
    std::sort(instances.begin(), instances.end(), [](const DrawInstance &a, const DrawInstance &b){
        if(a.distance != b.distance)
            return a.distance > b.distance;
        return a.order < b.order;
    });
}

void OpenGL3Renderer::drawOrdered(GLUU *gluu, const std::vector<DrawInstance> &instances,
                                  int pass){
    TerrainStateCache terrainState;
    DetailStateCache detailState;
    for(const DrawInstance &instance : instances){
        RenderItem *item = instance.packet;
        if(item->VAO == NULL)
            continue;
        applyItemState(gluu, f, item, instance.selectionId, detailState);
        applyTerrainState(gluu, item, terrainState);
        setModelMatrix(gluu, item->msMatrix);
        setMatrixUniform(gluu, gluu->currentShader->mvMatrixUniform, frameMatrix(instance.matrix));
        PacketRasterState raster(f, item);
        QOpenGLVertexArrayObject::Binder vaoBinder(item->VAO);
        drawItem(f, item, instance.selectionId,
                 static_cast<RenderStats::Category>(instance.category), pass);
    }
}

void OpenGL3Renderer::drawGrouped(GLUU *gluu, const std::vector<DrawInstance> &instances,
                                  int pass){
    TerrainStateCache terrainState;
    DetailStateCache detailState;
    quint32 currentSelection = 0;
    quint64 currentTexture = 0;
    bool textureGroupOpen = false;
    for(size_t i = 0; i < instances.size(); ){
        RenderItem *item = instances[i].packet;
        size_t end = i;
        while(end < instances.size() && instances[end].packet == item)
            ++end;
        if(item->VAO == NULL){
            i = end;
            continue;
        }
        const quint64 texture = textureKey(item);
        if(!textureGroupOpen || texture != currentTexture){
            currentTexture = texture;
            textureGroupOpen = true;
            if(RenderStats::inFrame())
                RenderStats::current().textureGroups++;
        }
        if(RenderStats::inFrame())
            RenderStats::current().groupedPackets++;

        applyItemState(gluu, f, item, instances[i].selectionId, detailState);
        currentSelection = instances[i].selectionId;
        applyTerrainState(gluu, item, terrainState);
        setModelMatrix(gluu, item->msMatrix);
        PacketRasterState raster(f, item);
        QOpenGLVertexArrayObject::Binder vaoBinder(item->VAO);
        for(; i < end; ++i){
            const DrawInstance &instance = instances[i];
            if(instance.selectionId != currentSelection){
                gluu->setSelectionId(instance.selectionId);
                currentSelection = instance.selectionId;
            }
            setMatrixUniform(gluu, gluu->currentShader->mvMatrixUniform,
                             frameMatrix(instance.matrix));
            drawItem(f, item, instance.selectionId,
                     static_cast<RenderStats::Category>(instance.category), pass);
        }
    }
}

void OpenGL3Renderer::consumePass(PassQueue &queue){
    for(const std::vector<DrawInstance> *list : {&queue.ordered, &queue.grouped})
        for(const DrawInstance &instance : *list)
            if(!instance.owned)
                queuedPackets--;
    queue.ordered.clear();
    queue.grouped.clear();
}

void OpenGL3Renderer::clearQueues(){
    for(PassQueue &queue : passes)
        consumePass(queue);
    for(RenderItem *item : ownedItems)
        delete item;
    ownedItems.clear();
    frameMatrices.clear();
    nextOrder = 0;
}

void OpenGL3Renderer::resetFrame(){
    clearQueues();
    Renderer::resetFrame();
}

void OpenGL3Renderer::renderPasses(RenderPass first, RenderPass last){
    GLUU *gluu = GLUU::get();
    QOpenGLContext *context = QOpenGLContext::currentContext();
    f = context != NULL ? context->functions() : NULL;
    const bool canDraw = gluu != NULL && f != NULL && gluu->currentShader != NULL;

    bool drew = false;
    for(int pass = first; pass <= last; ++pass){
        PassQueue &queue = passes[pass];
        if(queue.ordered.empty() && queue.grouped.empty())
            continue;
        if(canDraw){
            if(!drew){
                f->glActiveTexture(GL_TEXTURE0);
                if(RenderStats::inFrame())
                    RenderStats::current().flushes++;
            }
            drawOrdered(gluu, queue.ordered, pass);
            // Keep defaults predictable for the packet loop.
            gluu->setBrightness(1.0f);
            gluu->enableTextures();
            gluu->enableNormals();
            if(pass == PASS_BLENDED)
                sortBackToFront(queue.grouped);
            else
                sortByTexture(queue.grouped);
            drawGrouped(gluu, queue.grouped, pass);
            drew = true;
        }
        consumePass(queue);
    }
    if(!drew)
        return;

    gluu->currentShader->setUniformValue(gluu->currentShader->shaderSecondTexEnabled, 0.0f);
    gluu->currentShader->setUniformValue(gluu->currentShader->terrainTextureRemap, QVector3D());
    gluu->setBrightness(1.0f);
    gluu->enableTextures();
    gluu->enableNormals();
    gluu->currentShader->setUniformValue(gluu->currentShader->terrainPaged, 0);
    context->extraFunctions()->glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0);
}

void OpenGL3Renderer::renderFrame(){
    renderPasses(PASS_SKY, PASS_UI);
    clearQueues();
    Renderer::renderFrame();
}
