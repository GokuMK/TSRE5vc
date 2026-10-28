/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// The physical sky of the QRhi renderer (task 28, after Hillaire 2020). The
// transmittance and multiple-scattering tables come from the CPU model
// (Atmosphere), uploaded once; each frame a sky-view table (192 x 108: sky
// light by view zenith and azimuth from the sun) is marched for the camera's
// height and the sun; each view (the main view, environment map faces, the
// water reflection) then starts with the sky drawn from it, before its
// geometry, in place of the clear colour.

#include "RhiRenderer.h"
#include "RhiProgram.h"
#include "RhiRenderSurface.h"
#include <QDebug>
#include <QMatrix4x4>
#include <algorithm>
#include <cmath>
#include <qfloat16.h>
#include <tsre/environment/Atmosphere.h>

namespace {

constexpr int SkyViewWidth = 192;
constexpr int SkyViewHeight = 108;

// std140, shared by both passes.
struct SkyParameters {
    float inverse[16];  // inverse of projection x camera rotation
    float sun[4];       // towards the sun
    float camera[4];    // height (km), y sign of the view's rows, exposure, 0
    float night[4];     // display colour kept at least; the scene's scale (auto exposure, task 29)
};

// The atmosphere, as Atmosphere.cpp.
const char *AtmosphereCommon = R"(
const float Pi = 3.14159265;
const float BottomRadius = 6360.0;
const float TopRadius = 6460.0;
const vec3 RayleighScattering = vec3(5.802e-3, 13.558e-3, 33.1e-3);
const float MieScattering = 3.996e-3;
const float MieExtinction = 4.440e-3;
const float MieG = 0.8;
const vec3 OzoneAbsorption = vec3(0.650e-3, 1.881e-3, 0.085e-3);

struct Medium {
    vec3 rayleigh;
    float mie;
    vec3 scattering;
    vec3 extinction;
};

Medium sampleMedium(float radius) {
    float h = max(0.0, radius - BottomRadius);
    Medium m;
    m.rayleigh = RayleighScattering * exp(-h / 8.0);
    float mieDensity = exp(-h / 1.2);
    m.mie = MieScattering * mieDensity;
    m.scattering = m.rayleigh + vec3(m.mie);
    m.extinction = m.rayleigh + vec3(MieExtinction * mieDensity)
            + OzoneAbsorption * max(0.0, 1.0 - abs(h - 25.0) / 15.0);
    return m;
}

// Far hit from inside, near hit from outside; -1 for none.
float sphereDistance(vec3 p, vec3 d, float radius) {
    float b = dot(p, d);
    float c = dot(p, p) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0)
        return -1.0;
    float root = sqrt(discriminant);
    float near = -b - root, far = -b + root;
    if (near >= 0.0)
        return near;
    return far >= 0.0 ? far : -1.0;
}

vec2 transmittanceUv(float radius, float mu) {
    float h = sqrt(TopRadius * TopRadius - BottomRadius * BottomRadius);
    float rho = sqrt(max(0.0, radius * radius - BottomRadius * BottomRadius));
    float discriminant = radius * radius * (mu * mu - 1.0) + TopRadius * TopRadius;
    float d = max(0.0, -radius * mu + sqrt(max(0.0, discriminant)));
    float dMin = TopRadius - radius;
    float dMax = rho + h;
    return vec2((d - dMin) / (dMax - dMin), rho / h);
}

float rayleighPhase(float c) {
    return 3.0 / (16.0 * Pi) * (1.0 + c * c);
}

float miePhase(float g, float c) {
    float k = 3.0 / (8.0 * Pi) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + c * c) / pow(1.0 + g * g - 2.0 * g * c, 1.5);
}

float fromUnitToSubUvs(float u, float resolution) {
    return (u + 0.5 / resolution) * (resolution / (resolution + 1.0));
}

float fromSubUvsToUnit(float u, float resolution) {
    return (u - 0.5 / resolution) * (resolution / (resolution - 1.0));
}
)";

const char *SkyViewFragment = R"(
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(std140, binding = 0) uniform Sky {
    mat4 inverse;
    vec4 sun;
    vec4 camera;
    vec4 night;
};
layout(binding = 1) uniform sampler2D transmittanceTable;
layout(binding = 2) uniform sampler2D multipleTable;

void main() {
    vec2 coord = vec2(fromSubUvsToUnit(uv.x, 192.0), fromSubUvsToUnit(uv.y, 108.0));
    float radius = BottomRadius + max(camera.x, 0.001);
    float horizonLength = sqrt(max(0.0, radius * radius - BottomRadius * BottomRadius));
    float beta = acos(clamp(horizonLength / radius, -1.0, 1.0));
    float zenithHorizon = Pi - beta;
    float viewCos;
    if (coord.y < 0.5) {
        float c = 1.0 - 2.0 * coord.y;
        c = 1.0 - c * c;
        viewCos = cos(zenithHorizon * c);
    } else {
        float c = coord.y * 2.0 - 1.0;
        viewCos = cos(zenithHorizon + beta * c * c);
    }
    float lightViewCos = -(coord.x * coord.x * 2.0 - 1.0);
    float viewSin = sqrt(max(0.0, 1.0 - viewCos * viewCos));
    vec3 d = vec3(viewSin * lightViewCos, viewCos, viewSin * sqrt(max(0.0, 1.0 - lightViewCos * lightViewCos)));
    float sunCos = clamp(normalize(sun.xyz).y, -1.0, 1.0);
    vec3 sunDir = vec3(sqrt(max(0.0, 1.0 - sunCos * sunCos)), sunCos, 0.0);
    vec3 p = vec3(0.0, radius, 0.0);
    float ground = sphereDistance(p, d, BottomRadius);
    float distance = ground > 0.0 ? ground : sphereDistance(p, d, TopRadius);
    vec3 luminance = vec3(0.0);
    if (distance > 0.0) {
        float cosAngle = dot(d, sunDir);
        float phaseR = rayleighPhase(cosAngle);
        float phaseM = miePhase(MieG, cosAngle);
        const int Steps = 30;
        float dt = distance / float(Steps);
        vec3 throughput = vec3(1.0);
        for (int s = 0; s < Steps; ++s) {
            vec3 q = p + d * ((float(s) + 0.3) * dt);
            float qr = length(q);
            Medium m = sampleMedium(qr);
            vec3 sampleT = exp(-m.extinction * dt);
            float qSunCos = dot(q, sunDir) / qr;
            float shadow = sphereDistance(q, sunDir, BottomRadius) > 0.0 ? 0.0 : 1.0;
            vec3 sunT = shadow * textureLod(transmittanceTable, transmittanceUv(qr, qSunCos), 0.0).rgb;
            vec3 ms = textureLod(multipleTable, vec2(qSunCos * 0.5 + 0.5,
                                                     (qr - BottomRadius) / (TopRadius - BottomRadius)), 0.0).rgb;
            vec3 lit = sunT * (m.rayleigh * phaseR + vec3(m.mie * phaseM)) + ms * m.scattering;
            luminance += throughput * (lit - lit * sampleT) / m.extinction;
            throughput *= sampleT;
        }
    }
    result = vec4(luminance, 1.0);
}
)";

const char *SkyFragment = R"(
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(std140, binding = 0) uniform Sky {
    mat4 inverse;
    vec4 sun;
    vec4 camera;
    vec4 night;
};
layout(binding = 1) uniform sampler2D skyView;

void main() {
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * camera.y);
    vec4 far = inverse * vec4(ndc, 1.0, 1.0);
    vec3 d = normalize(far.xyz / far.w);
    float radius = BottomRadius + max(camera.x, 0.001);
    float horizonLength = sqrt(max(0.0, radius * radius - BottomRadius * BottomRadius));
    float beta = acos(clamp(horizonLength / radius, -1.0, 1.0));
    float zenithHorizon = Pi - beta;
    // Under the horizon (gaps past the distant terrain) the horizon's colour.
    float angle = min(acos(clamp(d.y, -1.0, 1.0)), zenithHorizon - 1e-4);
    float c = 1.0 - sqrt(max(0.0, 1.0 - angle / zenithHorizon));
    float v = c * 0.5;
    vec2 dh = d.xz, sh = sun.xz;
    float lightViewCos = (dot(dh, dh) > 1e-10 && dot(sh, sh) > 1e-10) ? dot(normalize(dh), normalize(sh)) : 1.0;
    float u = sqrt(clamp(-lightViewCos * 0.5 + 0.5, 0.0, 1.0));
    vec3 light = textureLod(skyView, vec2(fromUnitToSubUvs(u, 192.0), fromUnitToSubUvs(v, 108.0)), 0.0).rgb;
    vec3 display = pow(1.0 - exp(-light * camera.z), vec3(1.0 / 2.2));
    result = vec4(max(display, night.rgb) * night.w, 1.0);
}
)";

QByteArray skySource(const char *body) {
    return QByteArray("#version 440\n") + AtmosphereCommon + body;
}

}

QList<QByteArray> RhiRenderer::skyShaders() {
    return {skySource(SkyViewFragment), skySource(SkyFragment)};
}

bool RhiRenderer::drawsPhysicalSky() const {
    return sky.physical;
}

void RhiRenderer::setSky(const Sky &next) {
    sky = next;
    if (!sky.physical)
        return;
    Haze &h = haze;
    float across[2] = {sky.sun[0], sky.sun[2]};
    const float length = std::hypot(across[0], across[1]);
    if (length > 1e-4f) {
        h.sun[0] = across[0] / length;
        h.sun[1] = across[1] / length;
    }
    // The sky along the horizon, as the fog colour's (Atmosphere::
    // horizonColour), but by direction: towards the sun bright, away from it
    // the deep blue the haze of far mountains takes. Again when the sun has
    // moved about a hundredth of a degree or the camera 20 m up or down.
    const float moved = std::abs(sky.sun[0] - h.drawnSun[0]) + std::abs(sky.sun[1] - h.drawnSun[1])
            + std::abs(sky.sun[2] - h.drawnSun[2]);
    if (!h.valid || moved > 2e-4f || std::abs(sky.cameraHeight - h.drawnHeight) > 20.0f) {
        h.valid = true;
        std::copy(sky.sun, sky.sun + 3, h.drawnSun);
        h.drawnHeight = sky.cameraHeight;
        const double height = std::max(0.001, sky.cameraHeight / 1000.0);
        const double elevation = 2.0 * M_PI / 180.0;
        for (int i = 0; i < 8; ++i) {
            const double a = M_PI * i / 7.0;
            const double x = std::cos(a) * h.sun[0] - std::sin(a) * h.sun[1];
            const double z = std::cos(a) * h.sun[1] + std::sin(a) * h.sun[0];
            const float d[3] = {float(std::cos(elevation) * x), float(std::sin(elevation)),
                                float(std::cos(elevation) * z)};
            const Atmosphere::Rgb c = Atmosphere::toDisplay(Atmosphere::model().skyRadiance(height, d, sky.sun));
            for (int k = 0; k < 3; ++k)
                h.clear[i][k] = std::max(c[k], sky.night[k]);
        }
        // The sky's light from above: three bands from the zenith, each a
        // half circle from the sun's side (the sky is the same on both),
        // weighted by the light they give a level surface, in the sky
        // shader's colour (display, the night's at least) taken to linear
        // light, as the cube holds it.
        double sum[3] = {0.0, 0.0, 0.0};
        for (const double zenith : {15.0, 45.0, 75.0}) {
            const double t = zenith * M_PI / 180.0;
            for (int i = 0; i <= 4; ++i) {
                const double a = M_PI * i / 4.0;
                const double weight = std::cos(t) * std::sin(t) * (i == 0 || i == 4 ? 0.5 : 1.0);
                const double x = std::cos(a) * h.sun[0] - std::sin(a) * h.sun[1];
                const double z = std::cos(a) * h.sun[1] + std::sin(a) * h.sun[0];
                const float d[3] = {float(std::sin(t) * x), float(std::cos(t)), float(std::sin(t) * z)};
                const Atmosphere::Rgb c = Atmosphere::toDisplay(Atmosphere::model().skyRadiance(height, d, sky.sun));
                for (int k = 0; k < 3; ++k)
                    sum[k] += weight * std::pow(double(std::max(c[k], sky.night[k])), 2.2);
            }
        }
        for (int k = 0; k < 3; ++k)
            h.aboveClear[k] = float(sum[k]);
    }
    // Under a covered sky the haze turns grey, as the fog colour does
    // (RouteEditorGLWidget::applyClouds).
    const float o = std::clamp((sky.overcast - 0.3f) / 0.6f, 0.0f, 1.0f);
    for (int i = 0; i < 8; ++i) {
        const float *c = h.clear[i];
        const float grey = (c[0] * 0.3f + c[1] * 0.55f + c[2] * 0.15f) * 0.85f;
        for (int k = 0; k < 3; ++k)
            h.ring[i][k] = c[k] + (grey - c[k]) * 0.8f * o;
    }
    // From above as grey as the clouds make it (its brightness kept: its
    // colour is what the shading takes), from no cover up. Clouds give more
    // of the light than their cover from straight above: seen slantwise
    // their sides fill the gaps between them (cumulus stand as tall as they
    // are wide), and lit by the sun they are brighter than the sky; taken
    // together as three layers of the cover, a sky a third covered gives
    // two thirds of its light from clouds. Greyed as the haze (from 30 %),
    // fair cumulus left the shade the clear sky's blue, where the cube, drawn
    // with its clouds, had it greyer (the user, 2026-10-28, CMK: about half
    // cloud, half sky).
    const float *a = h.aboveClear;
    const float luminance = 0.2126f * a[0] + 0.7152f * a[1] + 0.0722f * a[2];
    const float cover = std::clamp(sky.overcast, 0.0f, 1.0f);
    const float clouds = 1.0f - (1.0f - cover) * (1.0f - cover) * (1.0f - cover);
    for (int k = 0; k < 3; ++k)
        h.above[k] = a[k] + (luminance - a[k]) * 0.8f * clouds;
}

void RhiRenderer::hazeParameters(float *out) const {
    out[0] = std::max(sky.cameraHeight, 0.0f) / 1000.0f;
    // The weather's haze (visibility, scaled by the fog density setting),
    // as the clouds' (high.y).
    out[1] = std::clamp(sky.haze, 0.0f, 0.02f) * 1000.0f;
    out[2] = sky.physical && haze.valid && !plainImage ? 1.0f : 0.0f;
    out[3] = 0.0f;
}

bool RhiRenderer::skyFor(int which) const {
    return sky.physical && skyInverseValid[which]
            && (which == TARGET_VIEW || which == TargetEnvironment || which == TargetReflection);
}

bool RhiRenderer::createSkyResources() {
    SkyResources &s = skyResources;
    if (s.viewPipeline != nullptr)
        return true;
    releaseSkyResources();
    const QRhiTexture::Format format = rhi->isTextureFormatSupported(QRhiTexture::RGBA16F)
            ? QRhiTexture::RGBA16F : QRhiTexture::RGBA32F;
    s.transmittance = rhi->newTexture(format, QSize(Atmosphere::TransmittanceWidth, Atmosphere::TransmittanceHeight));
    s.multiple = rhi->newTexture(format, QSize(Atmosphere::MultipleWidth, Atmosphere::MultipleHeight));
    s.view = rhi->newTexture(format, QSize(SkyViewWidth, SkyViewHeight), 1, QRhiTexture::RenderTarget);
    s.viewUniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(SkyParameters));
    s.linear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                               QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    if (!s.transmittance->create() || !s.multiple->create() || !s.view->create() || !s.viewUniforms->create()
            || !s.linear->create()) {
        releaseSkyResources();
        return false;
    }
    s.tableFormat = format;
    s.viewTarget = rhi->newTextureRenderTarget({QRhiColorAttachment(s.view)});
    s.viewPass = s.viewTarget->newCompatibleRenderPassDescriptor();
    s.viewTarget->setRenderPassDescriptor(s.viewPass);
    if (!s.viewTarget->create()) {
        releaseSkyResources();
        return false;
    }
    const auto fragment = QRhiShaderResourceBinding::FragmentStage;
    s.viewBindings = rhi->newShaderResourceBindings();
    s.viewBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, fragment, s.viewUniforms),
                                 QRhiShaderResourceBinding::sampledTexture(1, fragment, s.transmittance, s.linear),
                                 QRhiShaderResourceBinding::sampledTexture(2, fragment, s.multiple, s.linear)});
    s.viewBindings->create();
    static QShader vertex, viewShader;
    if (!vertex.isValid()) {
        vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
        viewShader = bakeInline(skySource(SkyViewFragment).constData(), QShader::FragmentStage, rhi);
    }
    s.viewPipeline = rhi->newGraphicsPipeline();
    s.viewPipeline->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, viewShader}});
    s.viewPipeline->setShaderResourceBindings(s.viewBindings);
    s.viewPipeline->setRenderPassDescriptor(s.viewPass);
    if (!s.viewPipeline->create()) {
        delete s.viewPipeline;
        s.viewPipeline = nullptr;
        releaseSkyResources();
        return false;
    }
    s.tablesUploaded = false;
    return true;
}

void RhiRenderer::releaseSkyResources() {
    SkyResources &s = skyResources;
    for (auto &entry : s.pipelines) {
        delete entry.second;
        delete entry.first;
    }
    for (SkyResources::Use &use : s.uses) {
        delete use.bindings;
        delete use.uniforms;
    }
    delete s.viewPipeline;
    delete s.viewBindings;
    delete s.viewTarget;
    delete s.viewPass;
    delete s.viewUniforms;
    delete s.view;
    delete s.transmittance;
    delete s.multiple;
    delete s.linear;
    s = SkyResources();
}

void RhiRenderer::setSkyInverse(const float *projection, const float *viewMatrix) {
    // The camera's rotation only: directions, not positions.
    QMatrix4x4 rotation(viewMatrix);
    rotation = rotation.transposed();
    rotation.setColumn(3, QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
    QMatrix4x4 p(projection);
    p = p.transposed();
    bool invertible = false;
    const QMatrix4x4 inverse = (p * rotation).inverted(&invertible);
    skyInverseValid[currentTarget] = invertible;
    if (invertible)
        std::copy(inverse.constData(), inverse.constData() + 16, skyInverse[currentTarget]);
    const QMatrix4x4 forward = p * rotation;
    std::copy(forward.constData(), forward.constData() + 16, skyForward[currentTarget]);
}

void RhiRenderer::drawSky(QRhiCommandBuffer *cb, Attachments &attachments, QRhiResourceUpdateBatch *&updates) {
    if (!createSkyResources())
        return;
    SkyResources &s = skyResources;
    const Atmosphere::Model &model = Atmosphere::model();
    if (!s.tablesUploaded) {
        // Half floats where the GPU filters them.
        auto upload = [&](QRhiTexture *texture, const std::vector<float> &table, int width, int height) {
            QByteArray bytes;
            if (s.tableFormat == QRhiTexture::RGBA16F) {
                bytes.resize(int(table.size() * sizeof(qfloat16)));
                qfloat16 *out = reinterpret_cast<qfloat16 *>(bytes.data());
                for (size_t i = 0; i < table.size(); ++i)
                    out[i] = qfloat16(table[i]);
            } else {
                bytes = QByteArray(reinterpret_cast<const char *>(table.data()), int(table.size() * sizeof(float)));
            }
            QRhiTextureSubresourceUploadDescription data(bytes);
            data.setSourceSize(QSize(width, height));
            if (updates == nullptr)
                updates = rhi->nextResourceUpdateBatch();
            updates->uploadTexture(texture, QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, data)));
        };
        upload(s.transmittance, model.transmittanceTable(), Atmosphere::TransmittanceWidth,
               Atmosphere::TransmittanceHeight);
        upload(s.multiple, model.multipleTable(), Atmosphere::MultipleWidth, Atmosphere::MultipleHeight);
        s.tablesUploaded = true;
    }
    SkyParameters parameters = {};
    const float length = std::sqrt(sky.sun[0] * sky.sun[0] + sky.sun[1] * sky.sun[1] + sky.sun[2] * sky.sun[2]);
    for (int i = 0; i < 3; ++i)
        parameters.sun[i] = length > 0.0f ? sky.sun[i] / length : (i == 1 ? 1.0f : 0.0f);
    parameters.camera[0] = std::max(0.001f, sky.cameraHeight / 1000.0f);
    parameters.camera[2] = Atmosphere::SkyExposure;
    std::copy(sky.night, sky.night + 3, parameters.night);
    parameters.night[3] = sceneScaleDisplay();
    if (!s.viewRendered) {
        // The sky-view table, once a frame.
        s.viewRendered = true;
        if (updates == nullptr)
            updates = rhi->nextResourceUpdateBatch();
        updates->updateDynamicBuffer(s.viewUniforms, 0, sizeof(parameters), &parameters);
        cb->beginPass(s.viewTarget, Qt::black, {1.0f, 0}, updates);
        updates = nullptr;
        cb->setGraphicsPipeline(s.viewPipeline);
        cb->setViewport(QRhiViewport(0, 0, float(SkyViewWidth), float(SkyViewHeight)));
        cb->setShaderResources(s.viewBindings);
        cb->draw(3);
        cb->endPass();
    }
    // This view's pass: uniforms of its own, as all of a frame's passes see
    // a dynamic buffer's last values.
    const int which = currentTarget;
    std::copy(skyInverse[which], skyInverse[which] + 16, parameters.inverse);
    // Rows in OpenGL's order (cube faces) or where the framebuffer's y
    // points up: NDC's y grows with the row; otherwise against it.
    parameters.camera[1] = (attachments.openGlRows || rhi->isYUpInFramebuffer()) ? 1.0f : -1.0f;
    const auto fragment = QRhiShaderResourceBinding::FragmentStage;
    if (s.usesThisFrame >= s.uses.size()) {
        SkyResources::Use use;
        use.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(SkyParameters));
        use.uniforms->create();
        use.bindings = rhi->newShaderResourceBindings();
        use.bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, fragment, use.uniforms),
                                   QRhiShaderResourceBinding::sampledTexture(1, fragment, s.view, s.linear)});
        use.bindings->create();
        s.uses.push_back(use);
    }
    const SkyResources::Use &use = s.uses[s.usesThisFrame++];
    if (attachments.skyTarget == nullptr) {
        QRhiColorAttachment color(attachments.color);
        color.setLayer(attachments.colorLayer);
        color.setLevel(attachments.colorLevel);
        attachments.skyTarget = rhi->newTextureRenderTarget({color});
        attachments.skyPass = attachments.skyTarget->newCompatibleRenderPassDescriptor();
        attachments.skyTarget->setRenderPassDescriptor(attachments.skyPass);
        if (!attachments.skyTarget->create()) {
            delete attachments.skyTarget;
            delete attachments.skyPass;
            attachments.skyTarget = nullptr;
            attachments.skyPass = nullptr;
            return;
        }
    }
    QRhiGraphicsPipeline *pipeline = nullptr;
    for (auto &entry : s.pipelines)
        if (entry.first->isCompatible(attachments.skyPass))
            pipeline = entry.second;
    if (pipeline == nullptr) {
        static QShader vertex, skyShader;
        if (!skyShader.isValid()) {
            vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
            skyShader = bakeInline(skySource(SkyFragment).constData(), QShader::FragmentStage, rhi);
        }
        pipeline = rhi->newGraphicsPipeline();
        pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, skyShader}});
        pipeline->setShaderResourceBindings(use.bindings);
        // A descriptor of its own: the attachments' one goes with them.
        QRhiRenderPassDescriptor *key = attachments.skyTarget->newCompatibleRenderPassDescriptor();
        pipeline->setRenderPassDescriptor(key);
        if (!pipeline->create()) {
            delete pipeline;
            delete key;
            return;
        }
        s.pipelines.push_back({key, pipeline});
    }
    if (updates == nullptr)
        updates = rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(use.uniforms, 0, sizeof(parameters), &parameters);
    cb->beginPass(attachments.skyTarget, Qt::black, {1.0f, 0}, updates);
    updates = nullptr;
    cb->setGraphicsPipeline(pipeline);
    cb->setViewport(QRhiViewport(0, 0, float(attachments.size.width()), float(attachments.size.height())));
    cb->setShaderResources(use.bindings);
    cb->draw(3);
    cb->endPass();
}
