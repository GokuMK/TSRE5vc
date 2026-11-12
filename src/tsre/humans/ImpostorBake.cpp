/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ImpostorBake.h"

#include <QElapsedTimer>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <tsre/renderer/RenderItem.h>
#include <tsre/shape/SkeletalAnimation.h>
#include <tsre/texture/DxtCodec.h>

namespace Humans {

namespace {

// Floats of a PBR_SKINNED vertex: position, normal, texture coordinates,
// alpha, tangent (xyz, handedness), second coordinates, colour, joints,
// weights.
constexpr int Stride = RenderItem::PBR_SKINNED;
constexpr int Uv = 6, Tangent = 9, Joints = 19, Weights = 23;

// A texture to sample: RGBA8, repeating, bilinear. Large ones are halved
// down to 1024 texels first: a cell is a few hundred pixels.
struct Sampler {
    int width = 0;
    int height = 0;
    QImage image;
    bool load(const GltfShape::SkinnedSource::Image &source) {
        if (!source.bytes.isEmpty())
            image.loadFromData(source.bytes);
        else if (!source.path.isEmpty())
            image.load(source.path);
        if (image.isNull())
            return false;
        if (image.width() > 1024 || image.height() > 1024)
            image = image.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        image = image.convertToFormat(QImage::Format_RGBA8888);
        width = image.width();
        height = image.height();
        return width > 0 && height > 0;
    }
    void sample(float u, float v, float out[4]) const {
        const float x = u * float(width) - 0.5f, y = v * float(height) - 0.5f;
        const float fx0 = std::floor(x), fy0 = std::floor(y);
        const float fx = x - fx0, fy = y - fy0;
        auto wrap = [](long value, int size) { return int(((value % size) + size) % size); };
        const int x0 = wrap(long(fx0), width), x1 = wrap(long(fx0) + 1, width);
        const int y0 = wrap(long(fy0), height), y1 = wrap(long(fy0) + 1, height);
        const uchar *r0 = image.constScanLine(y0), *r1 = image.constScanLine(y1);
        for (int c = 0; c < 4; ++c) {
            const float top = float(r0[x0 * 4 + c]) * (1.0f - fx) + float(r0[x1 * 4 + c]) * fx;
            const float bottom = float(r1[x0 * 4 + c]) * (1.0f - fx) + float(r1[x1 * 4 + c]) * fx;
            out[c] = (top * (1.0f - fy) + bottom * fy) / 255.0f;
        }
    }
};

// The least alpha of each block of a texture (AlphaGrid blocks a side): a
// triangle whose texture coordinates stay where no texel is cut out needs no
// alpha test per sample.
constexpr int AlphaGrid = 128;

struct Material {
    Sampler color, normal;
    bool hasColor = false;
    bool hasNormal = false;
    const GltfShape::SkinnedSource::Primitive *source = nullptr;
    std::vector<float> leastAlpha;
    void makeAlphaGrid() {
        leastAlpha.assign(size_t(AlphaGrid) * AlphaGrid, 1.0f);
        if (!hasColor)
            return;
        for (int y = 0; y < color.height; ++y) {
            const uchar *row = color.image.constScanLine(y);
            const size_t gy = size_t(y) * AlphaGrid / size_t(color.height);
            for (int x = 0; x < color.width; ++x) {
                float &least = leastAlpha[gy * AlphaGrid + size_t(x) * AlphaGrid / size_t(color.width)];
                least = std::min(least, float(row[x * 4 + 3]) / 255.0f);
            }
        }
    }
    // Whether every texel the coordinates' box touches (with a block
    // around it) keeps its alpha at or above the cutoff.
    bool opaqueIn(float u0, float v0, float u1, float v1, float cutoff) const {
        if (cutoff < 0.0f || !hasColor)
            return true;
        if (u0 < 0.0f || v0 < 0.0f || u1 > 1.0f || v1 > 1.0f)
            return false;
        const int x0 = std::max(0, int(u0 * AlphaGrid) - 1), x1 = std::min(AlphaGrid - 1, int(u1 * AlphaGrid) + 1);
        const int y0 = std::max(0, int(v0 * AlphaGrid) - 1), y1 = std::min(AlphaGrid - 1, int(v1 * AlphaGrid) + 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if (leastAlpha[size_t(y) * AlphaGrid + size_t(x)] < cutoff)
                    return false;
        return true;
    }
};

// A primitive posed: positions, normals and tangents (xyz, handedness).
struct Posed {
    std::vector<float> position, normal, tangent;
};

void normalize(float v[3]) {
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length > 1e-12f)
        for (int c = 0; c < 3; ++c)
            v[c] /= length;
}

float dot3(const float *a, const float *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// What a sample sees: the nearest surface's depth, its primitive and
// triangle (-1: nothing) and two of its barycentric weights.
struct Hit {
    float depth = -std::numeric_limits<float>::infinity();
    int primitive = -1;
    int triangle = 0;
    float w0 = 0.0f;
    float w1 = 0.0f;
};

// The atlases' mipmaps, compressed (see ImpostorBake). The averages go
// down unscaled; each level's alpha is scaled per cell (while a cell is a
// pixel or more, then over the whole level) to keep level 0's share of
// texels at or above the cutoff.
void compressLevels(ImpostorBake::Result &r, float cutoff) {
    int w = r.color.width(), h = r.color.height();
    const int limit = int(std::lround(cutoff * 255.0f));
    std::vector<uchar> color(size_t(w) * size_t(h) * 4), normal(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y) {
        std::memcpy(color.data() + size_t(y) * size_t(w) * 4, r.color.constScanLine(y), size_t(w) * 4);
        std::memcpy(normal.data() + size_t(y) * size_t(w) * 4, r.normal.constScanLine(y), size_t(w) * 4);
    }
    // The share passing in a region of a level, its alpha times a scale.
    auto passing = [limit](const std::vector<uchar> &rgba, int width, int x0, int y0, int x1, int y1, float scale) {
        int passed = 0, all = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x, ++all)
                passed += float(rgba[(size_t(y) * size_t(width) + size_t(x)) * 4 + 3]) * scale >= float(limit);
        return all > 0 ? double(passed) / double(all) : 0.0;
    };
    const int cells = r.columns * r.rows;
    std::vector<double> target(static_cast<size_t>(cells));
    for (int cell = 0; cell < cells; ++cell) {
        const int cx = cell % r.columns, cy = cell / r.columns;
        target[size_t(cell)] = passing(color, w, cx * r.cellWidth, cy * r.cellHeight, (cx + 1) * r.cellWidth,
                                       (cy + 1) * r.cellHeight, 1.0f);
    }
    const double whole = passing(color, w, 0, 0, w, h, 1.0f);
    auto encode = [](const std::vector<uchar> &rgba, int width, int height, DxtCodec::Format format, bool alpha) {
        QByteArray blocks;
        QString error;
        DxtCodec::encode(rgba.data(), qsizetype(rgba.size()), width, height, 4, format, alpha, blocks, error);
        return blocks;
    };
    r.colorLevels.push_back(encode(color, w, h, DxtCodec::Format::Dxt5, true));
    r.normalLevels.push_back(encode(normal, w, h, DxtCodec::Format::Dxt1, false));
    while (w > 1 || h > 1) {
        const int lw = std::max(1, w / 2), lh = std::max(1, h / 2);
        std::vector<uchar> nextColor(size_t(lw) * size_t(lh) * 4), nextNormal(size_t(lw) * size_t(lh) * 4);
        for (int y = 0; y < lh; ++y)
            for (int x = 0; x < lw; ++x) {
                const int sx[2] = {std::min(2 * x, w - 1), std::min(2 * x + 1, w - 1)};
                const int sy[2] = {std::min(2 * y, h - 1), std::min(2 * y + 1, h - 1)};
                float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f}, n[3] = {0.0f, 0.0f, 0.0f};
                for (int j = 0; j < 2; ++j)
                    for (int i = 0; i < 2; ++i) {
                        const size_t at = (size_t(sy[j]) * size_t(w) + size_t(sx[i])) * 4;
                        for (int c = 0; c < 4; ++c)
                            sum[c] += float(color[at + size_t(c)]);
                        for (int c = 0; c < 3; ++c)
                            n[c] += float(normal[at + size_t(c)]) / 127.5f - 1.0f;
                    }
                normalize(n);
                const size_t out = (size_t(y) * size_t(lw) + size_t(x)) * 4;
                for (int c = 0; c < 4; ++c)
                    nextColor[out + size_t(c)] = uchar(std::lround(sum[c] / 4.0f));
                for (int c = 0; c < 3; ++c)
                    nextNormal[out + size_t(c)] = uchar(std::clamp(int(std::lround((n[c] + 1.0f) * 127.5f)), 0, 255));
                nextNormal[out + 3] = 255;
            }
        // Alpha scaled to the coverage wanted, region by region.
        std::vector<uchar> scaled = nextColor;
        const bool byCell = lw >= r.columns && lh >= r.rows;
        const int regions = byCell ? cells : 1;
        for (int region = 0; region < regions; ++region) {
            int x0 = 0, y0 = 0, x1 = lw, y1 = lh;
            if (byCell) {
                const int cw = lw / r.columns, ch = lh / r.rows;
                x0 = (region % r.columns) * cw;
                y0 = (region / r.columns) * ch;
                x1 = x0 + cw;
                y1 = y0 + ch;
            }
            const double want = byCell ? target[size_t(region)] : whole;
            float low = 1.0f / 16.0f, high = 16.0f;
            for (int step = 0; step < 20; ++step) {
                const float mid = std::sqrt(low * high);
                if (passing(nextColor, lw, x0, y0, x1, y1, mid) < want)
                    low = mid;
                else
                    high = mid;
            }
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    uchar &a = scaled[(size_t(y) * size_t(lw) + size_t(x)) * 4 + 3];
                    a = uchar(std::min(255, int(std::lround(float(a) * high))));
                }
        }
        r.colorLevels.push_back(encode(scaled, lw, lh, DxtCodec::Format::Dxt5, true));
        r.normalLevels.push_back(encode(nextNormal, lw, lh, DxtCodec::Format::Dxt1, false));
        color.swap(nextColor);
        normal.swap(nextNormal);
        w = lw;
        h = lh;
    }
}

}

void ImpostorBake::Result::cellRect(int cell, float uv[4]) const {
    const int direction = directions > 0 ? cell % directions : 0;
    const int pose = directions > 0 ? cell / directions : 0;
    const float width = float(columns * cellWidth), height = float(rows * cellHeight);
    uv[0] = float(direction * cellWidth) / width;
    uv[1] = float(pose * cellHeight) / height;
    uv[2] = float((direction + 1) * cellWidth) / width;
    uv[3] = float((pose + 1) * cellHeight) / height;
}

ImpostorBake::Result ImpostorBake::bake(const GltfShape::SkinnedSource &source, const SkeletalAnimation &rig,
                                        const Request &request) {
    QElapsedTimer timer;
    timer.start();
    Result result;
    const int directions = std::clamp(request.directions, 1, 64);
    const int ss = std::clamp(request.supersample, 1, 4);
    const int cw = std::clamp(request.cellWidth, 4, 1024), ch = std::clamp(request.cellHeight, 4, 1024);
    std::vector<Pose> poses = request.poses;
    if (poses.empty())
        poses.push_back(Pose());
    if (source.primitives.empty()) {
        result.error = "no skinned triangles";
        return result;
    }

    std::vector<Material> materials(source.primitives.size());
    for (size_t m = 0; m < materials.size(); ++m) {
        materials[m].source = &source.primitives[m];
        materials[m].hasColor = materials[m].color.load(source.primitives[m].color);
        materials[m].hasNormal = materials[m].normal.load(source.primitives[m].normal);
        materials[m].makeAlphaGrid();
    }

    // Every pose's vertices, skinned on the CPU as the renderers' programs
    // skin them, and the bounds of them all.
    std::shared_ptr<SkeletalAnimation::Pose> pose = rig.newPose();
    std::vector<std::vector<Posed>> posed(poses.size(), std::vector<Posed>(source.primitives.size()));
    float radius = 0.0f, low = std::numeric_limits<float>::max(), high = -std::numeric_limits<float>::max();
    for (size_t p = 0; p < poses.size(); ++p) {
        SkeletalAnimation::Play play;
        play.clip = poses[p].clip;
        play.time = poses[p].time;
        rig.evaluate(*pose, &play, poses[p].clip >= 0 ? 1 : 0);
        for (size_t m = 0; m < source.primitives.size(); ++m) {
            const GltfShape::SkinnedSource::Primitive &prim = source.primitives[m];
            const float *rows = rig.jointRows(*pose, prim.skin);
            const int joints = prim.skin >= 0 && prim.skin < rig.skinCount() ? rig.jointCount(prim.skin) : 0;
            const size_t count = prim.vertices.size() / Stride;
            Posed &out = posed[p][m];
            out.position.resize(count * 3);
            out.normal.resize(count * 3);
            out.tangent.resize(count * 4);
            for (size_t i = 0; i < count; ++i) {
                const float *v = prim.vertices.data() + i * Stride;
                float matrix[12] = {};
                for (int k = 0; k < 4; ++k) {
                    const int joint = int(v[Joints + k] + 0.5f);
                    const float weight = v[Weights + k];
                    if (weight <= 0.0f || rows == nullptr || joint < 0 || joint >= joints)
                        continue;
                    for (int e = 0; e < 12; ++e)
                        matrix[e] += rows[joint * 12 + e] * weight;
                }
                float *position = out.position.data() + i * 3;
                float *normal = out.normal.data() + i * 3;
                float *tangent = out.tangent.data() + i * 4;
                for (int r = 0; r < 3; ++r) {
                    const float *row = matrix + r * 4;
                    position[r] = row[0] * v[0] + row[1] * v[1] + row[2] * v[2] + row[3];
                    normal[r] = row[0] * v[3] + row[1] * v[4] + row[2] * v[5];
                    tangent[r] = row[0] * v[Tangent] + row[1] * v[Tangent + 1] + row[2] * v[Tangent + 2];
                }
                tangent[3] = v[Tangent + 3] < 0.0f ? -1.0f : 1.0f;
                normalize(normal);
                normalize(tangent);
                radius = std::max(radius, std::sqrt(position[0] * position[0] + position[2] * position[2]));
                low = std::min(low, position[1]);
                high = std::max(high, position[1]);
            }
        }
    }
    if (!(high > low)) {
        result.error = "no height";
        return result;
    }

    // One rectangle for every cell, square pixels: the feet at the bottom.
    const float metres = std::max(2.0f * radius / float(cw), (high - low) / float(ch)) * 1.03f;
    result.directions = directions;
    result.poses = int(poses.size());
    result.columns = directions;
    result.rows = int(poses.size());
    result.cellWidth = cw;
    result.cellHeight = ch;
    result.halfWidth = 0.5f * metres * float(cw);
    result.bottom = low - 0.01f * (high - low);
    result.top = result.bottom + metres * float(ch);
    result.color = QImage(directions * cw, int(poses.size()) * ch, QImage::Format_RGBA8888);
    result.normal = QImage(directions * cw, int(poses.size()) * ch, QImage::Format_RGBA8888);

    const int sw = cw * ss, sh = ch * ss;
    const float pixel = metres / float(ss);
    std::vector<Hit> hits(size_t(sw) * size_t(sh));
    // A cell's pixels: colour, normal, coverage (0: none, to be filled from
    // the neighbours).
    std::vector<float> cellColor(size_t(cw) * ch * 3), cellNormal(size_t(cw) * ch * 3), cellAlpha(size_t(cw) * ch);
    std::vector<char> filled(size_t(cw) * ch), next(size_t(cw) * ch);
    for (size_t p = 0; p < poses.size(); ++p) {
        for (int d = 0; d < directions; ++d) {
            const float angle = 2.0f * float(M_PI) * float(d) / float(directions);
            // Towards the viewer, right, up.
            const float toward[3] = {std::sin(angle), 0.0f, std::cos(angle)};
            const float right[3] = {std::cos(angle), 0.0f, -std::sin(angle)};
            std::fill(hits.begin(), hits.end(), Hit());
            // What each sample sees, nearest first; cut-out texels let the
            // view through.
            for (size_t m = 0; m < source.primitives.size(); ++m) {
                const Material &material = materials[m];
                const GltfShape::SkinnedSource::Primitive &prim = *material.source;
                const Posed &mesh = posed[p][m];
                const size_t triangles = mesh.position.size() / 9;
                for (size_t t = 0; t < triangles; ++t) {
                    float sx[3], sy[3], depth[3];
                    const float *v[3];
                    float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
                    for (int k = 0; k < 3; ++k) {
                        const float *position = mesh.position.data() + (t * 3 + size_t(k)) * 3;
                        sx[k] = (dot3(position, right) + result.halfWidth) / pixel;
                        sy[k] = (result.top - position[1]) / pixel;
                        depth[k] = dot3(position, toward);
                        v[k] = prim.vertices.data() + (t * 3 + size_t(k)) * Stride;
                        u0 = std::min(u0, v[k][Uv]);
                        u1 = std::max(u1, v[k][Uv]);
                        v0 = std::min(v0, v[k][Uv + 1]);
                        v1 = std::max(v1, v[k][Uv + 1]);
                    }
                    const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0]);
                    if (std::fabs(area) < 1e-9f)
                        continue;
                    const int x0 = std::max(0, int(std::floor(std::min({sx[0], sx[1], sx[2]}))));
                    const int x1 = std::min(sw - 1, int(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
                    const int y0 = std::max(0, int(std::floor(std::min({sy[0], sy[1], sy[2]}))));
                    const int y1 = std::min(sh - 1, int(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
                    if (x0 > x1 || y0 > y1)
                        continue;
                    const bool tested = !material.opaqueIn(u0, v0, u1, v1, prim.alphaCutoff);
                    // The weights are linear in the sample's place.
                    const float a0 = (sy[1] - sy[2]) / area, b0 = (sx[2] - sx[1]) / area;
                    const float c0 = (sx[1] * sy[2] - sy[1] * sx[2]) / area;
                    const float a1 = (sy[2] - sy[0]) / area, b1 = (sx[0] - sx[2]) / area;
                    const float c1 = (sx[2] * sy[0] - sy[2] * sx[0]) / area;
                    for (int y = y0; y <= y1; ++y) {
                        const float py = float(y) + 0.5f;
                        Hit *row = hits.data() + size_t(y) * size_t(sw);
                        for (int x = x0; x <= x1; ++x) {
                            const float px = float(x) + 0.5f;
                            const float w0 = a0 * px + b0 * py + c0;
                            const float w1 = a1 * px + b1 * py + c1;
                            const float w2 = 1.0f - w0 - w1;
                            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                                continue;
                            const float z = w0 * depth[0] + w1 * depth[1] + w2 * depth[2];
                            Hit &hit = row[x];
                            if (z <= hit.depth)
                                continue;
                            if (tested) {
                                float color[4];
                                material.color.sample(w0 * v[0][Uv] + w1 * v[1][Uv] + w2 * v[2][Uv],
                                                      w0 * v[0][Uv + 1] + w1 * v[1][Uv + 1] + w2 * v[2][Uv + 1], color);
                                if (color[3] * prim.baseColor[3] < prim.alphaCutoff)
                                    continue;
                            }
                            hit.depth = z;
                            hit.primitive = int(m);
                            hit.triangle = int(t);
                            hit.w0 = w0;
                            hit.w1 = w1;
                        }
                    }
                }
            }

            // Each visible sample shaded once; a pixel takes their mean and
            // its coverage.
            auto shade = [&](const Hit &hit, float color[3], float normalOut[3]) {
                const Material &material = materials[size_t(hit.primitive)];
                const GltfShape::SkinnedSource::Primitive &prim = *material.source;
                const Posed &mesh = posed[p][size_t(hit.primitive)];
                const size_t t = size_t(hit.triangle);
                const float w[3] = {hit.w0, hit.w1, 1.0f - hit.w0 - hit.w1};
                float u = 0.0f, vv = 0.0f, normal[3] = {0.0f, 0.0f, 0.0f}, tangent[3] = {0.0f, 0.0f, 0.0f};
                for (int k = 0; k < 3; ++k) {
                    const float *vertex = prim.vertices.data() + (t * 3 + size_t(k)) * Stride;
                    u += w[k] * vertex[Uv];
                    vv += w[k] * vertex[Uv + 1];
                    for (int c = 0; c < 3; ++c) {
                        normal[c] += w[k] * mesh.normal[(t * 3 + size_t(k)) * 3 + size_t(c)];
                        tangent[c] += w[k] * mesh.tangent[(t * 3 + size_t(k)) * 4 + size_t(c)];
                    }
                }
                float sampled[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                if (material.hasColor)
                    material.color.sample(u, vv, sampled);
                normalize(normal);
                if (material.hasNormal) {
                    // The normal map in the vertex's tangent space.
                    const float along = dot3(tangent, normal);
                    for (int c = 0; c < 3; ++c)
                        tangent[c] -= normal[c] * along;
                    normalize(tangent);
                    const float handed = mesh.tangent[(t * 3) * 4 + 3];
                    const float bitangent[3] = {
                        (normal[1] * tangent[2] - normal[2] * tangent[1]) * handed,
                        (normal[2] * tangent[0] - normal[0] * tangent[2]) * handed,
                        (normal[0] * tangent[1] - normal[1] * tangent[0]) * handed};
                    float map[4];
                    material.normal.sample(u, vv, map);
                    const float nx = (2.0f * map[0] - 1.0f) * prim.normalScale;
                    const float ny = (2.0f * map[1] - 1.0f) * prim.normalScale;
                    const float nz = 2.0f * map[2] - 1.0f;
                    for (int c = 0; c < 3; ++c)
                        normal[c] = tangent[c] * nx + bitangent[c] * ny + normal[c] * nz;
                    normalize(normal);
                }
                // A face seen from behind (hair cards) lit from the front.
                float facing = dot3(normal, toward);
                if (facing < 0.0f) {
                    for (float &c : normal)
                        c = -c;
                    facing = -facing;
                }
                for (int c = 0; c < 3; ++c)
                    color[c] = sampled[c] * prim.baseColor[c];
                normalOut[0] = dot3(normal, right);
                normalOut[1] = normal[1];
                normalOut[2] = facing;
            };
            for (int y = 0; y < ch; ++y)
                for (int x = 0; x < cw; ++x) {
                    float color[3] = {0.0f, 0.0f, 0.0f}, normal[3] = {0.0f, 0.0f, 0.0f};
                    int covered = 0;
                    for (int j = 0; j < ss; ++j)
                        for (int i = 0; i < ss; ++i) {
                            const Hit &hit = hits[size_t(y * ss + j) * size_t(sw) + size_t(x * ss + i)];
                            if (hit.primitive < 0)
                                continue;
                            ++covered;
                            float c[3], n[3];
                            shade(hit, c, n);
                            for (int k = 0; k < 3; ++k) {
                                color[k] += c[k];
                                normal[k] += n[k];
                            }
                        }
                    const size_t at = size_t(y) * size_t(cw) + size_t(x);
                    cellAlpha[at] = float(covered) / float(ss * ss);
                    filled[at] = covered > 0;
                    if (covered == 0)
                        continue;
                    normalize(normal);
                    for (int k = 0; k < 3; ++k) {
                        cellColor[at * 3 + size_t(k)] = color[k] / float(covered);
                        cellNormal[at * 3 + size_t(k)] = normal[k];
                    }
                }
            // Uncovered pixels take their covered neighbours' values, a ring
            // at a time; the rest the cell's mean.
            for (int pass = 0; pass < 8; ++pass) {
                bool grew = false;
                next = filled;
                for (int y = 0; y < ch; ++y)
                    for (int x = 0; x < cw; ++x) {
                        const size_t at = size_t(y) * size_t(cw) + size_t(x);
                        if (filled[at])
                            continue;
                        float color[3] = {0.0f, 0.0f, 0.0f}, normal[3] = {0.0f, 0.0f, 0.0f};
                        int found = 0;
                        for (int j = -1; j <= 1; ++j)
                            for (int i = -1; i <= 1; ++i) {
                                const int nx = x + i, ny = y + j;
                                if (nx < 0 || ny < 0 || nx >= cw || ny >= ch)
                                    continue;
                                const size_t n = size_t(ny) * size_t(cw) + size_t(nx);
                                if (!filled[n])
                                    continue;
                                ++found;
                                for (int c = 0; c < 3; ++c) {
                                    color[c] += cellColor[n * 3 + size_t(c)];
                                    normal[c] += cellNormal[n * 3 + size_t(c)];
                                }
                            }
                        if (found == 0)
                            continue;
                        normalize(normal);
                        for (int c = 0; c < 3; ++c) {
                            cellColor[at * 3 + size_t(c)] = color[c] / float(found);
                            cellNormal[at * 3 + size_t(c)] = normal[c];
                        }
                        next[at] = 1;
                        grew = true;
                    }
                filled.swap(next);
                if (!grew)
                    break;
            }
            double mean[3] = {0.0, 0.0, 0.0};
            int meanCount = 0;
            for (size_t at = 0; at < filled.size(); ++at)
                if (cellAlpha[at] > 0.0f) {
                    ++meanCount;
                    for (int c = 0; c < 3; ++c)
                        mean[c] += double(cellColor[at * 3 + size_t(c)]);
                }
            for (size_t at = 0; at < filled.size(); ++at)
                if (!filled[at])
                    for (int c = 0; c < 3; ++c) {
                        cellColor[at * 3 + size_t(c)] = meanCount > 0 ? float(mean[c] / meanCount) : 0.5f;
                        cellNormal[at * 3 + size_t(c)] = c == 2 ? 1.0f : 0.0f;
                    }
            // Into the atlases.
            const int left = d * cw, upper = int(p) * ch;
            auto byte = [](float value) { return uchar(std::clamp(int(std::lround(value * 255.0f)), 0, 255)); };
            for (int y = 0; y < ch; ++y) {
                uchar *color = result.color.scanLine(upper + y) + left * 4;
                uchar *normal = result.normal.scanLine(upper + y) + left * 4;
                for (int x = 0; x < cw; ++x) {
                    const size_t at = size_t(y) * size_t(cw) + size_t(x);
                    for (int c = 0; c < 3; ++c) {
                        color[x * 4 + c] = byte(cellColor[at * 3 + size_t(c)]);
                        normal[x * 4 + c] = byte(0.5f + 0.5f * cellNormal[at * 3 + size_t(c)]);
                    }
                    color[x * 4 + 3] = byte(cellAlpha[at]);
                    normal[x * 4 + 3] = 255;
                }
            }
        }
    }
    if (request.compress)
        compressLevels(result, request.alphaCutoff);
    result.ok = true;
    result.milliseconds = double(timer.nsecsElapsed()) / 1e6;
    return result;
}

}
