/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "Crowd.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QDebug>
#include <QThreadPool>
#include <qopengl.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

#include <tsre/humans/ImpostorBake.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/renderer/Mesh.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/shape/ShapeLoader.h>
#include <tsre/shape/SkeletalAnimation.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

namespace Humans {

namespace {

constexpr unsigned int NoState = std::numeric_limits<unsigned int>::max();
// Frames of the walk an impostor holds.
constexpr int WalkFrames = 8;
// Idle poses an impostor holds at most.
constexpr int MaxIdlePoses = 6;
// An impostor's alpha cutoff, and how far past the last level's coverage
// (either way) a person changes between it and the mesh, so a person at the
// border does not flicker.
constexpr float ImpostorCutoff = 0.5f;
constexpr float FarBand = 0.05f;

// A texture made from a baked atlas (RGBA8888), registered by its key.
Texture *textureFrom(const QImage &image, const QString &key, bool data) {
    auto *texture = new Texture(key);
    texture->width = image.width();
    texture->height = image.height();
    texture->bytesPerPixel = 4;
    texture->bpp = 32;
    texture->type = GL_RGBA;
    texture->imageSize = texture->width * texture->height * 4;
    texture->imageData = new unsigned char[size_t(texture->imageSize)];
    for (int y = 0; y < texture->height; ++y)
        std::memcpy(texture->imageData + size_t(y) * size_t(texture->width) * 4, image.constScanLine(y),
                    size_t(texture->width) * 4);
    texture->data = data;
    texture->classifyAlpha();
    texture->editable = true;
    texture->loaded = true;
    return texture;
}

// A texture made from a baked atlas's compressed levels (level 0, then its
// mipmaps), registered by its key.
Texture *textureFrom(const std::vector<QByteArray> &levels, int width, int height, int glFormat, const QString &key,
                     bool data) {
    auto *texture = new Texture(key);
    texture->width = width;
    texture->height = height;
    texture->bytesPerPixel = 4;
    texture->bpp = 32;
    texture->type = GL_RGBA;
    texture->compressedGLFormat = glFormat;
    texture->compressedData = levels.front();
    int w = width, h = height;
    for (size_t level = 1; level < levels.size(); ++level) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        TextureMip mip;
        mip.width = w;
        mip.height = h;
        mip.compressedFormat = glFormat;
        mip.data = levels[level];
        texture->sourceMipmaps.push_back(mip);
    }
    texture->data = data;
    texture->classifyAlpha();
    texture->loaded = true;
    return texture;
}

// A repeatable random number in [0, 1) from a person's seed and what it is
// for.
float unitRandom(quint32 seed, quint32 salt) {
    quint32 x = seed * 0x9E3779B9u + salt * 0x85EBCA6Bu + 0x7F4A7C15u;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return float(x >> 8) * (1.0f / 16777216.0f);
}

}

struct Crowd::Kind {
    GltfShape *shape = nullptr;
    QString clipsFile;
    bool prepared = false;
    // Not a human file (no skin), or it failed to load.
    bool failed = false;
    std::vector<int> idles, walks;
    // Each clip's ground speed (m/s).
    std::vector<float> speeds;
    // Where the shape faces (radians about +y from +z): its walk's way.
    float forwardAngle = 0.0f;
    // Its largest extent and the middle of its height (the bounds' at
    // rest), for its coverage.
    float extent = 1.75f;
    float middle = 0.9f;
    // The least coverage of each level (one level 0 without levels).
    std::vector<float> coverage;
    // Each clip's shared phases: states posed once, NoState until used.
    struct Bank {
        float duration = 0.0f;
        std::vector<unsigned int> states;
    };
    std::vector<Bank> banks;
    // Own-pose states not in use, and the rest pose's state.
    std::vector<unsigned int> freeOwn;
    std::vector<unsigned int> ownInUse;
    unsigned int rest = NoState;
    // The impostor (step 3): its bake on a worker, then its textures (TexLib
    // ids; the colour's backend object once uploaded), quads and packets
    // (pose * directions + direction; pose 0 idle, then the walk's frames).
    struct BakeJob {
        std::atomic<bool> done{false};
        ImpostorBake::Result result;
    };
    std::shared_ptr<BakeJob> bake;
    bool impostorReady = false;
    int directions = 0;
    int walkFrames = 0;
    // Each idle clip's pose (one per distinct idle, a clips file's copy
    // sharing its original's), and the first walk frame's.
    std::vector<int> idlePose;
    int walkPose = 0;
    // Its atlases came with their mipmaps.
    bool compressed = false;
    float roughness = 0.8f;
    int colorTexture = -1;
    int normalTexture = -1;
    unsigned int colorObject = 0;
    MeshHandle impostorMesh;
    std::vector<RenderItem *> impostorPackets;
};

struct Crowd::Person {
    PersonSpec spec;
    bool removed = false;
    bool chosen = false;
    bool walking = false;
    int clip = -1;
    // Steered: its idle and walk clips, its speed now, and how far into
    // its clip it has played (its rate changes with its speed).
    int idleClip = -1;
    int walkClip = -1;
    float speed = 0.0f;
    double played = 0.0;
    // A clip its owner plays (steerClip), -1 none; at that time.
    int forcedClip = -1;
    float forcedTime = 0.0f;
    // Seconds into its clip at time 0, and its playing speed.
    float offset = 0.0f;
    float rate = 1.0f;
    // On its circle: the angle at time 0 and radians a second.
    float startAngle = 0.0f;
    float angularSpeed = 0.0f;
    // As drawn now.
    float position[3] = {0.0f, 0.0f, 0.0f};
    float heading = 0.0f;
    float distance = 0.0f;
    int level = -1;
    int tier = -1;
    unsigned int own = NoState;
    unsigned int draw = NoState;
    // Its impostor cell (far tier), -1 for its mesh.
    int cell = -1;
};

Crowd::Crowd() = default;

Crowd::~Crowd() {
    clear();
}

int Crowd::addKind(GltfShape *shape, const QString &clipsFile) {
    auto kind = std::make_unique<Kind>();
    kind->shape = shape;
    kind->clipsFile = clipsFile;
    kind->failed = shape == nullptr;
    kinds.push_back(std::move(kind));
    return int(kinds.size()) - 1;
}

int Crowd::addPerson(const PersonSpec &spec) {
    if (spec.kind < 0 || spec.kind >= kindCount())
        return -1;
    Person person;
    person.spec = spec;
    if (!freePeople.empty()) {
        const int index = freePeople.back();
        freePeople.pop_back();
        people[size_t(index)] = person;
        return index;
    }
    people.push_back(person);
    return int(people.size()) - 1;
}

int Crowd::personCount() const {
    return int(people.size());
}

void Crowd::steer(int person, const float *position, float way, float speed) {
    if (person < 0 || size_t(person) >= people.size())
        return;
    Person &p = people[size_t(person)];
    std::copy(position, position + 3, p.spec.position);
    p.spec.heading = way;
    p.speed = speed;
    p.forcedClip = -1;
}

void Crowd::steerClip(int person, const float *position, float way, int clip, float time) {
    if (person < 0 || size_t(person) >= people.size())
        return;
    Person &p = people[size_t(person)];
    std::copy(position, position + 3, p.spec.position);
    p.spec.heading = way;
    p.speed = 0.0f;
    p.forcedClip = clip;
    p.forcedTime = time;
}

int Crowd::kindOf(int person) const {
    return person >= 0 && size_t(person) < people.size() && !people[size_t(person)].removed
            ? people[size_t(person)].spec.kind : -1;
}

int Crowd::clipNamed(int kind, const QString &name) const {
    if (kind < 0 || kind >= kindCount() || !kinds[size_t(kind)]->prepared)
        return -1;
    const GltfShape *shape = kinds[size_t(kind)]->shape;
    for (int clip = 0; clip < shape->clipCount(); ++clip) {
        const QString own = shape->clipName(clip);
        if (own == name || own.startsWith(name + QStringLiteral(" (")))
            return clip;
    }
    return -1;
}

float Crowd::clipLength(int kind, int clip) const {
    if (kind < 0 || kind >= kindCount() || !kinds[size_t(kind)]->prepared || clip < 0
            || size_t(clip) >= kinds[size_t(kind)]->banks.size())
        return 0.0f;
    return kinds[size_t(kind)]->banks[size_t(clip)].duration;
}

bool Crowd::clipTravelOf(int person, int clip, float way, float *travel) {
    const int kindIndex = kindOf(person);
    if (kindIndex < 0 || !kinds[size_t(kindIndex)]->prepared)
        return false;
    const Kind &kind = *kinds[size_t(kindIndex)];
    const SkeletalAnimation *rig = kind.shape->skeleton();
    float model[3];
    if (rig == nullptr || !rig->clipTravel(clip, model, 0))
        return false;
    // Turned as the person is drawn (its heading about +y), at its size.
    const float heading = way - kind.forwardAngle, scale = people[size_t(person)].spec.scale;
    const float c = std::cos(heading), s = std::sin(heading);
    travel[0] = (model[0] * c + model[2] * s) * scale;
    travel[1] = model[1] * scale;
    travel[2] = (-model[0] * s + model[2] * c) * scale;
    return true;
}

void Crowd::releaseOwn(Person &p) {
    if (p.own == NoState || p.spec.kind < 0 || p.spec.kind >= kindCount())
        return;
    Kind &kind = *kinds[size_t(p.spec.kind)];
    kind.freeOwn.push_back(p.own);
    const auto used = std::find(kind.ownInUse.begin(), kind.ownInUse.end(), p.own);
    if (used != kind.ownInUse.end())
        kind.ownInUse.erase(used);
    p.own = NoState;
}

void Crowd::removePerson(int person) {
    if (person < 0 || size_t(person) >= people.size() || people[size_t(person)].removed)
        return;
    Person &p = people[size_t(person)];
    releaseOwn(p);
    p.removed = true;
    p.tier = -1;
    p.draw = NoState;
    p.cell = -1;
    freePeople.push_back(person);
}

float Crowd::walkSpeedOf(int person) {
    if (person < 0 || size_t(person) >= people.size() || people[size_t(person)].removed)
        return 0.0f;
    Person &p = people[size_t(person)];
    const Kind &kind = *kinds[size_t(p.spec.kind)];
    if (!kind.prepared)
        return 0.0f;
    if (!p.chosen)
        choosePlay(p, kind);
    if (p.walkClip < 0)
        return 0.0f;
    return kind.speeds[size_t(p.walkClip)] * p.spec.scale * p.rate;
}

void Crowd::clear() {
    for (std::unique_ptr<Kind> &kind : kinds) {
        if (kind->shape == nullptr)
            continue;
        auto release = [&](unsigned int state) {
            if (state != NoState)
                kind->shape->releaseState(state);
        };
        for (const Kind::Bank &bank : kind->banks)
            for (unsigned int state : bank.states)
                release(state);
        for (unsigned int state : kind->freeOwn)
            release(state);
        for (unsigned int state : kind->ownInUse)
            release(state);
        release(kind->rest);
        for (RenderItem *packet : kind->impostorPackets)
            Renderer::retirePacket(packet);
        Meshes::release(kind->impostorMesh);
    }
    people.clear();
    freePeople.clear();
    kinds.clear();
    clipFiles.clear();
    figures = Stats();
}

bool Crowd::prepare(Kind &kind) {
    GltfShape *shape = kind.shape;
    if (shape->hasLoadFailed()) {
        kind.failed = true;
        return false;
    }
    if (!shape->isLoaded()) {
        // As a shape drawn: a worker loads it, or this thread when it may.
        if (ShapeLoader::request(shape) == ShapeLoader::Request::LoadHere)
            shape->load();
        if (!shape->isLoaded())
            return false;
    }
    const SkeletalAnimation *rig = shape->skeleton();
    if (rig == nullptr || rig->skinCount() == 0) {
        qWarning() << "Crowd:" << shape->getPathId() << "has no skin, not a human";
        kind.failed = true;
        return false;
    }
    if (!kind.clipsFile.isEmpty()) {
        std::unique_ptr<GltfShape> &file = clipFiles[kind.clipsFile];
        if (!file) {
            const QFileInfo info(kind.clipsFile);
            file = std::make_unique<GltfShape>(kind.clipsFile, info.fileName(), info.path());
            file->load();
            if (!file->isLoaded())
                qWarning() << "Crowd: clips file" << kind.clipsFile << "did not load";
        }
        // Named after the file, as the Shape Viewer names them: a human's
        // own clips keep their names (and a name already there is skipped).
        if (file->isLoaded())
            shape->addClipsFrom(*file, QString(" (%1)").arg(QFileInfo(kind.clipsFile).completeBaseName()));
    }

    const int clips = shape->clipCount();
    kind.speeds.assign(size_t(clips), 0.0f);
    kind.banks.assign(size_t(clips), Kind::Bank());
    bool facing = false;
    for (int clip = 0; clip < clips; ++clip) {
        const SkeletalAnimation::GroundMotion motion = rig->groundMotion(clip, 0);
        kind.speeds[size_t(clip)] = motion.speed;
        // Clips played by name only (steps into a car, stairs).
        const QString name = shape->clipName(clip);
        if (name.startsWith(QStringLiteral("steps_")) || name.startsWith(QStringLiteral("stairs_"))) {
        } else if (motion.speed >= WalkSpeed) {
            kind.walks.push_back(clip);
            if (!facing) {
                facing = true;
                kind.forwardAngle = std::atan2(motion.direction[0], motion.direction[2]);
            }
        } else {
            kind.idles.push_back(clip);
        }
        Kind::Bank &bank = kind.banks[size_t(clip)];
        bank.duration = shape->clipDuration(clip);
        const int phases = std::clamp(int(std::lround(bank.duration * tuning.bankRate)), 4, 64);
        bank.states.assign(size_t(phases), NoState);
    }

    // Bounds: max x, min x, max y, min y, max z, min z.
    const float *b = shape->getBound();
    kind.extent = std::max({b[0] - b[1], b[2] - b[3], b[4] - b[5]});
    kind.middle = 0.5f * (b[2] + b[3]);
    kind.coverage.clear();
    for (int level = 0; level < shape->distanceLevelCount(); ++level)
        kind.coverage.push_back(shape->distanceLevelCoverage(level));
    if (kind.coverage.empty())
        kind.coverage.push_back(0.0f);
    kind.rest = shape->newState();
    kind.prepared = true;
    if (tuning.impostors)
        startBake(kind);
    return true;
}

// The bake on a worker: the first idle at its middle (the rest pose without
// idles), then the first walk's frames. The kept triangles and the skeleton
// are shared with it, read only.
void Crowd::startBake(Kind &kind) {
    std::shared_ptr<const GltfShape::SkinnedSource> source = kind.shape->skinnedSource();
    std::shared_ptr<const SkeletalAnimation> rig = kind.shape->sharedSkeleton();
    if (!source || !rig)
        return;
    ImpostorBake::Request request;
    request.directions = std::clamp(tuning.impostorDirections, 4, 32);
    request.cellHeight = std::clamp(tuning.impostorHeight, 16, 512);
    request.cellWidth = request.cellHeight / 2;
    // An idle's middle per distinct idle (by name, a clips file's suffix
    // left out), at most MaxIdlePoses; the rest pose without idles.
    kind.idlePose.assign(kind.banks.size(), 0);
    QStringList idleNames;
    for (int clip : kind.idles) {
        QString name = kind.shape->clipName(clip);
        const int suffix = name.lastIndexOf(" (");
        if (suffix > 0 && name.endsWith(')'))
            name.truncate(suffix);
        int pose = int(idleNames.indexOf(name));
        if (pose < 0 && idleNames.size() < MaxIdlePoses) {
            pose = int(idleNames.size());
            idleNames << name;
            ImpostorBake::Pose idle;
            idle.clip = clip;
            idle.time = 0.5f * kind.banks[size_t(clip)].duration;
            request.poses.push_back(idle);
        }
        kind.idlePose[size_t(clip)] = std::max(pose, 0);
    }
    if (request.poses.empty())
        request.poses.push_back(ImpostorBake::Pose());
    kind.walkPose = int(request.poses.size());
    kind.walkFrames = kind.walks.empty() ? 0 : WalkFrames;
    for (int frame = 0; frame < kind.walkFrames; ++frame) {
        ImpostorBake::Pose walk;
        walk.clip = kind.walks.front();
        walk.time = kind.banks[size_t(walk.clip)].duration * float(frame) / float(kind.walkFrames);
        request.poses.push_back(walk);
    }
    float roughness = 0.0f;
    for (const GltfShape::SkinnedSource::Primitive &prim : source->primitives)
        roughness += prim.roughness;
    kind.roughness = source->primitives.empty() ? 0.8f : roughness / float(source->primitives.size());
    auto job = std::make_shared<Kind::BakeJob>();
    kind.bake = job;
    QThreadPool::globalInstance()->start([job, source, rig, request]() {
        job->result = ImpostorBake::bake(*source, *rig, request);
        job->done.store(true, std::memory_order_release);
    });
}

// The baked atlases as textures, a quad per cell in its direction's plane,
// and a packet each.
void Crowd::finishBake(Kind &kind) {
    std::shared_ptr<Kind::BakeJob> job = std::move(kind.bake);
    const ImpostorBake::Result &r = job->result;
    ++bakesDone;
    bakesMs += r.milliseconds;
    if (!r.ok) {
        qWarning() << "Crowd:" << kind.shape->getPathId() << "impostor not baked:" << r.error;
        return;
    }
    const QString key = QString("gltfimg:impostor:%1:%2x%3x%4").arg(kind.shape->getPathId()).arg(r.directions)
            .arg(r.cellWidth).arg(r.cellHeight);
    kind.compressed = !r.colorLevels.empty() && !r.normalLevels.empty();
    if (kind.compressed) {
        kind.colorTexture = TexLib::addTex(textureFrom(r.colorLevels, r.color.width(), r.color.height(),
                                                       GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, key + ":color", false));
        kind.normalTexture = TexLib::addTex(textureFrom(r.normalLevels, r.normal.width(), r.normal.height(),
                                                        GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, key + ":normal", true));
    } else {
        kind.colorTexture = TexLib::addTex(textureFrom(r.color, key + ":color", false));
        kind.normalTexture = TexLib::addTex(textureFrom(r.normal, key + ":normal", true));
    }

    const int stride = RenderItem::PBR;
    const int cells = r.directions * r.poses;
    MeshData data;
    data.layout = RenderItem::PBR;
    data.vertices.assign(size_t(cells) * 6 * size_t(stride), 0.0f);
    for (int cell = 0; cell < cells; ++cell) {
        const float angle = 2.0f * float(M_PI) * float(cell % r.directions) / float(r.directions);
        const float toward[3] = {std::sin(angle), 0.0f, std::cos(angle)};
        const float right[3] = {std::cos(angle), 0.0f, -std::sin(angle)};
        float uv[4];
        r.cellRect(cell, uv);
        // Bottom left, bottom right, top right, top left (v down).
        const float corners[4][4] = {{-r.halfWidth, r.bottom, uv[0], uv[3]}, {r.halfWidth, r.bottom, uv[2], uv[3]},
                                     {r.halfWidth, r.top, uv[2], uv[1]}, {-r.halfWidth, r.top, uv[0], uv[1]}};
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int v = 0; v < 6; ++v) {
            const float *c = corners[order[v]];
            float *vertex = data.vertices.data() + (size_t(cell) * 6 + size_t(v)) * size_t(stride);
            for (int i = 0; i < 3; ++i) {
                vertex[i] = right[i] * c[0] + (i == 1 ? c[1] : 0.0f);
                vertex[3 + i] = toward[i];
                vertex[9 + i] = right[i];
            }
            vertex[6] = c[2];
            vertex[7] = c[3];
            vertex[8] = -ImpostorCutoff;
            vertex[12] = 1.0f;
            for (int i = 15; i < 19; ++i)
                vertex[i] = 1.0f;
        }
    }
    kind.impostorMesh = Meshes::create(std::move(data));
    const float center[3] = {0.0f, 0.5f * (r.bottom + r.top), 0.0f};
    const float radius = std::sqrt(r.halfWidth * r.halfWidth + 0.25f * (r.top - r.bottom) * (r.top - r.bottom));
    for (int cell = 0; cell < cells; ++cell) {
        auto *packet = new RenderItem();
        packet->mesh.handle = kind.impostorMesh;
        packet->mesh.layout = RenderItem::PBR;
        packet->mesh.primitive = RenderItem::PRIMITIVE_TRIANGLES;
        packet->mesh.first = unsigned(cell) * 6;
        packet->mesh.count = 6;
        packet->shared = true;
        packet->material.lit = true;
        packet->material.brightness = 1.0f;
        packet->material.surface = RenderItem::SURFACE_ALPHA_TEST;
        packet->material.doubleSided = true;
        packet->pbr.enabled = true;
        packet->pbr.metallic = 0.0f;
        packet->pbr.roughness = kind.roughness;
        packet->pbr.alphaCutoff = ImpostorCutoff;
        packet->pbr.textures[RenderItem::Pbr::MAP_NORMAL] = kind.normalTexture;
        packet->setBounds(center, radius, nullptr);
        packet->disableTextures(1.0f, 1.0f, 1.0f, 1.0f);
        kind.impostorPackets.push_back(packet);
    }
    kind.directions = r.directions;
    kind.impostorReady = true;
    qInfo().noquote() << QString("Crowd: %1 impostor baked: %2 directions, %3 poses, %4 x %5 px, %6 ms")
                         .arg(kind.shape->getPathId()).arg(r.directions).arg(r.poses).arg(r.color.width())
                         .arg(r.color.height()).arg(r.milliseconds, 0, 'f', 1);
}

// Its atlases uploaded with mipmaps (the colour's keeping its coverage),
// once; its packets drawing the colour then.
void Crowd::syncImpostorTextures(Kind &kind) {
    const bool chain = kind.compressed;
    auto upload = [chain](int id, bool color) -> unsigned int {
        const auto found = TexLib::mtex.find(id);
        Texture *texture = found == TexLib::mtex.end() ? nullptr : found->second;
        if (texture == nullptr || !texture->loaded)
            return 0;
        if (!texture->glLoaded) {
            // Its own mipmaps keep the coverage; else the upload's (QRhi).
            if (color && !chain)
                texture->coverageCutoff = ImpostorCutoff;
            texture->GLTextures(true);
        }
        return texture->glLoaded && texture->tex != nullptr ? texture->tex[0] : 0;
    };
    const unsigned int color = upload(kind.colorTexture, true);
    upload(kind.normalTexture, false);
    if (color == kind.colorObject)
        return;
    kind.colorObject = color;
    for (RenderItem *packet : kind.impostorPackets) {
        if (color != 0)
            packet->enableTextures(color);
        else
            packet->disableTextures(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

void Crowd::choosePlay(Person &person, const Kind &kind) {
    const quint32 seed = person.spec.seed;
    person.chosen = true;
    if (person.spec.behaviour == Behaviour::Steered) {
        auto pick = [&](const std::vector<int> &clips, quint32 salt) {
            return clips.empty() ? -1 : clips[std::min(size_t(unitRandom(seed, salt) * float(clips.size())), clips.size() - 1)];
        };
        person.idleClip = pick(kind.idles, 1);
        person.walkClip = pick(kind.walks, 6);
        person.clip = person.idleClip >= 0 ? person.idleClip : person.walkClip;
        person.rate = 0.95f + 0.1f * unitRandom(seed, 5);
        person.played = person.clip >= 0 ? double(unitRandom(seed, 2) * kind.banks[size_t(person.clip)].duration) : 0.0;
        return;
    }
    person.walking = person.spec.behaviour == Behaviour::Circle && !kind.walks.empty();
    const std::vector<int> &clips = person.walking ? kind.walks : kind.idles;
    if (clips.empty()) {
        person.clip = -1;
        return;
    }
    person.clip = clips[std::min(size_t(unitRandom(seed, 1) * float(clips.size())), clips.size() - 1)];
    person.offset = unitRandom(seed, 2) * kind.banks[size_t(person.clip)].duration;
    // People do not move in step: within 5 % of the clip's speed.
    person.rate = 0.95f + 0.1f * unitRandom(seed, 5);
    if (person.walking) {
        const float speed = kind.speeds[size_t(person.clip)] * person.spec.scale * person.rate;
        const float radius = std::max(person.spec.radius, 0.1f);
        person.startAngle = unitRandom(seed, 3) * 2.0f * float(M_PI);
        person.angularSpeed = speed / radius * (unitRandom(seed, 4) < 0.5f ? -1.0f : 1.0f);
    }
}

unsigned int Crowd::bankState(Kind &kind, int clip, int phase) {
    Kind::Bank &bank = kind.banks[size_t(clip)];
    unsigned int &state = bank.states[size_t(phase)];
    if (state == NoState) {
        state = kind.shape->newState();
        ComplexShape::ClipPlay play;
        play.clip = clip;
        play.time = bank.duration * float(phase) / float(bank.states.size());
        kind.shape->setClips(state, &play, 1);
        kind.shape->updatePose(state);
        ++figures.bankStates;
    }
    return state;
}

unsigned int Crowd::ownState(Kind &kind) {
    unsigned int state;
    if (!kind.freeOwn.empty()) {
        state = kind.freeOwn.back();
        kind.freeOwn.pop_back();
    } else {
        state = kind.shape->newState();
    }
    kind.ownInUse.push_back(state);
    return state;
}

void Crowd::update(float seconds, const View &view) {
    QElapsedTimer timer;
    timer.start();
    clock += double(seconds);
    const int bankStates = figures.bankStates;
    const double pushMs = figures.pushMs;
    figures = Stats();
    figures.bankStates = bankStates;
    figures.pushMs = pushMs;
    figures.people = personCount() - int(freePeople.size());
    for (std::unique_ptr<Kind> &kind : kinds) {
        if (!kind->prepared && !kind->failed)
            prepare(*kind);
        if (kind->bake && kind->bake->done.load(std::memory_order_acquire))
            finishBake(*kind);
    }
    figures.impostorsBaked = bakesDone;
    figures.bakeMs = bakesMs;

    const float viewScale = 2.0f * std::tan(view.fieldOfView * float(M_PI) / 360.0f);
    nearCandidates.clear();
    for (size_t i = 0; i < people.size(); ++i) {
        Person &p = people[i];
        if (p.removed)
            continue;
        Kind &kind = *kinds[size_t(p.spec.kind)];
        const int previousTier = p.tier;
        p.tier = -1;
        p.level = -1;
        p.cell = -1;
        if (!kind.prepared) {
            ++figures.waiting;
            continue;
        }
        if (!p.chosen)
            choosePlay(p, kind);
        // Its place: the walkers along their circles, facing the way.
        std::copy(p.spec.position, p.spec.position + 3, p.position);
        p.heading = p.spec.heading;
        if (p.spec.behaviour == Behaviour::Steered) {
            // Its walk played at the speed it is moved (within reason), its
            // clip time carried on as that changes.
            if (p.forcedClip >= 0 && size_t(p.forcedClip) < kind.banks.size()) {
                // Its owner's clip at its owner's time (far off: walk frames).
                p.walking = true;
                p.clip = p.forcedClip;
                p.played = double(p.forcedTime);
            } else {
                p.walking = p.walkClip >= 0 && p.speed >= WalkSpeed;
                p.clip = p.walking ? p.walkClip : p.idleClip;
                const float natural = p.walking ? kind.speeds[size_t(p.clip)] * p.spec.scale : 0.0f;
                const float rate = natural > 0.0f ? std::clamp(p.speed / natural, 0.5f, 1.8f) : p.rate;
                p.played += double(seconds) * double(rate);
            }
            p.heading = p.spec.heading - kind.forwardAngle;
        } else if (p.walking) {
            const float angle = p.startAngle + p.angularSpeed * float(clock);
            const float radius = std::max(p.spec.radius, 0.1f);
            p.position[0] += radius * std::cos(angle);
            p.position[2] += radius * std::sin(angle);
            const float way = p.angularSpeed < 0.0f ? -1.0f : 1.0f;
            p.heading = std::atan2(-std::sin(angle) * way, std::cos(angle) * way) - kind.forwardAngle;
        }
        // Its coverage, as ShapeLod::coverage measures it.
        const float dx = p.position[0] - view.camera[0];
        const float dy = p.position[1] + kind.middle * p.spec.scale - view.camera[1];
        const float dz = p.position[2] - view.camera[2];
        p.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float span = p.distance * viewScale;
        const float coverage = span > 1e-6f ? kind.extent * p.spec.scale / span : 1e6f;
        const int levels = int(kind.coverage.size());
        p.level = levels - 1;
        for (int level = 0; level < levels; ++level)
            if (coverage >= kind.coverage[size_t(level)] * tuning.coverageScale) {
                p.level = level;
                break;
            }
        // Far past the last level's coverage, with a band either way.
        const float band = previousTier == 2 ? 1.0f + FarBand : 1.0f - FarBand;
        const bool far = levels > 1 && coverage < kind.coverage.back() * tuning.coverageScale * band;
        p.tier = far ? 2 : p.level == 0 ? 0 : 1;
        if (p.tier == 0)
            nearCandidates.push_back(int(i));
    }
    // Own poses for the nearest few only.
    if (int(nearCandidates.size()) > tuning.nearCap) {
        const size_t cap = size_t(std::max(tuning.nearCap, 0));
        std::nth_element(nearCandidates.begin(), nearCandidates.begin() + long(cap), nearCandidates.end(),
                         [this](int a, int b) { return people[size_t(a)].distance < people[size_t(b)].distance; });
        for (size_t k = cap; k < nearCandidates.size(); ++k)
            people[size_t(nearCandidates[k])].tier = 1;
    }

    for (Person &p : people) {
        if (p.tier < 0)
            continue;
        Kind &kind = *kinds[size_t(p.spec.kind)];
        if (p.tier != 0 && p.own != NoState) {
            kind.freeOwn.push_back(p.own);
            kind.ownInUse.erase(std::find(kind.ownInUse.begin(), kind.ownInUse.end(), p.own));
            p.own = NoState;
        }
        if (p.tier == 0)
            ++figures.nearTier;
        else if (p.tier == 1)
            ++figures.middleTier;
        else
            ++figures.farTier;
        if (p.clip < 0) {
            p.draw = kind.rest;
            ++figures.unposed;
            continue;
        }
        const Kind::Bank &bank = kind.banks[size_t(p.clip)];
        const float duration = bank.duration;
        const double t = p.spec.behaviour == Behaviour::Steered ? p.played : clock * double(p.rate) + double(p.offset);
        const float phaseTime = duration > 0.0f ? float(t - std::floor(t / double(duration)) * double(duration)) : 0.0f;
        if (p.tier == 2 && kind.impostorReady && kind.directions > 0) {
            // The baked direction nearest the view's, in the shape's space,
            // and its walk frame.
            const float angle = std::atan2(view.camera[0] - p.position[0], view.camera[2] - p.position[2]) - p.heading;
            const float step = 2.0f * float(M_PI) / float(kind.directions);
            int direction = int(std::lround(angle / step)) % kind.directions;
            if (direction < 0)
                direction += kind.directions;
            int pose = size_t(p.clip) < kind.idlePose.size() ? kind.idlePose[size_t(p.clip)] : 0;
            if (p.walking && kind.walkFrames > 0 && duration > 0.0f)
                pose = kind.walkPose + int(std::lround(phaseTime / duration * float(kind.walkFrames))) % kind.walkFrames;
            p.cell = pose * kind.directions + direction;
            p.draw = NoState;
            continue;
        }
        if (p.tier == 0) {
            if (p.own == NoState)
                p.own = ownState(kind);
            ComplexShape::ClipPlay play;
            play.clip = p.clip;
            play.time = phaseTime;
            kind.shape->setClips(p.own, &play, 1);
            kind.shape->updatePose(p.own);
            p.draw = p.own;
            ++figures.ownPoses;
        } else {
            const int phases = int(bank.states.size());
            const int phase = duration > 0.0f ? int(std::lround(phaseTime / duration * float(phases))) % phases : 0;
            p.draw = bankState(kind, p.clip, phase);
        }
        if (kind.shape->poseJointRows(p.draw, 0) == nullptr)
            ++figures.unposed;
    }
    figures.updateMs = double(timer.nsecsElapsed()) / 1e6;
}

void Crowd::push(RenderQueue &queue) {
    QElapsedTimer timer;
    timer.start();
    const bool animated = queue.animatedEnabled();
    const bool casting = queue.shadowCastingEnabled();
    queue.setAnimated(true);
    for (std::unique_ptr<Kind> &kind : kinds)
        if (kind->impostorReady)
            syncImpostorTextures(*kind);
    figures.impostorsDrawn = 0;
    for (const Person &p : people) {
        if (p.tier < 0)
            continue;
        const Kind &kind = *kinds[size_t(p.spec.kind)];
        if (p.cell >= 0) {
            // Its impostor: no shadow; nothing until its texture is up.
            if (kind.colorObject == 0 || size_t(p.cell) >= kind.impostorPackets.size())
                continue;
            queue.pushTransform();
            float *mv = queue.transform();
            Mat4::translate(mv, mv, p.position[0], p.position[1], p.position[2]);
            Mat4::rotateY(mv, mv, p.heading);
            if (p.spec.scale != 1.0f)
                for (int c = 0; c < 12; ++c)
                    mv[c] *= p.spec.scale;
            queue.setShadowCasting(false);
            queue.submit(kind.impostorPackets[size_t(p.cell)]);
            queue.setShadowCasting(casting);
            queue.popTransform();
            ++figures.impostorsDrawn;
            continue;
        }
        if (p.draw == NoState)
            continue;
        GltfShape *shape = kind.shape;
        // Near people cast shadows; middle ones as the quality says.
        queue.setShadowCasting(casting && (p.tier == 0 || tuning.middleShadows));
        queue.pushTransform();
        float *mv = queue.transform();
        Mat4::translate(mv, mv, p.position[0], p.position[1], p.position[2]);
        Mat4::rotateY(mv, mv, p.heading);
        if (p.spec.scale != 1.0f)
            for (int c = 0; c < 12; ++c)
                mv[c] *= p.spec.scale;
        shape->setCurrentDistanceLevel(p.draw, p.level);
        shape->pushRenderItem(queue, 0, p.draw);
        queue.popTransform();
    }
    queue.setShadowCasting(casting);
    queue.setAnimated(animated);
    figures.pushMs = double(timer.nsecsElapsed()) / 1e6;
}

std::vector<int> Crowd::walkClips(int kind) const {
    return kind >= 0 && kind < kindCount() ? kinds[size_t(kind)]->walks : std::vector<int>();
}

std::vector<int> Crowd::idleClips(int kind) const {
    return kind >= 0 && kind < kindCount() ? kinds[size_t(kind)]->idles : std::vector<int>();
}

float Crowd::clipSpeed(int kind, int clip) const {
    if (kind < 0 || kind >= kindCount() || clip < 0 || size_t(clip) >= kinds[size_t(kind)]->speeds.size())
        return 0.0f;
    return kinds[size_t(kind)]->speeds[size_t(clip)];
}

int Crowd::tierOf(int person) const {
    return person >= 0 && person < personCount() ? people[size_t(person)].tier : -1;
}

int Crowd::levelOf(int person) const {
    return person >= 0 && person < personCount() ? people[size_t(person)].level : -1;
}

unsigned int Crowd::stateOf(int person) const {
    return person >= 0 && person < personCount() ? people[size_t(person)].draw : NoState;
}

const float *Crowd::placeOf(int person) const {
    return person >= 0 && person < personCount() ? people[size_t(person)].position : nullptr;
}

bool Crowd::baking() const {
    for (const std::unique_ptr<Kind> &kind : kinds)
        if (kind->bake)
            return true;
    return false;
}

bool Crowd::impostorReady(int kind) const {
    return kind >= 0 && kind < kindCount() && kinds[size_t(kind)]->impostorReady;
}

int Crowd::impostorDirections(int kind) const {
    return kind >= 0 && kind < kindCount() ? kinds[size_t(kind)]->directions : 0;
}

int Crowd::impostorCellOf(int person) const {
    return person >= 0 && person < personCount() ? people[size_t(person)].cell : -1;
}

Crowd::Tuning Crowd::quality(int level) {
    Tuning t;
    switch (std::clamp(level, 0, 3)) {
    case 0:
        t.nearCap = 10;
        t.coverageScale = 2.0f;
        t.impostorDirections = 8;
        t.impostorHeight = 64;
        t.middleShadows = false;
        break;
    case 1:
        t.nearCap = 20;
        t.coverageScale = 1.4f;
        t.impostorDirections = 8;
        t.impostorHeight = 96;
        t.middleShadows = false;
        break;
    case 3:
        t.nearCap = 80;
        t.coverageScale = 0.7f;
        break;
    default:
        break;
    }
    return t;
}

}
