/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */


#ifndef OPENGL3RENDERER_H
#define OPENGL3RENDERER_H

#include <tsre/renderer/QueueRenderer.h>
#include <tsre/renderer/SelectionRenderer.h>
#include <tsre/renderer/WaterNormalMap.h>
#include <unordered_map>
#include <vector>

class QOpenGLFunctions;
class QOpenGLContext;
class GLUU;
class Shader;

class OpenGL3Renderer : public QueueRenderer {
public:
    OpenGL3Renderer();
    virtual ~OpenGL3Renderer();
    void renderPasses(RenderPass first, RenderPass last) override;
    void renderPassesRetained(RenderPass first, RenderPass last) override;
    void renderPassesMeasured(RenderPass first, RenderPass last) override;
    long long measuredSamples() override;
    void beginViewBand(const LayeredView &view, ViewBand band) override;
    void endView(const LayeredView &view) override;
    bool programsReady() const override;
    void useProgram(Program program) override;
    void releaseProgram() override;
    void applyFrameUniforms() override;
    void setFogLod(float lod) override;
    void createShadowMaps(int nearSize, int farSize) override;
    void bindTarget(Target target) override;
    bool beginSelection(int width, int height) override;
    quint32 readSelection(int x, int y) override;
    void endSelection() override;
    // GLUU program the main program is (StandardFast for manual testing).
    QString mainProgramName = "StandardFog";
    void resetState() override;
    bool setBlending(bool enabled) override;
    void clear(bool color, bool depth, const float *clearColor = nullptr) override;
    void setViewport(int x, int y, int width, int height) override;
    void viewport(int *rectangle) const override;
    float readDepth(int x, int y) override;
    void readColor(int x, int y, int width, int height, unsigned char *rgba) override;
    void renderShadowCasters(float range, int statsSlot,
                             const float *viewProjection = nullptr) override;
    void renderFrame() override;

private:
    void drawOrdered(GLUU *gluu, Shader *base, const std::vector<DrawInstance> &instances,
                     int pass);
    void drawGrouped(GLUU *gluu, Shader *base, const std::vector<DrawInstance> &instances,
                     int pass);
    void drawPasses(RenderPass first, RenderPass last, bool consume);
    // Uploads instanceUpload to the instance buffer texture on unit 8.
    bool uploadInstances();
    bool instanceRowsFit(int first, int count) override;
    void releaseInstanceBuffer();

    // Vertex arrays of renderer-owned meshes in this renderer's context.
    // Mesh buffers are shared between contexts; vertex arrays are not.
    struct MeshArray {
        quint32 generation = 0;
        quint64 stamp = 0;
        unsigned int vao = 0;
    };
    // Binds a packet's mesh for the lifetime of the binding.
    struct MeshBinding;
    // Binds the packet's mesh, uploading pending data; false when there is
    // nothing to draw.
    bool bindMesh(const RenderItem *item);
    void unbindMesh(const RenderItem *item);
    // Deletes released mesh buffers and this renderer's vertex arrays of
    // released meshes.
    void collectMeshes();
    void releaseMeshArrays();
    std::unordered_map<quint32, MeshArray> meshArrays;
    // Sampler objects overriding the wrap mode of material maps that do not
    // repeat (PBR packets), by wrap modes, and the one bound on each unit.
    void applyWrapSamplers(const RenderItem *item);
    void bindWrapSampler(int unit, quint32 wrap);
    void releaseWrapSamplers();
    void applyWaterState(GLUU *gluu, const RenderItem *item);
    WaterNormalMap waterNormals;
    // Copies the frame drawn so far for the transmission pass; returns its
    // mipmap levels, 0 when transmissive surfaces see the environment.
    float copyFrameForTransmission(GLUU *gluu, Shader *base);
    unsigned int sceneCopyTexture = 0;
    unsigned int sceneCopyFramebuffer = 0;
    int sceneCopyWidth = 0;
    int sceneCopyHeight = 0;
    QOpenGLContext *copyContext = nullptr;
    // Samples query of renderPassesMeasured(), in queryContext.
    unsigned int samplesQuery = 0;
    QOpenGLContext *queryContext = nullptr;
    bool samplesPending = false;
    long long lastSamples = -1;
    std::unordered_map<quint32, unsigned int> wrapSamplers;
    // Indexed by texture unit; covers every unit a PBR map uses.
    static constexpr int SamplerUnits = 32;
    unsigned int boundSamplers[SamplerUnits] = {};
    QOpenGLContext *samplerContext = nullptr;
    QOpenGLContext *meshContext = nullptr;
    quint64 sweptReleases = 0;

    unsigned int instanceBuffer = 0;
    unsigned int instanceTexture = 0;
    QOpenGLContext *instanceContext = nullptr;
    int maxInstanceTexels = 0;

    QOpenGLFunctions *f = nullptr;
    // Shadow map framebuffers (near, middle, far) and their depth textures.
    unsigned int shadowFramebuffers[3] = {0, 0, 0};
    unsigned int shadowTextures[3] = {0, 0, 0};
    SelectionRenderer selectionTarget;
};

#endif /* OPENGL3RENDERER_H */
