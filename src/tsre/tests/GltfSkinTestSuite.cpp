/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Skinned and animated glTF (task humans 02): the Khronos samples' skins and
// clips as TSRE reads and plays them (through ozz), against an evaluation
// of the same files written here from the glTF specification: node
// matrices at several times of every clip (LINEAR with slerp, STEP,
// CUBICSPLINE), skins' joint matrices, and the packets a posed state
// submits. Needs TSRE_GLTF_SAMPLE_ASSETS (the glTF-Sample-Assets folder);
// TSRE_HUMANS_SHAPES (humans-gen's out/route/SHAPES) adds the humans.

#include "GltfSkinTestSuite.h"
#include "TokenTestSupport.h"
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/shape/SkeletalAnimation.h>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>

namespace {

using Mat = std::array<float, 16>;

Mat identityMat() {
    Mat m{};
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    return m;
}

Mat multiply(const Mat &a, const Mat &b) {
    Mat out{};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += a[size_t(k * 4 + r)] * b[size_t(c * 4 + k)];
            out[size_t(c * 4 + r)] = sum;
        }
    return out;
}

Mat fromTrs(const float *t, const float *q, const float *s) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    Mat m = identityMat();
    m[0] = (1 - 2 * (y * y + z * z)) * s[0];
    m[1] = (2 * (x * y + z * w)) * s[0];
    m[2] = (2 * (x * z - y * w)) * s[0];
    m[4] = (2 * (x * y - z * w)) * s[1];
    m[5] = (1 - 2 * (x * x + z * z)) * s[1];
    m[6] = (2 * (y * z + x * w)) * s[1];
    m[8] = (2 * (x * z + y * w)) * s[2];
    m[9] = (2 * (y * z - x * w)) * s[2];
    m[10] = (1 - 2 * (x * x + y * y)) * s[2];
    m[12] = t[0];
    m[13] = t[1];
    m[14] = t[2];
    return m;
}

// The file, read here independently of GltfShape.
struct Reference {
    QJsonObject root;
    std::vector<QByteArray> buffers;
    struct Node {
        int parent = -1;
        bool hasMatrix = false;
        Mat matrix = identityMat();
        float t[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
    };
    std::vector<Node> nodes;

    bool load(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        const QByteArray bytes = file.readAll();
        QByteArray json, bin;
        if (path.endsWith(".glb", Qt::CaseInsensitive)) {
            int at = 12;
            while (at + 8 <= bytes.size()) {
                const quint32 length = qFromLittleEndian<quint32>(bytes.constData() + at);
                const quint32 type = qFromLittleEndian<quint32>(bytes.constData() + at + 4);
                const QByteArray chunk = bytes.mid(at + 8, int(length));
                if (type == 0x4E4F534Au)
                    json = chunk;
                else if (type == 0x004E4942u)
                    bin = chunk;
                at += 8 + int(length);
            }
        } else {
            json = bytes;
        }
        root = QJsonDocument::fromJson(json).object();
        for (const QJsonValue &value : root.value("buffers").toArray()) {
            const QString uri = value.toObject().value("uri").toString();
            if (uri.isEmpty())
                buffers.push_back(bin);
            else if (uri.startsWith("data:"))
                buffers.push_back(QByteArray::fromBase64(uri.section(',', 1).toLatin1()));
            else {
                QFile buffer(QFileInfo(path).path() + "/" + uri);
                buffers.push_back(buffer.open(QIODevice::ReadOnly) ? buffer.readAll() : QByteArray());
            }
        }
        const QJsonArray list = root.value("nodes").toArray();
        nodes.resize(size_t(list.size()));
        for (int i = 0; i < list.size(); ++i) {
            const QJsonObject o = list[i].toObject();
            Node &n = nodes[size_t(i)];
            if (o.contains("matrix")) {
                n.hasMatrix = true;
                const QJsonArray m = o.value("matrix").toArray();
                for (int k = 0; k < 16 && k < m.size(); ++k)
                    n.matrix[size_t(k)] = float(m[k].toDouble());
            }
            const QJsonArray t = o.value("translation").toArray(), r = o.value("rotation").toArray(),
                    s = o.value("scale").toArray();
            for (int k = 0; k < 3 && k < t.size(); ++k)
                n.t[k] = float(t[k].toDouble());
            for (int k = 0; k < 4 && k < r.size(); ++k)
                n.r[k] = float(r[k].toDouble());
            for (int k = 0; k < 3 && k < s.size(); ++k)
                n.s[k] = float(s[k].toDouble());
        }
        for (int i = 0; i < list.size(); ++i)
            for (const QJsonValue &child : list[i].toObject().value("children").toArray())
                if (child.toInt() >= 0 && size_t(child.toInt()) < nodes.size())
                    nodes[size_t(child.toInt())].parent = i;
        return !root.isEmpty();
    }

    // An accessor's values as floats (integers normalized).
    std::vector<float> floats(int index) const {
        std::vector<float> out;
        const QJsonObject a = root.value("accessors").toArray()[index].toObject();
        const QJsonObject view = root.value("bufferViews").toArray()[a.value("bufferView").toInt()].toObject();
        const QByteArray &buffer = buffers[size_t(view.value("buffer").toInt())];
        const QString type = a.value("type").toString();
        const int components = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4
                : type == "MAT4" ? 16 : 0;
        const int componentType = a.value("componentType").toInt();
        const int size = componentType == 5126 ? 4 : componentType == 5122 || componentType == 5123 ? 2 : 1;
        const int stride = view.value("byteStride").toInt(components * size);
        const int base = view.value("byteOffset").toInt() + a.value("byteOffset").toInt();
        for (int i = 0; i < a.value("count").toInt(); ++i)
            for (int c = 0; c < components; ++c) {
                const char *p = buffer.constData() + base + i * stride + c * size;
                float v = 0.0f;
                switch (componentType) {
                case 5126: v = qFromLittleEndian<float>(p); break;
                case 5120: v = std::max(float(*reinterpret_cast<const qint8 *>(p)) / 127.0f, -1.0f); break;
                case 5121: v = float(*reinterpret_cast<const quint8 *>(p)) / 255.0f; break;
                case 5122: v = std::max(float(qFromLittleEndian<qint16>(p)) / 32767.0f, -1.0f); break;
                case 5123: v = float(qFromLittleEndian<quint16>(p)) / 65535.0f; break;
                }
                out.push_back(v);
            }
        return out;
    }

    float duration(int animation) const {
        float end = 0.0f;
        const QJsonObject a = root.value("animations").toArray()[animation].toObject();
        for (const QJsonValue &sampler : a.value("samplers").toArray()) {
            const std::vector<float> times = floats(sampler.toObject().value("input").toInt());
            if (!times.empty())
                end = std::max(end, times.back());
        }
        return end;
    }

    // Node world matrices with an animation at a time (-1: rest).
    std::vector<Mat> world(int animation, float time) const {
        std::vector<Node> posed = nodes;
        if (animation >= 0) {
            const QJsonObject a = root.value("animations").toArray()[animation].toObject();
            const QJsonArray samplers = a.value("samplers").toArray();
            for (const QJsonValue &value : a.value("channels").toArray()) {
                const QJsonObject channel = value.toObject();
                const QJsonObject target = channel.value("target").toObject();
                const QString path = target.value("path").toString();
                const int node = target.value("node").toInt(-1);
                if (node < 0 || size_t(node) >= posed.size() || path == "weights")
                    continue;
                const QJsonObject sampler = samplers[channel.value("sampler").toInt()].toObject();
                const std::vector<float> times = floats(sampler.value("input").toInt());
                const std::vector<float> values = floats(sampler.value("output").toInt());
                const QString interpolation = sampler.value("interpolation").toString("LINEAR");
                const int n = path == "rotation" ? 4 : 3;
                float out[4];
                sample(times, values, n, interpolation, path == "rotation", time, out);
                Node &target_ = posed[size_t(node)];
                target_.hasMatrix = false;
                std::copy(out, out + n, path == "translation" ? target_.t : path == "rotation" ? target_.r : target_.s);
            }
        }
        std::vector<Mat> local(posed.size()), result(posed.size());
        for (size_t i = 0; i < posed.size(); ++i)
            local[i] = posed[i].hasMatrix ? posed[i].matrix : fromTrs(posed[i].t, posed[i].r, posed[i].s);
        std::vector<char> done(posed.size(), 0);
        std::function<const Mat &(size_t)> resolve = [&](size_t i) -> const Mat & {
            if (!done[i]) {
                done[i] = 1;
                const int parent = posed[i].parent;
                result[i] = parent >= 0 ? multiply(resolve(size_t(parent)), local[i]) : local[i];
            }
            return result[i];
        };
        for (size_t i = 0; i < posed.size(); ++i)
            resolve(i);
        return result;
    }

    static void sample(const std::vector<float> &times, const std::vector<float> &values, int n,
                       const QString &interpolation, bool rotation, float t, float *out) {
        const bool cubic = interpolation == "CUBICSPLINE";
        const int perKey = cubic ? 3 * n : n;
        auto value = [&](size_t k, int c) { return values[k * size_t(perKey) + size_t(cubic ? n : 0) + size_t(c)]; };
        if (t <= times.front()) {
            for (int c = 0; c < n; ++c) out[c] = value(0, c);
            return;
        }
        if (t >= times.back()) {
            for (int c = 0; c < n; ++c) out[c] = value(times.size() - 1, c);
            return;
        }
        size_t k = 0;
        while (k + 1 < times.size() && times[k + 1] <= t)
            ++k;
        const float span = times[k + 1] - times[k];
        const float u = span > 0.0f ? (t - times[k]) / span : 0.0f;
        if (interpolation == "STEP") {
            for (int c = 0; c < n; ++c) out[c] = value(k, c);
            return;
        }
        if (cubic) {
            const float u2 = u * u, u3 = u2 * u;
            for (int c = 0; c < n; ++c) {
                const float b0 = values[k * size_t(perKey) + size_t(2 * n) + size_t(c)];
                const float a1 = values[(k + 1) * size_t(perKey) + size_t(c)];
                out[c] = (2 * u3 - 3 * u2 + 1) * value(k, c) + (u3 - 2 * u2 + u) * span * b0
                        + (-2 * u3 + 3 * u2) * value(k + 1, c) + (u3 - u2) * span * a1;
            }
        } else if (rotation) {
            float a[4], b[4];
            for (int c = 0; c < 4; ++c) { a[c] = value(k, c); b[c] = value(k + 1, c); }
            float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
            if (d < 0.0f) { d = -d; for (float &c : b) c = -c; }
            if (d > 0.9995f) {
                for (int c = 0; c < 4; ++c) out[c] = a[c] + (b[c] - a[c]) * u;
            } else {
                const float theta = std::acos(std::min(d, 1.0f));
                const float wa = std::sin((1 - u) * theta) / std::sin(theta), wb = std::sin(u * theta) / std::sin(theta);
                for (int c = 0; c < 4; ++c) out[c] = wa * a[c] + wb * b[c];
            }
        } else {
            for (int c = 0; c < n; ++c) out[c] = value(k, c) + (value(k + 1, c) - value(k, c)) * u;
        }
        if (rotation) {
            const float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
            for (int c = 0; c < 4; ++c) out[c] /= length > 0.0f ? length : 1.0f;
        }
    }
};

// The largest difference between two matrices' entries, the translation
// relative to the model's size.
float matrixGap(const float *a, const float *b, float size) {
    float gap = 0.0f;
    for (int i = 0; i < 16; ++i) {
        const float scale = i >= 12 && i < 15 ? std::max(size, 1e-3f) : 1.0f;
        gap = std::max(gap, std::abs(a[i] - b[i]) / scale);
    }
    return gap;
}

struct PacketRecorder : RenderQueue {
    QVector<RenderItem *> packets;
    using RenderQueue::submit;
    void submit(RenderItem *item, quint32 = 0, SubmitOrder = SUBMIT_GROUPED) override { packets.push_back(item); }
    void submit(const QVector<RenderItem *> &items, quint32 = 0) override { packets += items; }
    void submitFrameItem(RenderItem *item) override { packets.push_back(item); }
};

QString samplePath(const QString &models, const QString &name) {
    const QString binary = models + "/" + name + "/glTF-Binary/" + name + ".glb";
    if (QFileInfo::exists(binary))
        return binary;
    return models + "/" + name + "/glTF/" + name + ".gltf";
}

} // namespace

namespace TsreTests {

int runGltfSkinSuite(bool verbose) {
    TokenTest::Suite test{"[tests:gltf-skin]", verbose};
    const QString root = qEnvironmentVariable("TSRE_GLTF_SAMPLE_ASSETS");
    if (root.isEmpty() || !QFileInfo::exists(root + "/Models")) {
        qInfo() << "[tests:gltf-skin] set TSRE_GLTF_SAMPLE_ASSETS to the glTF-Sample-Assets folder";
        return 2;
    }
    const QString models = root + "/Models";

    struct Case {
        const char *name;
        int skins;     // -1: any
        int clips;     // -1: any
        bool skinned;  // its packets skinned
    };
    const Case cases[] = {
        {"SimpleSkin", 1, 1, true},     {"RiggedSimple", 1, 1, true}, {"RiggedFigure", 1, 1, true},
        {"CesiumMan", 1, 1, true},      {"Fox", 1, 3, true},          {"BrainStem", -1, 1, true},
        {"RecursiveSkeletons", -1, -1, true}, {"InterpolationTest", 0, -1, false},
        {"AnimatedCube", 0, 1, false},  {"BoxAnimated", 0, 1, false},
    };
    for (const Case &c : cases) {
        const QString path = samplePath(models, c.name);
        Reference reference;
        if (!reference.load(path)) {
            test.check(false, QString("%1: the reference reads %2").arg(c.name, path));
            continue;
        }
        GltfShape shape(path, QFileInfo(path).fileName(), QFileInfo(path).path());
        shape.load();
        test.check(shape.isLoaded(), QString("%1 loads").arg(c.name));
        const SkeletalAnimation *rig = shape.skeleton();
        test.check(rig != nullptr, QString("%1 has a skeleton").arg(c.name));
        if (!shape.isLoaded() || rig == nullptr)
            continue;
        const int fileClips = reference.root.value("animations").toArray().size();
        test.check(shape.clipCount() == fileClips && (c.clips < 0 || fileClips == c.clips),
                   QString("%1: %2 clips (the file %3)").arg(c.name).arg(shape.clipCount()).arg(fileClips));
        test.check(c.skins < 0 || rig->skinCount() == c.skins,
                   QString("%1: %2 skins").arg(c.name).arg(rig->skinCount()));
        const float size = std::max(shape.getSize(), 1e-3f);

        // Node matrices: rest, then each clip at its start, between keys
        // and at its end.
        float worstNode = 0.0f, worstJoint = 0.0f;
        int compared = 0;
        auto compare = [&](int clip, float time) {
            ComplexShape::ClipPlay play;
            play.clip = clip;
            play.time = time;
            shape.setClips(0, &play, clip >= 0 ? 1 : 0);
            shape.updatePose(0);
            const std::vector<Mat> expected = reference.world(clip, time);
            for (size_t n = 0; n < expected.size(); ++n) {
                const float *got = shape.poseNodeMatrix(0, int(n));
                if (got == nullptr)
                    continue;
                worstNode = std::max(worstNode, matrixGap(got, expected[n].data(), size));
                ++compared;
            }
            // Skins: rows of joint global times inverse bind.
            const QJsonArray skins = reference.root.value("skins").toArray();
            for (int s = 0; s < skins.size() && s < rig->skinCount(); ++s) {
                const QJsonObject skin = skins[s].toObject();
                const QJsonArray joints = skin.value("joints").toArray();
                const std::vector<float> inverse = skin.contains("inverseBindMatrices")
                        ? reference.floats(skin.value("inverseBindMatrices").toInt()) : std::vector<float>();
                const float *rows = shape.poseJointRows(0, s);
                if (rows == nullptr)
                    continue;
                for (int k = 0; k < joints.size(); ++k) {
                    Mat ibm = identityMat();
                    if (inverse.size() >= size_t(k + 1) * 16)
                        std::copy(inverse.begin() + k * 16, inverse.begin() + k * 16 + 16, ibm.begin());
                    const Mat m = multiply(expected[size_t(joints[k].toInt())], ibm);
                    for (int r = 0; r < 3; ++r)
                        for (int col = 0; col < 4; ++col) {
                            const float scale = col == 3 ? size : 1.0f;
                            worstJoint = std::max(worstJoint, std::abs(rows[k * 12 + r * 4 + col] - m[size_t(col * 4 + r)]) / scale);
                        }
                }
            }
        };
        compare(-1, 0.0f);
        for (int clip = 0; clip < fileClips; ++clip) {
            const float duration = reference.duration(clip);
            for (float f : {0.0f, 0.37f, 0.5f, 1.0f})
                compare(clip, duration * f);
        }
        // ozz interpolates rotations normalized-linearly and samples
        // CUBICSPLINE at 60 a second: a little off the specification's slerp
        // and curve between keys.
        test.check(compared > 0 && worstNode < 0.02f,
                   QString("%1: node matrices as the specification's, worst %2 (%3 compared)")
                   .arg(c.name).arg(double(worstNode)).arg(compared));
        test.check(worstJoint < 0.02f, QString("%1: joint matrices, worst %2").arg(c.name).arg(double(worstJoint)));

        // Packets: skinned ones point at the state's joints, in the
        // skeleton's space; two states pose apart.
        const unsigned int other = shape.newState();
        ComplexShape::ClipPlay a, b;
        a.clip = b.clip = fileClips > 0 ? 0 : -1;
        a.time = 0.0f;
        b.time = fileClips > 0 ? reference.duration(0) * 0.5f : 0.0f;
        shape.setClips(0, &a, a.clip >= 0 ? 1 : 0);
        shape.setClips(other, &b, b.clip >= 0 ? 1 : 0);
        PacketRecorder first, second;
        shape.pushRenderItem(first, 0, 0);
        shape.pushRenderItem(second, 0, other);
        bool skinnedOk = !first.packets.isEmpty(), apart = first.packets.size() == second.packets.size();
        for (int k = 0; k < first.packets.size() && k < second.packets.size(); ++k) {
            const RenderItem *p = first.packets[k];
            const bool skinned = p->mesh.layout == RenderItem::PBR_SKINNED;
            if (c.skinned && skinned)
                skinnedOk = skinnedOk && p->skin.rows != nullptr && p->skin.joints > 0 && p->bounds.radius > 0.0f;
            apart = apart && p != second.packets[k];
        }
        const bool anySkinned = std::any_of(first.packets.begin(), first.packets.end(),
                                            [](const RenderItem *p) { return p->mesh.layout == RenderItem::PBR_SKINNED; });
        test.check(skinnedOk && anySkinned == c.skinned,
                   QString("%1: %2 packets, skinned %3").arg(c.name).arg(first.packets.size()).arg(anySkinned));
        test.check(apart, QString("%1: two states draw their own packets").arg(c.name));
        shape.releaseState(other);
    }

    // A file without animations or skins keeps no skeleton.
    {
        const QString path = samplePath(models, "Box");
        GltfShape shape(path, "Box", QFileInfo(path).path());
        shape.load();
        test.check(shape.isLoaded() && shape.skeleton() == nullptr && shape.clipCount() == 0,
                   "Box: no skeleton, drawn as before");
    }

    // The humans (humans-gen), when given: one skin of 53 joints, 7 clips,
    // three levels.
    const QString humans = qEnvironmentVariable("TSRE_HUMANS_SHAPES");
    if (!humans.isEmpty()) {
        for (const char *name : {"man", "woman", "young"}) {
            const QString path = humans + "/" + name + ".glb";
            GltfShape shape(path, QFileInfo(path).fileName(), humans + "/../TEXTURES");
            shape.load();
            const SkeletalAnimation *rig = shape.skeleton();
            test.check(shape.isLoaded() && rig != nullptr && rig->skinCount() == 1 && rig->jointCount(0) == 53
                       && shape.clipCount() == 7 && shape.distanceLevelCount() == 3,
                       QString("%1: a 53-joint skin, %2 clips, %3 levels").arg(name)
                       .arg(shape.clipCount()).arg(shape.distanceLevelCount()));
        }
        // The shared clips file bound to the woman (by joint names, the
        // pelvis moved by the ratio of the pelvis heights) plays as her own
        // clips: humans-gen gives every human the same joint frames.
        const QString clipsPath = humans + "/people_clips.glb";
        GltfShape clips(clipsPath, "people_clips.glb", humans);
        clips.load();
        GltfShape woman(humans + "/woman.glb", "woman.glb", humans + "/../TEXTURES");
        woman.load();
        const int added = woman.addClipsFrom(clips, ":shared");
        test.check(clips.isLoaded() && added == 7, QString("the shared clips file: %1 clips bound").arg(added));
        float worst = 0.0f;
        const int nodes = woman.skeleton() ? woman.skeleton()->nodeCount() : 0;
        for (const char *clip : {"walk", "idle_phone", "walk_carry"}) {
            const int own = woman.skeleton() ? woman.skeleton()->clipIndex(clip) : -1;
            const int shared = woman.skeleton() ? woman.skeleton()->clipIndex(QString(clip) + ":shared") : -1;
            if (own < 0 || shared < 0) {
                worst = 1e9f;
                continue;
            }
            for (float f : {0.0f, 0.3f, 0.71f}) {
                const float time = woman.clipDuration(own) * f;
                ComplexShape::ClipPlay a, b;
                a.clip = own;
                b.clip = shared;
                a.time = b.time = time;
                woman.setClips(0, &a, 1);
                woman.updatePose(0);
                std::vector<float> expected;
                for (int n = 0; n < nodes; ++n)
                    if (const float *m = woman.poseNodeMatrix(0, n))
                        expected.insert(expected.end(), m, m + 16);
                woman.setClips(0, &b, 1);
                woman.updatePose(0);
                for (int n = 0, k = 0; n < nodes; ++n)
                    if (const float *m = woman.poseNodeMatrix(0, n)) {
                        worst = std::max(worst, matrixGap(m, expected.data() + k, 1.65f));
                        k += 16;
                    }
            }
        }
        test.check(worst < 0.002f, QString("shared clips on the woman as her own, worst %1").arg(double(worst)));

        // The cost a character a frame (printed; meaningful in a Release
        // build): sampling with its joint matrices, and the whole push
        // (sampling, posed packets, their bounds).
        GltfShape man(humans + "/man.glb", "man.glb", humans + "/../TEXTURES");
        man.load();
        const int walk = man.skeleton() ? man.skeleton()->clipIndex("walk") : -1;
        for (int count : {1, 100, 500}) {
            std::vector<unsigned int> states;
            for (int i = 0; i < count; ++i)
                states.push_back(i == 0 ? 0u : man.newState());
            const int frames = 20;
            qint64 sampling = 0, pushing = 0;
            for (int f = 0; f < frames; ++f) {
                QElapsedTimer timer;
                timer.start();
                for (int i = 0; i < count; ++i) {
                    ComplexShape::ClipPlay play;
                    play.clip = walk;
                    play.time = std::fmod(0.013f * float(i) + 0.033f * float(f), man.clipDuration(walk));
                    man.setClips(states[size_t(i)], &play, 1);
                    man.updatePose(states[size_t(i)]);
                }
                sampling += timer.nsecsElapsed();
                PacketRecorder recorder;
                timer.restart();
                for (int i = 0; i < count; ++i) {
                    ComplexShape::ClipPlay play;
                    play.clip = walk;
                    play.time = std::fmod(0.017f * float(i) + 0.033f * float(f), man.clipDuration(walk));
                    man.setClips(states[size_t(i)], &play, 1);
                    man.pushRenderItem(recorder, 0, states[size_t(i)]);
                }
                pushing += timer.nsecsElapsed();
            }
            qInfo().noquote() << QString("[tests:gltf-skin] cost: %1 characters, sampling %2 us a character, "
                                         "push %3 us a character (%4 ms a frame)")
                                 .arg(count).arg(double(sampling) / 1000.0 / frames / count, 0, 'f', 2)
                                 .arg(double(pushing) / 1000.0 / frames / count, 0, 'f', 2)
                                 .arg(double(pushing) / 1e6 / frames, 0, 'f', 2);
            for (unsigned int state : states)
                if (state != 0)
                    man.releaseState(state);
        }
    }
    return test.finish();
}

} // namespace TsreTests
