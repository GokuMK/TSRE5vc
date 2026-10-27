/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/MstsMaterials.h>

#include <QFileInfo>
#include <algorithm>
#include <tsre/renderer/RenderItem.h>

namespace MstsMaterials {

Shader shader(const QString &name) {
    if (name.compare(QLatin1String("PbrTexDiff"), Qt::CaseInsensitive) == 0)
        return Shader::Pbr;
    if (name.compare(QLatin1String("BlendAPbrTexDiff"), Qt::CaseInsensitive) == 0)
        return Shader::PbrBlended;
    return Shader::Other;
}

Factors Factors::read(const QVector<float> &values) {
    Factors f;
    float *fields[Count] = {&f.tint[0], &f.tint[1], &f.tint[2], &f.tint[3], &f.metalness, &f.roughness,
                            &f.normal, &f.occlusion, &f.clearcoat, &f.clearcoatRoughness, &f.transmission};
    for (int i = 0; i < Count && i < values.size(); ++i)
        if (std::isfinite(values[i]))
            *fields[i] = values[i];
    return f;
}

QVector<float> Factors::values(bool orm) const {
    const Factors defaults;
    const float written[Count] = {tint[0], tint[1], tint[2], tint[3], metalnessFor(orm), roughnessFor(orm),
                                  normal, occlusion, clearcoat, clearcoatRoughness, transmission};
    const float standard[Count] = {1.0f, 1.0f, 1.0f, 1.0f, defaults.metalnessFor(orm), defaults.roughnessFor(orm),
                                   1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    int last = -1;
    for (int i = 0; i < Count; ++i)
        if (written[i] != standard[i])
            last = i;
    return QVector<float>(written, written + last + 1);
}

bool Factors::operator==(const Factors &o) const {
    auto same = [](float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); };
    return std::equal(tint, tint + 4, o.tint) && same(metalness, o.metalness) && same(roughness, o.roughness)
            && normal == o.normal && occlusion == o.occlusion && clearcoat == o.clearcoat
            && clearcoatRoughness == o.clearcoatRoughness && transmission == o.transmission;
}

QString key(const QString &imageName) {
    QString name = imageName.trimmed();
    name.replace('\\', '/');
    return name.section('/', -1).toLower();
}

void fill(RenderItem &item, const Factors &factors, int ormTexture, int normalTexture, Shader shader,
          bool alphaTest) {
    RenderItem::Pbr &p = item.pbr;
    p = RenderItem::Pbr();
    p.enabled = shader != Shader::Other;
    if (!p.enabled)
        return;
    const bool orm = ormTexture >= 0;
    std::copy(factors.tint, factors.tint + 4, p.baseColor);
    p.metallic = factors.metalnessFor(orm);
    p.roughness = factors.roughnessFor(orm);
    p.normalScale = factors.normal;
    p.occlusionStrength = factors.occlusion;
    p.clearcoat = factors.clearcoat;
    p.clearcoatRoughness = factors.clearcoatRoughness;
    p.transmission = factors.transmission;
    p.textures[RenderItem::Pbr::MAP_METALLIC_ROUGHNESS] = ormTexture;
    p.textures[RenderItem::Pbr::MAP_OCCLUSION] = ormTexture;
    p.textures[RenderItem::Pbr::MAP_NORMAL] = normalTexture;
    p.clearcoatInMetallicRoughnessAlpha = orm;
    p.blend = shader == Shader::PbrBlended;
    // Glass: what it reflects is not faded with its alpha.
    p.glass = p.blend;
    p.alphaCutoff = alphaTest && !p.blend ? 0.51f : -1.0f;
    item.material.surface = p.blend ? RenderItem::SURFACE_BLENDED
            : alphaTest ? RenderItem::SURFACE_ALPHA_TEST : RenderItem::SURFACE_OPAQUE;
    item.material.lit = true;
    item.material.brightness = 1.0f;
    item.material.specularPower = 0.0f;
    item.material.nightTextureId = -1;
}

void tangents(int vertexCount, const VertexReader &vertex, const std::vector<int> &triangles,
              std::vector<float> &out) {
    out.assign(size_t(std::max(vertexCount, 0)) * 4, 0.0f);
    if (vertexCount <= 0)
        return;
    std::vector<float> tangentSum(size_t(vertexCount) * 3, 0.0f);
    std::vector<float> bitangentSum(size_t(vertexCount) * 3, 0.0f);
    float p[3][3], n[3], t[3][2];
    for (size_t k = 0; k + 2 < triangles.size(); k += 3) {
        const int ids[3] = {triangles[k], triangles[k + 1], triangles[k + 2]};
        bool valid = true;
        for (int c = 0; c < 3 && valid; ++c)
            valid = ids[c] >= 0 && ids[c] < vertexCount && vertex(ids[c], p[c], n, t[c]);
        if (!valid)
            continue;
        const float e1[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
        const float e2[3] = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
        const float du1 = t[1][0] - t[0][0], dv1 = t[1][1] - t[0][1];
        const float du2 = t[2][0] - t[0][0], dv2 = t[2][1] - t[0][1];
        const float det = du1 * dv2 - du2 * dv1;
        if (std::abs(det) < 1e-12f)
            continue;
        const float r = 1.0f / det;
        for (int id : ids)
            for (int c = 0; c < 3; ++c) {
                tangentSum[size_t(id) * 3 + c] += (e1[c] * dv2 - e2[c] * dv1) * r;
                bitangentSum[size_t(id) * 3 + c] += (e2[c] * du1 - e1[c] * du2) * r;
            }
    }
    for (int i = 0; i < vertexCount; ++i) {
        float position[3], uv[2];
        if (!vertex(i, position, n, uv))
            continue;
        const float normalLength = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (normalLength > 0.0f)
            for (float &c : n)
                c /= normalLength;
        const float *sum = &tangentSum[size_t(i) * 3];
        const float d = n[0] * sum[0] + n[1] * sum[1] + n[2] * sum[2];
        float o[3] = {sum[0] - n[0] * d, sum[1] - n[1] * d, sum[2] - n[2] * d};
        float length = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
        if (length < 1e-8f) {
            // Any direction across the normal.
            const float axis[3] = {std::abs(n[0]) < 0.9f ? 1.0f : 0.0f, std::abs(n[0]) < 0.9f ? 0.0f : 1.0f, 0.0f};
            const float e = n[0] * axis[0] + n[1] * axis[1];
            o[0] = axis[0] - n[0] * e;
            o[1] = axis[1] - n[1] * e;
            o[2] = -n[2] * e;
            length = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
        }
        const float *b = &bitangentSum[size_t(i) * 3];
        const float cross[3] = {n[1] * o[2] - n[2] * o[1], n[2] * o[0] - n[0] * o[2], n[0] * o[1] - n[1] * o[0]};
        float *tangent = &out[size_t(i) * 4];
        for (int c = 0; c < 3; ++c)
            tangent[c] = length > 0.0f ? o[c] / length : 0.0f;
        // The bitangent towards decreasing v (up the image: the map's +Y,
        // OpenGL normal maps); the gradient sums point down the image.
        tangent[3] = cross[0] * b[0] + cross[1] * b[1] + cross[2] * b[2] < 0.0f ? 1.0f : -1.0f;
    }
}

} // namespace MstsMaterials
