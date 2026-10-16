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
#include <qfloat16.h>
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
    bool drawsPhysicalSky() const override;
    void setSky(const Sky &sky) override;
    void setClouds(const Clouds &clouds) override;
    bool beginSkyEnclosure(int size, int map = 0) override;
    void finishSkyEnclosure(bool far, float radius, float metresPerDepth) override;
    void renderEnclosureCasters(float range, int statsSlot, const float *viewProjection,
                                float minRadius = 0.0f) override;
    void setSkyEnclosure(const SkyEnclosure &enclosure) override;
    void setExposureControl(const ExposureControl &control) override;
    bool autoExposureAvailable() const override;
    void setPlainImage(bool plain) override { plainImage = plain; }

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
        // Where the mesh starts in vertexBuffer (meshes share buffers).
        quint32 vertexOffset = 0;
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
        // The sky's pass: the colour attachment alone.
        QRhiTextureRenderTarget *skyTarget = nullptr;
        QRhiRenderPassDescriptor *skyPass = nullptr;
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
    // Targets by Renderer::Target, then the near sky enclosure map,
    // selection, an environment map face, the water reflection, the far sky
    // enclosure map and both seen from below.
    static constexpr int TargetEnclosure = TARGET_SHADOW_FAR + 1;
    static constexpr int TargetSelection = TargetEnclosure + 1;
    static constexpr int TargetEnvironment = TargetSelection + 1;
    static constexpr int TargetReflection = TargetEnvironment + 1;
    static constexpr int TargetEnclosureFar = TargetReflection + 1;
    static constexpr int TargetEnclosureBottom = TargetEnclosureFar + 1;
    static constexpr int TargetEnclosureFarBottom = TargetEnclosureBottom + 1;
    static constexpr int TargetCount = TargetEnclosureFarBottom + 1;
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
    // The frame and its depth for the water pass (the colour in sceneCopy);
    // returns the colour mipmap levels, 0 when water does not see the bed
    // (secondary views, core.rendering.water.depth off).
    float copyFrameForWater();
    bool copyDepthForWater();
    void releaseWaterDepth();
    float waterSceneLevels = 0.0f;
    struct WaterDepth {
        QRhiTexture *copy = nullptr;
        QRhiTextureRenderTarget *target = nullptr;
        QRhiRenderPassDescriptor *pass = nullptr;
        QRhiSampler *sampler = nullptr;
        QRhiShaderResourceBindings *bindings = nullptr;
        QRhiTexture *boundDepth = nullptr;
        QRhiGraphicsPipeline *pipeline = nullptr;
    } waterDepth;
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
    // Auto exposure (task 29; RhiImage.cpp). The view's log-average
    // luminance is measured (64 x 64, then 1 x 1) and read back a frame or
    // two later; the exposure follows it around the exposure of an ordinary
    // view (1 / sceneLight).
    ExposureControl exposureControl;
    bool plainImage = false;
    // The air between the camera and surfaces with a physical sky
    // (Haze.glsl): the horizon's colour by the angle from the sun's
    // direction across the ground (display, greyed under an overcast as the
    // fog colour is), drawn again when the sun or the camera's height moves.
    struct Haze {
        float clear[8][3] = {};
        float ring[8][3] = {};
        float sun[2] = {1.0f, 0.0f};
        float drawnSun[3] = {};
        float drawnHeight = -1e9f;
        bool valid = false;
    } haze;
    // The lit shaders' hazeParams: camera altitude (km), the weather's haze
    // (1/km), on.
    void hazeParameters(float *out) const;
    bool exposureActive() const;
    // The scale of gamma-space light colours (sceneLight^(1/2.2)), 1 off.
    float sceneScaleDisplay() const;
    // The exposure (linear) the final pass applies besides the setting.
    float autoExposure() const;
    // The signals' glow scale: signals are to be seen, so with auto
    // exposure they keep the look the lamps' scale gives them, with the
    // exposure offset and a boost (task 29).
    float signalScale() const;
    void measureLuminance(QRhiCommandBuffer *cb);
    void releaseLuminance();
    struct Luminance {
        QRhiTexture *grid = nullptr;     // 64 x 64 log2 luminance
        QRhiTexture *mean = nullptr;     // 1 x 1
        QRhiTextureRenderTarget *gridTarget = nullptr;
        QRhiTextureRenderTarget *meanTarget = nullptr;
        QRhiRenderPassDescriptor *gridPass = nullptr;
        QRhiRenderPassDescriptor *meanPass = nullptr;
        QRhiShaderResourceBindings *gridBindings = nullptr;
        QRhiShaderResourceBindings *meanBindings = nullptr;
        QRhiGraphicsPipeline *gridPipeline = nullptr;
        QRhiGraphicsPipeline *meanPipeline = nullptr;
        QRhiSampler *linear = nullptr;
        QRhiSampler *nearest = nullptr;
        const QRhiTexture *boundView = nullptr;
        const QRhiTexture *boundDepth = nullptr;
        QRhiReadbackResult readback;
        bool pending = false;
        // The last measurement (log2 luminance, scene light).
        float measuredLog2 = 0.0f;
        bool measured = false;
        // The adapted correction (stops) around an ordinary view's exposure.
        float correction = 0.0f;
        // Measured frames to take the target at once after a snap (the
        // read-back lags).
        int snapFrames = 0;
        // Bumped by a snap: read-backs issued before it are dropped.
        int generation = 0;
        int pendingGeneration = 0;
    } luminance;
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
        // The air's haze, as the lit shaders' (hazeParams).
        float haze[4] = {};
        QRhiViewport viewport;
        QRhiBuffer *instances = nullptr;
        quint32 capacity = 0;
        quint32 count = 0;
        QRhiBuffer *uniforms = nullptr;
        QRhiShaderResourceBindings *bindings = nullptr;
        QRhiGraphicsPipeline *pipeline = nullptr;
        QRhiRenderPassDescriptor *pipelinePass = nullptr;
        // Gathered for the sky enclosure (task 27), and the map bound.
        bool enclosed = false;
        QRhiTexture *enclosureTexture = nullptr;
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
public:
    static QList<QByteArray> skyShaders();
private:
    // Physical sky (RhiSky.cpp).
    struct SkyResources {
        QRhiTexture *transmittance = nullptr;
        QRhiTexture *multiple = nullptr;
        QRhiTexture *view = nullptr;
        QRhiTexture::Format tableFormat = QRhiTexture::RGBA16F;
        QRhiTextureRenderTarget *viewTarget = nullptr;
        QRhiRenderPassDescriptor *viewPass = nullptr;
        QRhiBuffer *viewUniforms = nullptr;
        QRhiSampler *linear = nullptr;
        QRhiShaderResourceBindings *viewBindings = nullptr;
        QRhiGraphicsPipeline *viewPipeline = nullptr;
        // One per sky pass in a frame (a frame may draw several cube faces):
        // a dynamic buffer holds its last values for the whole frame.
        struct Use {
            QRhiBuffer *uniforms = nullptr;
            QRhiShaderResourceBindings *bindings = nullptr;
        };
        std::vector<Use> uses;
        size_t usesThisFrame = 0;
        std::vector<std::pair<QRhiRenderPassDescriptor *, QRhiGraphicsPipeline *>> pipelines;
        bool tablesUploaded = false;
        bool viewRendered = false;
    } skyResources;
    float skyInverse[TargetCount][16] = {};
    // Projection x camera rotation (the clouds' reprojection).
    float skyForward[TargetCount][16] = {};
    // The main view's distant band far plane (m).
    float distantFar = 10000.0f;
    bool skyInverseValid[TargetCount] = {};
    bool skyFor(int which) const;
    bool createSkyResources();
    void releaseSkyResources();
    void setSkyInverse(const float *projection, const float *viewMatrix);
    void drawSky(QRhiCommandBuffer *cb, Attachments &attachments, QRhiResourceUpdateBatch *&updates);
public:
    static QList<QByteArray> cloudShaders();
private:
    // Volumetric clouds (RhiClouds.cpp).
    Clouds clouds;
    bool cloudsApplied = false;
    // A view's own clouds: the march, the image built from it over frames
    // and the composite into the view. The main view's are part of
    // CloudResources; the water reflection and each environment cube face
    // have their own.
    struct CloudView {
        // The march (one texel per update block) and the cloud image.
        QSize size;
        QSize imageSize;
        QRhiBuffer *uniforms = nullptr;
        QRhiBuffer *updateUniforms = nullptr;
        quint32 updateStride = 0;
        QRhiTexture *march = nullptr;
        // The finished cloud image (the history alternates): the composite
        // and the light shafts read it.
        QRhiTexture *image = nullptr;
        // Where the clouds of each pixel are (front, mean distance, layer,
        // each times their cover, and the cover): from the march, kept with
        // the image's history; the composite tests the scene's depth against
        // it, the history follows each pixel's own cloud.
        QRhiTexture *marchDepth = nullptr;
        QRhiTexture *historyDepth[2] = {};
        QRhiTexture *imageDepth = nullptr;
        QRhiTextureRenderTarget *marchTarget = nullptr;
        QRhiRenderPassDescriptor *marchPass = nullptr;
        QRhiShaderResourceBindings *marchBindings = nullptr;
        QRhiGraphicsPipeline *marchPipeline = nullptr;
        // The march splitting long steps in deep layers (storms), used while
        // deep is set (the main view, such layers in the weather): its
        // larger loop slows every view a little.
        QRhiGraphicsPipeline *marchDeepPipeline = nullptr;
        bool deep = false;
        QRhiTexture *history[2] = {};
        QRhiTextureRenderTarget *historyTargets[2] = {};
        QRhiRenderPassDescriptor *historyPasses[2] = {};
        QRhiShaderResourceBindings *temporalBindings[2] = {};
        QRhiGraphicsPipeline *temporalPipeline = nullptr;
        int current = 0;
        // The update pass (pixel of each block) the next run starts with.
        int nextPass = 0;
        bool historyValid = false;
        float previous[16] = {};
        bool previousValid = false;
        double lastEast = 0.0;
        double lastNorth = 0.0;
        // The layers' drift at the last frame (m east, north).
        float lastDrift[3][2] = {};
        bool driftValid = false;
        QRhiTexture *compositeColor = nullptr;
        QRhiTextureRenderTarget *compositeTarget = nullptr;
        QRhiRenderPassDescriptor *compositePass = nullptr;
        QRhiShaderResourceBindings *compositeBindings[2] = {};
        QRhiGraphicsPipeline *compositePipeline = nullptr;
        // Off where another view's pipelines are used (the cube's faces
        // share face 0's).
        bool ownsPipelines = true;
    };
    struct CloudResources : CloudView {
        int quality = 0;
        QRhiTexture *shape = nullptr;
        QRhiTexture *detail = nullptr;
        QRhiTexture *curl = nullptr;
        QRhiResourceUpdateBatch *pendingUploads = nullptr;
        QRhiTexture *weather = nullptr;
        quint64 weatherRevision = ~0ull;
        QRhiTexture *coverNear = nullptr;
        QRhiTexture *coverFar = nullptr;
        QRhiTextureRenderTarget *coverTargets[2] = {};
        QRhiRenderPassDescriptor *coverPasses[2] = {};
        QRhiBuffer *coverUniforms[2] = {};
        QRhiShaderResourceBindings *coverBindings[2] = {};
        QRhiGraphicsPipeline *coverPipeline = nullptr;
        std::vector<double> coverKey;
        // What the cloud shadow map was last drawn for.
        std::vector<double> shadowKey;
        int frame = 0;
        // The main view's march parameters as last written: the reflection
        // and the cube, drawn before this frame's are, take them with their
        // own views.
        std::vector<float> parameters;
        QRhiSampler *linear = nullptr;
        QRhiSampler *nearest = nullptr;
        QRhiSampler *repeat = nullptr;
        QRhiTexture *shadow = nullptr;
        QRhiTextureRenderTarget *shadowTarget = nullptr;
        QRhiRenderPassDescriptor *shadowPass = nullptr;
        QRhiGraphicsPipeline *shadowPipeline = nullptr;
        bool shadowValid = false;
        // The shadow map's centre per layer (low, mid) on that layer's own
        // ground, its drift taken off (m east, north): the map drifts with
        // the layer between draws.
        double shadowCentre[2][2] = {};
        // The centres moved back to the camera: the map is drawn there
        // before the view reads it.
        bool shadowPlaced = false;
        // The sky's light through the clouds around the camera: what they
        // let through straight down over the shadow map's extent (64 x 64),
        // then their cover and the mean of what they let through, read back
        // after the shadow map is drawn (cloudSkyLight).
        QRhiTexture *skyLight = nullptr;
        QRhiTextureRenderTarget *skyLightTarget = nullptr;
        QRhiRenderPassDescriptor *skyLightPass = nullptr;
        QRhiGraphicsPipeline *skyLightPipeline = nullptr;
        QRhiTexture *skyMean = nullptr;
        QRhiTextureRenderTarget *skyMeanTarget = nullptr;
        QRhiRenderPassDescriptor *skyMeanPass = nullptr;
        QRhiShaderResourceBindings *skyMeanBindings = nullptr;
        QRhiGraphicsPipeline *skyMeanPipeline = nullptr;
        QRhiReadbackResult skyReadback;
        bool skyWanted = false;
        bool skyPending = false;
        bool skyValid = false;
        float skyCover = 0.0f;
        float skyThrough = 1.0f;
    } cloudResources;
    // The share of the clear sky's light (linear) the ground gets with the
    // clouds overhead: the open sky, the light through the clouds, and the
    // sun's light their sides scatter down into a broken field; 1 without
    // clouds. The ambient takes it.
    float cloudSkyLight() const;
    bool cloudsPrepared = false;
    // The clouds in the water reflection: their own image at a quarter of
    // the main view's pixels (the reflection has a quarter of the view's).
    CloudView reflectionClouds;
    bool reflectionCloudsApplied = false;
    // The environment cube's clouds, per face (task 28): a preset below
    // Low, as the cube is read blurred.
    CloudView environmentClouds[6];
    int environmentFace = -1;
    bool environmentCloudsApplied = false;
    // Each target's scene band projection scale (x, y), for the clouds.
    float bandScale[TargetCount][2] = {};
    bool createCloudView(CloudView &v, const QSize &viewSize, int pixels, QRhiTexture *color,
                         QRhiTexture *glow, QRhiTexture *depth, int colorLayer = 0,
                         const CloudView *share = nullptr);
    void releaseCloudView(CloudView &v);
    void releaseEnvironmentClouds();
    void writeCloudUpdates(CloudView &v, QRhiResourceUpdateBatch *updates);
    void runCloudView(CloudView &v, QRhiCommandBuffer *cb, const QSize &viewSize, QRhiResourceUpdateBatch *updates,
                      int passes);
    // The clouds of the reflection or a cube face, after its opaque passes.
    void applySecondaryClouds();
    void applyViewClouds(CloudView &v, int target, const Attachments &attachments, int colorLayer, int pixels,
                         int steps, int lightSteps, int passes, const CloudView *share);
    // The sky enclosure maps (task 27): near, and far behind it; each also
    // seen from below.
    Attachments enclosureMap;
    Attachments enclosureFarMap;
    Attachments enclosureBottomMap;
    Attachments enclosureFarBottomMap;
    SkyEnclosure enclosure;
    bool enclosureDrawn = false;
    bool enclosureFarDrawn = false;
    // How open the sky is around each point of the near and the far map.
    struct EnclosureOpenness {
        QRhiTexture *texture = nullptr;
        QRhiTextureRenderTarget *target = nullptr;
        QRhiRenderPassDescriptor *pass = nullptr;
        QRhiBuffer *uniforms = nullptr;
        QRhiShaderResourceBindings *bindings = nullptr;
        const QRhiTexture *boundTop = nullptr;
        const QRhiTexture *boundBottom = nullptr;
        bool valid = false;
    } enclosureOpen[2];
    QRhiGraphicsPipeline *opennessPipeline = nullptr;
    bool enclosureTarget(int which) const;
    void releaseEnclosureOpenness();
    void recordCasters(int statsSlot);
    bool cloudShadowPlane(float &altitude) const;
    // The cloud shadow map's extent (km) and texels per side.
    static constexpr float CloudShadowExtent = 14.0f;
    static constexpr int CloudShadowSize = 512;
    // A layer's (0 low, 1 mid) cloud shadow map centre now: its offset from
    // the camera (m east, north) and its place in the view's space (x, z).
    void cloudShadowOffset(int layer, double &east, double &north) const;
    void cloudShadowCentre(int layer, float &x, float &z) const;
    // Moves a layer's map back near the camera when it has drifted too far,
    // before the frame's draws take its place.
    void placeCloudShadow();
    // The planet radius of the cloud shell (km): smaller than the Earth's,
    // so the clouds reach the horizon within the weather around the camera.
    float cloudShellRadius() const;
    // Extra samples a march ray of so many steps may take in deep layers
    // (none without such layers).
    float deepSamples(int steps) const;
    // The clouds' light (task 31): towards it (normalized) and its light
    // relative to the sun's. The sun, also under the horizon while it still
    // reaches the clouds; then the moon, fading in.
    void cloudLight(float *direction, float *colour) const;
    // The layers' moves since the view's last frame (km east, north: low and
    // mid; high) and the low layer's middle height (km), for its history;
    // the cloud shell's radius (km).
    void cloudMotion(CloudView &v, float *motion, float *motionHigh);
    void writeCloudShadowUniforms(RhiProgram *program);
    void prepareClouds(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *&updates);
public:
    static QList<QByteArray> lightShaftShaders();
private:
    // Screen-space light shafts (RhiLightShafts.cpp).
    struct LightShafts {
        QSize size;
        QRhiTexture *viewColor = nullptr;
        QRhiTexture *cloudTexture = nullptr;
        QRhiTexture *white = nullptr;
        QRhiResourceUpdateBatch *whiteUpload = nullptr;
        QRhiTexture *mask = nullptr;
        QRhiTexture *blurred = nullptr;
        QRhiTextureRenderTarget *maskTarget = nullptr;
        QRhiTextureRenderTarget *blurTarget = nullptr;
        QRhiTextureRenderTarget *addTarget = nullptr;
        QRhiRenderPassDescriptor *maskPass = nullptr;
        QRhiRenderPassDescriptor *blurPass = nullptr;
        QRhiRenderPassDescriptor *addPass = nullptr;
        QRhiShaderResourceBindings *maskBindings = nullptr;
        QRhiShaderResourceBindings *blurBindings = nullptr;
        QRhiShaderResourceBindings *addBindings = nullptr;
        QRhiGraphicsPipeline *maskPipeline = nullptr;
        QRhiGraphicsPipeline *blurPipeline = nullptr;
        QRhiGraphicsPipeline *addPipeline = nullptr;
        QRhiBuffer *uniforms = nullptr;
        QRhiSampler *linear = nullptr;
        // Volumetric (step 8).
        QRhiBuffer *volumeUniforms = nullptr;
        QRhiShaderResourceBindings *volumeBindings = nullptr;
        QRhiShaderResourceBindings *volumeAddBindings = nullptr;
        QRhiGraphicsPipeline *volumePipeline = nullptr;
        QRhiGraphicsPipeline *volumeAddPipeline = nullptr;
        const QRhiTexture *volumeCloudMap = nullptr;
        const QRhiTexture *volumeSunMap = nullptr;
        const QRhiTexture *volumeShadeMap = nullptr;
    } lightShafts;
    bool shaftsApplied = false;
public:
    // The fog's passes (RhiFog.cpp), for tests: the injection and the
    // integration (compute), the sky's (fragment).
    static QList<QByteArray> fogShaders();
    void setFog(const Fog &fog) override;
private:
    // Fog lying on the ground (task 33, RhiFog.cpp). A volume of cells over
    // the main view (froxels): x and y by the screen, slices by the distance,
    // spaced evenly in its logarithm from 1 m to the far end. Each frame,
    // before the view's geometry, a compute pass fills every cell with the
    // fog's density there (the weather's fog where the floor map pools it)
    // and the light it scatters (the sun through the shadow maps and the
    // cloud shadow map, the sky), blended with last frame's cells moved with
    // the camera; a second sums each column front to back into the light
    // added and the share let through. The lit shaders read it at their
    // fragments (Haze.glsl), the sky and the clouds behind it a pass after
    // the clouds' composite. Vulkan and Direct3D 11 (compute); off elsewhere.
    Fog fog;
    struct FogResources {
        int quality = 0;
        QSize viewSize;
        int width = 0, height = 0, slices = 0;
        // The cells of this frame and the last (alternating), the sums.
        QRhiTexture *scatter[2] = {};
        QRhiTexture *integrated = nullptr;
        QRhiTexture *floor = nullptr;
        quint64 floorRevision = ~0ull;
        // The highest fog top on the map, for the map and depth it was found for.
        float highestTop = 0.0f;
        quint64 topRevision = ~0ull;
        float topDepth = -1.0f;
        float topPooling = -1.0f;
        QRhiBuffer *uniforms = nullptr;
        QRhiSampler *linear = nullptr;
        QRhiShaderResourceBindings *injectBindings[2] = {};
        QRhiShaderResourceBindings *integrateBindings[2] = {};
        QRhiComputePipeline *inject = nullptr;
        QRhiComputePipeline *integrate = nullptr;
        // The maps the injection's bindings hold (shadow maps, the cloud
        // shadow map, the terrain's shade, the lamps' and the signals' grids
        // come and go).
        const QRhiTexture *boundMaps[11] = {};
        int current = 0;
        bool historyValid = false;
        float previousForward[16] = {};
        float previousCamera[3] = {};
        // The scale of the light in the cells last frame (0: none yet).
        float cellScale = 0.0f;
        // How far the fog had drifted last frame (world metres east, north).
        double previousDrift[2] = {0.0, 0.0};
        unsigned frame = 0;
        // The sky and the clouds behind the fog.
        QRhiBuffer *skyUniforms = nullptr;
        QRhiTextureRenderTarget *skyTarget = nullptr;
        QRhiRenderPassDescriptor *skyPass = nullptr;
        QRhiShaderResourceBindings *skyBindings = nullptr;
        QRhiGraphicsPipeline *skyPipeline = nullptr;
        const QRhiTexture *skyViewColor = nullptr;
        // This frame's volume is filled; the sky has its fog; the volume
        // has been filled once (until then the lit shaders take none).
        bool ready = false;
        bool skyApplied = false;
        bool filled = false;
        bool unsupported = false;
    } fogResources;
    // A volume letting everything through, where a lit shader has no fog.
    QRhiTexture *dummy3D = nullptr;
    // Whether the main view has fog this frame.
    bool fogWanted() const;
    bool createFog(int quality, const QSize &viewSize);
    void releaseFog();
    // The volume, before the main view's geometry, which reads it.
    void prepareFog(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *&updates);
    // The sky and the clouds behind the fog, after the clouds' composite.
    void applySkyFog();
    // The lit shaders' fog uniforms (Haze.glsl).
    void writeFogUniforms(RhiProgram *program);
    // The volume's sums of light to the scene's.
    float fogLightScale() const;
    // The view's directions to the volume's x and y (0-1, its y sign in).
    void fogForward(float *out) const;
    // The patches' place (the floor map's corner in world metres, wrapped;
    // patchiness) and drift (wrapped), for the shaders' noise.
    void fogPatchPlace(float patches[4], float drift[4]) const;
    // The volume's slices: nearest and farthest distance (m).
    static constexpr float FogNear = 1.0f;
    static constexpr float FogFar = 10000.0f;
public:
    void setTerrainShade(const TerrainShade &shade) override;
private:
    // The terrain's shade of a low sun or moon past the shadow maps: its map
    // as a texture of the tops (half floats from the map's reference
    // height), read by the lit shaders (ShadowSampling.glsl), the volumetric
    // light shafts and the fog's light.
    TerrainShade terrainShade;
    struct ShadeResources {
        QRhiTexture *texture = nullptr;
        quint64 revision = ~0ull;
        float reference = 0.0f;
        bool filled = false;
    } shadeResources;
    // A changed map, uploaded in the frame's first pass, before anything
    // reads it.
    void prepareTerrainShade(QRhiResourceUpdateBatch *updates);
    void releaseTerrainShade();
    bool terrainShadeReady() const;
    // The map's place in the view: its north-west corner (x, z), size (m),
    // the view's y less heights above sea level; on, the tops' reference
    // height, 1 / the light's fading band (m), 0.
    void terrainShadePlace(float place[4], float params[4]) const;
    bool createLightShafts(QRhiTexture *cloudTexture);
    void applyLightShafts();
    void applyVolumetricShafts(const QVector3D &sunDirection, float elevation);
    void releaseLightShafts();
    bool cloudsWanted() const;
    bool createCloudVolumes();
    bool createCloudResources(const QSize &viewSize, int quality);
    void applyClouds();
    void releaseCloudBuffers();
    // The maps the views share (weather, cover, shadow, the sky's light).
    bool createSharedCloudMaps();
    // The main view's own cloud resources, for a new view target: the shared
    // maps and what they hold stay.
    void releaseMainCloudView();
    void releaseCloudResources();
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
    // A grid's lights, cells and indices into textures (grown to its needs)
    // in a batch; false when they cannot be made.
    bool uploadLightGrid(const LightGrid &grid, QRhiTexture *&data, QRhiTexture *&cells, QRhiTexture *&indices,
                         QRhiResourceUpdateBatch *batch);
    // The signals' lights for the fog (task 33): their emitters (the glow
    // splats'), binned as the lamps are; the lit shaders do not read them.
    LightGrid signalGrid;
    quint64 signalHash = 0;
    QRhiTexture *signalData = nullptr;
    QRhiTexture *signalCells = nullptr;
    QRhiTexture *signalIndices = nullptr;
    void prepareSignalGrid(QRhiResourceUpdateBatch *batch);
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
    // The wave cascades of the water program (WaterWaves), an RGBA16F
    // array updated once a frame.
    QRhiTexture *waterWaves();
    QRhiTexture *waterWaveArray = nullptr;
    quint64 waterWaveSerial = 0;
    std::vector<qfloat16> waterWaveTexels;
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
