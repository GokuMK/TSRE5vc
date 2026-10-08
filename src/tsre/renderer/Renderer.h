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
