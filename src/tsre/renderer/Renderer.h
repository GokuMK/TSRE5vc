/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RENDERER_H
#define RENDERER_H

#include <tsre/renderer/EnvironmentMap.h>
#include <tsre/renderer/PlanarReflection.h>
#include <tsre/renderer/RenderQueue.h>
#include <functional>
#include <memory>
#include <vector>

class RenderSurface;

// A render queue that also draws: the frame owner gathers producers into it,
// then draws the queued work pass by pass. Producers only see RenderQueue.
class Renderer : public RenderQueue {
public:
    // Passes draw in this order. The renderer routes each submission from the
    // current layer, the packet surface and the submission order. In the
    // scene layer terrain packets go to PASS_TERRAIN, ordered work to
    // PASS_OPAQUE before its batched packets, and grouped packets by surface.
    // Every other layer maps to its own pass. The frame sets the projection
    // and clears depth between sky, distant and scene passes.
    enum RenderPass {
        PASS_SKY = 0,
        PASS_DISTANT,
        PASS_TERRAIN,
        PASS_OPAQUE,
        PASS_ALPHA_TEST,
        PASS_BLENDED,
        PASS_OVERLAY,
        PASS_WATER,
        // glTF materials with transmission, drawn over a copy of the frame
        // taken when the pass starts.
        PASS_TRANSMISSION,
        PASS_UI,
        PASS_COUNT
    };

    Renderer();
    virtual ~Renderer();

    // Camera position in the submission space, for back-to-front sorting and
    // shadow caster range.
    void setViewPosition(const float *position);
    // View-projection the following passes are culled against: an instance
    // whose bounds lie outside it is skipped (clip position = viewProjection
    // * transform * msMatrix * vertex). Null draws everything; a new frame
    // starts without culling. Packets without bounds are never culled.
    void setCullView(const float *viewProjection);
    // Limits of a secondary view such as an environment map face: instances
    // other than terrain whose bounds lie farther than maxDistance from the
    // view position, or subtend less than minAngularRadius (radius over
    // distance), are skipped. Null removes them; a new frame starts without.
    struct ViewLimits {
        float maxDistance = 0.0f;
        float minAngularRadius = 0.0f;
    };
    void setViewLimits(const ViewLimits *limits);
    // Draws queued shadow casters with the bound shader without consuming
    // them, skipping instances whose origin lies farther than range from the
    // view position on the ground plane, and, with a light view-projection,
    // instances outside it. statsSlot labels the draws.
    virtual void renderShadowCasters(float range, int statsSlot,
                                     const float *viewProjection = nullptr) = 0;
    // Draws and consumes queued work of passes first..last; later passes stay
    // queued. Use it where direct drawing must happen between passes.
    virtual void renderPasses(RenderPass first, RenderPass last) = 0;
    // Draws queued work of passes first..last without consuming it, for an
    // extra view of the same frame (environment map faces).
    virtual void renderPassesRetained(RenderPass first, RenderPass last) = 0;
    // Draws passes like renderPasses and counts the samples they write; the
    // count is read with measuredSamples() in a later frame. While a count
    // is still pending, the passes are drawn without counting.
    virtual void renderPassesMeasured(RenderPass first, RenderPass last) = 0;
    // Samples counted by the last measured passes; -1 until a count is in.
    virtual long long measuredSamples() = 0;

    // A view drawn in three bands with their own depth ranges, as the
    // editors draw the scene: sky, distant terrain, then terrain, objects
    // and optionally water. Each band after the sky starts with a cleared
    // depth buffer.
    enum ViewBand {BAND_SKY = 0, BAND_DISTANT, BAND_SCENE};
    struct LayeredView {
        // View matrix (column-major).
        const float *view = nullptr;
        // Projection for a near and far plane.
        std::function<void(float nearPlane, float farPlane, float *projection)> projection;
        float sceneFar = 0.0f;
        float distantFar = 0.0f;
        // Limits of objects in the scene band; null draws all.
        const ViewLimits *limits = nullptr;
        // Mirror plane (n . p + d = 0): the view is seen mirrored in it,
        // triangles turn the other way round, and the distant and scene
        // bands are clipped just under it. Null for a plain view.
        const float *mirrorPlane = nullptr;
        bool water = true;
    };
    // Sets up one band of a view: clears depth (except for the sky), sets
    // the projection, culling, limits and mirroring. The band's passes are
    // then drawn by the caller.
    virtual void beginViewBand(const LayeredView &view, ViewBand band) = 0;
    // Ends a view: culling, limits and mirroring off.
    virtual void endView(const LayeredView &view) = 0;
    // For a view drawn without bands (Shape Viewer): its projection (without
    // the camera) and depth planes, which screen-space effects such as
    // ambient occlusion need to rebuild positions from depth.
    virtual void setSceneProjection(const float *projection, float zNear, float zFar) {}

    // The sky of the following frames (task 28). With physical set, a
    // renderer that can (QRhi) draws the atmosphere behind each view in
    // place of the sky band's background, and the caller leaves the skydome
    // out.
    struct Sky {
        bool physical = false;
        // Towards the sun (world: x east, y up, z south).
        float sun[3] = {0.0f, 1.0f, 0.0f};
        // The camera above sea level (m).
        float cameraHeight = 0.0f;
        // Display colour the sky keeps at least (night).
        float night[3] = {0.0f, 0.0f, 0.0f};
        // Extinction of the low haze (1/m), from the weather's visibility:
        // what volumetric light shafts scatter in.
        float haze = 1.5e-4f;
        // The share of the sun the clouds around the camera block on
        // average (0-1).
        float overcast = 0.0f;
    };
    virtual void setSky(const Sky &sky) { this->sky = sky; }
    // Whether the sky set is drawn here as the atmosphere.
    virtual bool drawsPhysicalSky() const { return false; }

    // The clouds of the following frames (task 28), with a physical sky.
    // World metres: east, north (as the weather's).
    struct Clouds {
        // 0 none, 1-3 quality.
        int quality = 0;
        // Each frame complete, without jitter or history (captures).
        bool still = false;
        // The weather around the camera: size x size texels per layer (low,
        // mid, high, one after the other), rows from north to south, RGBA:
        // coverage, type, base (km), top (km).
        int size = 0;
        double west = 0.0;
        double north = 0.0;
        float texel = 0.0f;
        std::shared_ptr<const std::vector<float>> layers;
        // Changes whenever the layers do.
        quint64 revision = 0;
        // How far each layer has drifted with its wind (east, north).
        float drift[3][2] = {};
        quint32 seed = 0;
        // The low and mid layers' extent (km): bottom and top.
        float bottom = 0.0f;
        float top = 0.0f;
        // The camera.
        double cameraEast = 0.0;
        double cameraNorth = 0.0;
    };
    virtual void setClouds(const Clouds &clouds) {}

    // The sky enclosure (task 27): a depth map from straight above around
    // the camera, of terrain and opaque surfaces. A surface with something
    // over it (terrain over a tunnel, a bridge deck, a roof) is enclosed:
    // its ambient light keeps only ambientFloor of the sky's, and lamps and
    // their glow keep their night strength there by day.
    struct SkyEnclosure {
        bool enabled = false;
        // World (view space of the queue) to the map, OpenGL clip space.
        float viewProjection[16] = {};
        // Metres one unit of the map's depth spans.
        float metresPerDepth = 1.0f;
        // The filter's spacing in map coordinates.
        float filter = 0.0f;
        float ambientFloor = 0.3f;
        // The far map behind it: wider and coarser, of terrain and large
        // objects; the near map fades into it, it into open sky.
        bool farEnabled = false;
        float farViewProjection[16] = {};
        float farFilter = 0.0f;
    };
    // Auto exposure (task 29): with it on, the light of the sun and the sky
    // is scaled by sceneLight (1 by day, Daylight::DayLocalLights at night),
    // lamps keep one scale, and the view's exposure adapts to its measured
    // brightness: it takes the exposure of an ordinary outdoor view as bright
    // as it is (the curve), an ordinary view of the hour 1 / sceneLight.
    struct ExposureControl {
        static constexpr int CurveSize = 24;
        bool enabled = false;
        float sceneLight = 1.0f;
        // log2 of the luminance an ordinary outdoor view has (scene light).
        float referenceLog2 = 0.0f;
        // Daylight::adaptationCurve: luminance (log2, ascending) to exposure
        // (log2).
        float curveLuminance[CurveSize] = {};
        float curveExposure[CurveSize] = {};
        int curvePoints = 0;
        // Stops added to the exposure (Daylight::exposureBias).
        float biasStops = 0.0f;
        // Take the exposure at once (camera jump, a jump of the time of day).
        bool snap = false;
        // Since the last frame.
        float seconds = 0.0f;
    };
    virtual void setExposureControl(const ExposureControl &control) {}
    // Whether the view can hold the light auto exposure needs (QRhi with
    // tone mapping).
    virtual bool autoExposureAvailable() const { return false; }
    // Starts a map (size texels) when the renderer has them: 0 the near map,
    // 1 the far one, 2 and 3 the same seen from below (the lowest surfaces).
    // The shadow program then draws enclosure casters into it, the far maps
    // only those of minRadius and more.
    virtual bool beginSkyEnclosure(int size, int map = 0) { return false; }
    // After a map and its view from below: how open the sky is around each
    // point (radius in map coordinates), for the gradual darkening under
    // cover.
    virtual void finishSkyEnclosure(bool far, float radius, float metresPerDepth) {}
    virtual void renderEnclosureCasters(float range, int statsSlot, const float *viewProjection,
                                        float minRadius = 0.0f) {}
    virtual void setSkyEnclosure(const SkyEnclosure &enclosure) {}
    // Draws all bands of a view without consuming the queue (secondary views
    // such as environment map faces and the water reflection). Transmissive
    // surfaces there see the environment instead of a copy of the view.
    void renderLayeredView(const LayeredView &view);

    // Programs the editors draw with: the main lit program (with its
    // terrain, unlit, PBR and water variants chosen per packet), the integer
    // selection program and the shadow depth program.
    enum Program {PROGRAM_MAIN = 0, PROGRAM_SELECTION, PROGRAM_SHADOW};
    // Whether the programs are built.
    virtual bool programsReady() const = 0;
    // Draws from now on use this program.
    virtual void useProgram(Program program) = 0;
    virtual void releaseProgram() = 0;
    // Takes the frame values held in GLUU (projection, fog and shadow
    // matrices, lights, colours, camera, environment) for the following draws.
    virtual void applyFrameUniforms() = 0;
    // Fog distance of the following draws (applyFrameUniforms sets the
    // object draw distance); 0 draws without fog.
    virtual void setFogLod(float lod) = 0;

    // Render targets: the view's frame, or one of the three shadow maps.
    enum Target {TARGET_VIEW = 0, TARGET_SHADOW_NEAR, TARGET_SHADOW_MID, TARGET_SHADOW_FAR};
    // The surface whose frame TARGET_VIEW is.
    void setSurface(RenderSurface *surface) { viewSurface = surface; }
    // Creates the shadow maps the main program samples: near and middle at
    // nearSize texels, far at farSize.
    virtual void createShadowMaps(int nearSize, int farSize) = 0;
    // Draws from now on go to this target.
    virtual void bindTarget(Target target) = 0;
    // The integer selection target of width x height pixels: beginSelection
    // binds it, cleared to 0 and with a viewport covering it; readSelection
    // reads the id drawn at a pixel (origin bottom-left) after renderFrame;
    // endSelection binds the view again with its previous viewport.
    virtual bool beginSelection(int width, int height) = 0;
    virtual quint32 readSelection(int x, int y) = 0;
    virtual void endSelection() = 0;
    // The backend's storage for an environment map and a water reflection
    // drawn by this renderer; the caller owns it.
    virtual EnvironmentMap::Storage *createEnvironmentStorage() = 0;
    virtual PlanarReflection::Storage *createReflectionStorage() = 0;

    // Backend state every frame starts from: depth test and writes, back-face
    // culling, alpha blending, all colour channels, no scissor, the default
    // line width.
    virtual void resetState() = 0;
    // Turns alpha blending on or off; returns whether it was on.
    virtual bool setBlending(bool enabled) = 0;
    // Clears the bound target's colour (to clearColor, RGB) and/or depth.
    virtual void clear(bool color, bool depth, const float *clearColor = nullptr) = 0;
    virtual void setViewport(int x, int y, int width, int height) = 0;
    // The viewport as x, y, width, height.
    virtual void viewport(int *rectangle) const = 0;
    // Depth (0..1) of the bound target at a pixel, origin bottom-left.
    virtual float readDepth(int x, int y) = 0;
    // As readDepth, without waiting for the GPU: the depth of the last read
    // that completed, which may be a frame or two old (the 3D pointer).
    virtual float readDepthLatest(int x, int y) { return readDepth(x, y); }
    // GPU time in milliseconds of the last frame the GPU completed;
    // negative where the renderer does not measure it.
    virtual float gpuFrameMs() const { return -1.0f; }
    // RGBA bytes of a rectangle of the bound target, rows from the bottom.
    virtual void readColor(int x, int y, int width, int height, unsigned char *rgba) = 0;
    // Bounding spheres (centre x, y, z and radius, in submission space) of
    // the queued instances of a pass that are inside the view-projection
    // (all of them when it is null). Instances without bounds are left out.
    virtual void visibleBounds(RenderPass pass, const float *viewProjection,
                               std::vector<float> &spheres) const = 0;
    // Draws all remaining passes and ends the frame's submissions.
    virtual void renderFrame();
    // Starts a frame: drops queued work and rebalances the transform stack.
    virtual void resetFrame();
    // Increments at every resetFrame() of any renderer.
    static quint64 frameNumber();

    // Deletes a producer-owned packet once no renderer can still draw it.
    static void retirePacket(RenderItem *packet);
    static int pendingRetiredPackets();

protected:
    Sky sky;
    // Borrowed packets queued in all renderers; retirement waits for zero.
    static int queuedPackets;
    static quint64 currentFrame;
    static void releaseRetiredPackets();
    float viewPosition[3] = {0, 0, 0};

    // Normalised clip planes of a view-projection; disabled draws everything.
    struct Frustum {
        float planes[6][4];
        bool enabled = false;
    };
    static Frustum frustumOf(const float *viewProjection);
    // Whether a sphere in submission space can be inside the frustum.
    static bool intersects(const Frustum &frustum, const float *center, float radius);
    Frustum cullFrustum;
    ViewLimits viewLimits;
    bool viewLimitsEnabled = false;
    // Set while a secondary view draws: no frame copy for transmission.
    bool secondaryView = false;
    RenderSurface *viewSurface = nullptr;
};

#endif /* RENDERER_H */
