#include "GltfLodTestSuite.h"
#include "TokenTestSupport.h"
#include <tsre/Game.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/shape/ShapeLod.h>
#include <QFile>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Records the packets a shape submits.
struct PacketRecorder : RenderQueue {
    QVector<RenderItem *> packets;
    using RenderQueue::submit;
    void submit(RenderItem *item, quint32 = 0, SubmitOrder = SUBMIT_GROUPED) override {
        packets.push_back(item);
    }
    void submit(const QVector<RenderItem *> &items, quint32 = 0) override {
        packets += items;
    }
    void submitFrameItem(RenderItem *item) override {
        packets.push_back(item);
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
    }
    return test.finish();
}
