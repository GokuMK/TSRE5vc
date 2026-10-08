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
#include <QVarLengthArray>
#include <QVector>
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>
#include <QMatrix4x4>
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
    void setSceneProjection(const float *projection, float zNear, float zFar) override;
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
    float readDepthLatest(int x, int y) override;
    float gpuFrameMs() const override { return gpuMs; }
    void readColor(int x, int y, int width, int height, unsigned char *rgba) override;
    bool beginSelection(int width, int height) override;
    quint32 readSelection(int x, int y) override;
    void endSelection() override;
    EnvironmentMap::Storage *createEnvironmentStorage() override;
    PlanarReflection::Storage *createReflectionStorage() override;
    void renderShadowCasters(float range, int statsSlot,
                             const float *viewProjection = nullptr) override;
    void renderFrame() override;
    void resetFrame() override;

    // The environment map and water reflection storage (RhiReflections.cpp)
    // drive these.
    bool createEnvironment(int faceSize, int levels);
    void beginEnvironmentFace(int face, const float *clearColor);
    void endEnvironmentFaces(const QVector<int> &prefilterFaces);
    void uploadEnvironment(const QByteArray *faces);
    void bindEnvironment(bool prefiltered);
    void drawEnvironmentPreview(int x, int y, int cellSize);
    void unbindEnvironment() { environment.sampled = nullptr; }
    bool environmentReady(int faceSize) const;
    void releaseEnvironment();
    bool createReflection(int width, int height);
    bool reflectionReady(int width, int height) const;
    void beginReflection(const float *clearColor);
    void endReflection();
    void bindReflection(bool bound);
    void releaseReflection();

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
        // Colour attachments of the pass (the view has two with ambient
        // occlusion on).
        quint8 colorCount = 1;
        bool operator==(const PipelineKey &other) const;
    };
    struct PipelineKeyHash { size_t operator()(const PipelineKey &key) const; };
    // Textures and samplers of a draw: one resource set per distinct key.
    struct BindingKey {
        const RhiProgram *program = nullptr;
        QRhiBuffer *uniforms = nullptr;
        // Inline: a key is made for every draw, and heap vectors cost two
        // allocations each (about a sixth of the frame's CPU time).
        QVarLengthArray<QRhiTexture *, 24> textures;
        QVarLengthArray<QRhiSampler *, 24> samplers;
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
        // Further colour outputs of the view, owned with color: the ambient
        // light share for ambient occlusion, and the emissive glow for bloom.
        QRhiTexture *color2 = nullptr;
        QRhiTexture *color3 = nullptr;
        QRhiTexture *depth = nullptr;
        // The colour subresource drawn into (cube face or array layer, and
        // mip level), and whether the attachments own their textures.
        int colorLayer = 0;
        int colorLevel = 0;
        bool ownsColor = true;
        bool ownsDepth = true;
        // Rows in OpenGL's order (first row at the bottom of the view), for
        // textures sampled by direction or projection rather than by screen
        // position: shadow maps and cube faces.
        bool openGlRows = false;
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
    // Targets by Renderer::Target, then selection, an environment map face
    // and the water reflection.
    static constexpr int TargetSelection = TARGET_SHADOW_FAR + 1;
    static constexpr int TargetEnvironment = TargetSelection + 1;
    static constexpr int TargetReflection = TargetEnvironment + 1;
    static constexpr int TargetCount = TargetReflection + 1;
    // Per-frame storage appended to by draws and uploaded per pass.
    struct Arena {
        QRhiBuffer *buffer = nullptr;
        std::vector<char> data;
        quint32 uploaded = 0;
        // The last block appended, which an equal block reuses.
        quint32 lastOffset = 0;
        quint32 lastSize = 0;
    };

    void beginFrameIfNeeded();
    // Colour and depth, or depth only (a shadow map) when colorFormat is
    // UnknownFormat.
    bool createAttachments(Attachments &attachments, QRhiTexture::Format colorFormat,
                           const QSize &size, QRhiTexture::Flags colorFlags,
                           QRhiTexture::Format secondFormat = QRhiTexture::UnknownFormat,
                           QRhiTexture::Format thirdFormat = QRhiTexture::UnknownFormat);
    // The render targets of attachments whose textures are set.
    bool buildTargets(Attachments &attachments);
    // Whether the current target keeps OpenGL's row order.
    bool openGlRowTarget() const;
    // The matrix taking OpenGL clip space to this backend's for a target
    // in OpenGL's row order: such textures are sampled as under OpenGL.
    QMatrix4x4 openGlRowCorrection() const;
    // The clip space correction of the current target.
    QMatrix4x4 targetCorrection() const;
    // Switches to an offscreen target, cleared to clearColor, with a
    // viewport covering it.
    void beginOffscreen(int target, Attachments *attachments, const float *clearColor);
    // Mipmaps and convolution the environment map still needs, run when a
    // frame is recording.
    void runEnvironmentWork();
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
    // Address modes: 0 repeat, 1 clamp to edge, 2 mirrored repeat.
    QRhiSampler *wrapSampler(bool mipmaps, int addressU, int addressV, bool nearest = false);
    // A library texture as a QRhi texture, uploading it when loaded; null
    // until then or when disabled.
    QRhiTexture *libraryTexture(int textureId, bool &mipmapped);
    // Copies the view for transmissive surfaces before the transmission
    // pass; returns the copy's mipmap levels, 0 without a copy.
    float copyFrameForTransmission();
    QRhiTexture *sceneCopy = nullptr;
    float sceneCopyLevels = 0.0f;
    // HDR and bloom (task 24, RhiImage.cpp). The view draws into floats
    // when a tone curve is chosen; present() applies exposure, adds bloom
    // and maps the result to the frame. Bloom is made only from the glow
    // the lit shaders write (emitted light), never from bright pixels.
    QRhiTexture::Format viewFormat() const;
    int toneMapping() const;
    bool bloomEnabled() const;
    // Runs the bloom chain from the view's glow; the texture present()
    // adds, or null without bloom.
    QRhiTexture *renderBloom(QRhiCommandBuffer *cb);
    void releaseBloom();
    struct Bloom {
        QSize viewSize;
        std::vector<QRhiTexture *> levels;
        // Per level: a target cleared by the downsample, and one the
        // upsample adds to.
        std::vector<QRhiTextureRenderTarget *> downTargets;
        std::vector<QRhiTextureRenderTarget *> upTargets;
        std::vector<QRhiRenderPassDescriptor *> passes;
        std::vector<QRhiShaderResourceBindings *> downBindings;
        std::vector<QRhiShaderResourceBindings *> upBindings;
        QRhiGraphicsPipeline *down = nullptr;
        QRhiGraphicsPipeline *up = nullptr;
        QRhiSampler *linear = nullptr;
    } bloom;
    // Glow of emitters drawn at their projected position (task 24): each
    // emissive emitter adds its glow to the main view's, spread over the four
    // pixels around it. A lens about a pixel across is drawn in one frame
    // and missed in the next; its own glow blinked, and bloom made the blink
    // a halo. Gathered with the lights, drawn in the view's pass with its
    // depth.
    struct GlowSplats {
        std::vector<LightGrid::Light> emitters;
        bool gathered = false;
        bool pending = false;
        // The main view's scene band: projection, fog and viewport.
        float projection[16] = {};
        float fogMatrix[16] = {};
        float camera[3] = {};
        float focal = 1.0f;
        float lod = 1.0f;
        float fogDensity = 1.0f;
        QRhiViewport viewport;
        QRhiBuffer *instances = nullptr;
        quint32 capacity = 0;
        quint32 count = 0;
        QRhiBuffer *uniforms = nullptr;
        QRhiShaderResourceBindings *bindings = nullptr;
        QRhiGraphicsPipeline *pipeline = nullptr;
        QRhiRenderPassDescriptor *pipelinePass = nullptr;
    } glowSplats;
    void gatherGlowSplats();
    void prepareGlowSplats();
    bool uploadGlowSplats(QRhiResourceUpdateBatch *batch, QRhiRenderPassDescriptor *pass);
    void drawGlowSplats(QRhiCommandBuffer *cb);
    void releaseGlowSplats();
public:
    // The fragment shaders of present and bloom, for tests.
    static QList<QByteArray> imageShaders();
    // The glow splats' vertex and fragment shaders, for tests.
    static QList<QByteArray> glowSplatShaders();
private:
    QRhiBuffer *presentUniforms = nullptr;
    QRhiSampler *presentLinear = nullptr;
    QRhiTexture *presentBloomTexture = nullptr;
    QRhiTexture *dummyBlack = nullptr;
    // Clears the view's glow to black: a pass clears every colour output
    // with one colour, and the background colour must not glow.
    QRhiTextureRenderTarget *glowClear = nullptr;
    QRhiRenderPassDescriptor *glowClearPass = nullptr;
    void releaseGlowClear();
    // Ambient occlusion (task 23, RhiAmbientOcclusion.cpp): GTAO from the
    // view's depth after the opaque passes, taken off the ambient light
    // share the lit shaders write as the view's second colour output.
    // Quality 0 is off.
    int ambientOcclusionQuality() const;
public:
    // The fragment shaders of the occlusion, blur and composite passes, for
    // tests.
    static QList<QByteArray> ambientOcclusionShaders();
private:
    void applyAmbientOcclusion();
    void releaseAmbientOcclusion();
    struct AmbientOcclusion {
        int quality = 0;
        QSize size;
        QRhiTexture *raw = nullptr;
        QRhiTexture *blurred = nullptr;
        QRhiTextureRenderTarget *rawTarget = nullptr;
        QRhiTextureRenderTarget *blurTarget = nullptr;
        QRhiTextureRenderTarget *compositeTarget = nullptr;
        QRhiRenderPassDescriptor *rawPass = nullptr;
        QRhiRenderPassDescriptor *blurPass = nullptr;
        QRhiRenderPassDescriptor *compositePass = nullptr;
        QRhiBuffer *uniforms = nullptr;
        QRhiSampler *nearest = nullptr;
        QRhiSampler *linear = nullptr;
        QRhiShaderResourceBindings *aoBindings = nullptr;
        QRhiShaderResourceBindings *blurBindings = nullptr;
        QRhiShaderResourceBindings *compositeBindings = nullptr;
        QRhiGraphicsPipeline *aoPipeline = nullptr;
        QRhiGraphicsPipeline *blurPipeline = nullptr;
        QRhiGraphicsPipeline *compositePipeline = nullptr;
    } ambientOcclusion;
    bool createAmbientOcclusion(int quality);
    // Whether this frame's main view has had its occlusion.
    bool ambientOcclusionApplied = false;
    // The main view's scene band: projection terms (x and y scale, near,
    // far) and its slice of the depth range, for reading its depth back.
    bool sceneProjectionValid = false;
    float sceneProjection[4] = {1.0f, 1.0f, 0.2f, 1000.0f};
    float sceneDepthRange[2] = {0.0f, 1.0f};
    // Local lights of the frame (task 21), binned once before the first
    // view draws and read by every view.
    void prepareLights();
    void releaseLights();
    void writeLightUniforms(RhiProgram *program);
    LightGrid lightGrid;
    std::vector<LightGrid::Light> frameLights;
    bool lightsPrepared = false;
    // Resources made during a frame (pipelines, resource sets, textures,
    // mesh buffers) and the time they took; a frame that spent long on them
    // (the first after a jump) is logged.
    struct Creation {
        int pipelines = 0, bindings = 0, textures = 0;
        qint64 pipelineNs = 0, bindingNs = 0, textureNs = 0, meshNs = 0;
    } creation;
    void logCreation();
    quint64 lightsHash = 0;
    QRhiTexture *lightData = nullptr;
    QRhiTexture *lightCells = nullptr;
    QRhiTexture *lightIndices = nullptr;
    // Reads a texture back now: ends the current pass and waits for the
    // GPU. Rows come top first unless the backend's framebuffer has y up.
    QByteArray readNow(QRhiTexture *texture, QSize *size = nullptr);
    // Depth probe: samples the view's depth at one pixel into a 1 x 1 float
    // target, which is read back.
    struct DepthProbe {
        QRhiTexture *result = nullptr;
        QRhiTextureRenderTarget *target = nullptr;
        QRhiRenderPassDescriptor *pass = nullptr;
        QRhiBuffer *uniforms = nullptr;
        QRhiSampler *sampler = nullptr;
        QRhiShaderResourceBindings *bindings = nullptr;
        QRhiTexture *boundDepth = nullptr;
        QRhiGraphicsPipeline *pipeline = nullptr;
    } depthProbe;
    // The asynchronous read of readDepthLatest, completed with its frame.
    struct LatestDepth {
        QRhiReadbackResult result;
        bool pending = false;
        bool valid = false;
        float depth = 1.0f;
    };
    std::unique_ptr<LatestDepth> latestDepth;
    // Draws the view's depth at a pixel into the probe's 1x1 texture; false
    // when there is no view to read.
    bool recordDepthProbe(int x, int y);
    void releaseDepthProbe();
    // The wave map of the water program (WaterNormalMap), made once.
    QRhiTexture *waterWaves();
    unsigned int waterWaveHandle = 0;
    QRhiTexture *packetTexture(const RenderItem *item, bool &mipmapped);
    quint32 appendUniforms(const RhiProgram *program);
    quint32 appendInstances(const float *const *matrices, int count);
    QRhiBuffer *uploadArena(Arena &arena, QRhiBuffer::UsageFlags usage,
                            QRhiResourceUpdateBatch *batch);
    void releaseResources();

    // Fragment program of the main, terrain and unlit variants
    // (StandardFast for manual testing, as OpenGL3Renderer::mainProgramName).
    const char *mainFragment = "StandardFog";
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
    // By wrapSampler's 6-bit key; looked up for every texture of every draw.
    std::array<QRhiSampler *, 64> samplers{};
    // Stand-ins for textures a program declares but a draw does not use.
    QRhiTexture *dummy2D = nullptr;
    QRhiTexture *dummyArray = nullptr;
    QRhiTexture *dummyCube = nullptr;
    QRhiTexture *dummyDepth = nullptr;
    QRhiSampler *shadowSampler = nullptr;

    // Offscreen view and the shader copying it to the surface's frame.
    Attachments view;
    QRhiGraphicsPipeline *presentPipeline = nullptr;
    QRhiShaderResourceBindings *presentBindings = nullptr;
    QRhiSampler *presentSampler = nullptr;
    QRhiRenderPassDescriptor *presentPassKey = nullptr;
    // The surface's overlay, composed over the view by present().
    QRhiTexture *overlayTexture = nullptr;
    QRhiShaderResourceBindings *overlayBindings = nullptr;
    QRhiGraphicsPipeline *overlayPipeline = nullptr;
    QRhiRenderPassDescriptor *overlayPassKey = nullptr;

    // Shadow maps: near, middle and far.
    Attachments shadowMaps[3];
    // Environment map: the cube drawn into, its convolved copy the scene
    // samples, and the prefilter pass.
    struct Environment {
        QRhiTexture *cube = nullptr;
        QRhiTexture *prefiltered = nullptr;
        QRhiTexture *depth = nullptr;
        Attachments faces[6];
        // Prefilter targets, face * levels + level.
        std::vector<Attachments> prefilterTargets;
        int size = 0;
        int levels = 0;
        QRhiTexture *sampled = nullptr;
        bool mipmapsPending = false;
        QVector<int> prefilterPending;
        std::unique_ptr<RhiProgram> program;
        QRhiBuffer *uniforms = nullptr;
        quint32 uniformStride = 0;
        QRhiShaderResourceBindings *bindings = nullptr;
        QRhiGraphicsPipeline *pipeline = nullptr;
        QRhiSampler *sampler = nullptr;
        // The preview cross of the raw faces, drawn over the view.
        std::unique_ptr<RhiProgram> previewProgram;
        QRhiBuffer *previewUniforms = nullptr;
        QRhiShaderResourceBindings *previewBindings = nullptr;
        QRhiGraphicsPipeline *previewPipeline = nullptr;
        QRhiRenderPassDescriptor *previewPass = nullptr;
    } environment;
    // Water reflection, sampled mipmapped by screen position.
    Attachments reflection;
    QRhiTexture *reflectionSampled = nullptr;
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
    // Draws pick their instances with firstInstance (QRhi::BaseInstance),
    // so consecutive draws keep one vertex input.
    bool baseInstance = false;
    // QRhi's timestamp of the last completed frame (gpuFrameMs).
    float gpuMs = -1.0f;
    QRhiResourceUpdateBatch *frameBatch = nullptr;
    quint64 frameSerial = 0;
};

#endif
