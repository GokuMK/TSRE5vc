/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// HDR and bloom of the QRhi renderer (task 24). With a tone curve chosen the
// view draws into floats, so lamps and glowing surfaces can exceed white;
// present() applies the exposure, adds bloom and maps the result through the
// curve. Bloom is built only from the glow the lit shaders write (emitted
// light), so sunlit walls, snow and glints never bloom: a downsample chain
// of 13-tap filters, then tent-filtered upsamples added back level by level
// (Jimenez, "Next generation post processing in Call of Duty", 2014).

#include "RhiRenderer.h"
#include "RhiProgram.h"
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <qfloat16.h>
#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/GLUU.h>

namespace {

// Smallest bloom level and the most levels: a wide but bounded halo.
constexpr int MinBloomSide = 8;
constexpr int MaxBloomLevels = 6;

// Glow splats: one emitter per instance, spread around its projected
// position by a tent R pixels wide each way, R its projected radius rounded
// to whole pixels (1 to 16). A tent of whole pixels sums to the same over the
// pixel grid wherever it falls, so the glow moves smoothly and never blinks;
// a lens drawn as pixels does, also a near one: seen nearly edge-on it is a
// sliver a pixel or two high. The total is the glow of the emitter seen as
// a sphere of its area and radiance (task 21): pi I (f/d)^2 in pixels,
// faded by fog. It hands over to the surface's own glow while the emitter's
// radius grows from 16 to 32 pixels (PbrShading.glsl). A lens's emitter (a
// spot: signal and train lamps) glows the way it faces only: by a ramp in
// the cosine off its axis, between its cone's two, as a flat lens's area
// seen (90 degrees: the cosine), none from behind.
constexpr int GlowSplatStride = 16 * sizeof(float);
constexpr int GlowSplatUniformBytes = 256;

const char *GlowSplatVertex = R"(#version 440
layout(location = 0) in vec4 emitter; // position, radius
layout(location = 1) in vec4 power;   // colour times intensity
layout(location = 2) in vec4 axis;    // a lens's: the way it faces, on
layout(location = 3) in vec4 cone;    // cosine of the outer half-angle, 1 / (inner's - outer's)
layout(location = 0) out vec2 offset;
layout(location = 1) out vec3 glow;
layout(location = 2) flat out float tentArea;
layout(std140, binding = 0) uniform Splats {
    mat4 projection;
    mat4 fogMatrix;
    vec4 camera;   // position, focal length in pixels
    vec4 viewport; // width, height, fog lod, fog density
    mat4 enclosure;        // the sky enclosure map (task 27)
    vec4 enclosureParams;  // on, metres per depth unit
    vec4 haze;             // the air's haze (Haze.glsl hazeParams): camera altitude (km), haze (1/km), on
};
layout(binding = 1) uniform sampler2D enclosureMap;

void main() {
    vec2 corner = vec2((gl_VertexIndex & 1) != 0 ? 1.0 : -1.0, (gl_VertexIndex & 2) != 0 ? 1.0 : -1.0);
    vec3 toEmitter = emitter.xyz - camera.xyz;
    float distance = max(length(toEmitter), 0.001);
    float pixels = emitter.w * camera.w / distance;
    float share = 1.0 - smoothstep(16.0, 32.0, pixels);
    float tent = clamp(floor(pixels + 0.5), 1.0, 16.0);
    tentArea = tent * tent;
    vec4 fogPosition = fogMatrix * vec4(emitter.xyz, 1.0);
    float fog = sqrt(fogPosition.x * fogPosition.x + fogPosition.z * fogPosition.z) / (viewport.z * 1.4);
    fog = abs(min(clamp(fog, 0.0, viewport.w), viewport.z));
    // power.a: the daylight scale of the emitter's glow; where the sky is
    // enclosed (a tunnel) it keeps its night strength.
    float day = power.a;
    if (enclosureParams.x > 0.0) {
        vec4 c = enclosure * vec4(emitter.xyz, 1.0);
        vec3 m = c.xyz / c.w * 0.5 + 0.5;
        // As the lit shaders: open outside the map and its depth range,
        // fading to open towards its edge.
        float inside = 1.0 - smoothstep(0.35, 0.48, max(abs(m.x - 0.5), abs(m.y - 0.5)));
        if (m.z > 0.0 && m.z < 1.0 && inside > 0.0) {
            float top = textureLod(enclosureMap, m.xy, 0.0).r;
            day = mix(day, 1.0, smoothstep(1.5, 4.0, (m.z - top) * enclosureParams.y) * inside);
        }
    }
    // Through the air as the lit surfaces (Haze.glsl), or the classic fog.
    vec3 through = vec3(1.0 - fog);
    if (haze.z > 0.5) {
        float h0 = haze.x, h1 = haze.x + toEmitter.y / 1000.0;
        float dh = h1 - h0;
        vec3 scale = vec3(8.0, 1.2, 1.5);
        vec3 mean = abs(dh) < 1e-3 ? exp(-0.5 * (h0 + h1) / scale) : (exp(-h0 / scale) - exp(-h1 / scale)) * scale / dh;
        vec3 sigma = vec3(5.802e-3, 13.558e-3, 33.1e-3) * mean.x + vec3(4.44e-3 * mean.y + haze.y * mean.z);
        through = exp(-sigma * distance / 1000.0);
    }
    float facing = axis.w > 0.5 ? clamp((dot(axis.xyz, -toEmitter / distance) - cone.x) * cone.y, 0.0, 1.0) : 1.0;
    glow = power.rgb * day * (3.14159265 * camera.w * camera.w / (distance * distance)) * share * through * facing;
    offset = corner;
    // Depth at the emitter's front, so its own housing does not hide it.
    vec3 front = emitter.xyz - toEmitter / distance * min(emitter.w, distance * 0.5);
    vec4 clip = projection * vec4(front, 1.0);
    if (share <= 0.0 || facing <= 0.0 || clip.w <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    gl_Position = vec4(clip.xy + corner * tent * 2.0 / viewport.xy * clip.w, clip.zw);
}
)";

const char *GlowSplatFragment = R"(#version 440
layout(location = 0) in vec2 offset;
layout(location = 1) in vec3 glow;
layout(location = 2) flat in float tentArea;
layout(location = 0) out vec4 colorOut;
layout(location = 1) out vec4 ambientOut;
layout(location = 2) out vec4 glowOut;

void main() {
    float weight = max(1.0 - abs(offset.x), 0.0) * max(1.0 - abs(offset.y), 0.0);
    colorOut = vec4(0.0);
    ambientOut = vec4(0.0);
    glowOut = vec4(glow * weight / tentArea, 0.0);
}
)";

const char *BloomDownFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D source;

void main() {
    vec2 t = 1.0 / vec2(textureSize(source, 0));
    vec3 a = texture(source, uv + t * vec2(-2.0, 2.0)).rgb;
    vec3 b = texture(source, uv + t * vec2(0.0, 2.0)).rgb;
    vec3 c = texture(source, uv + t * vec2(2.0, 2.0)).rgb;
    vec3 d = texture(source, uv + t * vec2(-2.0, 0.0)).rgb;
    vec3 e = texture(source, uv).rgb;
    vec3 f = texture(source, uv + t * vec2(2.0, 0.0)).rgb;
    vec3 g = texture(source, uv + t * vec2(-2.0, -2.0)).rgb;
    vec3 h = texture(source, uv + t * vec2(0.0, -2.0)).rgb;
    vec3 i = texture(source, uv + t * vec2(2.0, -2.0)).rgb;
    vec3 j = texture(source, uv + t * vec2(-1.0, 1.0)).rgb;
    vec3 k = texture(source, uv + t * vec2(1.0, 1.0)).rgb;
    vec3 l = texture(source, uv + t * vec2(-1.0, -1.0)).rgb;
    vec3 m = texture(source, uv + t * vec2(1.0, -1.0)).rgb;
    vec3 result = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625
            + (j + k + l + m) * 0.125;
    fragColor = vec4(result, 1.0);
}
)";

const char *BloomUpFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D source;

void main() {
    vec2 t = 1.0 / vec2(textureSize(source, 0));
    vec3 sum = texture(source, uv + t * vec2(-1.0, -1.0)).rgb
            + 2.0 * texture(source, uv + t * vec2(0.0, -1.0)).rgb
            + texture(source, uv + t * vec2(1.0, -1.0)).rgb
            + 2.0 * texture(source, uv + t * vec2(-1.0, 0.0)).rgb
            + 4.0 * texture(source, uv).rgb
            + 2.0 * texture(source, uv + t * vec2(1.0, 0.0)).rgb
            + texture(source, uv + t * vec2(-1.0, 1.0)).rgb
            + 2.0 * texture(source, uv + t * vec2(0.0, 1.0)).rgb
            + texture(source, uv + t * vec2(1.0, 1.0)).rgb;
    fragColor = vec4(sum / 16.0, 1.0);
}
)";

}

// The final pass: the view to the frame. Off (curve 0, exposure 1, no
// bloom) copies the view as before; otherwise the view's display colour is
// taken to linear light, the bloom added, the exposure applied and the curve
// maps it back to display colour.
const char *RhiPresentFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D view;
layout(binding = 1) uniform sampler2D bloomTexture;
layout(std140, binding = 2) uniform Image {
    // Tone curve, exposure factor, bloom strength, warmth.
    vec4 settings;
};

vec3 toLinear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }
vec3 toDisplay(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }

// The display colour as it is up to the knee, then rolled off towards
// white with the hue kept: the usual look, with highlights compressed.
vec3 softShoulder(vec3 display) {
    const float knee = 0.85;
    float peak = max(display.r, max(display.g, display.b));
    if (peak <= knee)
        return display;
    float rolled = knee + (1.0 - knee) * (1.0 - exp(-(peak - knee) / (1.0 - knee)));
    return display * (rolled / peak);
}

// ACES filmic curve (Narkowicz fit), linear in and out.
vec3 aces(vec3 x) {
    x *= 0.6;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// AgX (Sobotka; the minimal fit by Wrensch): linear in, display out.
vec3 agx(vec3 linear) {
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                            0.0784335999999992, 0.878468636469772, 0.0784336,
                            0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                             -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                             -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;
    vec3 c = inset * linear;
    c = clamp(log2(max(c, vec3(1e-10))), minEv, maxEv);
    c = (c - minEv) / (maxEv - minEv);
    vec3 c2 = c * c;
    vec3 c4 = c2 * c2;
    c = 15.5 * c4 * c2 - 40.14 * c4 * c + 31.96 * c4 - 6.868 * c2 * c + 0.4298 * c2
            + 0.1191 * c - 0.00232;
    return outset * c;
}

// Interleaved gradient noise (Jimenez 2014): 0-1, even over the screen.
float gradientNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec3 colour = texture(view, uv).rgb;
    int curve = int(settings.x + 0.5);
    if (curve == 0 && settings.y == 1.0 && settings.z == 0.0 && settings.w == 0.0) {
        fragColor = vec4(colour, 1.0);
        return;
    }
    vec3 linear = (toLinear(colour) + settings.z * texture(bloomTexture, uv).rgb) * settings.y;
    // White balance, as a camera's: at warmth 1 red 15 % up and blue 25 %
    // down, as bright. With the shade in the sky's blue the whole view came
    // out cooler than before (the user, 2026-10-27).
    vec3 balance = vec3(1.0 + 0.15 * settings.w, 1.0, 1.0 - 0.25 * settings.w);
    linear *= balance / dot(balance, vec3(0.2126, 0.7152, 0.0722));
    vec3 display;
    if (curve == 1)
        display = softShoulder(toDisplay(linear));
    else if (curve == 2)
        display = toDisplay(aces(linear));
    else if (curve == 3)
        display = agx(linear);
    else
        display = toDisplay(linear);
    // A step of the 8 bit frame either way (triangular, fixed on the
    // screen): dark gradients, the night sky and fog, banded without it (the
    // user, 2026-10-15).
    vec2 p = gl_FragCoord.xy;
    float dither = gradientNoise(p) + gradientNoise(p + vec2(47.0, 17.0)) - 1.0;
    fragColor = vec4(clamp(display + dither / 255.0, 0.0, 1.0), 1.0);
}
)";

QList<QByteArray> RhiRenderer::imageShaders() {
    return {RhiPresentFragment, BloomDownFragment, BloomUpFragment};
}

namespace {

// Auto exposure (task 29). Views within IgnoredStops of the hour's ordinary
// view keep its exposure. Beyond the brightest ordinary view the exposure
// falls by DarkenStrength of the excess, down to MaxDarken stops. By day the
// exposure also follows the view as a camera's metering does, by
// DayAdaptation of its difference from the ordinary view (a cloudy field
// brightened; the user, 2026-10-13: cloudy days were too dark).
constexpr float IgnoredStops = 0.5f;
constexpr float DayAdaptation = 0.75f;
constexpr float DarkenStrength = 0.75f;
constexpr float MaxDarken = 2.0f;
// Signals' glow over Dimmed's look (user, 2026-10-13: with the exposure
// offset the sun's and the moon's bloom outgrew it).
constexpr float AutoSignalBoost = 2.0f;
// Towards a brighter picture (into the dark) slowly, towards a darker one
// (into the light) fast.
constexpr float BrightenSeconds = 2.5f;
constexpr float DarkenSeconds = 0.5f;
constexpr int LuminanceGrid = 64;
// The sky's share in the measurement against the scene's: a bright sky (a
// cloudy day's) metered as much as the ground darkened the ground; the
// ordinary views (Daylight::viewLuminanceLog2) are measured the same way.
constexpr float SkyWeight = 0.2f;

// The view's log2 luminance over a 64 x 64 grid, 4 x 4 samples a cell, the
// sky (the sky band of the depth and beyond) weighted SkyWeight: the cell's
// weighted mean and its weight.
const char *LuminanceGridFragment = R"(
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(binding = 0) uniform sampler2D view;
layout(binding = 1) uniform sampler2D depth;
// (SkyWeight and SkyDepth, the sky band's start, put before it.)

void main() {
    vec2 cell = vec2(1.0 / 64.0);
    vec2 corner = uv - cell * 0.5;
    float sum = 0.0, total = 0.0;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            vec2 at = corner + (vec2(i, j) + 0.5) * cell * 0.25;
            vec3 c = textureLod(view, at, 0.0).rgb;
            vec3 l = pow(max(c, vec3(0.0)), vec3(2.2));
            float w = textureLod(depth, at, 0.0).r >= SkyDepth ? SkyWeight : 1.0;
            sum += w * log2(max(dot(l, vec3(0.2126, 0.7152, 0.0722)), 1e-6));
            total += w;
        }
    result = vec4(sum / total, total, 0.0, 1.0);
}
)";

// Their weighted mean.
const char *LuminanceMeanFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(binding = 0) uniform sampler2D grid;

void main() {
    float sum = 0.0, total = 0.0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            vec2 c = texelFetch(grid, ivec2(x, y), 0).rg;
            sum += c.x * c.y;
            total += c.y;
        }
    result = vec4(sum / max(total, 1e-6), 0.0, 0.0, 1.0);
}
)";

// The exposure (log2) of an adapted eye at a view's luminance (log2): the
// curve's, the night's below it, falling above it.
float adaptedExposure(const Renderer::ExposureControl &control, float luminance) {
    const float *l = control.curveLuminance;
    const float *e = control.curveExposure;
    const int last = control.curvePoints - 1;
    if (luminance <= l[0])
        return e[0];
    for (int i = 1; i <= last; ++i)
        if (luminance <= l[i])
            return e[i - 1] + (e[i] - e[i - 1]) * (luminance - l[i - 1]) / std::max(l[i] - l[i - 1], 1e-6f);
    return e[last] - DarkenStrength * (luminance - l[last]);
}

template <typename T>
void deleteResourceLater(T *&resource) {
    if (resource != nullptr)
        resource->deleteLater();
    resource = nullptr;
}

}

bool RhiRenderer::autoExposureAvailable() const {
    return toneMapping() > 0 && rhi->isTextureFormatSupported(QRhiTexture::RGBA16F);
}

bool RhiRenderer::exposureActive() const {
    return exposureControl.enabled && autoExposureAvailable();
}

float RhiRenderer::sceneScaleDisplay() const {
    return exposureActive() ? std::pow(std::max(exposureControl.sceneLight, 1e-6f), 1.0f / 2.2f) : 1.0f;
}

float RhiRenderer::autoExposure() const {
    if (!exposureActive())
        return 1.0f;
    return std::exp2(-std::log2(std::max(exposureControl.sceneLight, 1e-6f)) + luminance.correction
                     + exposureControl.biasStops);
}

float RhiRenderer::signalScale() const {
    if (!exposureActive())
        return gluu->signalLightAdaptation;
    // Brightened with the view by the offset, not by the adaptation (no
    // glare in a tunnel by day), and AutoSignalBoost stronger.
    return gluu->signalLightAdaptation * AutoSignalBoost * std::exp2(exposureControl.biasStops) / autoExposure();
}

void RhiRenderer::setExposureControl(const ExposureControl &control) {
    const bool wasActive = exposureActive();
    exposureControl = control;
    Luminance &l = luminance;
    if (!exposureActive()) {
        l.correction = 0.0f;
        l.snapFrames = 0;
        return;
    }
    // Back from frames without it (the map), the view is new as after a jump.
    if (control.snap || !wasActive) {
        // The read-backs in flight show the view before the jump: the
        // target is taken at once until two measurements of the new view.
        l.snapFrames = 2;
        l.measured = false;
        ++l.generation;
    }
    // An ordinary view of the hour keeps its exposure; another takes the
    // exposure of the ordinary view as bright as it is (a tunnel by day that
    // of dusk, a station lit by its lamps that of the night), or by day the
    // metered one if that adapts more. Never more than the night's exposure.
    float target = 0.0f;
    if (l.measured && control.curvePoints > 1) {
        const float off = l.measuredLog2 - control.referenceLog2;
        const float luminance = l.measuredLog2 - std::copysign(std::min(std::abs(off), IgnoredStops), off);
        const float curve = std::max(adaptedExposure(control, luminance)
                                     - adaptedExposure(control, control.referenceLog2), -MaxDarken);
        // The day's share: 1 with the sun 20 degrees up, 0 from sunset.
        const float night = control.curveExposure[0];
        const float sceneLog2 = std::log2(std::max(control.sceneLight, 1e-6f));
        const float day = std::clamp(1.0f + sceneLog2 / night, 0.0f, 1.0f);
        const float metered = std::clamp(DayAdaptation * day * (control.referenceLog2 - luminance), -MaxDarken,
                                         night + sceneLog2);
        target = off < 0.0f ? std::max(curve, metered) : std::min(curve, metered);
    }
    if (l.snapFrames > 0) {
        l.correction = target;
        if (l.measured)
            --l.snapFrames;
    } else {
        const float seconds = std::max(control.seconds, 0.0f);
        const float tau = target > l.correction ? BrightenSeconds : DarkenSeconds;
        l.correction += (target - l.correction) * (1.0f - std::exp(-seconds / tau));
    }
}

void RhiRenderer::measureLuminance(QRhiCommandBuffer *cb) {
    Luminance &l = luminance;
    const auto fragment = QRhiShaderResourceBinding::FragmentStage;
    if (l.grid == nullptr) {
        l.grid = rhi->newTexture(QRhiTexture::RGBA16F, QSize(LuminanceGrid, LuminanceGrid), 1,
                                 QRhiTexture::RenderTarget);
        l.mean = rhi->newTexture(QRhiTexture::RGBA16F, QSize(1, 1), 1,
                                 QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
        l.linear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                   QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        l.nearest = rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                    QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        if (!l.grid->create() || !l.mean->create() || !l.linear->create() || !l.nearest->create()) {
            releaseLuminance();
            return;
        }
        l.gridTarget = rhi->newTextureRenderTarget({QRhiColorAttachment(l.grid)});
        l.gridPass = l.gridTarget->newCompatibleRenderPassDescriptor();
        l.gridTarget->setRenderPassDescriptor(l.gridPass);
        l.meanTarget = rhi->newTextureRenderTarget({QRhiColorAttachment(l.mean)});
        l.meanPass = l.meanTarget->newCompatibleRenderPassDescriptor();
        l.meanTarget->setRenderPassDescriptor(l.meanPass);
        if (!l.gridTarget->create() || !l.meanTarget->create()) {
            releaseLuminance();
            return;
        }
        l.meanBindings = rhi->newShaderResourceBindings();
        l.meanBindings->setBindings({QRhiShaderResourceBinding::sampledTexture(0, fragment, l.grid, l.linear)});
        l.meanBindings->create();
        l.readback.completed = [this] {
            Luminance &m = luminance;
            m.pending = false;
            if (m.pendingGeneration != m.generation || m.readback.data.size() < 2)
                return;
            qfloat16 value;
            std::memcpy(&value, m.readback.data.constData(), sizeof(value));
            if (std::isfinite(float(value))) {
                m.measuredLog2 = float(value);
                m.measured = true;
            }
        };
    }
    if (view.depth == nullptr)
        return;
    if (l.gridBindings == nullptr || l.boundView != view.color || l.boundDepth != view.depth) {
        delete l.gridBindings;
        l.gridBindings = rhi->newShaderResourceBindings();
        l.gridBindings->setBindings({QRhiShaderResourceBinding::sampledTexture(0, fragment, view.color, l.linear),
                                     QRhiShaderResourceBinding::sampledTexture(1, fragment, view.depth, l.nearest)});
        l.gridBindings->create();
        l.boundView = view.color;
        l.boundDepth = view.depth;
    }
    static QShader vertex, gridShader, meanShader;
    if (!vertex.isValid()) {
        vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
        const QByteArray gridSource = QByteArray("#version 440\nconst float SkyWeight = ")
                + QByteArray::number(SkyWeight, 'f', 3) + ";\nconst float SkyDepth = 0.99;\n" + LuminanceGridFragment;
        gridShader = bakeInline(gridSource.constData(), QShader::FragmentStage, rhi);
        meanShader = bakeInline(LuminanceMeanFragment, QShader::FragmentStage, rhi);
    }
    auto pipeline = [&](const QShader &shader, QRhiShaderResourceBindings *set, QRhiRenderPassDescriptor *pass) {
        QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
        ps->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, shader}});
        ps->setShaderResourceBindings(set);
        ps->setRenderPassDescriptor(pass);
        if (!ps->create()) {
            delete ps;
            return static_cast<QRhiGraphicsPipeline *>(nullptr);
        }
        return ps;
    };
    if (l.gridPipeline == nullptr)
        l.gridPipeline = pipeline(gridShader, l.gridBindings, l.gridPass);
    if (l.meanPipeline == nullptr)
        l.meanPipeline = pipeline(meanShader, l.meanBindings, l.meanPass);
    if (l.gridPipeline == nullptr || l.meanPipeline == nullptr)
        return;
    cb->beginPass(l.gridTarget, Qt::black, {1.0f, 0});
    cb->setGraphicsPipeline(l.gridPipeline);
    cb->setViewport(QRhiViewport(0, 0, float(LuminanceGrid), float(LuminanceGrid)));
    cb->setShaderResources(l.gridBindings);
    cb->draw(3);
    cb->endPass();
    cb->beginPass(l.meanTarget, Qt::black, {1.0f, 0});
    cb->setGraphicsPipeline(l.meanPipeline);
    cb->setViewport(QRhiViewport(0, 0, 1.0f, 1.0f));
    cb->setShaderResources(l.meanBindings);
    cb->draw(3);
    cb->endPass();
    // One read-back at a time; it completes a frame or two later.
    if (!l.pending) {
        QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
        batch->readBackTexture(QRhiReadbackDescription(l.mean), &l.readback);
        cb->resourceUpdate(batch);
        l.pending = true;
        l.pendingGeneration = l.generation;
    }
}

void RhiRenderer::releaseLuminance() {
    Luminance &l = luminance;
    // Deleted after the frame: a read-back may be in flight.
    deleteResourceLater(l.gridPipeline);
    deleteResourceLater(l.meanPipeline);
    deleteResourceLater(l.gridBindings);
    deleteResourceLater(l.meanBindings);
    deleteResourceLater(l.gridTarget);
    deleteResourceLater(l.meanTarget);
    deleteResourceLater(l.gridPass);
    deleteResourceLater(l.meanPass);
    deleteResourceLater(l.grid);
    deleteResourceLater(l.mean);
    deleteResourceLater(l.linear);
    deleteResourceLater(l.nearest);
    l.boundView = nullptr;
    l.boundDepth = nullptr;
    // A read-back still in flight is dropped.
    ++l.generation;
    l.pending = false;
    l.measured = false;
}

int RhiRenderer::toneMapping() const {
    return std::clamp(Game::toneMapping, 0, 3);
}

QRhiTexture::Format RhiRenderer::viewFormat() const {
    if (toneMapping() > 0 && rhi->isTextureFormatSupported(QRhiTexture::RGBA16F))
        return QRhiTexture::RGBA16F;
    return QRhiTexture::RGBA8;
}

bool RhiRenderer::bloomEnabled() const {
    return Game::bloomStrength > 0.0f && rhi->isTextureFormatSupported(QRhiTexture::RGBA16F);
}

QRhiTexture *RhiRenderer::renderBloom(QRhiCommandBuffer *cb) {
    if (!bloomEnabled() || view.color3 == nullptr || cb == nullptr)
        return nullptr;
    Bloom &b = bloom;
    if (b.viewSize != view.size || b.levels.empty()) {
        releaseBloom();
        b.viewSize = view.size;
        b.linear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                   QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        b.linear->create();
        QSize size(std::max(1, view.size.width() / 2), std::max(1, view.size.height() / 2));
        while (int(b.levels.size()) < MaxBloomLevels
               && std::min(size.width(), size.height()) >= MinBloomSide) {
            QRhiTexture *level = rhi->newTexture(QRhiTexture::RGBA16F, size, 1, QRhiTexture::RenderTarget);
            if (!level->create()) {
                delete level;
                break;
            }
            b.levels.push_back(level);
            QRhiTextureRenderTarget *down = rhi->newTextureRenderTarget({QRhiColorAttachment(level)});
            QRhiTextureRenderTarget *up = rhi->newTextureRenderTarget(
                        {QRhiColorAttachment(level)}, QRhiTextureRenderTarget::PreserveColorContents);
            QRhiRenderPassDescriptor *downPass = down->newCompatibleRenderPassDescriptor();
            QRhiRenderPassDescriptor *upPass = up->newCompatibleRenderPassDescriptor();
            down->setRenderPassDescriptor(downPass);
            up->setRenderPassDescriptor(upPass);
            down->create();
            up->create();
            b.downTargets.push_back(down);
            b.upTargets.push_back(up);
            b.passes.push_back(downPass);
            b.passes.push_back(upPass);
            size = QSize(std::max(1, size.width() / 2), std::max(1, size.height() / 2));
        }
        if (b.levels.empty()) {
            releaseBloom();
            return nullptr;
        }
        const auto fragment = QRhiShaderResourceBinding::FragmentStage;
        for (size_t i = 0; i < b.levels.size(); ++i) {
            QRhiTexture *source = i == 0 ? view.color3 : b.levels[i - 1];
            QRhiShaderResourceBindings *down = rhi->newShaderResourceBindings();
            down->setBindings({QRhiShaderResourceBinding::sampledTexture(0, fragment, source, b.linear)});
            down->create();
            b.downBindings.push_back(down);
            QRhiShaderResourceBindings *up = rhi->newShaderResourceBindings();
            if (i + 1 < b.levels.size())
                up->setBindings({QRhiShaderResourceBinding::sampledTexture(0, fragment, b.levels[i + 1], b.linear)});
            else
                up->setBindings({QRhiShaderResourceBinding::sampledTexture(0, fragment, b.levels[i], b.linear)});
            up->create();
            b.upBindings.push_back(up);
        }
        static QShader vertex, downShader, upShader;
        if (!vertex.isValid()) {
            vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
            downShader = bakeInline(BloomDownFragment, QShader::FragmentStage, rhi);
            upShader = bakeInline(BloomUpFragment, QShader::FragmentStage, rhi);
        }
        b.down = rhi->newGraphicsPipeline();
        b.down->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, downShader}});
        b.down->setShaderResourceBindings(b.downBindings[0]);
        b.down->setRenderPassDescriptor(b.passes[0]);
        b.up = rhi->newGraphicsPipeline();
        b.up->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, upShader}});
        // Each level adds the wider levels below it.
        QRhiGraphicsPipeline::TargetBlend add;
        add.enable = true;
        add.srcColor = QRhiGraphicsPipeline::One;
        add.dstColor = QRhiGraphicsPipeline::One;
        add.srcAlpha = QRhiGraphicsPipeline::Zero;
        add.dstAlpha = QRhiGraphicsPipeline::One;
        b.up->setTargetBlends({add});
        b.up->setShaderResourceBindings(b.upBindings[0]);
        b.up->setRenderPassDescriptor(b.passes[1]);
        if (!b.down->create() || !b.up->create()) {
            releaseBloom();
            return nullptr;
        }
    }
    auto fullScreen = [cb](QRhiTextureRenderTarget *target, QRhiGraphicsPipeline *pipeline,
                           QRhiShaderResourceBindings *bindings, const QSize &size) {
        cb->beginPass(target, Qt::black, {1.0f, 0});
        cb->setGraphicsPipeline(pipeline);
        cb->setViewport(QRhiViewport(0, 0, float(size.width()), float(size.height())));
        cb->setShaderResources(bindings);
        cb->draw(3);
        cb->endPass();
    };
    for (size_t i = 0; i < b.levels.size(); ++i)
        fullScreen(b.downTargets[i], b.down, b.downBindings[i], b.levels[i]->pixelSize());
    for (size_t i = b.levels.size() - 1; i-- > 0; )
        fullScreen(b.upTargets[i], b.up, b.upBindings[i], b.levels[i]->pixelSize());
    return b.levels[0];
}

void RhiRenderer::releaseBloom() {
    Bloom &b = bloom;
    delete b.down;
    delete b.up;
    for (QRhiShaderResourceBindings *set : b.downBindings)
        delete set;
    for (QRhiShaderResourceBindings *set : b.upBindings)
        delete set;
    for (QRhiTextureRenderTarget *target : b.downTargets)
        delete target;
    for (QRhiTextureRenderTarget *target : b.upTargets)
        delete target;
    for (QRhiRenderPassDescriptor *pass : b.passes)
        delete pass;
    for (QRhiTexture *level : b.levels)
        delete level;
    delete b.linear;
    b = Bloom();
    // The present pass may hold the first level.
    if (presentBloomTexture != nullptr) {
        delete presentBindings;
        presentBindings = nullptr;
        presentBloomTexture = nullptr;
    }
}

QList<QByteArray> RhiRenderer::glowSplatShaders() {
    return {GlowSplatVertex, GlowSplatFragment};
}

// Once a frame, with the lights: the emissive emitters of the queue at
// their own power, without exposure or gain, as the surfaces' own glow has
// none, but following the lamps' light (task 21) as that glow does: faded
// by day, none with local lights off.
void RhiRenderer::gatherGlowSplats() {
    if (glowSplats.gathered)
        return;
    glowSplats.gathered = true;
    glowSplats.emitters.clear();
    if (bloomEnabled() && Game::localLightsEnabled) {
        // With the sky enclosure the splats take the daylight scale each,
        // and keep their strength where the sky is hidden (task 27).
        glowSplats.enclosed = enclosure.enabled && enclosureDrawn && enclosureMap.valid() && !exposureActive();
        if (glowSplats.enclosed)
            gatherLights(glowSplats.emitters, 0.0f, 1.0f, true, 1.0f);
        else
            gatherLights(glowSplats.emitters, 0.0f, gluu->localLightAdaptation, true, signalScale());
    }
}

// At the end of the main view: its scene band's projection, fog and
// viewport, for the splats drawn with the view's next pass.
void RhiRenderer::prepareGlowSplats() {
    GlowSplats &g = glowSplats;
    if (secondaryView || currentTarget != TARGET_VIEW || g.emitters.empty() || !bloomEnabled()
            || view.color3 == nullptr)
        return;
    const QMatrix4x4 correction = targetCorrection();
    Mat4::multiply(g.projection, const_cast<float *>(correction.constData()), gluu->pMatrix);
    std::copy(gluu->fMatrix, gluu->fMatrix + 16, g.fogMatrix);
    std::copy(viewPosition, viewPosition + 3, g.camera);
    const float w = viewportRect[2] > 0 ? float(viewportRect[2]) : float(view.size.width());
    const float h = viewportRect[3] > 0 ? float(viewportRect[3]) : float(view.size.height());
    g.viewport = QRhiViewport(float(viewportRect[0]), float(viewportRect[1]), w, h,
                              depthRange[0], depthRange[1]);
    g.focal = sceneProjection[1] * h * 0.5f;
    g.lod = fogLodOverride ? fogLod : Game::objectLod;
    g.fogDensity = gluu->fogDensity;
    hazeParameters(g.haze);
    g.pending = true;
}

bool RhiRenderer::uploadGlowSplats(QRhiResourceUpdateBatch *batch, QRhiRenderPassDescriptor *pass) {
    GlowSplats &g = glowSplats;
    const quint32 count = quint32(g.emitters.size());
    if (batch == nullptr || pass == nullptr || count == 0)
        return false;
    const quint32 bytes = count * GlowSplatStride;
    if (g.instances == nullptr || g.capacity < bytes) {
        delete g.instances;
        g.capacity = std::max<quint32>(4096, bytes + bytes / 2);
        g.instances = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, g.capacity);
        if (!g.instances->create()) {
            delete g.instances;
            g.instances = nullptr;
            g.capacity = 0;
            return false;
        }
    }
    std::vector<float> data(size_t(count) * 16);
    for (quint32 i = 0; i < count; ++i) {
        const LightGrid::Light &e = g.emitters[i];
        float *out = data.data() + i * 16;
        std::copy(e.position, e.position + 3, out);
        out[3] = e.radius;
        std::copy(e.color, e.color + 3, out + 4);
        out[7] = !g.enclosed ? 1.0f : e.signal ? gluu->signalLightAdaptation : gluu->localLightAdaptation;
        // A lens's cone, as the lights' grid has a spot's.
        std::copy(e.direction, e.direction + 3, out + 8);
        out[11] = e.spot ? 1.0f : 0.0f;
        const float spread = e.cosInner - e.cosOuter;
        out[12] = e.cosOuter;
        out[13] = spread > 1e-4f ? 1.0f / spread : 1e4f;
        out[14] = out[15] = 0.0f;
    }
    batch->updateDynamicBuffer(g.instances, 0, bytes, data.data());
    if (g.uniforms == nullptr) {
        g.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, GlowSplatUniformBytes);
        if (!g.uniforms->create()) {
            delete g.uniforms;
            g.uniforms = nullptr;
            return false;
        }
    }
    float block[GlowSplatUniformBytes / sizeof(float)];
    std::copy(g.projection, g.projection + 16, block);
    std::copy(g.fogMatrix, g.fogMatrix + 16, block + 16);
    std::copy(g.camera, g.camera + 3, block + 32);
    block[35] = g.focal;
    block[36] = g.viewport.viewport()[2];
    block[37] = g.viewport.viewport()[3];
    block[38] = g.lod;
    block[39] = g.fogDensity;
    std::copy(enclosure.viewProjection, enclosure.viewProjection + 16, block + 40);
    block[56] = g.enclosed ? 1.0f : 0.0f;
    block[57] = enclosure.metresPerDepth;
    block[58] = block[59] = 0.0f;
    std::copy(g.haze, g.haze + 4, block + 60);
    batch->updateDynamicBuffer(g.uniforms, 0, GlowSplatUniformBytes, block);
    QRhiTexture *enclosureTexture = g.enclosed ? enclosureMap.depth : dummyBlack;
    if (g.bindings != nullptr && g.enclosureTexture != enclosureTexture) {
        delete g.bindings;
        g.bindings = nullptr;
        delete g.pipeline;
        g.pipeline = nullptr;
    }
    if (g.bindings == nullptr) {
        g.enclosureTexture = enclosureTexture;
        g.bindings = rhi->newShaderResourceBindings();
        g.bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
                                         0, QRhiShaderResourceBinding::VertexStage, g.uniforms),
                                 QRhiShaderResourceBinding::sampledTexture(
                                         1, QRhiShaderResourceBinding::VertexStage, enclosureTexture,
                                         sampler(false, true, true))});
        g.bindings->create();
    }
    if (g.pipeline == nullptr || g.pipelinePass != pass) {
        delete g.pipeline;
        static QShader vertex, fragment;
        if (!vertex.isValid()) {
            vertex = bakeInline(GlowSplatVertex, QShader::VertexStage, rhi);
            fragment = bakeInline(GlowSplatFragment, QShader::FragmentStage, rhi);
        }
        g.pipeline = rhi->newGraphicsPipeline();
        g.pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
        QRhiVertexInputLayout layout;
        layout.setBindings({{GlowSplatStride, QRhiVertexInputBinding::PerInstance}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float4, 0},
                              {0, 1, QRhiVertexInputAttribute::Float4, 4 * sizeof(float)},
                              {0, 2, QRhiVertexInputAttribute::Float4, 8 * sizeof(float)},
                              {0, 3, QRhiVertexInputAttribute::Float4, 12 * sizeof(float)}});
        g.pipeline->setVertexInputLayout(layout);
        g.pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        // Behind the scene's surfaces they are hidden; they add to the glow
        // only, leaving the colour and the ambient share.
        g.pipeline->setDepthTest(true);
        g.pipeline->setDepthWrite(false);
        g.pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        QRhiGraphicsPipeline::TargetBlend untouched;
        untouched.colorWrite = {};
        QRhiGraphicsPipeline::TargetBlend add;
        add.enable = true;
        add.srcColor = QRhiGraphicsPipeline::One;
        add.dstColor = QRhiGraphicsPipeline::One;
        add.srcAlpha = QRhiGraphicsPipeline::Zero;
        add.dstAlpha = QRhiGraphicsPipeline::One;
        add.colorWrite = QRhiGraphicsPipeline::R | QRhiGraphicsPipeline::G | QRhiGraphicsPipeline::B;
        g.pipeline->setTargetBlends({untouched, untouched, add});
        g.pipeline->setShaderResourceBindings(g.bindings);
        g.pipeline->setRenderPassDescriptor(pass);
        g.pipelinePass = pass;
        if (!g.pipeline->create()) {
            qWarning() << "QRhi renderer: glow splat pipeline creation failed";
            delete g.pipeline;
            g.pipeline = nullptr;
            return false;
        }
    }
    g.count = count;
    return true;
}

void RhiRenderer::drawGlowSplats(QRhiCommandBuffer *cb) {
    GlowSplats &g = glowSplats;
    g.pending = false;
    if (g.pipeline == nullptr || g.count == 0)
        return;
    cb->setGraphicsPipeline(g.pipeline);
    cb->setViewport(g.viewport);
    cb->setShaderResources(g.bindings);
    const QRhiCommandBuffer::VertexInput input(g.instances, 0);
    cb->setVertexInput(0, 1, &input);
    cb->draw(4, g.count);
}

void RhiRenderer::releaseGlowSplats() {
    GlowSplats &g = glowSplats;
    delete g.pipeline;
    delete g.bindings;
    delete g.uniforms;
    delete g.instances;
    g = GlowSplats();
}
