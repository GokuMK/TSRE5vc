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
#include <tsre/renderer/Mesh.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderStats.h>
#include <tsre/renderer/RenderSurface.h>
#include <tsre/math3d/GLMatrix.h>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <tsre/ogl/GLUU.h>
#include <tsre/ogl/ScopedTerrainDecal.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>
#include <tsre/Game.h>

#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED 0x8914
#endif
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

// Resolves a TexLib texture when drawing. Returns false while the texture is
// not uploaded, failed to load or is disabled by the user.
bool resolveTexLibTexture(int textureId, unsigned int &address){
    const auto found = TexLib::mtex.find(textureId);
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

// Resolves a packet's texture handle when drawing.
bool resolveTexture(const RenderItem *item, unsigned int &address){
    if(item->material.textureId < 0){
        address = item->material.textureObject;
        return true;
    }
    return resolveTexLibTexture(item->material.textureId, address);
}

// Texture unit of each PBR map: the base colour is the packet texture, the
// clearcoat and specular maps use units that only terrain and water programs
// use otherwise. That takes all 16 units OpenGL 3.3 guarantees; the
// transmission and thickness maps use units 16 and 17 where the driver has
// them (current drivers have 32) and are left out otherwise.
const int PbrMapUnits[RenderItem::Pbr::MAP_COUNT] = {0, 11, 12, 13, 14, 4, 5, 6, 7, 15, 16, 17};
// The frame copy transmissive surfaces see through, on unit 1 (the detail
// texture unit, unused by the PBR program).
const int SceneCopyUnit = 1;

// Mipmap levels of the frame copy while the transmission pass draws; 0 when
// transmissive surfaces see the environment instead.
float sceneCopyLevels = 0.0f;

int textureUnitCount(){
    static int units = 0;
    if(units == 0){
        if(QOpenGLContext *context = QOpenGLContext::currentContext())
            context->functions()->glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
        if(units <= 0)
            units = 16;
    }
    return units;
}

// Metallic-roughness material uniforms and maps of a packet drawn by the
// PBR program. Maps not uploaded yet are left out.
void applyPbrState(GLUU *gluu, QOpenGLFunctions *f, const RenderItem *item){
    Shader *s = gluu->currentShader;
    if(!item->pbr.enabled || s->pbrBaseColor < 0)
        return;
    const RenderItem::Pbr &p = item->pbr;
    s->setUniformValue(s->pbrBaseColor, p.baseColor[0], p.baseColor[1], p.baseColor[2],
                       p.baseColor[3]);
    s->setUniformValue(s->pbrMetallicRoughness, p.metallic, p.roughness);
    s->setUniformValue(s->pbrEmissive, p.emissive[0], p.emissive[1], p.emissive[2]);
    s->setUniformValue(s->pbrNormalScale, p.normalScale);
    s->setUniformValue(s->pbrOcclusionStrength, p.occlusionStrength);
    s->setUniformValue(s->pbrAlphaCutoff, p.alphaCutoff);
    s->setUniformValue(s->pbrBlend, p.blend ? 1 : 0);
    s->setUniformValue(s->pbrUnlit, p.unlit ? 1 : 0);
    s->setUniformValue(s->pbrClearcoat, p.clearcoat, p.clearcoatRoughness, p.clearcoatNormalScale);
    s->setUniformValue(s->pbrSpecular, p.specularColor[0], p.specularColor[1], p.specularColor[2],
                       p.specular);
    s->setUniformValue(s->pbrIor, p.ior);
    s->setUniformValue(s->pbrTransmission, p.transmission, p.thickness, p.attenuationDistance,
                       sceneCopyLevels);
    s->setUniformValue(s->pbrAttenuationColor, p.attenuationColor[0], p.attenuationColor[1],
                       p.attenuationColor[2]);
    int present = 0;
    int texCoords = 0;
    int transformed = 0;
    float transforms[RenderItem::Pbr::MAP_COUNT * 6];
    for(int map = 0; map < RenderItem::Pbr::MAP_COUNT; ++map){
        if(p.texCoords[map] != 0)
            texCoords |= 1 << map;
        std::copy(p.uvTransforms[map].rows, p.uvTransforms[map].rows + 6, transforms + map * 6);
        if(!p.uvTransforms[map].identity())
            transformed |= 1 << map;
        unsigned int address = 0;
        if(map == RenderItem::Pbr::MAP_BASE_COLOR || p.textures[map] < 0
                || PbrMapUnits[map] >= textureUnitCount()
                || !resolveTexLibTexture(p.textures[map], address))
            continue;
        f->glActiveTexture(GL_TEXTURE0 + PbrMapUnits[map]);
        f->glBindTexture(GL_TEXTURE_2D, address);
        present |= 1 << (map - 1);
    }
    f->glActiveTexture(GL_TEXTURE0);
    s->setUniformValue(s->pbrTextures, present);
    s->setUniformValue(s->pbrTexCoords, texCoords);
    s->setUniformValue(s->pbrUvTransforms, transformed);
    if(transformed != 0)
        s->setUniformValueArray(s->pbrUvTransform, transforms, RenderItem::Pbr::MAP_COUNT * 2, 3);
}

// Grouping key; texture handles and raw addresses do not collide.
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
    MeshHandle params;
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
        cache.params = MeshHandle();
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
    if(!cache.valid || !cache.paged || !(cache.params == item->terrain.paramsBuffer)){
        QOpenGLContext *context = QOpenGLContext::currentContext();
        Meshes::Buffers params;
        const bool bound = Meshes::prepare(item->terrain.paramsBuffer, context->functions(), params);
        context->extraFunctions()->glBindBufferBase(GL_UNIFORM_BUFFER, 0,
                                                     bound ? params.vertexBuffer : 0);
    }
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

// Overlay and UI passes draw without lighting, shadows and fog.
bool usesUnlitProgram(int pass){
    return pass == Renderer::PASS_OVERLAY || pass == Renderer::PASS_UI;
}

// Binds the program a packet needs. A newly bound program receives the frame
// uniforms again and starts with empty caches.
void usePacketProgram(GLUU *gluu, Shader *base, const RenderItem *item, int pass,
                ProgramCaches &caches){
    Shader *wanted = usesTerrainProgram(item) ? gluu->terrainVariant(base)
            : usesUnlitProgram(pass) ? gluu->unlitVariant(base)
            : item->pbr.enabled ? gluu->pbrVariant(base)
            : item->water.enabled ? gluu->waterVariant(base) : base;
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

// Line width, polygon mode, culling and depth writes set for one packet and
// restored afterwards.
class PacketRasterState {
public:
    PacketRasterState(QOpenGLFunctions *f, const RenderItem *item)
        : f(f),
          lineWidth(item->material.lineWidth > 0 && item->material.lineWidth != Game::oglDefaultLineWidth),
          wireframe(item->material.wireframe),
          bothSides(item->material.doubleSided && f->glIsEnabled(GL_CULL_FACE)),
          keepDepth(item->pbr.enabled && item->pbr.blend && depthWrites(f)) {
        if(lineWidth)
            f->glLineWidth(item->material.lineWidth);
        if(wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        if(bothSides)
            f->glDisable(GL_CULL_FACE);
        // glTF BLEND surfaces are sorted by origin only: one drawn first
        // must not hide others behind it (lights behind a glass shell).
        if(keepDepth)
            f->glDepthMask(GL_FALSE);
    }
    ~PacketRasterState() {
        if(keepDepth)
            f->glDepthMask(GL_TRUE);
        if(wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        if(lineWidth)
            f->glLineWidth(Game::oglDefaultLineWidth);
        if(bothSides)
            f->glEnable(GL_CULL_FACE);
    }
private:
    static bool depthWrites(QOpenGLFunctions *f) {
        GLboolean mask = GL_TRUE;
        f->glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
        return mask == GL_TRUE;
    }
    QOpenGLFunctions *f;
    bool lineWidth;
    bool wireframe;
    bool bothSides;
    bool keepDepth;
};

}

OpenGL3Renderer::OpenGL3Renderer() {
}

OpenGL3Renderer::~OpenGL3Renderer() {
    clearQueues();
    selectionTarget.release();
    releaseInstanceBuffer();
    releaseMeshArrays();
    releaseWrapSamplers();
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context != NULL && context == queryContext && samplesQuery != 0)
        context->extraFunctions()->glDeleteQueries(1, &samplesQuery);
    if(context != NULL && context == copyContext){
        if(sceneCopyTexture != 0)
            context->functions()->glDeleteTextures(1, &sceneCopyTexture);
        if(sceneCopyFramebuffer != 0)
            context->functions()->glDeleteFramebuffers(1, &sceneCopyFramebuffer);
    }
    if(context != NULL && context == samplerContext)
        for(auto &entry : wrapSamplers)
            context->extraFunctions()->glDeleteSamplers(1, &entry.second);
}

struct OpenGL3Renderer::MeshBinding {
    OpenGL3Renderer &renderer;
    const RenderItem *item;
    bool bound;
    MeshBinding(OpenGL3Renderer &renderer, const RenderItem *item)
        : renderer(renderer), item(item), bound(renderer.bindMesh(item)) {}
    ~MeshBinding() {
        if(bound)
            renderer.unbindMesh(item);
    }
};

bool OpenGL3Renderer::bindMesh(const RenderItem *item){
    const MeshHandle handle = item->mesh.handle;
    if(!handle.valid())
        return false;
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context == NULL)
        return false;
    if(context != meshContext){
        // Vertex arrays belong to one context; a new one starts empty.
        meshArrays.clear();
        meshContext = context;
    }
    Meshes::Buffers buffers;
    if(!Meshes::prepare(handle, f, buffers) || buffers.format == MeshData::Buffer)
        return false;
    QOpenGLExtraFunctions *e = context->extraFunctions();
    MeshArray &array = meshArrays[handle.index];
    if(array.vao != 0 && array.generation == handle.generation && array.stamp == buffers.stamp){
        e->glBindVertexArray(array.vao);
        return true;
    }
    if(array.vao == 0)
        e->glGenVertexArrays(1, &array.vao);
    e->glBindVertexArray(array.vao);
    for(GLuint location = 0; location < 7; ++location)
        e->glDisableVertexAttribArray(location);
    f->glBindBuffer(GL_ARRAY_BUFFER, buffers.vertexBuffer);
    Meshes::setupAttributes(f, buffers);
    f->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers.indexBuffer);
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    array.generation = handle.generation;
    array.stamp = buffers.stamp;
    return true;
}

void OpenGL3Renderer::unbindMesh(const RenderItem *){
    QOpenGLContext::currentContext()->extraFunctions()->glBindVertexArray(0);
}

void OpenGL3Renderer::bindWrapSampler(int unit, quint32 wrap){
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context != samplerContext){
        // Sampler objects belong to their context; start over in a new one.
        wrapSamplers.clear();
        std::fill(std::begin(boundSamplers), std::end(boundSamplers), 0u);
        samplerContext = context;
    }
    QOpenGLExtraFunctions *e = context->extraFunctions();
    unsigned int sampler = 0;
    if(wrap != 0){
        auto found = wrapSamplers.find(wrap);
        if(found == wrapSamplers.end()){
            e->glGenSamplers(1, &sampler);
            // Filtering as TexLib uploads it; only the wrap modes differ.
            e->glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            e->glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            e->glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GLint(wrap >> 16));
            e->glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GLint(wrap & 0xffff));
            found = wrapSamplers.emplace(wrap, sampler).first;
        }
        sampler = found->second;
    }
    if(boundSamplers[unit] != sampler){
        e->glBindSampler(GLuint(unit), sampler);
        boundSamplers[unit] = sampler;
    }
}

void OpenGL3Renderer::applyWrapSamplers(const RenderItem *item){
    static_assert(17 < SamplerUnits, "sampler bookkeeping covers the PBR units");
    const int *units = PbrMapUnits;
    for(int map = 0; map < RenderItem::Pbr::MAP_COUNT; ++map){
        quint32 wrap = 0;
        const unsigned short *modes = item->pbr.wrap[map];
        if(item->pbr.enabled && (modes[0] != 0 || modes[1] != 0))
            wrap = (quint32(modes[0] != 0 ? modes[0] : GL_REPEAT) << 16)
                    | (modes[1] != 0 ? modes[1] : GL_REPEAT);
        // Maps on units the driver lacks are not bound (transmission).
        if(units[map] >= textureUnitCount())
            continue;
        if(wrap != 0 || boundSamplers[units[map]] != 0)
            bindWrapSampler(units[map], wrap);
    }
}

// Lower water layers on units 4 and 5 (terrain-only elsewhere) and the
// wave map on unit 15.
void OpenGL3Renderer::applyWaterState(GLUU *gluu, const RenderItem *item){
    Shader *s = gluu->currentShader;
    if(!item->water.enabled || s->waterLayers < 0)
        return;
    int present = 0;
    for(int layer = 0; layer < RenderItem::Water::LAYER_COUNT; ++layer){
        if(item->water.layers[layer] == 0)
            continue;
        f->glActiveTexture(GL_TEXTURE4 + layer);
        f->glBindTexture(GL_TEXTURE_2D, item->water.layers[layer]);
        present |= 1 << layer;
    }
    f->glActiveTexture(GL_TEXTURE0);
    s->setUniformValue(s->waterLayers, present);
    waterNormals.bind(f, 15);
}

void OpenGL3Renderer::releaseWrapSamplers(){
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context == NULL || context != samplerContext)
        return;
    for(int unit = 0; unit < SamplerUnits; ++unit)
        if(boundSamplers[unit] != 0){
            context->extraFunctions()->glBindSampler(GLuint(unit), 0);
            boundSamplers[unit] = 0;
        }
}

void OpenGL3Renderer::collectMeshes(){
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context == NULL || f == NULL)
        return;
    Meshes::collectGarbage(f);
    const quint64 releases = Meshes::releaseCount();
    if(releases == sweptReleases || context != meshContext)
        return;
    sweptReleases = releases;
    QOpenGLExtraFunctions *e = context->extraFunctions();
    for(auto it = meshArrays.begin(); it != meshArrays.end(); ){
        if(Meshes::alive(MeshHandle{it->first, it->second.generation})){
            ++it;
            continue;
        }
        e->glDeleteVertexArrays(1, &it->second.vao);
        it = meshArrays.erase(it);
    }
}

void OpenGL3Renderer::releaseMeshArrays(){
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context != NULL && context == meshContext){
        QOpenGLExtraFunctions *e = context->extraFunctions();
        for(auto &entry : meshArrays)
            e->glDeleteVertexArrays(1, &entry.second.vao);
    }
    meshArrays.clear();
    meshContext = NULL;
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

void OpenGL3Renderer::drawOrdered(GLUU *gluu, Shader *base,
                                  const std::vector<DrawInstance> &instances, int pass){
    ProgramCaches caches;
    for(const DrawInstance &instance : instances){
        RenderItem *item = instance.packet;
        if(!hasMesh(item) || !visible(instance, cullFrustum))
            continue;
        usePacketProgram(gluu, base, item, pass, caches);
        applyItemState(gluu, f, item, instance.selectionId, caches.detail);
        applyPbrState(gluu, f, item);
        applyWaterState(gluu, item);
        applyWrapSamplers(item);
        applyTerrainState(gluu, item, caches.terrain);
        applyProceduralTerrainState(gluu,f,item,caches.procedural);
        setModelMatrix(gluu, item->msMatrix);
        setMatrixUniform(gluu, gluu->currentShader->mvMatrixUniform, instanceMatrix(instance.matrix));
        PacketRasterState raster(f, item);
        MeshBinding mesh(*this, item);
        if(!mesh.bound)
            continue;
        drawItem(f, item, instance.selectionId,
                 static_cast<RenderStats::Category>(instance.category), pass);
    }
}

void OpenGL3Renderer::drawGrouped(GLUU *gluu, Shader *base,
                                  const std::vector<DrawInstance> &instances, int pass){
    // Plan the pass: cull each instance once, and give runs of two or more
    // visible instances of a packet with one selection ID an instanced draw.
    planGroups(instances);
    const bool instancing = !instanceUpload.empty() && uploadInstances();

    ProgramCaches caches;
    quint32 currentSelection = 0;
    quint64 currentTexture = 0;
    bool textureGroupOpen = false;
    for(const GroupPlan &plan : groupPlans){
        size_t i = plan.begin;
        const size_t end = plan.end;
        RenderItem *item = instances[i].packet;
        if(!hasMesh(item) || plan.visible == 0)
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

        usePacketProgram(gluu, base, item, pass, caches);
        applyItemState(gluu, f, item, instances[i].selectionId, caches.detail);
        applyPbrState(gluu, f, item);
        applyWaterState(gluu, item);
        applyWrapSamplers(item);
        currentSelection = instances[i].selectionId;
        applyTerrainState(gluu, item, caches.terrain);
        applyProceduralTerrainState(gluu,f,item,caches.procedural);
        setModelMatrix(gluu, item->msMatrix);
        PacketRasterState raster(f, item);
        MeshBinding mesh(*this, item);
        if(!mesh.bound)
            continue;
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

void OpenGL3Renderer::renderPasses(RenderPass first, RenderPass last){
    drawPasses(first, last, true);
}

void OpenGL3Renderer::renderPassesRetained(RenderPass first, RenderPass last){
    drawPasses(first, last, false);
}

void OpenGL3Renderer::drawPasses(RenderPass first, RenderPass last, bool consume){
    GLUU *gluu = GLUU::get();
    QOpenGLContext *context = QOpenGLContext::currentContext();
    f = context != NULL ? context->functions() : NULL;
    const bool canDraw = gluu != NULL && f != NULL && gluu->currentShader != NULL;
    if(canDraw)
        collectMeshes();
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
            // Transmissive surfaces see the frame drawn so far.
            if(pass == PASS_TRANSMISSION)
                sceneCopyLevels = copyFrameForTransmission(gluu, base);
            drawOrdered(gluu, base, queue.ordered, pass);
            // Keep defaults predictable for the packet loop.
            gluu->setBrightness(1.0f);
            gluu->enableTextures();
            gluu->enableNormals();
            if(pass == PASS_BLENDED || pass == PASS_TRANSMISSION)
                sortBackToFront(queue.grouped);
            else
                sortByTexture(queue.grouped);
            drawGrouped(gluu, base, queue.grouped, pass);
            drew = true;
        }
        if(pass == PASS_TRANSMISSION)
            sceneCopyLevels = 0.0f;
        if(consume)
            consumePass(queue);
    }
    releaseWrapSamplers();
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
    // Casters in range and inside the light view, grouped by packet.
    planShadowCasters(range, viewProjection);
    const bool instancing = !instanceUpload.empty() && uploadInstances();
    Shader *shader = gluu->currentShader;
    QOpenGLExtraFunctions *e = context->extraFunctions();
    for(const GroupPlan &plan : groupPlans){
        RenderItem *item = shadowCasters[plan.begin]->packet;
        applyItemState(gluu, f, item, 0, detailState);
        applyTerrainState(gluu, item, terrainState);
        setModelMatrix(gluu, item->msMatrix);
        MeshBinding mesh(*this, item);
        if(!mesh.bound)
            continue;
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

float OpenGL3Renderer::copyFrameForTransmission(GLUU *gluu, Shader *base){
    // Secondary views and programs without a PBR variant (selection) use no
    // copy: transmissive surfaces there see the environment.
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(secondaryView || context == NULL || gluu->pbrVariant(base) == base)
        return 0.0f;
    QOpenGLExtraFunctions *e = context->extraFunctions();
    GLint viewportRect[4] = {0, 0, 0, 0};
    e->glGetIntegerv(GL_VIEWPORT, viewportRect);
    const int width = viewportRect[2], height = viewportRect[3];
    if(width <= 0 || height <= 0)
        return 0.0f;
    if(copyContext != context){
        // Objects of another context cannot be used or deleted here.
        sceneCopyTexture = sceneCopyFramebuffer = 0;
        sceneCopyWidth = sceneCopyHeight = 0;
        copyContext = context;
    }
    if(sceneCopyTexture == 0 || width != sceneCopyWidth || height != sceneCopyHeight){
        if(sceneCopyTexture == 0)
            e->glGenTextures(1, &sceneCopyTexture);
        if(sceneCopyFramebuffer == 0)
            e->glGenFramebuffers(1, &sceneCopyFramebuffer);
        e->glActiveTexture(GL_TEXTURE0 + SceneCopyUnit);
        e->glBindTexture(GL_TEXTURE_2D, sceneCopyTexture);
        e->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                        GL_UNSIGNED_BYTE, nullptr);
        e->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        e->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // Refraction near the screen edge reads the last pixel row or column.
        e->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        e->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        e->glGenerateMipmap(GL_TEXTURE_2D);
        e->glActiveTexture(GL_TEXTURE0);
        sceneCopyWidth = width;
        sceneCopyHeight = height;
    }
    GLint drawFramebuffer = 0, readFramebuffer = 0;
    e->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    e->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    // A blit also resolves a multisampled frame.
    e->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneCopyFramebuffer);
    e->glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                              sceneCopyTexture, 0);
    e->glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(drawFramebuffer));
    e->glBlitFramebuffer(viewportRect[0], viewportRect[1], viewportRect[0] + width,
                         viewportRect[1] + height, 0, 0, width, height,
                         GL_COLOR_BUFFER_BIT, GL_NEAREST);
    e->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(drawFramebuffer));
    e->glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(readFramebuffer));
    e->glActiveTexture(GL_TEXTURE0 + SceneCopyUnit);
    e->glBindTexture(GL_TEXTURE_2D, sceneCopyTexture);
    e->glGenerateMipmap(GL_TEXTURE_2D);
    e->glActiveTexture(GL_TEXTURE0);
    return 1.0f + std::floor(std::log2(float(std::max(width, height))));
}

void OpenGL3Renderer::renderPassesMeasured(RenderPass first, RenderPass last){
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context == NULL || samplesPending){
        renderPasses(first, last);
        return;
    }
    QOpenGLExtraFunctions *e = context->extraFunctions();
    if(queryContext != context){
        // A query of another context cannot be used or deleted here.
        samplesQuery = 0;
        queryContext = context;
        lastSamples = -1;
    }
    if(samplesQuery == 0)
        e->glGenQueries(1, &samplesQuery);
    RenderStats::pauseSamples();
    e->glBeginQuery(GL_SAMPLES_PASSED, samplesQuery);
    renderPasses(first, last);
    e->glEndQuery(GL_SAMPLES_PASSED);
    RenderStats::resumeSamples(samplesQuery);
    samplesPending = true;
}

long long OpenGL3Renderer::measuredSamples(){
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(samplesPending && context != NULL && context == queryContext){
        QOpenGLExtraFunctions *e = context->extraFunctions();
        GLuint available = 0;
        e->glGetQueryObjectuiv(samplesQuery, GL_QUERY_RESULT_AVAILABLE, &available);
        if(available){
            GLuint samples = 0;
            e->glGetQueryObjectuiv(samplesQuery, GL_QUERY_RESULT, &samples);
            lastSamples = samples;
            samplesPending = false;
        }
    }
    return lastSamples;
}

void OpenGL3Renderer::beginViewBand(const LayeredView &view, ViewBand band){
    GLUU *gluu = GLUU::get();
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(gluu == NULL || context == NULL || gluu->currentShader == NULL || !view.projection)
        return;
    QOpenGLFunctions *functions = context->functions();
    float projection[16];
    float viewMatrix[16];
    std::copy(view.view, view.view + 16, viewMatrix);
    if(band == BAND_SKY){
        // A mirrored view sees mirrored triangles turn the other way round.
        if(view.mirrorPlane != nullptr)
            functions->glFrontFace(GL_CW);
        view.projection(0.2f, view.sceneFar, projection);
        Mat4::multiply(gluu->fMatrix, projection, viewMatrix);
        view.projection(100.0f, 10000.0f, projection);
        Mat4::multiply(gluu->pMatrix, projection, viewMatrix);
        gluu->setMatrixUniforms();
        gluu->currentShader->setUniformValue(gluu->currentShader->lod, 0.0f);
        setCullView(nullptr);
        return;
    }
    functions->glClear(GL_DEPTH_BUFFER_BIT);
    if(band == BAND_DISTANT){
        if(view.mirrorPlane != nullptr){
            // Below the plane only the water bed would show; clip it just
            // under the surface so banks meet the water without a gap.
            const float *plane = view.mirrorPlane;
            const float clip[4] = {plane[0], plane[1], plane[2], plane[3] + 0.05f};
            std::copy(clip, clip + 4, gluu->clipPlane);
            functions->glEnable(GL_CLIP_DISTANCE0);
        }
        view.projection(600.0f, view.distantFar, projection);
    } else {
        view.projection(0.2f, view.sceneFar, projection);
    }
    Mat4::multiply(gluu->pMatrix, projection, viewMatrix);
    gluu->setMatrixUniforms();
    setCullView(gluu->pMatrix);
    if(band == BAND_SCENE)
        setViewLimits(view.limits);
}

void OpenGL3Renderer::endView(const LayeredView &view){
    setViewLimits(nullptr);
    setCullView(nullptr);
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(view.mirrorPlane == nullptr || context == NULL)
        return;
    QOpenGLFunctions *functions = context->functions();
    functions->glDisable(GL_CLIP_DISTANCE0);
    GLUU *gluu = GLUU::get();
    const float keep[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    std::copy(keep, keep + 4, gluu->clipPlane);
    functions->glFrontFace(GL_CCW);
}

namespace {
QString programName(Renderer::Program program, const QString &main){
    switch(program){
    case Renderer::PROGRAM_SELECTION: return "Selection";
    case Renderer::PROGRAM_SHADOW: return "Shadows";
    case Renderer::PROGRAM_MAIN: break;
    }
    return main;
}
}

bool OpenGL3Renderer::programsReady() const{
    GLUU *gluu = GLUU::get();
    for(Program program : {PROGRAM_MAIN, PROGRAM_SELECTION, PROGRAM_SHADOW})
        if(gluu->shaders.value(programName(program, mainProgramName), nullptr) == nullptr)
            return false;
    return true;
}

void OpenGL3Renderer::useProgram(Program program){
    GLUU *gluu = GLUU::get();
    gluu->currentShader = gluu->shaders[programName(program, mainProgramName)];
    gluu->currentShader->bind();
}

void OpenGL3Renderer::releaseProgram(){
    GLUU *gluu = GLUU::get();
    if(gluu->currentShader != nullptr)
        gluu->currentShader->release();
}

void OpenGL3Renderer::applyFrameUniforms(){
    GLUU::get()->setMatrixUniforms();
}

void OpenGL3Renderer::setFogLod(float lod){
    GLUU *gluu = GLUU::get();
    gluu->currentShader->setUniformValue(gluu->currentShader->lod, lod);
}

void OpenGL3Renderer::createShadowMaps(int nearSize, int farSize){
    // The main program reads the near map on unit 9, the middle on unit 2
    // and the far map on unit 3.
    GLUU *gluu = GLUU::get();
    gluu->makeShadowFramebuffer(shadowFramebuffers[0], shadowTextures[0], nearSize, GL_TEXTURE9);
    gluu->makeShadowFramebuffer(shadowFramebuffers[1], shadowTextures[1], nearSize, GL_TEXTURE2);
    gluu->makeShadowFramebuffer(shadowFramebuffers[2], shadowTextures[2], farSize, GL_TEXTURE3);
}

void OpenGL3Renderer::bindTarget(Target target){
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context == NULL)
        return;
    QOpenGLFunctions *functions = context->functions();
    const unsigned int framebuffer = target == TARGET_VIEW
            ? (viewSurface != nullptr ? viewSurface->defaultFramebufferObject() : 0u)
            : shadowFramebuffers[target - TARGET_SHADOW_NEAR];
    functions->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    functions->glActiveTexture(GL_TEXTURE0);
}

bool OpenGL3Renderer::beginSelection(int width, int height){
    return selectionTarget.begin(width, height);
}

quint32 OpenGL3Renderer::readSelection(int x, int y){
    return selectionTarget.readPixel(x, y);
}

void OpenGL3Renderer::endSelection(){
    selectionTarget.end();
}

void OpenGL3Renderer::resetState(){
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context == NULL)
        return;
    QOpenGLFunctions *functions = context->functions();
    functions->glEnable(GL_DEPTH_TEST);
    functions->glDepthMask(GL_TRUE);
    functions->glDepthFunc(GL_LESS);
    functions->glEnable(GL_CULL_FACE);
    functions->glCullFace(GL_BACK);
    functions->glEnable(GL_BLEND);
    functions->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    functions->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    functions->glDisable(GL_SCISSOR_TEST);
    functions->glLineWidth(Game::oglDefaultLineWidth);
}

bool OpenGL3Renderer::setBlending(bool enabled){
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context == NULL)
        return false;
    QOpenGLFunctions *functions = context->functions();
    const bool previous = functions->glIsEnabled(GL_BLEND);
    if(enabled)
        functions->glEnable(GL_BLEND);
    else
        functions->glDisable(GL_BLEND);
    return previous;
}

void OpenGL3Renderer::clear(bool color, bool depth, const float *clearColor){
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context == NULL)
        return;
    QOpenGLFunctions *functions = context->functions();
    if(clearColor != nullptr)
        functions->glClearColor(clearColor[0], clearColor[1], clearColor[2], 1.0f);
    const GLbitfield bits = (color ? GL_COLOR_BUFFER_BIT : 0) | (depth ? GL_DEPTH_BUFFER_BIT : 0);
    if(bits != 0)
        functions->glClear(bits);
}

void OpenGL3Renderer::setViewport(int x, int y, int width, int height){
    if(QOpenGLContext *context = QOpenGLContext::currentContext())
        context->functions()->glViewport(x, y, width, height);
}

void OpenGL3Renderer::viewport(int *rectangle) const{
    std::fill(rectangle, rectangle + 4, 0);
    if(QOpenGLContext *context = QOpenGLContext::currentContext())
        context->functions()->glGetIntegerv(GL_VIEWPORT, rectangle);
}

float OpenGL3Renderer::readDepth(int x, int y){
    float depth = 1.0f;
    if(QOpenGLContext *context = QOpenGLContext::currentContext())
        context->functions()->glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    return depth;
}

void OpenGL3Renderer::readColor(int x, int y, int width, int height, unsigned char *rgba){
    if(QOpenGLContext *context = QOpenGLContext::currentContext())
        context->functions()->glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void OpenGL3Renderer::renderFrame(){
    renderPasses(PASS_SKY, PASS_UI);
    clearQueues();
    Renderer::renderFrame();
}
