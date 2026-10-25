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
struct UiDrawList;

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
    // Which shadow casters a draw takes (task 37): all, or only those of
    // static or of moving objects (RenderQueue::setShadowDynamic); and of
    // which set: all, all but vegetation (the main maps) or vegetation alone
    // (its own maps, RenderItem::Material::vegetation).
    enum ShadowCasters {CASTERS_ALL = 0, CASTERS_STATIC, CASTERS_DYNAMIC};
    enum ShadowSet {SET_ALL = 0, SET_MAIN, SET_VEGETATION};
    // Draws queued shadow casters with the bound shader without consuming
    // them, skipping instances whose origin lies farther than range from the
    // view position on the ground plane, and, with a light view-projection,
    // instances outside it. statsSlot labels the draws.
    virtual void renderShadowCasters(float range, int statsSlot,
                                     const float *viewProjection = nullptr,
                                     ShadowCasters casters = CASTERS_ALL,
                                     ShadowSet set = SET_ALL) = 0;
    // Vegetation's own shadow maps (task 37), when the renderer has them:
    // TARGET_VEGETATION_*, drawn with the main maps' matrices and sampled
    // beside them, vegetation itself taking them at a share of their
    // strength. Without them vegetation casts into the main maps. When the
    // renderer has them they must be drawn every frame the main ones are,
    // cleared at least: everything that reads the main maps reads them.
    virtual bool vegetationShadows() const { return false; }
    // Cached static shadow maps (task 37), when the renderer has them.
    // beginShadowCache binds the cache of a map (0 near, 1 middle, 2 far;
    // 3 to 5 the vegetation maps'), cleared and sized as the map, for its
    // static casters. copyShadowCache
    // makes the bound shadow map start as a copy of that cache instead of
    // cleared; the casters drawn into it after go on top. False when there
    // is no such cache (a new renderer): draw every caster then.
    virtual bool beginShadowCache(int map) { (void)map; return false; }
    virtual bool copyShadowCache(int map) { (void)map; return false; }
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
        // Extinction of the low haze (1/m), from the weather's visibility
        // scaled by the fog density (the aerial haze, task 32).
        float haze = 1.5e-4f;
        // The weather's own haze (1/m), unscaled: what the volumetric light
        // shafts scatter in (scaled with the rest, they all but vanished).
        float shaftHaze = 1.5e-4f;
        // The ground under the camera (m, as cameraHeight): the haze lying
        // on it, where the near light shafts are.
        float groundHeight = 0.0f;
        // The share of the sun the clouds around the camera block on
        // average (0-1).
        float overcast = 0.0f;
        // Towards the moon, and its light relative to the sun's (linear,
        // per colour; 0 without moonlight): it lights the clouds at night
        // (task 31).
        float moon[3] = {0.0f, 1.0f, 0.0f};
        float moonLight[3] = {0.0f, 0.0f, 0.0f};
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
        // On the same texels, RGBA: the low and mid layers' density (1 their
        // type's usual), precipitation (mm/h, the area's) and its snow share.
        // None: densities 1, dry.
        std::shared_ptr<const std::vector<float>> extra;
        // Changes whenever the layers do.
        quint64 revision = 0;
        // How far each layer has drifted with its wind (east, north).
        float drift[3][2] = {};
        // The drift the layers above were sampled with: the fields move with
        // their layer's wind, so the renderer reads them shifted by drift -
        // built instead of drawing them again each frame.
        float built[3][2] = {};
        quint32 seed = 0;
        // The low and mid layers' extent (km): bottom and top; the deepest
        // of them anywhere (km, its top above its base); their highest type
        // anywhere (storm clouds from 0.8); the heaviest rain or snow (mm/h).
        float bottom = 0.0f;
        float top = 0.0f;
        float deepest = 0.0f;
        float stormiest = 0.0f;
        float rainiest = 0.0f;
        // The camera.
        double cameraEast = 0.0;
        double cameraNorth = 0.0;
        // The pixels of each block of the image marched a frame (of 16): more
        // in a fast timelapse, where the clouds change much between frames.
        int passes = 1;
    };
    virtual void setClouds(const Clouds &clouds) {}

    // Fog lying on the ground (task 33): the weather's fog where the floor
    // map puts it. The QRhi renderer draws it with the physical sky.
    struct Fog {
        int quality = 0;              // core.rendering.mist: 0 off, 1 low, 2 high
        float amount = 0.0f;          // 0-1
        float visibility = 40000.0f;  // m, in the fog at the valley floor
        float depth = 0.0f;           // m, its top above the floor
        float base = 0.0f;            // m above the floor it has lifted to
        float patchiness = 0.0f;
        float pooling = 1.0f;           // 1 pooling in valleys, 0 lying evenly on all ground
        // The floor map: size x size cells, RGBA (floor, the share of the
        // depth pooled above it, ground, moisture; heights in the view's
        // y), row 0 at the north edge; its north-west corner (world metres
        // east, north) and cell size; a new revision when it changed.
        const std::vector<float> *map = nullptr;
        int size = 0;
        double west = 0.0;
        double north = 0.0;
        float cell = 64.0f;
        quint64 revision = 0;
        // The camera (world metres east, north).
        double cameraEast = 0.0;
        double cameraNorth = 0.0;
        // How far the fog has drifted with the wind near the ground (world
        // metres east, north): its patches move.
        double driftEast = 0.0;
        double driftNorth = 0.0;
        // Rain or snow at the camera (the area's, mm/h; its snow share) and
        // the low clouds' base there (m above sea level): the volume carries
        // it near the view, the clouds' rain far off (RhiRenderer). Set with
        // the fog off too; the volume then runs for the rain alone.
        float rain = 0.0f;
        float snow = 0.0f;
        float rainBase = 1000.0f;
        // At the camera itself (under a shower's cell or not, mm/h) and the
        // low wind (m/s east, north): the drops falling near it.
        float rainHere = 0.0f;
        double rainWindEast = 0.0;
        double rainWindNorth = 0.0;
    };
    virtual void setFog(const Fog &fog) {}

    // The terrain's shade of a low sun or moon past the shadow maps
    // (TerrainShade): size x size cells, the height (m above sea level)
    // under which the ground towards the light hides it, row 0 at the north
    // edge; its north-west corner (world metres east, north), cell size, a
    // height near its ground (the tops are stored as half floats from it); a
    // new revision when it changed. No map: no shade.
    struct TerrainShade {
        const std::vector<float> *map = nullptr;
        int size = 0;
        double west = 0.0;
        double north = 0.0;
        float cell = 64.0f;
        float reference = 0.0f;
        quint64 revision = 0;
        // The camera (world metres east, north).
        double cameraEast = 0.0;
        double cameraNorth = 0.0;
    };
    virtual void setTerrainShade(const TerrainShade &shade) {}

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
    // Frames shown as drawn (the map's palette): no tone curve, exposure or
    // bloom.
    virtual void setPlainImage(bool plain) {}
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

    // Render targets: the view's frame, one of the three shadow maps, or one
    // of vegetation's (vegetationShadows()).
    enum Target {TARGET_VIEW = 0, TARGET_SHADOW_NEAR, TARGET_SHADOW_MID, TARGET_SHADOW_FAR,
                 TARGET_VEGETATION_NEAR, TARGET_VEGETATION_MID, TARGET_VEGETATION_FAR};
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
    // The frame's 2D user interface (RmlUi's documents) over the finished
    // view, after tone mapping, once the frame is rendered. The list must
    // stay as it is until the frame is shown; the renderer takes its
    // releases and keeps its own copies of its geometry and textures.
    virtual void drawUi(UiDrawList &list) { (void)list; }
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
