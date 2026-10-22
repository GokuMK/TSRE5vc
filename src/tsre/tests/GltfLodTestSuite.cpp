#include "GltfLodTestSuite.h"
#include "TokenTestSupport.h"
#include <tsre/Game.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/shape/ShapeLod.h>
#include <settings/SettingsManager.h>
#include <QScopeGuard>
#include <QFile>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Records the packets a shape submits, each with the queue's shadow state:
// shadow-only, or casting.
struct PacketRecorder : RenderQueue {
    QVector<RenderItem *> packets;
    QVector<bool> shadowOnly;
    QVector<bool> casting;
    using RenderQueue::submit;
    void record(RenderItem *item) {
        packets.push_back(item);
        shadowOnly.push_back(shadowOnlyEnabled());
        casting.push_back(shadowCastingEnabled());
    }
    void submit(RenderItem *item, quint32 = 0, SubmitOrder = SUBMIT_GROUPED) override {
        record(item);
    }
    void submit(const QVector<RenderItem *> &items, quint32 = 0) override {
        for (RenderItem *item : items)
            record(item);
    }
    void submitFrameItem(RenderItem *item) override {
        record(item);
    }
};

// Four meshes of 1, 2, 3 and 4 triangles (3, 6, 9 and 12 vertices), each a
// copy of one triangle 1 m wide and 10 m high: a packet's vertex count tells
// its mesh.
QByteArray positions() {
    QByteArray bytes;
    const float triangle[9] = {0, 0, 0, 1, 0, 0, 0, 10, 0};
    for (int mesh = 1; mesh <= 4; ++mesh)
        for (int copy = 0; copy < mesh; ++copy)
            bytes.append(reinterpret_cast<const char *>(triangle), sizeof(triangle));
    return bytes;
}

// The group: node 0 (moved 100 m along x) holds node 1, level 0 (mesh of 3
// vertices) with MSFT_lod naming nodes 2 (6) and 3 (9, moved 5 m along z),
// and node 4 (12) beside it at every level.
const char *const LodNodes = R"([
  {"name": "parent", "translation": [100, 0, 0], "children": [1, 4]},
  {"name": "lod0", "mesh": 0, "extensions": {"MSFT_lod": {"ids": IDS}}, "extras": EXTRAS},
  {"name": "lod1", "mesh": 1 CHILDREN},
  {"name": "lod2", "mesh": 2, "translation": [0, 0, 5]},
  {"name": "common", "mesh": 3}
])";

QByteArray document(const QString &nodes, const QString &extensionsUsed, const QString &bufferUri) {
    QString json = R"({
 "asset": {"version": "2.0"},
 "extensionsUsed": USED,
 "scene": 0, "scenes": [{"nodes": [0]}],
 "nodes": NODES,
 "meshes": [
  {"primitives": [{"attributes": {"POSITION": 0}}]},
  {"primitives": [{"attributes": {"POSITION": 1}}]},
  {"primitives": [{"attributes": {"POSITION": 2}}]},
  {"primitives": [{"attributes": {"POSITION": 3}}]}
 ],
 "accessors": [
  {"bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 10, 0]},
  {"bufferView": 0, "byteOffset": 36, "componentType": 5126, "count": 6, "type": "VEC3", "min": [0, 0, 0], "max": [1, 10, 0]},
  {"bufferView": 0, "byteOffset": 108, "componentType": 5126, "count": 9, "type": "VEC3", "min": [0, 0, 0], "max": [1, 10, 0]},
  {"bufferView": 0, "byteOffset": 216, "componentType": 5126, "count": 12, "type": "VEC3", "min": [0, 0, 0], "max": [1, 10, 0]}
 ],
 "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 360}],
 "buffers": [{BUFFER"byteLength": 360}]
})";
    json.replace("USED", extensionsUsed).replace("NODES", nodes).replace("BUFFER", bufferUri);
    return json.toUtf8();
}

QString lodNodes(const QString &ids, const QString &extras = R"({"MSFT_screencoverage": [0.3, 0.1, 0.01]})",
                 const QString &children = {}) {
    return QString(LodNodes).replace("IDS", ids).replace("EXTRAS", extras).replace("CHILDREN", children);
}

QByteArray gltf(const QString &nodes, const QString &extensionsUsed = R"(["MSFT_lod"])") {
    return document(nodes, extensionsUsed,
                    "\"uri\": \"data:application/octet-stream;base64," + positions().toBase64() + "\", ");
}

QByteArray glb(const QString &nodes) {
    QByteArray json = document(nodes, R"(["MSFT_lod"])", {});
    while (json.size() % 4)
        json.append(' ');
    QByteArray bin = positions();
    while (bin.size() % 4)
        bin.append('\0');
    auto word = [](QByteArray &out, quint32 value) {
        for (int i = 0; i < 4; ++i)
            out.append(char((value >> (8 * i)) & 0xff));
    };
    QByteArray out("glTF");
    word(out, 2);
    word(out, quint32(12 + 8 + json.size() + 8 + bin.size()));
    word(out, quint32(json.size()));
    out.append("JSON");
    out.append(json);
    word(out, quint32(bin.size()));
    out.append("BIN", 4);
    out.append(bin);
    return out;
}

struct Loaded {
    std::unique_ptr<GltfShape> shape;
    unsigned int state = 0;
};

Loaded load(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes) {
    const QString path = dir.filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(bytes);
    file.close();
    Loaded loaded;
    loaded.shape = std::make_unique<GltfShape>(path, name, dir.path());
    loaded.shape->load();
    loaded.state = loaded.shape->newState();
    return loaded;
}

// The packets drawn at a level: their vertex counts, sorted, and the x and z
// of the packet of a mesh by its vertex count.
struct Drawn {
    QVector<int> counts;
    float x[13] = {};
    float z[13] = {};
};

Drawn drawn(Loaded &loaded, int level) {
    PacketRecorder recorder;
    loaded.shape->setCurrentDistanceLevel(loaded.state, level);
    loaded.shape->pushRenderItem(recorder, 0, loaded.state);
    Drawn result;
    for (const RenderItem *item : recorder.packets) {
        if (!item->mesh.handle.valid())
            continue;
        const int count = item->mesh.count;
        result.counts.push_back(count);
        if (count < 13 && item->msMatrix != nullptr) {
            result.x[count] = item->msMatrix[12];
            result.z[count] = item->msMatrix[14];
        }
    }
    std::sort(result.counts.begin(), result.counts.end());
    return result;
}

}

int TsreTests::runGltfLodSuite(bool verbose) {
    TokenTest::Suite test{"[tests:gltf-lod]", verbose};
    QTemporaryDir dir;
    using Counts = QVector<int>;

    for (const bool binary : {false, true}) {
        const QString kind = binary ? "GLB" : "glTF";
        const QString nodes = lodNodes("[2, 3]");
        Loaded shape = load(dir, binary ? "lod.glb" : "lod.gltf", binary ? glb(nodes) : gltf(nodes));
        test.check(shape.shape->isLoaded() && shape.shape->distanceLevelCount() == 3,
                   kind + ": three levels");
        test.check(shape.shape->distanceLevelCoverage(0) == 0.3f && shape.shape->distanceLevelCoverage(1) == 0.1f
                       && shape.shape->distanceLevelCoverage(2) == 0.01f,
                   kind + ": the coverages of MSFT_screencoverage");
        const Drawn level0 = drawn(shape, 0), level1 = drawn(shape, 1), level2 = drawn(shape, 2);
        test.check(level0.counts == Counts{3, 12} && level1.counts == Counts{6, 12} && level2.counts == Counts{9, 12},
                   kind + ": each level draws its node and the common one");
        test.check(level1.x[6] == 100.0f && level2.x[9] == 100.0f && level2.z[9] == 5.0f,
                   kind + ": level nodes under the group's parent");
        test.check(drawn(shape, 7).counts == Counts{3, 12}, kind + ": an unknown level draws level 0");
    }

    // Invalid groups: drawn at level 0 only, the nodes they name not drawn
    // (unless the scene holds them).
    struct Invalid {
        QString name, nodes, used;
        Counts counts;
    };
    const QString used = R"(["MSFT_lod"])";
    const Invalid invalid[] = {
        {"naming itself", lodNodes("[1]"), used, {3, 6, 9, 12}},
        {"naming a node out of range", lodNodes("[2, 9]"), used, {3, 9, 12}},
        {"naming a node of the scene", lodNodes("[4, 3]"), used, {3, 6, 12}},
        {"with a cycle", lodNodes("[2, 3]", "{}", R"(, "children": [3])"), used, {3, 12}},
        {"without MSFT_lod in extensionsUsed", lodNodes("[2, 3]"), "[]", {3, 12}},
    };
    for (const Invalid &group : invalid) {
        Loaded shape = load(dir, "invalid.gltf", gltf(group.nodes, group.used));
        test.check(shape.shape->isLoaded() && shape.shape->distanceLevelCount() == 0
                       && drawn(shape, 0).counts == group.counts,
                   "a group " + group.name + ": level 0 only");
    }

    {
        Loaded shape = load(dir, "defaults.gltf", gltf(lodNodes("[2, 3]", "{}")));
        test.check(shape.shape->distanceLevelCoverage(0) == 0.2f && shape.shape->distanceLevelCoverage(1) == 0.05f
                       && shape.shape->distanceLevelCoverage(2) == 0.001f,
                   "no MSFT_screencoverage: Open Rails' defaults");
    }
    {
        const QString plain = R"([{"mesh": 0, "children": [1]}, {"mesh": 1}, {"mesh": 2}])";
        Loaded shape = load(dir, "plain.gltf", gltf(plain, "[]"));
        test.check(shape.shape->distanceLevelCount() == 0 && drawn(shape, 0).counts == Counts{3, 6, 9},
                   "no MSFT_lod: every node drawn as before");
    }

    // External levels: "x_LOD01.gltf"... beside the entry, each a level.
    {
        auto write = [&](const QString &name, const QByteArray &bytes) {
            QDir().mkpath(QFileInfo(dir.filePath(name)).path());
            QFile file(dir.filePath(name));
            if (file.open(QIODevice::WriteOnly))
                file.write(bytes);
        };
        const QString entry = R"([{"mesh": 0, "extras": {"MSFT_screencoverage": [0.3, 0.1, 0.01]}}])";
        const QString level1 = R"([{"mesh": 1}])", level2 = R"([{"mesh": 2}])";
        write("ext/ext_lod01.gltf", gltf(level1, "[]"));
        write("ext/ext_lod02.gltf", gltf(level2, "[]"));
        Loaded ext = load(dir, "ext/ext.gltf", gltf(entry, "[]"));
        test.check(ext.shape->distanceLevelCount() == 3 && ext.shape->distanceLevelCoverage(1) == 0.1f,
                   "external levels: the companions spelt as the entry; the entry's coverages");
        test.check(drawn(ext, 0).counts == Counts{3} && drawn(ext, 1).counts == Counts{6}
                       && drawn(ext, 2).counts == Counts{9},
                   "external levels: each draws its file");

        write("glb/tree_lod01.glb", glb(level1));
        test.check(load(dir, "glb/tree.glb", glb(entry)).shape->distanceLevelCount() == 2,
                   "external levels of a GLB: GLB companions");
        write("mix/tree_lod01.glb", glb(level1));
        test.check(load(dir, "mix/tree.gltf", gltf(entry, "[]")).shape->distanceLevelCount() == 0,
                   "external levels: the entry's extension only");

        write("gap/gap_lod01.gltf", gltf(level1, "[]"));
        write("gap/gap_lod03.gltf", gltf(level2, "[]"));
        test.check(load(dir, "gap/gap.gltf", gltf(entry, "[]")).shape->distanceLevelCount() == 2,
                   "external levels: a missing number ends them");

        write("zero/tree_lod01.gltf", gltf(level1, "[]"));
        test.check(load(dir, "zero/tree_LOD00.gltf", gltf(entry, "[]")).shape->distanceLevelCount() == 2,
                   "external levels: an entry named _LOD00 is level 0");
        test.check(load(dir, "zero/tree_lod01.gltf", gltf(level1, "[]")).shape->distanceLevelCount() == 0,
                   "external levels: none for a lower level itself");

        write("both/both_lod01.gltf", gltf(level2, "[]"));
        Loaded both = load(dir, "both/both.gltf", gltf(lodNodes("[2, 3]")));
        test.check(both.shape->distanceLevelCount() == 3 && drawn(both, 1).counts == Counts{6, 12},
                   "internal levels take precedence over external ones");

        write("bad/bad_lod01.gltf", "not glTF");
        Loaded bad = load(dir, "bad/bad.gltf", gltf(entry, "[]"));
        test.check(bad.shape->distanceLevelCount() == 0 && drawn(bad, 0).counts == Counts{3},
                   "external levels: a broken first level, none");

        write("plain/plain_lod01.gltf", gltf(level1, "[]"));
        Loaded plain = load(dir, "plain/plain.gltf", gltf(R"([{"mesh": 0}])", "[]"));
        test.check(plain.shape->distanceLevelCoverage(0) == 0.2f && plain.shape->distanceLevelCoverage(1) == 0.05f,
                   "external levels without MSFT_screencoverage: Open Rails' defaults");
    }

    // Impostors: "tree.impostor" beside the shape, a quad per frame.
    {
        auto write = [&](const QString &name, const QByteArray &bytes) {
            QDir().mkpath(QFileInfo(dir.filePath(name)).path());
            QFile file(dir.filePath(name));
            if (file.open(QIODevice::WriteOnly))
                file.write(bytes);
        };
        auto stf = [](const QString &binding, int version = 1) {
            return QString(R"(SIMISA@@@@@@@@@@JINX0t1t______

Impostor (
	Version ( %1 )
	ColorTexture ( "tree-impostor-color.png" )
	NormalTexture ( "tree-impostor-normal.png" )
	BacklightTexture ( "tree-impostor-diffuse-transmission.png" )
	BacklightStrength ( 1 )
	AlphaCutoff ( 0.5 )
	LodBinding ( %2 )
	Pivot ( 0 0 0 )
	Bounds ( -1 0 -1 1 10 1 )
	Frames (
		Frame ( Direction ( 0 0 1 ) UVRect ( 0 0 0.5 1 ) Extent ( -1 1 0 10 ) )
		Frame ( Direction ( 1 0 0 ) UVRect ( 0.5 0 1 1 ) Extent ( -1 1 0 10 ) )
	)
)
)").arg(version).arg(binding).toUtf8();
        };
        write("imp/tree.impostor", stf("Append"));
        Loaded tree = load(dir, "imp/tree.gltf", gltf(lodNodes("[2, 3]")));
        test.check(tree.shape->distanceLevelCount() == 4 && tree.shape->distanceLevelCoverage(2) == 0.01f,
                   "impostor appended: a level after the last mesh level, from its coverage");
        auto impostorPacket = [&](Loaded &shape, int level, const float *direction) -> const RenderItem * {
            PacketRecorder recorder;
            shape.shape->setCurrentDistanceLevel(shape.state, level);
            shape.shape->setCameraDirection(shape.state, direction);
            shape.shape->pushRenderItem(recorder, 0, shape.state);
            return recorder.packets.size() == 1 && recorder.packets[0]->mesh.count == 6 ? recorder.packets[0] : nullptr;
        };
        const float front[3] = {0.0f, 0.0f, 1.0f}, side[3] = {0.8f, 0.0f, 0.6f};
        const RenderItem *facing = impostorPacket(tree, 3, front);
        const RenderItem *turned = impostorPacket(tree, 3, side);
        test.check(facing != nullptr && turned != nullptr && facing != turned,
                   "impostor: one quad, of the frame nearest to the camera's direction");
        test.check(facing != nullptr && facing->material.surface == RenderItem::SURFACE_ALPHA_TEST
                       && facing->pbr.textures[RenderItem::Pbr::MAP_NORMAL] >= 0
                       && facing->pbr.textures[RenderItem::Pbr::MAP_DIFFUSE_TRANSMISSION] >= 0
                       && facing->pbr.diffuseTransmission == 1.0f,
                   "impostor: cutout, normal map and backlight");
        test.check(drawn(tree, 2).counts == Counts{9, 12}, "impostor appended: the meshes stay at their levels");

        write("replace/tree.impostor", stf("ReplaceLast"));
        Loaded replaced = load(dir, "replace/tree.gltf", gltf(lodNodes("[2, 3]")));
        test.check(replaced.shape->distanceLevelCount() == 3 && impostorPacket(replaced, 2, front) != nullptr,
                   "impostor replacing the last level: drawn at it, without the meshes");

        write("single/tree.impostor", stf("Append"));
        Loaded single = load(dir, "single/tree.gltf", gltf(R"([{"mesh": 0}])", "[]"));
        test.check(single.shape->distanceLevelCount() == 2 && single.shape->distanceLevelCoverage(0) == 0.001f
                       && drawn(single, 0).counts == Counts{3},
                   "impostor of a shape without levels: after its mesh, from Open Rails' last default");

        write("version/tree.impostor", stf("Append", 2));
        test.check(load(dir, "version/tree.gltf", gltf(R"([{"mesh": 0}])", "[]")).shape->distanceLevelCount() == 0,
                   "an impostor of another version: none");

        // Trees cast a coarser level (task renderer 37): the level of three
        // times the distance, down to the last mesh level before the
        // impostor; the view's level then casts none.
        {
            auto pinLodBias = [](int value) { SettingsManager::instance().setSessionValue("core.rendering.lodBias", value); };
            const auto unpinLodBias = qScopeGuard([] { SettingsManager::instance().clearSessionValue("core.rendering.lodBias"); });
            pinLodBias(0);
            QScopedValueRollback<float> fov(Game::cameraFov, 55.0f);
            test.check(tree.shape->shadowFloorLevel() == 2 && replaced.shape->shadowFloorLevel() == 1
                           && single.shape->shadowFloorLevel() == 0,
                       "shadow floor: the last mesh level before the impostor (Append, ReplaceLast, no levels)");
            // 10 m high, coverages 0.3, 0.1 and 0.01: level 0 within 32 m,
            // 1 within 96 m, 2 within 960 m, then the impostor.
            auto shadowAt = [&](Loaded &shape, float distance) {
                const ShapeLod::Place place{distance, -5.0f};
                return ShapeLod::shadowLevel(*shape.shape, place, ShapeLod::level(*shape.shape, place));
            };
            test.check(shadowAt(tree, 20.0f) == 1 && shadowAt(tree, 50.0f) == 2,
                       "shadow level: the view's at three times the distance (0 casts 1, 1 casts 2)");
            test.check(shadowAt(tree, 5.0f) == -1 && shadowAt(tree, 200.0f) == -1 && shadowAt(tree, 5000.0f) == -1,
                       "shadow level: the view's own when the same, at the last mesh level and at the impostor");
            Loaded plain = load(dir, "noimpostor.gltf", gltf(lodNodes("[2, 3]")));
            test.check(plain.shape->isLoaded() && plain.shape->shadowFloorLevel() == -1
                           && shadowAt(plain, 20.0f) == -1,
                       "shadow level: shapes without an impostor cast their view's");

            struct Pushed {
                Counts view, shadow;
                bool viewCasts = false, vegetation = true, restored = false;
            };
            auto push = [&](Loaded &shape, int shadowLevel, quint32 selectionId = 0, bool casting = true) {
                PacketRecorder recorder;
                recorder.setShadowCasting(casting);
                shape.shape->setCurrentDistanceLevel(shape.state, 0);
                shape.shape->setShadowDistanceLevel(shape.state, shadowLevel);
                shape.shape->pushRenderItem(recorder, selectionId, shape.state);
                shape.shape->setShadowDistanceLevel(shape.state, -1);
                Pushed pushed;
                for (int i = 0; i < recorder.packets.size(); ++i) {
                    const RenderItem *packet = recorder.packets[i];
                    if (!packet->mesh.handle.valid())
                        continue;
                    pushed.vegetation = pushed.vegetation && packet->material.vegetation;
                    if (recorder.shadowOnly[i]) {
                        pushed.shadow.push_back(packet->mesh.count);
                    } else {
                        pushed.view.push_back(packet->mesh.count);
                        pushed.viewCasts = pushed.viewCasts || recorder.casting[i];
                    }
                }
                std::sort(pushed.view.begin(), pushed.view.end());
                std::sort(pushed.shadow.begin(), pushed.shadow.end());
                pushed.restored = recorder.shadowCastingEnabled() == casting && !recorder.shadowOnlyEnabled();
                return pushed;
            };
            const Pushed coarser = push(tree, 1);
            test.check(coarser.view == Counts{3, 12} && !coarser.viewCasts && coarser.shadow == Counts{6, 12}
                           && coarser.restored,
                       "a coarser shadow: the view's level without casting, level 1 shadow-only, the queue restored");
            const Pushed same = push(tree, -1);
            test.check(same.view == Counts{3, 12} && same.viewCasts && same.shadow.isEmpty(),
                       "the view's own shadow: one set, casting");
            const Pushed picking = push(tree, 1, 7);
            const Pushed uncast = push(tree, 1, 0, false);
            test.check(picking.shadow.isEmpty() && uncast.shadow.isEmpty() && !uncast.viewCasts && uncast.restored,
                       "no shadow packets when picking or when the object casts none");
            test.check(coarser.vegetation && !push(plain, -1).vegetation,
                       "packets of shapes with an impostor are vegetation, of others not");
        }
    }

    // The supplied spruce (TREES_EXT under the game root): external levels
    // loaded as on a worker, their textures registered when adopted.
    const QString spruce = Game::root + "/ROUTES/TREES_EXT/SHAPES/norway-spruce-v2-edge.gltf";
    if (QFileInfo::exists(spruce)) {
        GltfShape shape(spruce, "norway-spruce-v2-edge.gltf", QFileInfo(spruce).path() + "/../TEXTURES");
        std::unique_ptr<ComplexShape> copy(shape.detachedCopy());
        copy->loadDetached();
        shape.adopt(*copy);
        Loaded loaded;
        loaded.state = shape.newState();
        bool levels = shape.distanceLevelCount() == 4, textures = true;
        for (int level = 0; level < 4; ++level) {
            PacketRecorder recorder;
            shape.setCurrentDistanceLevel(loaded.state, level);
            shape.pushRenderItem(recorder, 0, loaded.state);
            levels = levels && recorder.packets.size() >= (level < 3 ? 2 : 1);
            for (const RenderItem *item : recorder.packets)
                for (int texture : item->pbr.textures)
                    textures = textures && texture < (1 << 30);
        }
        test.check(levels, "the spruce: three external levels and its impostor, each drawn");
        test.check(textures, "the spruce: every level's and the impostor's textures registered on adoption");
    } else if (verbose) {
        qInfo().noquote() << "[tests:gltf-lod] no TREES_EXT under" << Game::root;
    }

    // The trees' foliage: KHR_materials_diffuse_transmission reaches the
    // packets; without it, none.
    {
        QString nodes = R"([{"mesh": 0}, {"mesh": 1}])";
        QByteArray json = gltf(nodes, R"(["KHR_materials_diffuse_transmission"])");
        json.replace(R"({"primitives": [{"attributes": {"POSITION": 0}}]})",
                     R"({"primitives": [{"attributes": {"POSITION": 0}, "material": 0}]})");
        json.replace(R"({"primitives": [{"attributes": {"POSITION": 1}}]})",
                     R"({"primitives": [{"attributes": {"POSITION": 1}, "material": 1}]})");
        json.replace(R"( "meshes": [)", R"( "materials": [
  {"extensions": {"KHR_materials_diffuse_transmission": {"diffuseTransmissionFactor": 0.75,
   "diffuseTransmissionColorFactor": [0.5, 1, 0.25]}}},
  {}
 ],
 "meshes": [)");
        Loaded shape = load(dir, "foliage.gltf", json);
        PacketRecorder recorder;
        shape.shape->pushRenderItem(recorder, 0, shape.state);
        bool leaf = false, plain = false;
        for (const RenderItem *item : recorder.packets) {
            const RenderItem::Pbr &p = item->pbr;
            if (item->mesh.count == 3)
                leaf = p.diffuseTransmission == 0.75f && p.diffuseTransmissionColor[0] == 0.5f
                        && p.diffuseTransmissionColor[1] == 1.0f && p.diffuseTransmissionColor[2] == 0.25f
                        && p.textures[RenderItem::Pbr::MAP_DIFFUSE_TRANSMISSION] < 0;
            if (item->mesh.count == 6)
                plain = p.diffuseTransmission == 0.0f && p.diffuseTransmissionColor[0] == 1.0f;
        }
        test.check(leaf, "diffuse transmission: factor and colour reach the packet");
        test.check(plain, "no diffuse transmission: none");
    }

    // Levels by screen coverage: the bounds (level 0 and the common node) are
    // 10 m high; with a 55 degree view, coverage 0.3 is reached within 32 m,
    // 0.1 within 96 m.
    {
        // The LOD bias pinned, the profile's back after.
        auto pinLodBias = [](int value) { SettingsManager::instance().setSessionValue("core.rendering.lodBias", value); };
        const auto unpinLodBias = qScopeGuard([] { SettingsManager::instance().clearSessionValue("core.rendering.lodBias"); });
        pinLodBias(0);
        QScopedValueRollback<float> fov(Game::cameraFov, 55.0f);
        Loaded shape = load(dir, "select.gltf", gltf(lodNodes("[2, 3]")));
        // The bounds' middle at the camera's height.
        auto level = [&](float distance) { return ShapeLod::level(*shape.shape, {distance, -5.0f}); };
        test.check(std::abs(ShapeLod::coverage(*shape.shape, {100.0f, -5.0f}, 55.0f) - 10.0f / (200.0f * std::tan(27.5f * float(M_PI) / 180.0f))) < 1e-4f,
                   "coverage: the extent over the view's height at the distance");
        test.check(level(20.0f) == 0 && level(50.0f) == 1 && level(200.0f) == 2,
                   "levels by coverage: 0 within 32 m, 1 within 96 m, then 2");
        if (ShapeLod::mode() == ShapeLod::Mode::Levels)
            test.check(level(5000.0f) == 2, "the last level past them all");
        // Coverages divided by 1 + bias / 100: at 45 m, level 1 at 0, level 0 at 50.
        const int unbiased = level(45.0f);
        pinLodBias(50);
        test.check(unbiased == 1 && level(45.0f) == 0, "LOD bias 50: level 0 half as far again");
    }
    return test.finish();
}
