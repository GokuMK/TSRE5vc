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

// One draw of instanceCount instances whose matrices the shader reads.
void drawItemInstanced(QOpenGLFunctions *f, RenderItem *item, quint32 selectionId,
                       int instanceCount, RenderStats::Category category, int pass){
    RenderStats::countDraw(category, getItemDrawType(item), item->mesh.count * instanceCount);
    RenderStats::countPassDraw(pass);
    if(RenderStats::inFrame()){
        RenderStats::current().instancedDraws++;
        RenderStats::current().instancedInstances += instanceCount;
    }
    ScopedTerrainDecal decalState(f, item->material.decal && selectionId == 0);
    QOpenGLExtraFunctions *e = QOpenGLContext::currentContext()->extraFunctions();
    if(item->mesh.indexed){
        e->glDrawElementsInstancedBaseVertex(
                    getItemDrawType(item), item->mesh.count, getIndexType(item),
                    reinterpret_cast<void*>(static_cast<quintptr>(item->mesh.indexOffset)),
                    instanceCount, item->mesh.baseVertex);
    } else {
        e->glDrawArraysInstanced(getItemDrawType(item), item->mesh.first, item->mesh.count,
                                 instanceCount);
    }
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
    releaseInstanceBuffer();
}

#ifndef GL_TEXTURE_BUFFER
#define GL_TEXTURE_BUFFER 0x8C2A
#endif
#ifndef GL_MAX_TEXTURE_BUFFER_SIZE
#define GL_MAX_TEXTURE_BUFFER_SIZE 0x8C2B
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif

void OpenGL3Renderer::releaseInstanceBuffer(){
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context != NULL && context == instanceContext){
        QOpenGLExtraFunctions *e = context->extraFunctions();
        if(instanceTexture)
            e->glDeleteTextures(1, &instanceTexture);
        if(instanceBuffer)
            e->glDeleteBuffers(1, &instanceBuffer);
    }
    instanceTexture = instanceBuffer = 0;
    instanceContext = NULL;
}

bool OpenGL3Renderer::instanceRowsFit(int first, int count){
    if(maxInstanceTexels == 0){
        QOpenGLContext *context = QOpenGLContext::currentContext();
        if(context == NULL)
            return false;
        context->extraFunctions()->glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxInstanceTexels);
    }
    return (first + count) * 4 <= maxInstanceTexels;
}

bool OpenGL3Renderer::uploadInstances(){
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context == NULL)
        return false;
    QOpenGLExtraFunctions *e = context->extraFunctions();
    if(instanceContext != context){
        // Names from another context are not ours to delete here.
        instanceTexture = instanceBuffer = 0;
        instanceContext = context;
        e->glGenBuffers(1, &instanceBuffer);
        e->glGenTextures(1, &instanceTexture);
        e->glBindBuffer(GL_TEXTURE_BUFFER, instanceBuffer);
        e->glBufferData(GL_TEXTURE_BUFFER, 16 * sizeof(float), NULL, GL_STREAM_DRAW);
        e->glActiveTexture(GL_TEXTURE8);
        e->glBindTexture(GL_TEXTURE_BUFFER, instanceTexture);
        e->glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, instanceBuffer);
        e->glActiveTexture(GL_TEXTURE0);
        e->glBindBuffer(GL_TEXTURE_BUFFER, 0);
    }
    if(instanceBuffer == 0 || instanceTexture == 0)
        return false;
    e->glBindBuffer(GL_TEXTURE_BUFFER, instanceBuffer);
    e->glBufferData(GL_TEXTURE_BUFFER, instanceUpload.size() * sizeof(float),
                    instanceUpload.data(), GL_STREAM_DRAW);
    e->glBindBuffer(GL_TEXTURE_BUFFER, 0);
    e->glActiveTexture(GL_TEXTURE8);
    e->glBindTexture(GL_TEXTURE_BUFFER, instanceTexture);
    e->glActiveTexture(GL_TEXTURE0);
    return true;
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

bool OpenGL3Renderer::visible(const DrawInstance &instance, const Frustum &frustum) const{
    const RenderItem::Bounds &bounds = instance.packet->bounds;
    if(!frustum.enabled || !bounds.valid())
        return true;
    const float *m = instanceMatrix(instance.matrix);
    float center[3];
    for(int i = 0; i < 3; ++i)
        center[i] = m[i] * bounds.center[0] + m[4 + i] * bounds.center[1]
                + m[8 + i] * bounds.center[2] + m[12 + i];
    float scale = 0.0f;
    for(int column = 0; column < 3; ++column){
        const float *axis = m + column * 4;
        scale = std::max(scale, axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    }
    if(intersects(frustum, center, bounds.radius * std::sqrt(scale)))
        return true;
    if(RenderStats::inFrame())
        RenderStats::current().culledInstances++;
    return false;
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
        if(item->mesh.vao == NULL || !visible(instance, cullFrustum))
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
    // Plan the pass: cull each instance once, and give runs of two or more
    // visible instances of a packet with one selection ID an instanced draw.
    groupPlans.clear();
    instanceUpload.clear();
    instanceVisible.assign(instances.size(), 0);
    for(size_t i = 0; i < instances.size(); ){
        GroupPlan plan;
        plan.begin = i;
        RenderItem *item = instances[i].packet;
        while(i < instances.size() && instances[i].packet == item)
            ++i;
        plan.end = i;
        if(item->mesh.vao == NULL){
            groupPlans.push_back(plan);
            continue;
        }
        bool oneSelection = true;
        quint32 selection = 0;
        for(size_t k = plan.begin; k < plan.end; ++k){
            if(!visible(instances[k], cullFrustum))
                continue;
            instanceVisible[k] = 1;
            if(plan.visible == 0)
                selection = instances[k].selectionId;
            else if(instances[k].selectionId != selection)
                oneSelection = false;
            plan.visible++;
        }
        const int rows = static_cast<int>(instanceUpload.size() / 16);
        if(plan.visible >= 2 && oneSelection && instanceRowsFit(rows, plan.visible)){
            plan.base = rows;
            for(size_t k = plan.begin; k < plan.end; ++k)
                if(instanceVisible[k]){
                    const float *m = instanceMatrix(instances[k].matrix);
                    instanceUpload.insert(instanceUpload.end(), m, m + 16);
                }
        }
        groupPlans.push_back(plan);
    }
    const bool instancing = !instanceUpload.empty() && uploadInstances();

    ProgramCaches caches;
    quint32 currentSelection = 0;
    quint64 currentTexture = 0;
    bool textureGroupOpen = false;
    for(const GroupPlan &plan : groupPlans){
        size_t i = plan.begin;
        const size_t end = plan.end;
        RenderItem *item = instances[i].packet;
        if(item->mesh.vao == NULL || plan.visible == 0)
            continue;
        while(i < end && !instanceVisible[i])
            ++i;
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
        Shader *shader = gluu->currentShader;
        if(instancing && plan.base >= 0 && shader->instanced >= 0){
            shader->setUniformValue(shader->instanced, 1);
            shader->setUniformValue(shader->instanceBase, plan.base);
            drawItemInstanced(f, item, instances[i].selectionId, plan.visible,
                              static_cast<RenderStats::Category>(instances[i].category), pass);
            shader->setUniformValue(shader->instanced, 0);
            continue;
        }
        for(; i < end; ++i){
            const DrawInstance &instance = instances[i];
            if(!instanceVisible[i])
                continue;
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

void OpenGL3Renderer::renderShadowCasters(float range, int statsSlot,
                                          const float *viewProjection){
    GLUU *gluu = GLUU::get();
    QOpenGLContext *context = QOpenGLContext::currentContext();
    f = context != NULL ? context->functions() : NULL;
    if(gluu == NULL || f == NULL || gluu->currentShader == NULL)
        return;
    f->glActiveTexture(GL_TEXTURE0);
    TerrainStateCache terrainState;
    DetailStateCache detailState;
    const float rangeSquared = range * range;
    const Frustum lightFrustum = frustumOf(viewProjection);
    // Casters in range and inside the light view, grouped by packet. Depth
    // does not depend on draw order, so repeated packets draw instanced.
    shadowCasters.clear();
    for(PassQueue &queue : passes){
        for(const std::vector<DrawInstance> *list : {&queue.ordered, &queue.grouped}){
            for(const DrawInstance &instance : *list){
                if(!instance.castsShadow || instance.packet->mesh.vao == NULL)
                    continue;
                float origin[3];
                instanceOrigin(instance, origin);
                const float dx = origin[0] - viewPosition[0];
                const float dz = origin[2] - viewPosition[2];
                if(dx * dx + dz * dz > rangeSquared || !visible(instance, lightFrustum))
                    continue;
                shadowCasters.push_back(&instance);
            }
        }
    }
    std::stable_sort(shadowCasters.begin(), shadowCasters.end(),
                     [](const DrawInstance *a, const DrawInstance *b){
        return std::less<const RenderItem *>()(a->packet, b->packet);
    });
    groupPlans.clear();
    instanceUpload.clear();
    for(size_t i = 0; i < shadowCasters.size(); ){
        GroupPlan plan;
        plan.begin = i;
        while(i < shadowCasters.size() && shadowCasters[i]->packet == shadowCasters[plan.begin]->packet)
            ++i;
        plan.end = i;
        plan.visible = static_cast<int>(plan.end - plan.begin);
        const int rows = static_cast<int>(instanceUpload.size() / 16);
        if(plan.visible >= 2 && instanceRowsFit(rows, plan.visible)){
            plan.base = rows;
            for(size_t k = plan.begin; k < plan.end; ++k){
                const float *m = instanceMatrix(shadowCasters[k]->matrix);
                instanceUpload.insert(instanceUpload.end(), m, m + 16);
            }
        }
        groupPlans.push_back(plan);
    }
    const bool instancing = !instanceUpload.empty() && uploadInstances();
    Shader *shader = gluu->currentShader;
    QOpenGLExtraFunctions *e = context->extraFunctions();
    for(const GroupPlan &plan : groupPlans){
        RenderItem *item = shadowCasters[plan.begin]->packet;
        applyItemState(gluu, f, item, 0, detailState);
        applyTerrainState(gluu, item, terrainState);
        setModelMatrix(gluu, item->msMatrix);
        QOpenGLVertexArrayObject::Binder vaoBinder(item->mesh.vao);
        const bool instanced = instancing && plan.base >= 0 && shader->instanced >= 0;
        if(instanced){
            shader->setUniformValue(shader->instanced, 1);
            shader->setUniformValue(shader->instanceBase, plan.base);
        }
        for(size_t k = plan.begin; k < plan.end; ++k){
            const int count = instanced ? plan.visible : 1;
            if(!instanced)
                setMatrixUniform(gluu, gluu->currentShader->mvMatrixUniform,
                                 instanceMatrix(shadowCasters[k]->matrix));
            RenderStats::countPassDraw(statsSlot);
            if(item->mesh.indexed){
                e->glDrawElementsInstancedBaseVertex(
                            getItemDrawType(item), item->mesh.count, getIndexType(item),
                            reinterpret_cast<void*>(static_cast<quintptr>(item->mesh.indexOffset)),
                            count, item->mesh.baseVertex);
            } else {
                e->glDrawArraysInstanced(getItemDrawType(item), item->mesh.first,
                                         item->mesh.count, count);
            }
            if(instanced)
                break;
        }
        if(instanced)
            shader->setUniformValue(shader->instanced, 0);
    }
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
