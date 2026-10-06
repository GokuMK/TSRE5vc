/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RHIRENDERER_H
#define RHIRENDERER_H

#include <QHash>
#include <QVector>
#include <memory>
#include <unordered_map>
#include <vector>
#include <rhi/qrhi.h>
#include <tsre/renderer/QueueRenderer.h>

class GLUU;
class RhiContext;
class RhiRenderSurface;
struct RhiProgram;

// The QRhi renderer: draws the queue with the converted shaders330
// programs. Draws are recorded per target and executed in one render pass
// when the target ends (QRhi cannot change uniforms inside a pass); each
// draw copies its program's uniform block into a per-frame buffer and binds
// it with a dynamic offset, as the OpenGL renderer sets uniforms.
class RhiRenderer : public QueueRenderer {
public:
    explicit RhiRenderer(RhiContext *context);
    ~RhiRenderer() override;

    void renderPasses(RenderPass first, RenderPass last) override;
    void renderPassesRetained(RenderPass first, RenderPass last) override;
    void renderPassesMeasured(RenderPass first, RenderPass last) override;
    long long measuredSamples() override { return -1; }
    void beginViewBand(const LayeredView &view, ViewBand band) override;
    void endView(const LayeredView &view) override;
    bool programsReady() const override;
    void useProgram(Program program) override;
    void releaseProgram() override {}
    void applyFrameUniforms() override;
    void setFogLod(float lod) override;
    void createShadowMaps(int nearSize, int farSize) override;
    void bindTarget(Target target) override;
    void resetState() override {}
    bool setBlending(bool enabled) override;
    void clear(bool color, bool depth, const float *clearColor = nullptr) override;
    void setViewport(int x, int y, int width, int height) override;
    void viewport(int *rectangle) const override;
    float readDepth(int x, int y) override;
    void readColor(int x, int y, int width, int height, unsigned char *rgba) override;
    bool beginSelection(int width, int height) override;
    quint32 readSelection(int x, int y) override;
    void endSelection() override;
    void renderShadowCasters(float range, int statsSlot,
                             const float *viewProjection = nullptr) override;
    void renderFrame() override;
    void resetFrame() override;

protected:
    bool instanceRowsFit(int, int) override { return true; }

private:
    // Fixed-function state of a draw: one pipeline per distinct key.
    struct PipelineKey {
        const RhiProgram *program = nullptr;
        QRhiRenderPassDescriptor *pass = nullptr;
        quint8 topology = 0;
        quint8 layout = 0;
        quint8 format = 0;
        bool blend = true;
        bool depthWrite = true;
        bool decal = false;
        bool cullBack = true;
        bool frontCw = false;
        bool wireframe = false;
        quint8 lineWidth = 1;
        bool operator==(const PipelineKey &other) const;
    };
    struct PipelineKeyHash { size_t operator()(const PipelineKey &key) const; };
    // Textures and samplers of a draw: one resource set per distinct key.
    struct BindingKey {
        const RhiProgram *program = nullptr;
        QRhiBuffer *uniforms = nullptr;
        QRhiBuffer *terrainPatches = nullptr;
        std::vector<QRhiTexture *> textures;
        std::vector<QRhiSampler *> samplers;
        bool operator==(const BindingKey &other) const;
    };
    struct BindingKeyHash { size_t operator()(const BindingKey &key) const; };
    struct DrawCommand {
        QRhiGraphicsPipeline *pipeline = nullptr;
        QRhiShaderResourceBindings *bindings = nullptr;
        quint32 uniformOffset = 0;
        QRhiBuffer *vertexBuffer = nullptr;
        QRhiBuffer *instanceBuffer = nullptr;
        quint32 instanceOffset = 0;
        QRhiBuffer *indexBuffer = nullptr;
        quint32 indexOffset = 0;
        QRhiCommandBuffer::IndexFormat indexFormat = QRhiCommandBuffer::IndexUInt16;
        quint32 count = 0;
        quint32 instances = 1;
        quint32 first = 0;
        qint32 baseVertex = 0;
        QRhiViewport viewport;
    };
    // Textures a target draws into, with a render target for each
    // combination of clears at the start of a pass (index: colour 1, depth
    // 2); pipelines are made against the one that keeps both.
    struct Attachments {
        QRhiTexture *color = nullptr;
        QRhiTexture *depth = nullptr;
        QRhiTextureRenderTarget *targets[4] = {};
        QRhiRenderPassDescriptor *passes[4] = {};
        QSize size;
        bool valid() const { return targets[0] != nullptr; }
        QRhiRenderPassDescriptor *pipelinePass() const { return passes[0]; }
    };
    // A target the renderer draws into: the view (offscreen colour and
    // depth, copied to the surface's frame at the end), a shadow map or the
    // selection ids. Draws wait here until the target is flushed.
    struct TargetState {
        Attachments *attachments = nullptr;
        bool clearColor = false;
        bool clearDepth = false;
        QColor color = Qt::black;
        std::vector<DrawCommand> draws;
        // Pipeline and resource-set keys of the draws, resolved when the
        // pass runs (the per-frame buffers are known then).
        std::vector<PipelineKey> keys;
        std::vector<BindingKey> bindings;
    };
    // Targets by Renderer::Target, then the selection target.
    static constexpr int TargetSelection = TARGET_SHADOW_FAR + 1;
    static constexpr int TargetCount = TargetSelection + 1;
    // Per-frame storage appended to by draws and uploaded per pass.
    struct Arena {
        QRhiBuffer *buffer = nullptr;
        std::vector<char> data;
        quint32 uploaded = 0;
    };

    void beginFrameIfNeeded();
    bool createAttachments(Attachments &attachments, QRhiTexture::Format colorFormat,
                           const QSize &size, QRhiTexture::Flags colorFlags);
    void releaseAttachments(Attachments &attachments);
    bool ensureViewTarget(const QSize &size);
    // Whether the current target can be drawn into this frame.
    bool targetReady();
    TargetState &target();
    void flushTarget();
    void present();
    void drawPasses(RenderPass first, RenderPass last, bool consume);
    void recordInstances(const std::vector<DrawInstance> &instances, int pass, bool grouped);
    void recordDraw(RenderItem *item, const float *const *matrices, int count,
                    quint32 selectionId, int pass, int category);
    RhiProgram *programFor(const RenderItem *item, int pass);
    RhiProgram *variant(int index);
    void writeFrameUniforms(RhiProgram *program);
    void writeItemUniforms(RhiProgram *program, RenderItem *item, quint32 selectionId);
    QRhiGraphicsPipeline *pipeline(const PipelineKey &key);
    QRhiShaderResourceBindings *bindings(const BindingKey &key);
    QRhiSampler *sampler(bool mipmaps, bool clamp, bool nearest = false);
    // The wave map of the water program (WaterNormalMap), made once.
    QRhiTexture *waterWaves();
    unsigned int waterWaveHandle = 0;
    QRhiTexture *packetTexture(const RenderItem *item, bool &mipmapped);
    quint32 appendUniforms(const RhiProgram *program);
    quint32 appendInstances(const float *const *matrices, int count);
    QRhiBuffer *uploadArena(Arena &arena, QRhiBuffer::UsageFlags usage,
                            QRhiResourceUpdateBatch *batch);
    void releaseResources();

    RhiContext *context;
    QRhi *rhi;
    GLUU *gluu;
    RhiRenderSurface *surface() const;

    // Program variants: index = base program * VariantsPerProgram + variant.
    std::vector<std::unique_ptr<RhiProgram>> programs;
    Program baseProgram = PROGRAM_MAIN;
    RhiProgram *currentVariant = nullptr;
    float fogLod = 0.0f;
    bool fogLodOverride = false;
    bool blending = true;
    std::unordered_map<PipelineKey, QRhiGraphicsPipeline *, PipelineKeyHash> pipelines;
    std::unordered_map<BindingKey, QRhiShaderResourceBindings *, BindingKeyHash> resourceSets;
    QHash<int, QRhiSampler *> samplers;
    // Stand-ins for textures a program declares but a draw does not use.
    QRhiTexture *dummy2D = nullptr;
    QRhiTexture *dummyArray = nullptr;
    QRhiTexture *dummyCube = nullptr;
    QRhiTexture *dummyDepth = nullptr;
    QRhiSampler *shadowSampler = nullptr;
    QRhiBuffer *dummyTerrainPatches = nullptr;

    // Offscreen view and the shader copying it to the surface's frame.
    Attachments view;
    QRhiGraphicsPipeline *presentPipeline = nullptr;
    QRhiShaderResourceBindings *presentBindings = nullptr;
    QRhiSampler *presentSampler = nullptr;
    QRhiRenderPassDescriptor *presentPassKey = nullptr;

    // Selection ids, read back once per selection pass.
    Attachments selection;
    QByteArray selectionIds;
    bool selectionRead = false;
    int selectionViewport[4] = {0, 0, 0, 0};

    TargetState targets[TargetCount];
    int currentTarget = TARGET_VIEW;
    QColor nextClearColor = Qt::black;
    QRhiViewport currentViewport;
    int viewportRect[4] = {0, 0, 0, 0};
    float depthRange[2] = {0.0f, 1.0f};
    bool frontCw = false;

    Arena uniformArena;
    Arena instanceArena;
    quint32 uniformStride = 256;
    QRhiResourceUpdateBatch *frameBatch = nullptr;
    quint64 frameSerial = 0;
};

#endif
