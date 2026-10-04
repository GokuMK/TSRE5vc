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
    if(item->material.textureId < 0){
        address = item->material.textureObject;
        return true;
    }
    const auto found = TexLib::mtex.find(item->material.textureId);
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
    if(item->material.textureId >= 0)
        return (quint64(1) << 32) | quint32(item->material.textureId);
    return item->material.textureObject;
}

void applyItemState(GLUU *gluu, QOpenGLFunctions *f, RenderItem *item,
                    quint32 selectionId, DetailStateCache &detail){
    if(item->material.lit)
        gluu->enableNormals();
    else
        gluu->disableNormals();

    gluu->setBrightness(item->material.brightness);

    gluu->setSelectionId(selectionId);
    if (detail.remap != item->terrain.textureRemap) {
        gluu->currentShader->setUniformValue(gluu->currentShader->terrainTextureRemap, item->terrain.textureRemap);
        detail.remap=item->terrain.textureRemap;
    }
    const float detailScale = item->material.textured && !selectionId
            ? item->material.detailScale : 0.0f;
    if (detailScale != 0.0f && detail.texture != item->material.detailTextureObject) {
        f->glActiveTexture(GL_TEXTURE1);
        f->glBindTexture(GL_TEXTURE_2D, item->material.detailTextureObject);
        f->glActiveTexture(GL_TEXTURE0);
        detail.texture = item->material.detailTextureObject;
    }
    if (detail.scale != detailScale) {
        gluu->currentShader->setUniformValue(gluu->currentShader->shaderSecondTexEnabled, detailScale);
        detail.scale = detailScale;
    }
    unsigned int address = 0;
    if(item->material.textured && resolveTexture(item, address)){
        gluu->enableTextures();
        gluu->bindTexture(f, address);
    } else if(item->material.textured){
        gluu->disableTextures(1.0f, 0.0f, 1.0f, 1.0f);
    } else {
        gluu->disableTextures(item->material.color[0], item->material.color[1], item->material.color[2], item->material.color[3]);
    }
}

unsigned int getItemDrawType(const RenderItem *item){
    switch(item->mesh.primitive){
    case RenderItem::PRIMITIVE_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
    case RenderItem::PRIMITIVE_TRIANGLE_FAN: return GL_TRIANGLE_FAN;
    case RenderItem::PRIMITIVE_LINES: return GL_LINES;
    case RenderItem::PRIMITIVE_LINE_STRIP: return GL_LINE_STRIP;
    case RenderItem::PRIMITIVE_LINE_LOOP: return GL_LINE_LOOP;
    case RenderItem::PRIMITIVE_POINTS: return GL_POINTS;
    case RenderItem::PRIMITIVE_TRIANGLES: break;
    }
    return GL_TRIANGLES;
}

unsigned int getIndexType(const RenderItem *item){
    return item->mesh.indexType == RenderItem::INDEX_U32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
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

struct ProceduralTerrainStateCache {
    bool valid = false;
    bool enabled = false;
    unsigned int map = 0;
    unsigned int textures = 0;
    unsigned int details = 0;
    unsigned int params = 0;
    QVector3D remap;
    int side = 0;
    float noiseScale = 0.0f;
};

#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif

void applyProceduralTerrainState(GLUU *gluu, QOpenGLFunctions *f,
                                 RenderItem *item,
                                 ProceduralTerrainStateCache &cache){
    Shader *shader=gluu ? gluu->currentShader : NULL;
    if(shader==NULL || item==NULL) return;
    const RenderItem::Terrain &terrain=item->terrain;
    const bool enabled=terrain.materialMap!=0 && terrain.materialTextures!=0
            && terrain.materialParams!=0;
    if(!cache.valid || cache.enabled!=enabled)
        shader->setUniformValue(shader->terrainMaterialEnabled,enabled ? 1 : 0);
    if(enabled){
        const bool rebind=!cache.valid || !cache.enabled;
        if(rebind || cache.map!=terrain.materialMap
                || cache.textures!=terrain.materialTextures
                || cache.details!=terrain.materialDetails
                || cache.params!=terrain.materialParams){
            f->glActiveTexture(GL_TEXTURE4);
            f->glBindTexture(GL_TEXTURE_2D,terrain.materialMap);
            f->glActiveTexture(GL_TEXTURE5);
            f->glBindTexture(GL_TEXTURE_2D_ARRAY,terrain.materialTextures);
            f->glActiveTexture(GL_TEXTURE6);
            f->glBindTexture(GL_TEXTURE_2D_ARRAY,terrain.materialDetails);
            f->glActiveTexture(GL_TEXTURE7);
            f->glBindTexture(GL_TEXTURE_2D,terrain.materialParams);
            f->glActiveTexture(GL_TEXTURE0);
        }
        if(!cache.valid || !cache.enabled || cache.remap!=item->terrain.materialMapRemap)
            shader->setUniformValue(shader->terrainMaterialMapRemap,item->terrain.materialMapRemap);
        if(!cache.valid || !cache.enabled || cache.side!=item->terrain.materialMapSide)
            shader->setUniformValue(shader->terrainMaterialMapSide,item->terrain.materialMapSide);
        if(!cache.valid || !cache.enabled || cache.noiseScale!=item->terrain.materialNoiseScale)
            shader->setUniformValue(shader->terrainMaterialNoiseScale,item->terrain.materialNoiseScale);
    }
    cache.valid=true;
    cache.enabled=enabled;
    cache.map=terrain.materialMap;
    cache.textures=terrain.materialTextures;
    cache.details=terrain.materialDetails;
    cache.params=terrain.materialParams;
    cache.remap=item->terrain.materialMapRemap;
    cache.side=item->terrain.materialMapSide;
    cache.noiseScale=item->terrain.materialNoiseScale;
}

void applyTerrainState(GLUU *gluu, RenderItem *item,
                       TerrainStateCache &cache){
    if(gluu == NULL || gluu->currentShader == NULL || item == NULL)
        return;
    Shader *shader = gluu->currentShader;
    if(!cache.valid || cache.paged != item->terrain.paged)
        shader->setUniformValue(shader->terrainPaged, item->terrain.paged ? 1 : 0);
    if(!item->terrain.paged){
        cache.valid = true;
        cache.paged = false;
        cache.params = NULL;
        return;
    }
    if(!cache.valid || !cache.paged
            || cache.verticesPerPatch != item->terrain.verticesPerPatch)
        shader->setUniformValue(shader->terrainVerticesPerPatch,
                                item->terrain.verticesPerPatch);
    if(!cache.valid || !cache.paged
            || cache.patchSide != item->terrain.patchSide)
        shader->setUniformValue(shader->terrainPatchSide,
                                item->terrain.patchSide);
    if(!cache.valid || !cache.paged
            || cache.sampleSpacing != item->terrain.sampleSpacing)
        shader->setUniformValue(shader->terrainSampleSpacing,
                                item->terrain.sampleSpacing);
    if(!cache.valid || !cache.paged || cache.applyGaps != item->terrain.applyGaps)
        shader->setUniformValue(shader->terrainApplyGaps,
                                item->terrain.applyGaps ? 1 : 0);
    if(!cache.valid || !cache.paged || cache.mapPass != item->terrain.mapPass)
        shader->setUniformValue(shader->terrainMapPass,
                                item->terrain.mapPass ? 1 : 0);
    if(!cache.valid || !cache.paged || cache.params != item->terrain.paramsBuffer)
        QOpenGLContext::currentContext()->extraFunctions()->glBindBufferBase(
                    GL_UNIFORM_BUFFER, 0,
                    item->terrain.paramsBuffer == NULL ? 0
                    : item->terrain.paramsBuffer->bufferId());
    cache.valid = true;
    cache.paged = true;
    cache.params = item->terrain.paramsBuffer;
    cache.verticesPerPatch = item->terrain.verticesPerPatch;
    cache.patchSide = item->terrain.patchSide;
    cache.sampleSpacing = item->terrain.sampleSpacing;
    cache.applyGaps = item->terrain.applyGaps;
    cache.mapPass = item->terrain.mapPass;
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
    RenderStats::countDraw(category, getItemDrawType(item), item->mesh.count);
    RenderStats::countPassDraw(pass);
    ScopedTerrainDecal decalState(f, item->material.decal && selectionId == 0);
    if(item->mesh.indexed){
        QOpenGLContext::currentContext()->extraFunctions()->glDrawElementsBaseVertex(
                    getItemDrawType(item), item->mesh.count, getIndexType(item),
                    reinterpret_cast<void*>(static_cast<quintptr>(item->mesh.indexOffset)),
                    item->mesh.baseVertex);
    } else {
        f->glDrawArrays(getItemDrawType(item), item->mesh.first, item->mesh.count);
    }
}

// Terrain packets draw with the terrain variant of the bound program.
bool usesTerrainProgram(const RenderItem *item){
    return item->material.surface == RenderItem::SURFACE_TERRAIN || item->terrain.paged
            || item->terrain.materialMap != 0 || !item->terrain.textureRemap.isNull();
}

// Draw-time state that belongs to the bound program.
struct ProgramCaches {
    TerrainStateCache terrain;
    ProceduralTerrainStateCache procedural;
    DetailStateCache detail;
};

// Binds the program a packet needs. A newly bound program receives the frame
// uniforms again and starts with empty caches.
void useProgram(GLUU *gluu, Shader *base, const RenderItem *item, ProgramCaches &caches){
    Shader *wanted = usesTerrainProgram(item) ? gluu->terrainVariant(base) : base;
    if(wanted == gluu->currentShader)
        return;
    gluu->currentShader = wanted;
    wanted->bind();
    gluu->setMatrixUniforms();
    caches = ProgramCaches();
}

// Line width and polygon mode set for one packet and restored afterwards.
class PacketRasterState {
public:
    PacketRasterState(QOpenGLFunctions *f, const RenderItem *item)
        : f(f),
          lineWidth(item->material.lineWidth > 0 && item->material.lineWidth != Game::oglDefaultLineWidth),
          wireframe(item->material.wireframe) {
        if(lineWidth)
            f->glLineWidth(item->material.lineWidth);
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
}

OpenGL3Renderer::~OpenGL3Renderer() {
    clearQueues();
}

quint32 OpenGL3Renderer::captureMatrix(const float *matrix){
    const quint32 index = static_cast<quint32>(instanceMatrices.size() / 16);
    instanceMatrices.insert(instanceMatrices.end(), matrix, matrix + 16);
    return index;
}

const float *OpenGL3Renderer::instanceMatrix(quint32 index) const{
    return instanceMatrices.data() + index * 16;
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
    if(packet->material.surface == RenderItem::SURFACE_TERRAIN)
        return PASS_TERRAIN;
    if(order == SUBMIT_ORDERED)
        return PASS_OPAQUE;
    if(packet->material.surface == RenderItem::SURFACE_ALPHA_TEST)
        return PASS_ALPHA_TEST;
    if(packet->material.surface == RenderItem::SURFACE_BLENDED)
        return PASS_BLENDED;
    return PASS_OPAQUE;
}

bool OpenGL3Renderer::castsShadow(const RenderItem *packet) const{
    if(!shadowCasting)
        return false;
    if(currentLayer != LAYER_SCENE && currentLayer != LAYER_OVERLAY)
        return false;
    if(packet->material.surface == RenderItem::SURFACE_TERRAIN || packet->material.decal)
        return false;
    if(packet->mesh.layout != RenderItem::VNT && packet->mesh.layout != RenderItem::VNTA)
        return false;
    return packet->mesh.primitive == RenderItem::PRIMITIVE_TRIANGLES;
}

// Position of the packet origin in submission space.
void OpenGL3Renderer::instanceOrigin(const DrawInstance &instance, float *origin) const{
    const float *matrix = instanceMatrix(instance.matrix);
    const float *ms = instance.packet->msMatrix;
    const float local[3] = {ms ? ms[12] : 0.0f, ms ? ms[13] : 0.0f, ms ? ms[14] : 0.0f};
    for(int i = 0; i < 3; ++i)
        origin[i] = matrix[i] * local[0] + matrix[4 + i] * local[1]
                + matrix[8 + i] * local[2] + matrix[12 + i];
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
    instance.castsShadow = castsShadow(packet);
    if(pass == PASS_BLENDED && order == SUBMIT_GROUPED){
        // Distance from the camera to the packet origin, for back-to-front order.
        float origin[3];
        instanceOrigin(instance, origin);
        float distance = 0.0f;
        for(int i = 0; i < 3; ++i)
            distance += (origin[i] - viewPosition[i]) * (origin[i] - viewPosition[i]);
        instance.distance = distance;
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

void OpenGL3Renderer::submitFrameItem(RenderItem* r){
    if(r == NULL)
        return;
    RenderItem *queuedItem = r;
    if(r->shared){
        queuedItem = new RenderItem(*r);
        queuedItem->shared = false;
    }
    ownedItems.push_back(queuedItem);
    queueInstance(queuedItem, mvMatrix, queuedItem->selectionId, SUBMIT_ORDERED, true);
}

void OpenGL3Renderer::submit(RenderItem *packet, quint32 selectionId, SubmitOrder order){
    if(packet != NULL)
        queueInstance(packet, mvMatrix, selectionId, order, false);
}

void OpenGL3Renderer::submit(const QVector<RenderItem*> &items, quint32 selectionId){
    for(RenderItem *packet : items){
        if(packet != NULL)
            queueInstance(packet, mvMatrix, selectionId, SUBMIT_GROUPED, false);
    }
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

void OpenGL3Renderer::drawOrdered(GLUU *gluu, Shader *base,
                                  const std::vector<DrawInstance> &instances, int pass){
    ProgramCaches caches;
    for(const DrawInstance &instance : instances){
        RenderItem *item = instance.packet;
        if(item->mesh.vao == NULL)
            continue;
        useProgram(gluu, base, item, caches);
        applyItemState(gluu, f, item, instance.selectionId, caches.detail);
        applyTerrainState(gluu, item, caches.terrain);
        applyProceduralTerrainState(gluu,f,item,caches.procedural);
        setModelMatrix(gluu, item->msMatrix);
        setMatrixUniform(gluu, gluu->currentShader->mvMatrixUniform, instanceMatrix(instance.matrix));
        PacketRasterState raster(f, item);
        QOpenGLVertexArrayObject::Binder vaoBinder(item->mesh.vao);
        drawItem(f, item, instance.selectionId,
                 static_cast<RenderStats::Category>(instance.category), pass);
    }
}

void OpenGL3Renderer::drawGrouped(GLUU *gluu, Shader *base,
                                  const std::vector<DrawInstance> &instances, int pass){
    ProgramCaches caches;
    quint32 currentSelection = 0;
    quint64 currentTexture = 0;
    bool textureGroupOpen = false;
    for(size_t i = 0; i < instances.size(); ){
        RenderItem *item = instances[i].packet;
        size_t end = i;
        while(end < instances.size() && instances[end].packet == item)
            ++end;
        if(item->mesh.vao == NULL){
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

        useProgram(gluu, base, item, caches);
        applyItemState(gluu, f, item, instances[i].selectionId, caches.detail);
        currentSelection = instances[i].selectionId;
        applyTerrainState(gluu, item, caches.terrain);
        applyProceduralTerrainState(gluu,f,item,caches.procedural);
        setModelMatrix(gluu, item->msMatrix);
        PacketRasterState raster(f, item);
        QOpenGLVertexArrayObject::Binder vaoBinder(item->mesh.vao);
        for(; i < end; ++i){
            const DrawInstance &instance = instances[i];
            if(instance.selectionId != currentSelection){
                gluu->setSelectionId(instance.selectionId);
                currentSelection = instance.selectionId;
            }
            setMatrixUniform(gluu, gluu->currentShader->mvMatrixUniform,
                             instanceMatrix(instance.matrix));
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
    instanceMatrices.clear();
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
    // Program the frame bound; terrain packets may switch to its variant.
    Shader *base = canDraw ? gluu->currentShader : NULL;

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
            drawOrdered(gluu, base, queue.ordered, pass);
            // Keep defaults predictable for the packet loop.
            gluu->setBrightness(1.0f);
            gluu->enableTextures();
            gluu->enableNormals();
            if(pass == PASS_BLENDED)
                sortBackToFront(queue.grouped);
            else
                sortByTexture(queue.grouped);
            drawGrouped(gluu, base, queue.grouped, pass);
            drew = true;
        }
        consumePass(queue);
    }
    if(!drew)
        return;

    if(gluu->currentShader != base){
        gluu->currentShader = base;
        base->bind();
        gluu->setMatrixUniforms();
    }
    gluu->currentShader->setUniformValue(gluu->currentShader->shaderSecondTexEnabled, 0.0f);
    gluu->currentShader->setUniformValue(gluu->currentShader->terrainTextureRemap, QVector3D());
    gluu->setBrightness(1.0f);
    gluu->enableTextures();
    gluu->enableNormals();
    gluu->currentShader->setUniformValue(gluu->currentShader->terrainPaged, 0);
    gluu->currentShader->setUniformValue(gluu->currentShader->terrainMaterialEnabled, 0);
    context->extraFunctions()->glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0);
}

void OpenGL3Renderer::renderShadowCasters(float range, int statsSlot){
    GLUU *gluu = GLUU::get();
    QOpenGLContext *context = QOpenGLContext::currentContext();
    f = context != NULL ? context->functions() : NULL;
    if(gluu == NULL || f == NULL || gluu->currentShader == NULL)
        return;
    f->glActiveTexture(GL_TEXTURE0);
    TerrainStateCache terrainState;
    DetailStateCache detailState;
    const float rangeSquared = range * range;
    RenderItem *current = NULL;
    for(PassQueue &queue : passes){
        for(const std::vector<DrawInstance> *list : {&queue.ordered, &queue.grouped}){
            for(const DrawInstance &instance : *list){
                RenderItem *item = instance.packet;
                if(!instance.castsShadow || item->mesh.vao == NULL)
                    continue;
                float origin[3];
                instanceOrigin(instance, origin);
                const float dx = origin[0] - viewPosition[0];
                const float dz = origin[2] - viewPosition[2];
                if(dx * dx + dz * dz > rangeSquared)
                    continue;
                if(item != current){
                    if(current != NULL)
                        current->mesh.vao->release();
                    applyItemState(gluu, f, item, 0, detailState);
                    applyTerrainState(gluu, item, terrainState);
                    setModelMatrix(gluu, item->msMatrix);
                    item->mesh.vao->bind();
                    current = item;
                }
                setMatrixUniform(gluu, gluu->currentShader->mvMatrixUniform,
                                 instanceMatrix(instance.matrix));
                RenderStats::countPassDraw(statsSlot);
                if(item->mesh.indexed){
                    context->extraFunctions()->glDrawElementsBaseVertex(
                                getItemDrawType(item), item->mesh.count, getIndexType(item),
                                reinterpret_cast<void*>(static_cast<quintptr>(item->mesh.indexOffset)),
                                item->mesh.baseVertex);
                } else {
                    f->glDrawArrays(getItemDrawType(item), item->mesh.first, item->mesh.count);
                }
            }
        }
    }
    if(current != NULL)
        current->mesh.vao->release();
    gluu->currentShader->setUniformValue(gluu->currentShader->shaderSecondTexEnabled, 0.0f);
    gluu->currentShader->setUniformValue(gluu->currentShader->terrainTextureRemap, QVector3D());
    gluu->setBrightness(1.0f);
    gluu->enableTextures();
    gluu->enableNormals();
}

void OpenGL3Renderer::renderFrame(){
    renderPasses(PASS_SKY, PASS_UI);
    clearQueues();
    Renderer::renderFrame();
}
