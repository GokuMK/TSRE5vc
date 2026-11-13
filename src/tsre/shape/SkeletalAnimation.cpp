/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "SkeletalAnimation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <functional>
#include <string>

#include <QHash>

#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/memory/unique_ptr.h>
#include <ozz/base/memory/allocator.h>
#include <cstdint>

namespace {

// ozz's own heap allocator counts its blocks and, in a build with asserts,
// aborts at the program's end when any are left: shapes are not freed before
// it ends. ozz allocates through this one instead, never destroyed.
class OzzAllocator : public ozz::memory::Allocator {
public:
    void *Allocate(size_t size, size_t alignment) override {
        alignment = std::max<size_t>(alignment, alignof(void *));
        char *raw = static_cast<char *>(std::malloc(size + sizeof(void *) + alignment - 1));
        if (raw == nullptr)
            return nullptr;
        const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(raw + sizeof(void *));
        const std::uintptr_t aligned = (start + alignment - 1) & ~(std::uintptr_t(alignment) - 1);
        reinterpret_cast<void **>(aligned)[-1] = raw;
        return reinterpret_cast<void *>(aligned);
    }
    void Deallocate(void *block) override {
        if (block != nullptr)
            std::free(reinterpret_cast<void **>(block)[-1]);
    }
};

const bool ozzAllocatorSet = [] {
    ozz::memory::SetDefaulAllocator(new OzzAllocator());
    return true;
}();

using ozz::animation::offline::RawAnimation;
using ozz::animation::offline::RawSkeleton;

// CUBICSPLINE segments are sampled this often (keys per second).
constexpr float CubicSampleRate = 60.0f;
// ozz interpolates rotations normalized-linearly; glTF wants slerp. Keys
// farther apart than this get slerp keys between them, so the two differ
// by far under a hundredth of a degree.
constexpr float MaxRotationStep = 10.0f * 3.14159265f / 180.0f;

ozz::math::Float3 float3(const float *v) {
    return ozz::math::Float3(v[0], v[1], v[2]);
}

ozz::math::Quaternion quaternion(const float *v) {
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
    if (length < 1e-12f)
        return ozz::math::Quaternion::identity();
    return ozz::math::Quaternion(v[0] / length, v[1] / length, v[2] / length, v[3] / length);
}

// A channel's keys as (time, value) with linear interpolation between them:
// STEP holds each value until just before the next key, CUBICSPLINE is
// sampled along its Hermite curve.
struct LinearKey {
    float time;
    float value[4];
};

std::vector<LinearKey> linearKeys(const SkeletalAnimation::Channel &channel, int components) {
    using Interpolation = SkeletalAnimation::Channel::Interpolation;
    std::vector<LinearKey> keys;
    const size_t count = channel.times.size();
    const bool cubic = channel.interpolation == Interpolation::CubicSpline;
    const size_t perKey = size_t(components) * (cubic ? 3 : 1);
    if (count == 0 || channel.values.size() < count * perKey)
        return keys;
    // The value of key i (for CUBICSPLINE, the middle of its triple).
    auto value = [&](size_t i, float *out) {
        const float *v = channel.values.data() + i * perKey + (cubic ? components : 0);
        std::copy(v, v + components, out);
    };
    auto push = [&](float time, const float *v) {
        if (!keys.empty() && time <= keys.back().time)
            return;
        LinearKey key{time, {0.0f, 0.0f, 0.0f, 0.0f}};
        std::copy(v, v + components, key.value);
        keys.push_back(key);
    };
    for (size_t i = 0; i < count; ++i) {
        float v[4];
        value(i, v);
        push(channel.times[i], v);
        if (i + 1 == count)
            break;
        const float t0 = channel.times[i], t1 = channel.times[i + 1];
        const float span = t1 - t0;
        if (span <= 0.0f)
            continue;
        if (channel.interpolation == Interpolation::Step) {
            // Held until just before the next key.
            push(t1 - std::min(1e-4f, span * 0.5f), v);
        } else if (!cubic && components == 4) {
            float v1[4];
            value(i + 1, v1);
            float d = v[0] * v1[0] + v[1] * v1[1] + v[2] * v1[2] + v[3] * v1[3];
            const float sign = d < 0.0f ? -1.0f : 1.0f;
            d = std::min(std::abs(d), 1.0f);
            const float theta = std::acos(d);
            const int steps = int(std::ceil(2.0f * theta / MaxRotationStep));
            for (int k = 1; k < steps; ++k) {
                const float s = float(k) / float(steps);
                const float wa = std::sin((1.0f - s) * theta) / std::sin(theta);
                const float wb = std::sin(s * theta) / std::sin(theta) * sign;
                float out[4];
                for (int c = 0; c < 4; ++c)
                    out[c] = wa * v[c] + wb * v1[c];
                push(t0 + s * span, out);
            }
        } else if (cubic) {
            const float *b0 = channel.values.data() + i * perKey + 2 * components;   // out-tangent of i
            const float *a1 = channel.values.data() + (i + 1) * perKey;              // in-tangent of i + 1
            float v1[4];
            value(i + 1, v1);
            const int steps = std::max(1, int(std::ceil(span * CubicSampleRate)));
            for (int k = 1; k < steps; ++k) {
                const float s = float(k) / float(steps);
                const float s2 = s * s, s3 = s2 * s;
                const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f, h10 = s3 - 2.0f * s2 + s;
                const float h01 = -2.0f * s3 + 3.0f * s2, h11 = s3 - s2;
                float out[4];
                for (int c = 0; c < components; ++c)
                    out[c] = h00 * v[c] + h10 * span * b0[c] + h01 * v1[c] + h11 * span * a1[c];
                push(t0 + s * span, out);
            }
        }
    }
    return keys;
}

} // namespace

struct SkeletalAnimation::Data {
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
    std::vector<ozz::unique_ptr<ozz::animation::Animation>> clips;
    std::vector<QString> clipNames;
    // ozz joint -> file node, and back (-1 for none).
    std::vector<int> nodeOfJoint;
    std::vector<int> jointOfNode;
    // Per skin: its joints' ozz indices and inverse bind matrices.
    std::vector<std::vector<int>> skinJoints;
    std::vector<ozz::vector<ozz::math::Float4x4>> inverseBind;
};

class SkeletalAnimation::Pose {
public:
    // One sampling slot per play.
    std::vector<std::unique_ptr<ozz::animation::SamplingJob::Context>> contexts;
    std::vector<ozz::vector<ozz::math::SoaTransform>> sampled;
    ozz::vector<ozz::math::SoaTransform> blended;
    ozz::vector<ozz::math::Float4x4> models;
    std::vector<float> nodes;
    std::vector<std::vector<float>> joints;
};

SkeletalAnimation::SkeletalAnimation() : data(new Data()) {
}

SkeletalAnimation::~SkeletalAnimation() = default;

bool SkeletalAnimation::build(const QVector<Node> &nodes, const QVector<ClipSource> &clips, const QVector<Skin> &skinList,
                              QString &error, QStringList &warnings) {
    const int count = nodes.size();
    if (count <= 0) {
        error = "no nodes";
        return false;
    }
    if (count > ozz::animation::Skeleton::kMaxJoints) {
        error = QString("%1 nodes, over ozz's %2").arg(count).arg(int(ozz::animation::Skeleton::kMaxJoints));
        return false;
    }
    nodeNames.clear();
    for (const Node &node : nodes)
        nodeNames.push_back(node.name);

    // The node tree, each joint named by its node's index.
    std::vector<std::vector<int>> children(static_cast<size_t>(count));
    std::vector<int> roots;
    for (int i = 0; i < count; ++i) {
        const int parent = nodes[i].parent;
        if (parent >= 0 && parent < count && parent != i)
            children[size_t(parent)].push_back(i);
        else
            roots.push_back(i);
    }
    RawSkeleton raw;
    std::vector<char> visited(size_t(count), 0);
    std::function<void(int, RawSkeleton::Joint &)> fill = [&](int index, RawSkeleton::Joint &joint) {
        visited[size_t(index)] = 1;
        const Node &node = nodes[index];
        joint.name = std::to_string(index).c_str();
        joint.transform.translation = float3(node.translation);
        joint.transform.rotation = quaternion(node.rotation);
        joint.transform.scale = float3(node.scale);
        for (int child : children[size_t(index)]) {
            if (visited[size_t(child)])
                continue;
            joint.children.emplace_back();
            fill(child, joint.children.back());
        }
    };
    for (int root : roots) {
        raw.roots.emplace_back();
        fill(root, raw.roots.back());
    }
    if (std::count(visited.begin(), visited.end(), 0) > 0) {
        error = "the node tree has a cycle";
        return false;
    }
    ozz::animation::offline::SkeletonBuilder skeletonBuilder;
    data->skeleton = skeletonBuilder(raw);
    if (!data->skeleton) {
        error = "ozz refused the skeleton";
        return false;
    }
    const int joints = data->skeleton->num_joints();
    data->nodeOfJoint.assign(size_t(joints), -1);
    data->jointOfNode.assign(size_t(count), -1);
    const auto names = data->skeleton->joint_names();
    for (int j = 0; j < joints; ++j) {
        const int node = std::atoi(names[size_t(j)]);
        data->nodeOfJoint[size_t(j)] = node;
        if (node >= 0 && node < count)
            data->jointOfNode[size_t(node)] = j;
    }

    // Clips: a track per joint, rest values where a clip keys nothing.
    data->clips.clear();
    data->clipNames.clear();
    restNodes = nodes;
    sources.clear();
    for (const ClipSource &source : clips)
        if (buildClip(source, source.name, warnings))
            sources.push_back(source);

    // Skins: their joints as ozz joints, their inverse bind matrices.
    skins.clear();
    data->skinJoints.clear();
    data->inverseBind.clear();
    for (const Skin &skin : skinList) {
        Skin kept = skin;
        std::vector<int> ozzJoints;
        ozz::vector<ozz::math::Float4x4> inverse;
        for (int k = 0; k < skin.joints.size(); ++k) {
            const int node = skin.joints[k];
            ozzJoints.push_back(node >= 0 && node < count ? data->jointOfNode[size_t(node)] : -1);
            ozz::math::Float4x4 m = ozz::math::Float4x4::identity();
            if (skin.inverseBind.size() >= size_t(k + 1) * 16) {
                const float *v = skin.inverseBind.data() + size_t(k) * 16;
                for (int c = 0; c < 4; ++c)
                    m.cols[c] = ozz::math::simd_float4::LoadPtrU(v + c * 4);
            }
            inverse.push_back(m);
        }
        skins.push_back(kept);
        data->skinJoints.push_back(ozzJoints);
        data->inverseBind.push_back(std::move(inverse));
    }
    return true;
}

bool SkeletalAnimation::buildClip(const ClipSource &source, const QString &name, QStringList &warnings) {
    const int count = restNodes.size();
    const int joints = data->skeleton->num_joints();
    RawAnimation clip;
    clip.name = name.toStdString().c_str();
    clip.tracks.resize(size_t(joints));
    float duration = 0.0f;
    for (const Channel &channel : source.channels)
        if (!channel.times.empty())
            duration = std::max(duration, channel.times.back());
    clip.duration = std::max(duration, 1e-3f);
    std::vector<char> keyed(size_t(joints) * 3, 0);
    for (const Channel &channel : source.channels) {
        if (channel.node < 0 || channel.node >= count || data->jointOfNode[size_t(channel.node)] < 0) {
            warnings << QString("clip %1: a channel's node %2 does not exist").arg(name).arg(channel.node);
            continue;
        }
        const int joint = data->jointOfNode[size_t(channel.node)];
        const int path = int(channel.path);
        if (keyed[size_t(joint) * 3 + size_t(path)]) {
            warnings << QString("clip %1: node %2 keyed twice, the first kept").arg(name).arg(channel.node);
            continue;
        }
        const int components = channel.path == Channel::Path::Rotation ? 4 : 3;
        std::vector<LinearKey> keys = linearKeys(channel, components);
        if (keys.empty()) {
            warnings << QString("clip %1: node %2 has no usable keys").arg(name).arg(channel.node);
            continue;
        }
        // Before the first key the first value (glTF); ozz wants keys from 0.
        if (keys.front().time > 0.0f) {
            LinearKey first = keys.front();
            first.time = 0.0f;
            keys.insert(keys.begin(), first);
        }
        keyed[size_t(joint) * 3 + size_t(path)] = 1;
        RawAnimation::JointTrack &track = clip.tracks[size_t(joint)];
        for (const LinearKey &key : keys) {
            const float time = std::clamp(key.time, 0.0f, clip.duration);
            if (channel.path == Channel::Path::Translation)
                track.translations.push_back({time, float3(key.value)});
            else if (channel.path == Channel::Path::Rotation)
                track.rotations.push_back({time, quaternion(key.value)});
            else
                track.scales.push_back({time, float3(key.value)});
        }
    }
    for (int j = 0; j < joints; ++j) {
        const int node = data->nodeOfJoint[size_t(j)];
        if (node < 0 || node >= count)
            continue;
        const Node &rest = restNodes[node];
        RawAnimation::JointTrack &track = clip.tracks[size_t(j)];
        if (!keyed[size_t(j) * 3 + 0])
            track.translations.push_back({0.0f, float3(rest.translation)});
        if (!keyed[size_t(j) * 3 + 1])
            track.rotations.push_back({0.0f, quaternion(rest.rotation)});
        if (!keyed[size_t(j) * 3 + 2])
            track.scales.push_back({0.0f, float3(rest.scale)});
    }
    if (!clip.Validate()) {
        warnings << QString("clip %1: ozz refused its keys, left out").arg(name);
        return false;
    }
    ozz::animation::offline::AnimationBuilder animationBuilder;
    ozz::unique_ptr<ozz::animation::Animation> built = animationBuilder(clip);
    if (!built) {
        warnings << QString("clip %1: ozz could not build it, left out").arg(name);
        return false;
    }
    data->clips.push_back(std::move(built));
    data->clipNames.push_back(name);
    return true;
}

// Each node's rest height in shape space (y, glTF's up).
std::vector<float> SkeletalAnimation::restHeights() const {
    std::vector<float> heights(nodeNames.size(), 0.0f);
    std::shared_ptr<Pose> pose = newPose();
    evaluate(*pose, nullptr, 0);
    for (size_t n = 0; n < nodeNames.size(); ++n)
        if (const float *m = nodeMatrix(*pose, int(n)))
            heights[n] = m[13];
    return heights;
}

int SkeletalAnimation::addClips(const SkeletalAnimation &from, const QString &suffix, QStringList &warnings) {
    if (!data->skeleton || !from.data->skeleton)
        return 0;
    // From's nodes by name; the rest heights of both, for translations.
    QHash<QString, int> byName;
    for (int n = 0; n < int(nodeNames.size()); ++n)
        if (!nodeNames[size_t(n)].isEmpty() && !byName.contains(nodeNames[size_t(n)].toLower()))
            byName.insert(nodeNames[size_t(n)].toLower(), n);
    const std::vector<float> ownHeights = restHeights(), fromHeights = from.restHeights();
    int added = 0;
    for (const ClipSource &source : from.sources) {
        const QString name = source.name + suffix;
        if (clipIndex(name) >= 0)
            continue;
        ClipSource bound;
        bound.name = name;
        QStringList missing;
        for (const Channel &channel : source.channels) {
            if (channel.node < 0 || channel.node >= int(from.nodeNames.size()))
                continue;
            const QString nodeName = from.nodeNames[size_t(channel.node)];
            const auto found = byName.constFind(nodeName.toLower());
            if (found == byName.constEnd()) {
                if (!missing.contains(nodeName))
                    missing << nodeName;
                continue;
            }
            if (channel.path == Channel::Path::Scale)
                continue;
            Channel own = channel;
            own.node = found.value();
            if (channel.path == Channel::Path::Translation) {
                // This skeleton's rest, moved as the clip moves it from the
                // clip skeleton's rest, scaled by their heights.
                const float *fromRest = from.restNodes[channel.node].translation;
                const float *ownRest = restNodes[own.node].translation;
                const float fromHeight = fromHeights[size_t(channel.node)];
                const float ratio = std::abs(fromHeight) > 1e-4f ? ownHeights[size_t(own.node)] / fromHeight : 1.0f;
                const bool cubic = channel.interpolation == Channel::Interpolation::CubicSpline;
                for (size_t k = 0; k < own.values.size(); k += 3) {
                    // CUBICSPLINE keeps tangents, scaled only.
                    const bool tangent = cubic && (k / 3) % 3 != 1;
                    for (int c = 0; c < 3; ++c)
                        own.values[k + size_t(c)] = tangent ? channel.values[k + size_t(c)] * ratio
                                : ownRest[c] + (channel.values[k + size_t(c)] - fromRest[c]) * ratio;
                }
            }
            bound.channels.push_back(std::move(own));
        }
        if (!missing.isEmpty())
            warnings << QString("clip %1: %2 nodes not in this skeleton (%3)").arg(source.name).arg(missing.size())
                        .arg(missing.mid(0, 4).join(", "));
        if (buildClip(bound, name, warnings)) {
            sources.push_back(bound);
            ++added;
        }
    }
    return added;
}

int SkeletalAnimation::clipCount() const {
    return int(data->clips.size());
}

QString SkeletalAnimation::clipName(int clip) const {
    return clip >= 0 && clip < clipCount() ? data->clipNames[size_t(clip)] : QString();
}

float SkeletalAnimation::clipDuration(int clip) const {
    return clip >= 0 && clip < clipCount() ? data->clips[size_t(clip)]->duration() : 0.0f;
}

int SkeletalAnimation::clipIndex(const QString &name) const {
    for (int i = 0; i < clipCount(); ++i)
        if (data->clipNames[size_t(i)] == name)
            return i;
    return -1;
}

std::shared_ptr<SkeletalAnimation::Pose> SkeletalAnimation::newPose() const {
    std::shared_ptr<Pose> pose(new Pose());
    if (!data->skeleton)
        return pose;
    const int soa = data->skeleton->num_soa_joints();
    pose->blended.resize(size_t(soa));
    pose->models.resize(size_t(data->skeleton->num_joints()));
    pose->nodes.assign(nodeNames.size() * 16, 0.0f);
    for (size_t n = 0; n < nodeNames.size(); ++n)
        for (int d = 0; d < 4; ++d)
            pose->nodes[n * 16 + size_t(d) * 5] = 1.0f;
    for (const Skin &skin : skins)
        pose->joints.emplace_back(size_t(skin.joints.size()) * 12, 0.0f);
    return pose;
}

void SkeletalAnimation::evaluate(Pose &pose, const Play *plays, int count) const {
    const ozz::animation::Skeleton *skeleton = data->skeleton.get();
    if (skeleton == nullptr || pose.models.empty())
        return;
    const int soa = skeleton->num_soa_joints();
    // The plays that sample something.
    std::vector<const Play *> active;
    for (int i = 0; i < count; ++i)
        if (plays[i].clip >= 0 && plays[i].clip < clipCount() && plays[i].weight > 0.0f)
            active.push_back(&plays[i]);
    while (pose.contexts.size() < active.size()) {
        pose.contexts.emplace_back(new ozz::animation::SamplingJob::Context());
        pose.sampled.emplace_back(size_t(soa));
    }
    auto sample = [&](size_t slot, const Play &play, ozz::span<ozz::math::SoaTransform> output) {
        const ozz::animation::Animation &clip = *data->clips[size_t(play.clip)];
        ozz::animation::SamplingJob::Context &context = *pose.contexts[slot];
        if (context.max_tracks() < clip.num_tracks())
            context.Resize(clip.num_tracks());
        ozz::animation::SamplingJob job;
        job.animation = &clip;
        job.context = &context;
        job.ratio = clip.duration() > 0.0f ? std::clamp(play.time / clip.duration(), 0.0f, 1.0f) : 0.0f;
        job.output = output;
        job.Run();
    };
    if (active.empty()) {
        const auto rest = skeleton->joint_rest_poses();
        std::copy(rest.begin(), rest.end(), pose.blended.begin());
    } else if (active.size() == 1 && active[0]->weight >= 1.0f) {
        sample(0, *active[0], ozz::make_span(pose.blended));
    } else {
        std::vector<ozz::animation::BlendingJob::Layer> layers(active.size());
        for (size_t i = 0; i < active.size(); ++i) {
            sample(i, *active[i], ozz::make_span(pose.sampled[i]));
            layers[i].weight = active[i]->weight;
            layers[i].transform = ozz::make_span(pose.sampled[i]);
        }
        ozz::animation::BlendingJob blend;
        blend.layers = ozz::make_span(layers);
        blend.rest_pose = skeleton->joint_rest_poses();
        blend.output = ozz::make_span(pose.blended);
        blend.Run();
    }
    ozz::animation::LocalToModelJob toModel;
    toModel.skeleton = skeleton;
    toModel.input = ozz::make_span(pose.blended);
    toModel.output = ozz::make_span(pose.models);
    toModel.Run();

    for (size_t j = 0; j < data->nodeOfJoint.size(); ++j) {
        const int node = data->nodeOfJoint[j];
        if (node < 0 || size_t(node) >= nodeNames.size())
            continue;
        float *out = pose.nodes.data() + size_t(node) * 16;
        for (int c = 0; c < 4; ++c)
            ozz::math::StorePtrU(pose.models[j].cols[c], out + c * 4);
    }
    for (size_t s = 0; s < data->skinJoints.size(); ++s) {
        const std::vector<int> &joints = data->skinJoints[s];
        std::vector<float> &rows = pose.joints[s];
        for (size_t k = 0; k < joints.size(); ++k) {
            float m[16];
            if (joints[k] >= 0) {
                const ozz::math::Float4x4 skinned = pose.models[size_t(joints[k])] * data->inverseBind[s][k];
                for (int c = 0; c < 4; ++c)
                    ozz::math::StorePtrU(skinned.cols[c], m + c * 4);
            } else {
                std::fill(m, m + 16, 0.0f);
                m[0] = m[5] = m[10] = m[15] = 1.0f;
            }
            float *out = rows.data() + k * 12;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 4; ++c)
                    out[r * 4 + c] = m[c * 4 + r];
        }
    }
}

const float *SkeletalAnimation::nodeMatrix(const Pose &pose, int node) const {
    if (node < 0 || size_t(node) * 16 + 16 > pose.nodes.size())
        return nullptr;
    return pose.nodes.data() + size_t(node) * 16;
}

const float *SkeletalAnimation::jointRows(const Pose &pose, int skin) const {
    if (skin < 0 || size_t(skin) >= pose.joints.size())
        return nullptr;
    return pose.joints[size_t(skin)].data();
}

bool SkeletalAnimation::clipTravel(int clip, float *travel, int skinIndex) const {
    travel[0] = travel[1] = travel[2] = 0.0f;
    if (clip < 0 || clip >= clipCount() || skinIndex < 0 || skinIndex >= skinCount())
        return false;
    // Every joint of the skin from the clip's start to its end: the median
    // of their moves on each axis (the body carried as a whole; the root
    // joint above the pelvis never moves, one hand may reach out).
    const QVector<int> &joints = skins[size_t(skinIndex)].joints;
    std::shared_ptr<Pose> pose = newPose();
    std::vector<float> at[2];
    for (int i = 0; i < 2; ++i) {
        Play play;
        play.clip = clip;
        play.time = i == 0 ? 0.0f : clipDuration(clip);
        evaluate(*pose, &play, 1);
        for (int joint : joints) {
            const float *m = nodeMatrix(*pose, joint);
            if (m != nullptr)
                at[i].insert(at[i].end(), m + 12, m + 15);
        }
    }
    const size_t count = std::min(at[0].size(), at[1].size()) / 3;
    if (count == 0)
        return false;
    for (int c = 0; c < 3; ++c) {
        std::vector<float> moves(count);
        for (size_t j = 0; j < count; ++j)
            moves[j] = at[1][j * 3 + size_t(c)] - at[0][j * 3 + size_t(c)];
        std::nth_element(moves.begin(), moves.begin() + long(count / 2), moves.end());
        travel[c] = moves[count / 2];
    }
    return true;
}

float SkeletalAnimation::clipStartLift(int clip, int skinIndex) const {
    if (clip < 0 || clip >= clipCount() || skinIndex < 0 || skinIndex >= skinCount())
        return 0.0f;
    // The skin's ends (feet, hands, the head's end): the rig's root joint
    // stands on the ground whatever the clip does.
    const QVector<int> &joints = skins[size_t(skinIndex)].joints;
    const int nodes = std::min(nodeCount(), int(restNodes.size()));
    std::vector<char> inSkin(size_t(std::max(nodes, 0)), 0), hasChild(inSkin.size(), 0);
    for (int joint : joints)
        if (joint >= 0 && joint < nodes)
            inSkin[size_t(joint)] = 1;
    for (int joint : joints) {
        const int parent = joint >= 0 && joint < nodes ? restNodes[joint].parent : -1;
        if (parent >= 0 && parent < nodes && inSkin[size_t(parent)])
            hasChild[size_t(parent)] = 1;
    }
    std::shared_ptr<Pose> pose = newPose();
    float lowest[2];
    for (int i = 0; i < 2; ++i) {
        Play play;
        play.clip = clip;
        play.time = 0.0f;
        evaluate(*pose, &play, i == 0 ? 0 : 1);
        lowest[i] = std::numeric_limits<float>::max();
        for (int joint : joints) {
            if (joint < 0 || joint >= nodes || hasChild[size_t(joint)])
                continue;
            const float *m = nodeMatrix(*pose, joint);
            if (m != nullptr)
                lowest[i] = std::min(lowest[i], m[13]);
        }
    }
    if (lowest[0] == std::numeric_limits<float>::max() || lowest[1] == std::numeric_limits<float>::max())
        return 0.0f;
    return lowest[1] - lowest[0];
}

bool SkeletalAnimation::clipHipsTravel(int clip, float *travel, int skinIndex) const {
    travel[0] = travel[1] = travel[2] = 0.0f;
    if (clip < 0 || clip >= clipCount() || skinIndex < 0 || skinIndex >= skinCount())
        return false;
    const QVector<int> &joints = skins[size_t(skinIndex)].joints;
    const int nodes = std::min(nodeCount(), int(restNodes.size()));
    std::vector<char> inSkin(size_t(std::max(nodes, 0)), 0);
    for (int joint : joints)
        if (joint >= 0 && joint < nodes)
            inSkin[size_t(joint)] = 1;
    std::vector<std::vector<int>> children(inSkin.size());
    int top = -1;
    for (int joint : joints) {
        if (joint < 0 || joint >= nodes)
            continue;
        const int parent = restNodes[joint].parent;
        if (parent >= 0 && parent < nodes && inSkin[size_t(parent)])
            children[size_t(parent)].push_back(joint);
        else if (top < 0)
            top = joint;
    }
    if (top < 0)
        return false;
    int hips = top;
    while (children[size_t(hips)].size() == 1)
        hips = children[size_t(hips)].front();
    std::shared_ptr<Pose> pose = newPose();
    float at[2][3];
    for (int i = 0; i < 2; ++i) {
        Play play;
        play.clip = clip;
        play.time = i == 0 ? 0.0f : clipDuration(clip);
        evaluate(*pose, &play, 1);
        const float *m = nodeMatrix(*pose, hips);
        if (m == nullptr)
            return false;
        std::copy(m + 12, m + 15, at[i]);
    }
    for (int c = 0; c < 3; ++c)
        travel[c] = at[1][c] - at[0][c];
    return true;
}

SkeletalAnimation::GroundMotion SkeletalAnimation::groundMotion(int clip, int skinIndex) const {
    GroundMotion motion;
    if (clip < 0 || clip >= clipCount() || skinIndex < 0 || skinIndex >= skinCount())
        return motion;
    const QVector<int> &joints = skins[size_t(skinIndex)].joints;
    const int nodes = nodeCount();
    std::vector<char> inSkin(size_t(nodes), 0), hasChild(size_t(nodes), 0);
    for (int joint : joints)
        if (joint >= 0 && joint < nodes && joint < restNodes.size())
            inSkin[size_t(joint)] = 1;
    for (int joint : joints)
        if (joint >= 0 && joint < restNodes.size() && restNodes[joint].parent >= 0
                && restNodes[joint].parent < nodes && inSkin[size_t(restNodes[joint].parent)])
            hasChild[size_t(restNodes[joint].parent)] = 1;
    // The ends (feet, hands, the head's end) and the top joint.
    std::vector<int> ends;
    int top = -1;
    for (int joint : joints) {
        if (joint < 0 || joint >= nodes || joint >= restNodes.size())
            continue;
        if (!hasChild[size_t(joint)])
            ends.push_back(joint);
        const int parent = restNodes[joint].parent;
        if (top < 0 && (parent < 0 || parent >= nodes || !inSkin[size_t(parent)]))
            top = joint;
    }
    if (ends.empty() || top < 0)
        return motion;
    const float duration = clipDuration(clip);
    if (!(duration > 0.0f))
        return motion;
    const int samples = std::max(2, int(std::ceil(duration * 30.0f)));
    const float step = duration / float(samples);
    std::shared_ptr<Pose> pose = newPose();
    std::vector<float> speeds;
    double along[2] = {0.0, 0.0};
    int lastFoot = -1;
    float lastFootAt[2] = {0.0f, 0.0f}, lastTopAt[2] = {0.0f, 0.0f};
    for (int i = 0; i <= samples; ++i) {
        Play play;
        play.clip = clip;
        play.time = step * float(i);
        evaluate(*pose, &play, 1);
        int foot = -1;
        float lowest = 0.0f;
        for (int end : ends) {
            const float *m = nodeMatrix(*pose, end);
            if (m != nullptr && (foot < 0 || m[13] < lowest)) {
                foot = end;
                lowest = m[13];
            }
        }
        const float *f = nodeMatrix(*pose, foot);
        const float *t = nodeMatrix(*pose, top);
        if (f == nullptr || t == nullptr)
            return motion;
        if (foot == lastFoot) {
            // The ground moving under the body, as the body sees it.
            const float dx = (f[12] - lastFootAt[0]) - (t[12] - lastTopAt[0]);
            const float dz = (f[14] - lastFootAt[1]) - (t[14] - lastTopAt[1]);
            speeds.push_back(std::sqrt(dx * dx + dz * dz) / step);
            along[0] -= dx;
            along[1] -= dz;
        }
        lastFoot = foot;
        lastFootAt[0] = f[12];
        lastFootAt[1] = f[14];
        lastTopAt[0] = t[12];
        lastTopAt[1] = t[14];
    }
    if (speeds.empty())
        return motion;
    std::nth_element(speeds.begin(), speeds.begin() + long(speeds.size() / 2), speeds.end());
    motion.speed = speeds[speeds.size() / 2];
    const double length = std::sqrt(along[0] * along[0] + along[1] * along[1]);
    if (length > 1e-6) {
        motion.direction[0] = float(along[0] / length);
        motion.direction[1] = 0.0f;
        motion.direction[2] = float(along[1] / length);
    }
    return motion;
}
