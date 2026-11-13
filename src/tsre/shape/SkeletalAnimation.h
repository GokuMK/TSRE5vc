/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SKELETALANIMATION_H
#define SKELETALANIMATION_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <vector>

// A glTF shape's moving nodes (task humans 02): its node tree as one ozz
// skeleton (every node a joint, so rigid node animation and skins share
// one sampler), its animations as ozz clips, played per shape state into
// node matrices (shape space) and skins' joint matrices.
//
// glTF's sampler kinds become ozz's linear keys: STEP as pairs of keys,
// CUBICSPLINE sampled at 60 per second. A node a clip does not key keeps its
// rest transform (ozz would take identity). Built on a worker: plain maths,
// no Qt objects.
class SkeletalAnimation {
public:
    SkeletalAnimation();
    ~SkeletalAnimation();
    SkeletalAnimation(const SkeletalAnimation &) = delete;
    SkeletalAnimation &operator=(const SkeletalAnimation &) = delete;

    // A node of the file, its rest transform as glTF's TRS.
    struct Node {
        QString name;
        int parent = -1;
        float translation[3] = {0.0f, 0.0f, 0.0f};
        float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // x, y, z, w
        float scale[3] = {1.0f, 1.0f, 1.0f};
    };
    // A glTF animation channel with its sampler's keys: `values` holds
    // `components` floats per key (3 for translation and scale, 4 for
    // rotation), three times that for CUBICSPLINE (in-tangent, value,
    // out-tangent).
    struct Channel {
        enum class Path { Translation, Rotation, Scale };
        enum class Interpolation { Linear, Step, CubicSpline };
        int node = -1;
        Path path = Path::Rotation;
        Interpolation interpolation = Interpolation::Linear;
        std::vector<float> times;
        std::vector<float> values;
    };
    struct ClipSource {
        QString name;
        std::vector<Channel> channels;
    };
    // A skin: its joints (node indices) and their inverse bind matrices
    // (column major, 16 floats each; identity when the file gives none).
    struct Skin {
        QVector<int> joints;
        std::vector<float> inverseBind;
    };

    // Builds the skeleton from `nodes` (in the file's order) and the clips
    // from `clips`. False with `error` when ozz refuses them (over 1024 nodes,
    // a cycle). Clip problems that only drop a channel go to `warnings`.
    bool build(const QVector<Node> &nodes, const QVector<ClipSource> &clips, const QVector<Skin> &skins,
               QString &error, QStringList &warnings);

    // Adds the clips of another skeleton (a shared clips file, task humans
    // 02), bound to this one by node names (any case): rotations as they
    // are; translations moved from the clip skeleton's rest to this one's,
    // scaled by the ratio of the node's rest heights (a pelvis on shorter
    // legs moves less); scale left out. Named with `suffix` added; a name
    // already here is skipped (a file's own clips come first). Returns how
    // many were added.
    int addClips(const SkeletalAnimation &from, const QString &suffix, QStringList &warnings);

    int nodeCount() const { return int(nodeNames.size()); }
    int clipCount() const;
    QString clipName(int clip) const;
    float clipDuration(int clip) const;
    int clipIndex(const QString &name) const;
    int skinCount() const { return int(skins.size()); }
    const Skin &skin(int index) const { return skins[size_t(index)]; }

    // What a state plays: clips at their times (seconds, wrapped by the
    // caller), blended by weight (ozz normalizes the weights; all of them
    // near zero gives the rest pose).
    struct Play {
        int clip = -1;
        float time = 0.0f;
        float weight = 1.0f;
    };
    // A state's sampling buffers and results (shared: its deleter is made
    // here, where the type is complete).
    class Pose;
    std::shared_ptr<Pose> newPose() const;
    // Samples and blends `plays` into the pose's node matrices and skin
    // joint matrices. No plays: the rest pose.
    void evaluate(Pose &pose, const Play *plays, int count) const;
    // Results, valid after evaluate().
    const float *nodeMatrix(const Pose &pose, int node) const; // 16 floats, column major
    // A skin's joint matrices (joint global * inverse bind), three rows of
    // four floats each (the matrix's transpose without its last row), in the
    // skin's joint order.
    const float *jointRows(const Pose &pose, int skin) const;
    int jointCount(int skin) const { return skins[size_t(skin)].joints.size(); }

    // How a clip carries its person over the ground (task humans 03), found
    // from its feet: the skin's lowest end joint (a joint without child
    // joints) stands on the ground, so it moves against the walk relative
    // to the skin's top joint (an in-place walk), or stays while that joint
    // goes on (root motion). Its speed (m/s, the median while one foot
    // stands) and the direction walked (shape space, level, unit; +z when
    // it stands still). A clip standing still gives about 0. Sampled 30
    // times a second.
    struct GroundMotion {
        float speed = 0.0f;
        float direction[3] = {0.0f, 0.0f, 1.0f};
    };
    GroundMotion groundMotion(int clip, int skin = 0) const;
    // How far a clip carries the body (task humans 06: steps into a car):
    // the median of its joints' moves from the clip's start to its end, in
    // the model's space (metres). False for none.
    bool clipTravel(int clip, float *travel, int skin = 0) const;
    // How far a clip carries the hips (task humans 09: sitting down, the
    // body folding, its joints' median the hands'): the first joint down
    // from the skin's top that branches (the pelvis: legs and spine). False
    // for none.
    bool clipHipsTravel(int clip, float *travel, int skin = 0) const;

private:
    struct Data;
    std::unique_ptr<Data> data;
    std::vector<QString> nodeNames;
    std::vector<Skin> skins;
    // The rest transforms and the clips as built (for addClips).
    QVector<Node> restNodes;
    std::vector<ClipSource> sources;
    bool buildClip(const ClipSource &source, const QString &name, QStringList &warnings);
    std::vector<float> restHeights() const;
};

#endif
