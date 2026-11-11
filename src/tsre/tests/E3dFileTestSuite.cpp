#include "E3dFileTestSuite.h"
#include "TokenTestSupport.h"
#include <tsre/Game.h>
#include <tsre/maszyna/Cp1250.h>
#include <tsre/maszyna/E3dFile.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/maszyna/MaszynaMaterial.h>
#include <tsre/maszyna/MaszynaPaths.h>
#include <tsre/maszyna/MaszynaText.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#define MINIZ_HEADER_FILE_ONLY
#include <miniz/miniz.h>
#include <conEditor/VehicleFilesDialog.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/trains/VehicleFiles.h>
#include <tsre/trains/CarAnimation.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/VehicleDefinition.h>
#include <tsre/shape/E3dShape.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>
#include <QThread>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <QScopedValueRollback>

namespace {

void putU32(QByteArray &out, quint32 value) {
    out.append(reinterpret_cast<const char *>(&value), sizeof value);
}

void putF32(QByteArray &out, float value) {
    out.append(reinterpret_cast<const char *>(&value), sizeof value);
}

QByteArray chunk(const char *id, const QByteArray &payload) {
    QByteArray out(id, 4);
    putU32(out, quint32(payload.size() + 8));
    return out + payload;
}

QByteArray file(const QList<QByteArray> &chunks) {
    QByteArray joined;
    for (const QByteArray &c : chunks)
        joined += c;
    return chunk("E3D0", joined);
}

struct Record {
    int next = 0, child = 0, type = 4, name = -1;
    quint32 flags = 0x10;
    int matrix = -1, vertexCount = 0, vertexOffset = 0, material = 0;
    int indexCount = 0, indexOffset = 0;
    float maxDistance = 1000.0f, minDistance = 0.0f;
    float diffuse[4] = {0.5f, 0.25f, 1.0f, 1.0f};
    float multiplier = 1.0f;
    float threshold = -1.0f;
};

QByteArray records(const QList<Record> &list, int variant = 0) {
    QByteArray out;
    for (const Record &r : list) {
        QByteArray one(256 + 64 * variant, '\0');
        const auto set = [&one](int at, const void *value) { std::memcpy(one.data() + at, value, 4); };
        set(0, &r.next);
        set(4, &r.child);
        set(8, &r.type);
        set(12, &r.name);
        set(20, &r.flags);
        set(24, &r.matrix);
        set(28, &r.vertexCount);
        set(32, &r.vertexOffset);
        set(36, &r.material);
        set(44, &r.threshold);
        for (int c = 0; c < 4; ++c)
            set(64 + c * 4, &r.diffuse[c]);
        const float maxSquared = r.maxDistance * r.maxDistance;
        const float minSquared = r.minDistance * r.minDistance;
        set(116, &maxSquared);
        set(120, &minSquared);
        set(156, &r.indexCount);
        set(160, &r.indexOffset);
        set(164, &r.multiplier);
        out += one;
    }
    return chunk(variant == 0 ? "SUB0" : "SUB1", out);
}

// A right triangle in the xy plane at z, three vertices; tangents only for VNT2.
QByteArray triangle(float z, bool tangents, float size = 1.0f) {
    QByteArray out;
    const float corners[3][4] = {{0, 0, 0, 0}, {size, 0, 1, 0}, {0, size, 0, 1}};
    for (const auto &c : corners) {
        for (float v : {c[0], c[1], z, 0.0f, 0.0f, 1.0f, c[2], c[3]})
            putF32(out, v);
        if (tangents)
            for (float v : {1.0f, 0.0f, 0.0f, -1.0f})
                putF32(out, v);
    }
    return out;
}

QByteArray indices16(std::initializer_list<int> values) {
    QByteArray out;
    for (int v : values) {
        const quint16 i = quint16(v);
        out.append(reinterpret_cast<const char *>(&i), sizeof i);
    }
    return out;
}

QByteArray matrix(float x, float y, float z, float scale = 1.0f) {
    QByteArray out;
    for (float v : {scale, 0.0f, 0.0f, 0.0f, 0.0f, scale, 0.0f, 0.0f, 0.0f, 0.0f, scale, 0.0f, x, y, z, 1.0f})
        putF32(out, v);
    return out;
}

QByteArray matrixDoubles(float x, float y, float z) {
    QByteArray out;
    for (double v : {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, double(x), double(y), double(z), 1.0})
        out.append(reinterpret_cast<const char *>(&v), sizeof v);
    return out;
}

QByteArray strings(const QList<QByteArray> &list) {
    QByteArray out;
    for (const QByteArray &s : list) {
        out += s;
        out += '\0';
    }
    return out;
}

bool near(float a, float b) {
    return std::abs(a - b) < 1e-4f;
}

std::shared_ptr<E3dModel> parsed(const QByteArray &bytes, QString *error = nullptr) {
    QString local;
    return E3dFile::parse(bytes, error != nullptr ? error : &local);
}

void testVertices(TokenTest::Suite &test) {
    Record part;
    part.vertexCount = 3;
    part.indexCount = 3;
    auto model = parsed(file({records({part}), chunk("VNT2", triangle(2.0f, true)), chunk("IDX2", indices16({2, 1, 0}))}));
    test.check(model != nullptr && model->vertexCount() == 3, "VNT2: three vertices");
    if (model == nullptr)
        return;
    const float *v = model->vertices.data() + 19;
    test.check(near(v[0], 1.0f) && near(v[2], 2.0f) && near(v[5], 1.0f), "VNT2: position and normal in the PBR layout");
    test.check(near(v[6], 1.0f) && near(v[7], 0.0f), "VNT2: texture coordinates");
    test.check(near(v[8], 1.0f), "PBR layout: alpha 1");
    test.check(near(v[9], 1.0f) && near(v[12], -1.0f), "VNT2: tangent and handedness kept");
    test.check(near(v[13], 0.0f) && near(v[14], 0.0f) && near(v[15], 1.0f) && near(v[18], 1.0f),
               "PBR layout: second coordinates 0, colour 1");
    test.check(model->indexBytes == 2 && model->indices == indices16({2, 1, 0}), "IDX2 kept as it is");
    test.check(model->parts[0].indexCount == 3 && model->parts[0].firstVertex == 0, "a part's range");

    Record second = part;
    second.vertexOffset = 3;
    second.indexOffset = 3;
    Record first = part;
    first.next = 1;
    auto two = parsed(file({records({first, second}), chunk("VNT2", triangle(0.0f, true) + triangle(1.0f, true)),
                            chunk("IDX2", indices16({0, 1, 2, 0, 1, 2}))}));
    test.check(two != nullptr && two->parts[1].firstVertex == 3 && two->parts[1].firstIndex == 3,
               "offsets: in vertices and in indices");

    QByteArray one;
    for (int i : {0, 1, 2})
        one += char(i);
    auto byte = parsed(file({records({part}), chunk("VNT2", triangle(0.0f, true)), chunk("IDX1", one)}));
    test.check(byte != nullptr && byte->indexBytes == 2 && byte->indices == indices16({0, 1, 2}) && byte->fileIndexBytes == 1,
               "IDX1 widened to 16 bits");

    QByteArray four;
    putU32(four, 2);
    putU32(four, 1);
    putU32(four, 0);
    auto wide = parsed(file({records({part}), chunk("VNT2", triangle(0.0f, true)), chunk("IDX4", four)}));
    test.check(wide != nullptr && wide->indexBytes == 4 && wide->indices == four, "IDX4 kept as it is");

    Record legacy;
    legacy.vertexCount = 3;
    auto plain = parsed(file({records({legacy}), chunk("VNT0", triangle(0.0f, false))}));
    test.check(plain != nullptr && plain->fileVertexVariant == 0 && plain->parts[0].indexCount == 0, "VNT0 without indices");
    Record stale = legacy;
    stale.indexCount = 99;
    auto staleCount = parsed(file({records({stale}), chunk("VNT0", triangle(0.0f, false))}));
    test.check(staleCount != nullptr && staleCount->parts[0].indexCount == 0, "no index chunk: a record's index count means nothing");
    if (plain != nullptr) {
        const float *t = plain->vertices.data() + 9;
        test.check(near(std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]), 1.0f), "VNT0: tangents computed");
        test.check(near(t[0], 1.0f), "VNT0: the tangent along u");
    }
}

void testTree(TokenTest::Suite &test) {
    // Root 0 with children 2 (before it in no order) and 1: record 2 is the
    // first child, its sibling 1; part order must put parents first.
    Record root;
    root.type = 256;
    root.child = 2;
    root.flags = 0x8000;
    root.matrix = 0;
    Record sibling;
    sibling.flags = 0x10;           // no 0xC000: the matrix it names does not count
    sibling.matrix = 1;
    sibling.vertexCount = 3;
    Record child;
    child.next = 1;
    child.flags = 0x8010;
    child.matrix = 1;
    child.vertexCount = 3;
    QByteArray matrices = matrix(10.0f, 0.0f, 0.0f) + matrix(0.0f, 5.0f, 0.0f, 2.0f);
    auto model = parsed(file({records({root, sibling, child}), chunk("VNT2", triangle(0.0f, true)), chunk("TRA0", matrices)}));
    test.check(model != nullptr && model->parts.size() == 3, "three parts");
    if (model == nullptr)
        return;
    bool parentsFirst = true;
    for (int i = 0; i < int(model->parts.size()); ++i)
        parentsFirst = parentsFirst && model->parts[size_t(i)].parent < i;
    test.check(parentsFirst, "parents come first");
    test.check(model->parts[0].kind == E3dModel::Kind::Pivot && model->parts[1].kind == E3dModel::Kind::Mesh, "kinds");
    // Order: root, its first child, then the child's sibling.
    const E3dModel::Part &first = model->parts[1];
    const E3dModel::Part &second = model->parts[2];
    test.check(first.parent == 0 && second.parent == 0 && model->parts[0].firstChild == 1 && first.nextSibling == 2,
               "links after the reorder");
    test.check(near(first.world[12], 10.0f) && near(first.world[13], 5.0f) && near(first.world[0], 2.0f), "world through the parent");
    test.check(near(second.rest[13], 0.0f) && near(second.world[12], 10.0f), "a matrix counts only with 0xC000");

    Record held;
    held.type = 256;
    held.flags = 0x8000;
    held.matrix = 0;
    auto doubles = parsed(file({records({held}), chunk("TRA1", matrixDoubles(3.0f, 4.0f, 5.0f))}));
    test.check(doubles != nullptr && near(doubles->parts[0].rest[12], 3.0f) && near(doubles->parts[0].rest[14], 5.0f), "TRA1: doubles");

    // Bounds, through the matrices: the triangle in part 1 (scaled 2, moved 10, 5).
    test.check(near(model->bound[0], 12.0f) && near(model->bound[1], 10.0f), "bounds along x through the matrices");
    test.check(near(model->bound[2], 7.0f) && near(model->bound[3], 0.0f), "bounds along y");
    test.check(first.boundRadius > 0.0f && near(first.boundCenter[0], 0.5f), "a part's sphere in its own space");

    Record longRoot;
    longRoot.type = 256;
    auto sub1 = parsed(file({records({longRoot}, 1)}));
    test.check(sub1 != nullptr && sub1->parts.size() == 1, "SUB1: records 64 bytes longer");

    auto unknown = parsed(file({chunk("XXXX", QByteArray(12, 'q')), records({longRoot})}));
    test.check(unknown != nullptr, "a chunk not known is passed over");

    // A record no link reaches.
    Record alone;
    alone.type = 256;
    auto orphan = parsed(file({records({longRoot, alone})}));
    test.check(orphan != nullptr && orphan->parts.size() == 1 && orphan->unreachableParts == 1 && orphan->fileKinds[1] == 2,
               "a record not linked is left out, and counted");
}

void testNames(TokenTest::Suite &test) {
    // cp1250 "Łódź" (0xA3 0xF3 0x64 0x9F), and backslashes in material names.
    QByteArray polish("\xA3\xF3\x64\x9F");
    test.check(Maszyna::fromCp1250(polish.constData(), polish.size()) == QString::fromUtf8("Łódź"), "cp1250");

    // Tree: 0 "a" with next 1 "dup" and child 2 "dup": the sibling is found
    // before the child. Case: "Light_On1", "lamp_on", "lamp_ON".
    Record root;
    root.type = 256;
    root.name = 0;
    root.next = 1;
    root.child = 2;
    Record sibling = root;
    sibling.name = 1;
    sibling.next = 0;
    sibling.child = 3;
    Record child = root;
    child.name = 2;
    child.next = 0;
    child.child = 0;
    Record lit = child;
    lit.name = 3;
    lit.next = 4;
    Record light = child;
    light.name = 4;
    light.next = 5;
    Record upper = child;
    upper.name = 5;
    QByteArray names = strings({"a", "Dup", "dup", "lamp_on", "Light_On1", "lamp_ON"});
    QByteArray materials = strings({"", "tabor\\cien:st"});
    auto model = parsed(file({records({root, sibling, child, lit, light, upper}), chunk("NAM0", names), chunk("TEX0", materials)}));
    test.check(model != nullptr, "names read");
    if (model == nullptr)
        return;
    test.check(model->materialNames.value(1) == "tabor/cien:st", "material names: slashes");
    const int found = model->byName.value("dup", -1);
    test.check(found >= 0 && model->names.value(model->parts[size_t(found)].name) == "Dup", "repeated name: the sibling first");
    test.check(model->byName.contains("lamp_on") && !model->byName.contains("LAMP_ON"), "the keys are lower case");
    const auto hidden = [&](const char *name) {
        const int i = model->byName.value(QString::fromLatin1(name).toLower(), -1);
        return i >= 0 && model->parts[size_t(i)].startsHidden;
    };
    test.check(hidden("lamp_on"), "\"_on\" starts hidden");
    test.check(hidden("Light_On1"), "\"Light_On\" starts hidden");
    test.check(!hidden("a"), "others start shown");
    bool upperShown = true;
    for (const E3dModel::Part &p : model->parts)
        if (p.name >= 0 && model->names[p.name] == "lamp_ON")
            upperShown = !p.startsHidden;
    test.check(upperShown, "\"_ON\" is another name (exact case)");
}

void testPartValues(TokenTest::Suite &test) {
    Record part;
    part.vertexCount = 3;
    part.material = 1;
    part.flags = 0x20;
    part.threshold = 2.0f;
    part.multiplier = 2.0f;
    Record slot = part;
    slot.material = -3;
    slot.flags = 0x01;
    Record both = part;
    both.flags = 0x30;
    both.material = 9;      // past the table: a colour part
    Record none = part;
    none.flags = 0;
    Record root = part;
    root.type = 256;
    root.vertexCount = 0;
    root.child = 1;
    part.next = 2;
    slot.next = 3;
    both.next = 4;
    auto model = parsed(file({records({root, part, slot, both, none}), chunk("VNT2", triangle(0.0f, true)),
                              chunk("TEX0", strings({"", "m"}))}));
    test.check(model != nullptr && model->parts.size() == 5, "parts read");
    if (model == nullptr)
        return;
    const auto find = [&](int material, unsigned char passes) {
        for (const E3dModel::Part &p : model->parts)
            if (p.kind == E3dModel::Kind::Mesh && p.material == material && p.passes == passes)
                return &p;
        return static_cast<const E3dModel::Part *>(nullptr);
    };
    test.check(find(1, E3dModel::Blended) != nullptr, "0x20: the blended pass");
    test.check(find(-3, E3dModel::BySlot) != nullptr, "0x01..0x08: by its slot, material -3 kept");
    test.check(find(0, E3dModel::Opaque | E3dModel::Blended) != nullptr, "0x30: both; a material past the table is a colour");
    test.check(find(1, 0) != nullptr, "no flags: none set");
    const E3dModel::Part *p = find(1, E3dModel::Blended);
    test.check(p != nullptr && near(p->lightThreshold, 2.0f), "light threshold");
    test.check(p != nullptr && near(p->diffuse[0], 0.25f) && near(p->diffuse[3], 1.0f), "the diffuse multiplier divides the colour");
}

void testLevels(TokenTest::Suite &test) {
    // Root: 0-100. Near: 0-20 (child of root). Far: 20-500, out to 100 by its
    // parent. Lone: 0-1000, its own sibling of the root.
    Record root;
    root.type = 256;
    root.maxDistance = 100.0f;
    root.child = 2;
    root.next = 1;
    Record lone;
    lone.vertexCount = 3;
    lone.maxDistance = 1000.0f;
    Record nearPart;
    nearPart.vertexCount = 3;
    nearPart.maxDistance = 20.0f;
    nearPart.next = 3;
    Record farPart;
    farPart.vertexCount = 3;
    farPart.minDistance = 20.0f;
    farPart.maxDistance = 500.0f;
    auto model = parsed(file({records({root, lone, nearPart, farPart}), chunk("VNT2", triangle(0.0f, true))}));
    test.check(model != nullptr, "levels: read");
    if (model == nullptr)
        return;
    test.check(model->levelLimits.size() == 3 && near(model->levelLimits[0], 20.0f) && near(model->levelLimits[1], 100.0f)
                       && near(model->levelLimits[2], 1000.0f),
               "the limits are where something changes (500 is cut by the parent)");
    const auto count = [&](size_t level) { return model->levelParts[level].size(); };
    test.check(model->levelParts.size() == 3 && count(0) == 2 && count(1) == 2 && count(2) == 1, "each level's parts");
}

void testSections(TokenTest::Suite &test) {
    // Under the root: cab1 holding driver, Cab2, przedzial01, przedzial02,
    // przedzial04 (after a gap), korytarz1 (a chain from 1), lamp.
    const QList<QByteArray> names = {"root", "cab1", "driver", "Cab2", "przedzial01", "przedzial02",
                                     "przedzial04", "korytarz1", "lamp"};
    QList<Record> list;
    for (int i = 0; i < names.size(); ++i) {
        Record r;
        r.type = 256;
        r.name = i;
        list << r;
    }
    list[0].child = 1;
    list[1].child = 2;
    list[1].next = 3;
    for (int i = 3; i < 8; ++i)
        list[i].next = i + 1;
    auto model = parsed(file({records(list), chunk("NAM0", strings(names))}));
    test.check(model != nullptr, "sections: read");
    if (model == nullptr)
        return;
    test.check(model->sections == QStringList({"cab1", "cab2", "korytarz1", "przedzial01", "przedzial02"}),
               "the sections: cabs, then each chain from 0 or 1 to its first gap: " + model->sections.join(' '));
    const auto sectionOf = [&](const char *name) {
        const int part = model->byName.value(QLatin1String(name), -1);
        return part >= 0 ? model->parts[size_t(part)].section : -2;
    };
    test.check(sectionOf("driver") == sectionOf("cab1") && sectionOf("cab1") == 0, "a section reaches its children");
    test.check(sectionOf("przedzial04") == -1 && sectionOf("lamp") == -1 && sectionOf("root") == -1, "the rest is in none");
}

void testRefused(TokenTest::Suite &test) {
    Record part;
    part.vertexCount = 3;
    const QByteArray recordChunk = records({part});
    const QByteArray vertices = chunk("VNT2", triangle(0.0f, true));
    const auto refused = [&](const QByteArray &bytes, const QString &label) {
        QString error;
        const auto model = E3dFile::parse(bytes, &error);
        test.check(model == nullptr && !error.isEmpty(), label);
    };
    refused(QByteArray("not a model at all"), "another format");
    refused(QByteArray(), "an empty file");
    refused(file({vertices}), "no submodels");

    QByteArray cut = file({recordChunk, vertices});
    refused(cut.left(cut.size() - 10), "the file cut short");
    QByteArray inner = file({recordChunk, vertices});
    std::memcpy(inner.data() + 8 + 4, "\xFF\xFF\xFF\x7F", 4);
    refused(inner, "a chunk's size past the end");
    QByteArray small = file({recordChunk});
    const quint32 three = 3;
    std::memcpy(small.data() + 8 + 4, &three, 4);
    refused(small, "a chunk's size below its header");
    refused(file({recordChunk, recordChunk, vertices}), "a repeated chunk");
    refused(file({recordChunk, chunk("VNT1", QByteArray(60, '\0'))}), "packed vertices are not read");
    refused(file({recordChunk, vertices, chunk("IDX3", QByteArray(9, '\0'))}), "an index size not known");

    Record tooMany = part;
    tooMany.vertexCount = 4;
    refused(file({records({tooMany}), vertices}), "vertices past the chunk");
    Record indexed = part;
    indexed.indexCount = 3;
    refused(file({records({indexed}), vertices, chunk("IDX2", indices16({0, 1}))}), "indices past the chunk");
    refused(file({records({indexed}), vertices, chunk("IDX2", indices16({0, 1, 3}))}), "an index past the part's vertices");

    Record cycleA;
    cycleA.type = 256;
    cycleA.child = 1;
    Record cycleB = cycleA;
    cycleB.child = 1;           // its own child
    refused(file({records({cycleA, cycleB})}), "a cycle");
    Record twice = cycleA;
    twice.child = 1;
    twice.next = 1;
    Record leaf;
    leaf.type = 256;
    refused(file({records({twice, leaf})}), "a part linked twice");
    Record past = cycleA;
    past.child = 7;
    refused(file({records({past})}), "a child past the table");

    Record withMatrix = part;
    withMatrix.flags = 0x8010;
    withMatrix.matrix = 2;
    refused(file({records({withMatrix}), vertices, chunk("TRA0", matrix(0, 0, 0))}), "a matrix past the table");
    Record named = part;
    named.name = 5;
    refused(file({records({named}), vertices, chunk("NAM0", strings({"a"}))}), "a name past the table");
}

void testCache(TokenTest::Suite &test) {
    QTemporaryDir dir;
    const QString path = dir.path() + "/m.e3d";
    Record root;
    root.type = 256;
    QFile out(path);
    out.open(QIODevice::WriteOnly);
    out.write(file({records({root})}));
    out.close();
    QString error;
    std::shared_ptr<const E3dModel> a = E3dFile::read(path, &error);
    std::shared_ptr<const E3dModel> b = E3dFile::read(path, &error);
    test.check(a != nullptr && a == b, "one model while anything holds it");
    std::shared_ptr<const E3dModel> c = E3dFile::read(path, &error, true);
    test.check(c != nullptr && c != a, "a fresh read goes past it");
    std::shared_ptr<const E3dModel> missing = E3dFile::read(dir.path() + "/none.e3d", &error);
    test.check(missing == nullptr && !error.isEmpty(), "a missing file: a message");
}

}

int TsreTests::runE3dFileSuite(bool verbose) {
    TokenTest::Suite test{"[tests:e3d-file]", verbose};
    testVertices(test);
    testTree(test);
    testNames(test);
    testPartValues(test);
    testLevels(test);
    testSections(test);
    testRefused(test);
    testCache(test);
    return test.finish();
}

namespace {

struct PacketRecorder : RenderQueue {
    QVector<RenderItem *> packets;
    using RenderQueue::submit;
    void submit(RenderItem *item, quint32 = 0, SubmitOrder = SUBMIT_GROUPED) override { packets.push_back(item); }
    void submit(const QVector<RenderItem *> &items, quint32 = 0) override { packets += items; }
    void submitFrameItem(RenderItem *item) override { delete item; }
};

QVector<RenderItem *> drawn(E3dShape &shape, unsigned int state) {
    PacketRecorder queue;
    shape.pushRenderItem(queue, 0, state);
    return queue.packets;
}

// Root pivot "root" at (10, 0, 0) holds "arm" at (0, 5, 0) (a mesh, slot -1,
// 0-100 m) holding "hand" (a mesh, material 1, 0-100 m); "far" beside it
// (material 1, opaque and blended, 100-1000 m), "lamp_on" under the root.
QByteArray shapeFile() {
    Record root;
    root.type = 256;
    root.name = 0;
    root.child = 1;
    root.flags = 0x8000;
    root.matrix = 0;
    Record arm;
    arm.name = 1;
    arm.next = 2;
    arm.child = 3;
    arm.flags = 0x8010;
    arm.matrix = 1;
    arm.vertexCount = 3;
    arm.indexCount = 3;
    arm.material = -1;
    arm.maxDistance = 100.0f;
    Record far = arm;
    far.name = 2;
    far.next = 4;
    far.child = 0;
    far.flags = 0x30;
    far.matrix = -1;
    far.material = 1;
    far.vertexOffset = 3;
    far.indexOffset = 3;
    far.minDistance = 100.0f;
    far.maxDistance = 1000.0f;
    Record hand = arm;
    hand.name = 3;
    hand.next = 0;
    hand.child = 0;
    hand.flags = 0x10;
    hand.matrix = -1;
    hand.material = 1;
    hand.vertexOffset = 3;
    hand.indexOffset = 3;
    Record lamp = hand;
    lamp.name = 4;
    return file({records({root, arm, far, hand, lamp}), chunk("VNT2", triangle(0.0f, true) + triangle(1.0f, true)),
                 chunk("IDX2", indices16({0, 1, 2, 0, 1, 2})),
                 chunk("TRA0", matrix(10.0f, 0.0f, 0.0f) + matrix(0.0f, 5.0f, 0.0f)),
                 chunk("TEX0", strings({"", "fixed"})), chunk("NAM0", strings({"root", "arm", "far", "hand", "lamp_on"}))});
}

void writeFile(const QString &path, const QByteArray &bytes) {
    QFile out(path);
    out.open(QIODevice::WriteOnly);
    out.write(bytes);
}

// A .mmd vehicle's model: MaSzyna's lookups (task shapes 18).
const ShapeLoadOptions VehicleLookups{{QString::fromLatin1(ShapeLoadOption::MaszynaLookups), true}};

}

// A 4 x 4 DDS of one block.
static QByteArray dds(const char *fourCc, const QByteArray &block) {
    QByteArray out("DDS ", 4);
    for (quint32 v : {124u, 0x1007u, 4u, 4u, quint32(block.size()), 0u, 1u})
        putU32(out, v);
    out += QByteArray(44, '\0');
    putU32(out, 32);
    putU32(out, 4);
    out.append(fourCc, 4);
    out += QByteArray(20, '\0');
    putU32(out, 0x1000);
    out += QByteArray(16, '\0');
    return out + block;
}

// Until every texture asked for has been read or found missing.
static void waitForTextures() {
    QElapsedTimer waited;
    waited.start();
    for (const auto &entry : TexLib::mtex)
        while (entry.second != nullptr && !entry.second->loaded && !entry.second->missing && !entry.second->error
               && waited.elapsed() < 3000)
            QThread::msleep(5);
}

int TsreTests::runE3dShapeSuite(bool verbose) {
    TokenTest::Suite test{"[tests:e3d-shape]", verbose};
    QTemporaryDir dir;
    QDir(dir.path()).mkpath("veh/model");
    const QString vehicle = dir.path() + "/veh";
    writeFile(vehicle + "/a.mmd", "models: endmodels");
    writeFile(vehicle + "/model/m.e3d", shapeFile());
    writeFile(vehicle + "/fixed.mat", "texture1: fixed_t\nopacity: 0.8\nparam_glossiness: 2\n");
    // Alpha 0: the parts it paints are cut by the .mat's opacity.
    writeFile(vehicle + "/fixed_t.dds", dds("DXT5", QByteArray(8, '\0') + QByteArray("\xFF\xFF\x00\x00\x00\x00\x00\x00", 8)));
    writeFile(vehicle + "/skin_a.mat", "texture1: skin_a_t\n");
    writeFile(vehicle + "/skin_b.mat", "texture1: skin_b_t\n");
    writeFile(vehicle + "/skin_a_t.dds", "");
    writeFile(vehicle + "/skin_b_t.dds", "");
    // A livery on MaSzyna's normalmap_specgloss shader (task maszyna 04).
    writeFile(vehicle + "/skin_g.mat", "shader: normalmap_specgloss\ntexture1: skin_a_t\ntexture_normalmap: skin_g_n\n"
                                       "texture_specgloss: skin_g_sg\nparam_glossiness: 150\n");
    writeFile(vehicle + "/skin_g_n.dds", "");
    writeFile(vehicle + "/skin_g_sg.dds", "");

    E3dShape shape(vehicle + "/model/m.e3d", "m.e3d", vehicle + "/model");
    shape.setLoadOptions(VehicleLookups);
    shape.load();
    test.check(shape.isLoaded(), "loaded");
    if (!shape.isLoaded())
        return test.finish();

    // Every texture settled first: packets made again once their alpha is
    // known would not compare equal below.
    {
        const unsigned a = shape.newState(), b = shape.newState();
        shape.setRepaint(a, "skin_a");
        shape.setRepaint(b, "skin_b");
        drawn(shape, a);
        drawn(shape, b);
        waitForTextures();
        shape.invalidateRenderState();
        shape.releaseState(a);
        shape.releaseState(b);
    }

    test.check(shape.distanceLevelCount() == 2, "two levels");
    const float radius = shape.getSize() * 0.5f;
    test.check(near(shape.distanceLevelDistance(0) + radius, 100.0f), "a level's distance gives the limit with ShapeLod's radius");
    test.check(shape.partCount() == 5 && shape.partName(0) == "root" && shape.partParent(0) == -1, "the parts");

    const int arm = shape.partCount() > 0 ? [&] {
        for (int i = 0; i < shape.partCount(); ++i)
            if (shape.partName(i) == "arm")
                return i;
        return -1;
    }() : -1;
    const auto partNamed = [&](const char *name) {
        for (int i = 0; i < shape.partCount(); ++i)
            if (shape.partName(i) == name)
                return i;
        return -1;
    };
    const int hand = partNamed("hand");

    const unsigned plain = shape.newState();
    const QVector<RenderItem *> level0 = drawn(shape, plain);
    test.check(level0.size() == 2, "level 0: the arm and the hand, lamp_on hidden");
    if (level0.size() == 2) {
        test.check(level0[0]->mesh.indexed && level0[0]->mesh.count == 3 && level0[0]->mesh.layout == RenderItem::PBR, "indexed PBR ranges");
        test.check(level0[1]->mesh.baseVertex == 3 && level0[1]->mesh.indexOffset == 6, "offsets in the one buffer");
        test.check(near(level0[0]->pbr.baseColor[0], 0.5f) && !level0[0]->material.textured, "a skin slot without a repaint: plain grey");
        test.check(near(level0[1]->msMatrix[12], 10.0f) && near(level0[1]->msMatrix[13], 5.0f), "the rest pose through the parents");
    }
    shape.setCurrentDistanceLevel(plain, 1);
    const QVector<RenderItem *> level1 = drawn(shape, plain);
    test.check(level1.size() == 2 && level1[0]->material.surface != RenderItem::SURFACE_BLENDED
                       && level1[1]->material.surface == RenderItem::SURFACE_BLENDED,
               "both passes: two packets");
    if (level1.size() == 2) {
        test.check(near(level1[0]->pbr.alphaCutoff, 0.8f), ".mat opacity is the cut");
        test.check(level1[0]->pbr.specular == 0.0f && level1[0]->pbr.textures[RenderItem::Pbr::MAP_SPECULAR] < 0,
                   "MaSzyna's default material (one texture): no reflection, as its mat_default");
    }
    // A specgloss livery (task maszyna 04): its map in the metallic-roughness
    // slot read by its own channels (roughness 1 - G, metal B, the specular
    // strength times R), no occlusion from it; its reflection masked by the
    // normal map's alpha.
    {
        const unsigned glossy = shape.newState();
        shape.setRepaint(glossy, "skin_g");
        const QVector<RenderItem *> items = drawn(shape, glossy);
        const RenderItem::Pbr *p = items.isEmpty() ? nullptr : &items[0]->pbr;
        test.check(p != nullptr && p->metallicRoughnessLayout == RenderItem::Pbr::MetallicRoughness::SpecGloss
                           && p->textures[RenderItem::Pbr::MAP_METALLIC_ROUGHNESS] >= 0
                           && p->textures[RenderItem::Pbr::MAP_OCCLUSION] < 0
                           && near(p->roughness, 1.0f) && near(p->metallic, 1.0f) && near(p->specular, 1.0f)
                           && p->textures[RenderItem::Pbr::MAP_SPECULAR] >= 0,
                   "a specgloss livery: its map read as MaSzyna's channels, factors 1");
        shape.releaseState(glossy);
    }

    const unsigned posed = shape.newState();
    float out[16];
    float turn[16];
    Mat4::identity(turn);
    turn[12] = 2.0f;
    shape.setPartTransform(posed, arm, turn);
    test.check(shape.partMatrix(posed, hand, out) && near(out[12], 12.0f), "a posed part moves its children");
    test.check(shape.partMatrix(plain, hand, out) && near(out[12], 10.0f), "other states are not disturbed");
    const QVector<RenderItem *> movedPackets = drawn(shape, posed);
    test.check(movedPackets.size() == 2 && near(movedPackets[1]->msMatrix[12], 12.0f) && movedPackets[1] != level0[1], "a moved part draws a packet of its own");
    turn[12] = 3.0f;
    shape.setPartTransform(posed, arm, turn);
    const QVector<RenderItem *> movedAgain = drawn(shape, posed);
    test.check(movedAgain.size() == 2 && movedAgain[1] == movedPackets[1] && near(movedAgain[1]->msMatrix[12], 13.0f), "the packet stays, its matrix follows");
    shape.setPartTransform(posed, arm, nullptr);
    test.check(drawn(shape, posed) == level0, "back at rest: the shared packets");

    const unsigned hider = shape.newState();
    shape.enableSubObjByName(hider, "ARM", false);
    test.check(drawn(shape, hider).isEmpty(), "a hidden part hides its children (any case)");
    shape.enableSubObjByName(hider, "arm", true);
    shape.enableSubObjByName(hider, "lamp_on", true);
    test.check(drawn(shape, hider).size() == 3, "a part that starts hidden can be shown");
    test.check(drawn(shape, plain).size() == 2, "only in its state");

    const unsigned a1 = shape.newState(), a2 = shape.newState(), b = shape.newState();
    shape.setRepaint(a1, "skin_a");
    shape.setRepaint(a2, "SKIN_A");
    shape.setRepaint(b, "skin_b");
    const QVector<RenderItem *> first = drawn(shape, a1), second = drawn(shape, a2), other = drawn(shape, b);
    test.check(first.size() == 2 && first == second, "states of one repaint share their packets");
    test.check(other.size() == 2 && other[0] != first[0], "another repaint: its own packets for the skin's part");
    test.check(other.size() == 2 && first.size() == 2 && other[1] == first[1], "parts without a skin are shared between repaints");
    test.check(first.size() == 2 && first[0]->material.textured && other.size() == 2 && first[0]->material.textureId != other[0]->material.textureId,
               "each repaint its texture");
    shape.setRepaint(a1, QString());
    test.check(drawn(shape, a1) == level0, "no repaint: the shape's own again");

    shape.releaseState(b);
    test.check(shape.newState() == b, "a released state is given again");
    test.check(drawn(shape, b) == level0, "without its repaint");

    shape.reload();
    test.check(!shape.isLoaded() && shape.partCount() == 0, "reload unloads");
    shape.load();
    test.check(shape.isLoaded() && drawn(shape, plain).size() == 2, "and loads again");

    // Which parts a livery shows: MaSzyna's rule, by the texture file's format.
    {
        const QString folder = dir.path() + "/alpha";
        QDir(dir.path()).mkpath("alpha");
        writeFile(folder + "/a.mmd", "models: endmodels");
        const QByteArray colour("\xFF\xFF\x00\x00\x00\x00\x00\x00", 8);
        writeFile(folder + "/solid.dds", dds("DXT5", QByteArray(2, '\xFF') + QByteArray(6, '\0') + colour));
        writeFile(folder + "/cut.dds", dds("DXT5", QByteArray(8, '\0') + colour));
        // Three-colour blocks, index 3 transparent.
        writeFile(folder + "/plain.dds", dds("DXT1", QByteArray("\x00\x00\xFF\xFF\xFF\xFF\xFF\xFF", 8)));
        writeFile(folder + "/fixedplain.dds", dds("DXT1", colour));
        writeFile(folder + "/fixedalpha.dds", dds("DXT5", QByteArray(2, '\xFF') + QByteArray(6, '\0') + colour));

        Record root;
        root.type = 256;
        root.child = 1;
        root.flags = 0;
        Record bySlot;
        bySlot.vertexCount = 3;
        bySlot.flags = 0x01;
        bySlot.material = -1;
        bySlot.next = 2;
        Record otherSlot = bySlot;      // flagged for slot 2, drawing slot 1
        otherSlot.flags = 0x02;
        otherSlot.next = 3;
        Record colourNoPass = bySlot;   // no pass, no material: not drawn
        colourNoPass.flags = 0;
        colourNoPass.material = 0;
        colourNoPass.next = 4;
        Record fixedPlain = colourNoPass;
        fixedPlain.material = 1;
        fixedPlain.next = 5;
        Record fixedAlpha = colourNoPass;
        fixedAlpha.material = 2;
        fixedAlpha.next = 0;
        writeFile(folder + "/m.e3d", file({records({root, bySlot, otherSlot, colourNoPass, fixedPlain, fixedAlpha}),
                                           chunk("VNT2", triangle(0.0f, true)), chunk("TEX0", strings({"", "fixedplain", "fixedalpha"}))}));

        E3dShape model(folder + "/m.e3d", "m.e3d", folder);
        model.setLoadOptions(VehicleLookups);
        model.load();
        const auto surfaces = [&](const QString &repaint) {
            const unsigned state = model.newState();
            model.setRepaint(state, repaint);
            drawn(model, state);    // its textures start loading
            waitForTextures();
            model.invalidateRenderState();
            QStringList out;
            for (RenderItem *item : drawn(model, state))
                out << QString::number(item->material.surface);
            model.releaseState(state);
            return out.join(' ');
        };
        const QString opaque = QString::number(RenderItem::SURFACE_OPAQUE);
        const QString tested = QString::number(RenderItem::SURFACE_ALPHA_TEST);
        const QString blended = QString::number(RenderItem::SURFACE_BLENDED);
        // The two parts with their own materials are the last two packets:
        // a DXT1 without opacity opaque, a DXT5 without opacity blended.
        const QString own = opaque + " " + blended;
        test.check(surfaces(QString()) == own, "no repaint: a slot's parts are not drawn, nor a part with no pass and no material: " + surfaces(QString()));
        test.check(surfaces("solid") == opaque + " " + own, "DXT5 with solid alpha: drawn, nothing to blend: " + surfaces("solid"));
        test.check(surfaces("cut") == tested + " " + blended + " " + own, "DXT5 with transparent pixels: cut and blended: " + surfaces("cut"));
        test.check(surfaces("plain") == own, "DXT1 has no alpha channel, even with transparent pixels: not drawn: " + surfaces("plain"));
        test.check(surfaces("plain|solid") == tested + " " + blended + " " + own,
                   "a part follows the slot its flag names: " + surfaces("plain|solid"));
    }

    // Lit sections: cab1 (always lit) holding driver (always lit), lamp
    // (always lit, in no section), przedzial01 (never lit), dusk (lit below
    // 0.35, in no section); colour parts, told apart by their first vertex.
    {
        const QString folder = dir.path() + "/sections";
        QDir(dir.path()).mkpath("sections");
        writeFile(folder + "/a.mmd", "models: endmodels");
        Record root;
        root.type = 256;
        root.name = 0;
        root.child = 1;
        root.flags = 0;
        Record cab;
        cab.name = 1;
        cab.vertexCount = 3;
        cab.threshold = 2.0f;
        cab.child = 2;
        cab.next = 3;
        Record driver = cab;
        driver.name = 2;
        driver.vertexOffset = 3;
        driver.child = 0;
        driver.next = 0;
        Record lamp = driver;
        lamp.name = 3;
        lamp.vertexOffset = 6;
        lamp.next = 4;
        Record compartment = driver;
        compartment.name = 4;
        compartment.vertexOffset = 9;
        compartment.threshold = -1.0f;
        compartment.next = 5;
        Record dusk = driver;
        dusk.name = 5;
        dusk.vertexOffset = 12;
        dusk.threshold = 0.35f;
        writeFile(folder + "/m.e3d", file({records({root, cab, driver, lamp, compartment, dusk}),
                                           chunk("VNT2", triangle(0.0f, true) + triangle(1.0f, true) + triangle(2.0f, true)
                                                         + triangle(3.0f, true) + triangle(4.0f, true)),
                                           chunk("NAM0", strings({"root", "cab1", "driver", "lamp", "przedzial01", "dusk"}))}));
        const auto packet = [](const QVector<RenderItem *> &list, unsigned first) {
            for (RenderItem *item : list)
                if (item->mesh.first == first)
                    return item;
            return static_cast<RenderItem *>(nullptr);
        };
        const auto glow = [&](const QVector<RenderItem *> &list, unsigned first) {
            const RenderItem *item = packet(list, first);
            return item != nullptr ? item->pbr.emissive[0] : -1.0f;
        };

        E3dShape model(folder + "/m.e3d", "m.e3d", folder);
        model.setLoadOptions(VehicleLookups);
        const unsigned early = model.newState();
        model.setClassLight(early, "cab1", 0.5f);
        model.load();
        const unsigned dark = model.newState(), lit = model.newState();
        const QVector<RenderItem *> unlit = drawn(model, dark);
        test.check(unlit.size() == 5 && glow(unlit, 0) == 0.0f && glow(unlit, 3) == 0.0f,
                   "a section starts unlit, and what is under it");
        // How each glow meets the exposure (task maszyna 04).
        const RenderItem *lampPacket = packet(unlit, 6), *duskPacket = packet(unlit, 12), *cabPacket = packet(unlit, 0);
        test.check(lampPacket != nullptr && near(lampPacket->pbr.emissive[0], 0.5f) && near(lampPacket->pbr.emissive[1], 0.25f)
                           && lampPacket->pbr.emissiveExposure == 0.0f,
                   "an always lit part in no section glows, its colour (no texture), as painted: a lamp read as on by day");
        test.check(duskPacket != nullptr && near(duskPacket->pbr.emissive[0], 0.5f) && duskPacket->pbr.emissiveExposure == 1.0f,
                   "a dusk part (threshold below 1) glows as a night texture, following the exposure");
        test.check(cabPacket != nullptr && cabPacket->pbr.emissiveExposure == 1.0f,
                   "a lit class's part follows the exposure, as a lit interior");
        model.setClassLight(lit, "CAB1", 0.6f);
        const QVector<RenderItem *> cabLit = drawn(model, lit);
        // Colour parts: their colour (red 0.5) times the light.
        test.check(near(glow(cabLit, 0), 0.3f) && near(glow(cabLit, 3), 0.3f),
                   "a lit section glows at its light (any case), its children too");
        test.check(packet(cabLit, 0) != packet(unlit, 0) && packet(cabLit, 6) == packet(unlit, 6),
                   "the lit parts draw copies of their own, the rest the shared packets");
        test.check(drawn(model, dark) == unlit, "other states stay unlit");
        test.check(near(glow(drawn(model, early), 0), 0.25f), "a light set before the load is kept");
        model.setClassLight(lit, "przedzial01", 1.0f);
        test.check(glow(drawn(model, lit), 9) == 0.0f, "a never lit part stays unlit in a lit section");

        int cabPart = -1;
        for (int i = 0; i < model.partCount(); ++i)
            if (model.partName(i) == "cab1")
                cabPart = i;
        float shift[16];
        Mat4::identity(shift);
        shift[12] = 4.0f;
        model.setPartTransform(lit, cabPart, shift);
        const QVector<RenderItem *> posedLit = drawn(model, lit);
        test.check(packet(posedLit, 0) != nullptr && near(packet(posedLit, 0)->msMatrix[12], 4.0f) && near(glow(posedLit, 0), 0.3f),
                   "posed and lit: one copy, moved and glowing");
        model.setPartTransform(lit, cabPart, nullptr);
        model.setClassLight(lit, "cab1", 0.0f);
        model.setClassLight(lit, "przedzial01", 0.0f);
        test.check(drawn(model, lit) == unlit, "light 0: the shared packets again");

        // The whole shape (the empty class: a MaSzyna cab model lit by its
        // cab light, task maszyna 04), but what glows of its own.
        const unsigned whole = model.newState();
        model.setClassLight(whole, QString(), 0.4f);
        const QVector<RenderItem *> wholeLit = drawn(model, whole);
        const RenderItem *wholeCab = packet(wholeLit, 0), *wholeCompartment = packet(wholeLit, 9),
                         *wholeLamp = packet(wholeLit, 6), *wholeDusk = packet(wholeLit, 12);
        test.check(wholeCab != nullptr && wholeCompartment != nullptr && near(wholeCompartment->pbr.emissive[0], 0.4f * 0.5f)
                           && near(wholeCompartment->pbr.emissive[1], 0.4f * 0.25f)
                           && wholeCompartment->pbr.emissiveExposure == 1.0f && near(wholeCab->pbr.emissive[0], 0.2f)
                           && wholeLamp == packet(unlit, 6) && wholeDusk == packet(unlit, 12),
                   "the whole shape lit: every part its colour times the light, following the exposure; lamps and "
                   "dusk parts keep their own glow");
        model.setClassLight(whole, QString(), 0.0f);
        test.check(drawn(model, whole) == unlit, "the whole light off: the shared packets again");
        model.releaseState(whole);

        model.setClassLight(lit, "cab1", 0.3f);
        model.reload();
        model.load();
        test.check(near(glow(drawn(model, lit), 0), 0.15f), "a reload keeps the lights");
    }

    // TSRE's rules (task shapes 18): a model that is not a .mmd vehicle's
    // takes its names as written from its texture path alone, as an S file.
    {
        const QString game = dir.path() + "/game";
        const QString route = game + "/ROUTES/r";
        for (const char *sub : {"/ROUTES/r/SHAPES", "/ROUTES/r/TEXTURES/sub", "/TEXTURES/MASZYNA"})
            QDir(game).mkpath(game + sub);
        QScopedValueRollback<QString> rootBack(Game::root, game);
        const QString textures = route + "/TEXTURES";
        writeFile(textures + "/sub/Tex.dds", "");
        writeFile(textures + "/sub/tex2.dds", "");
        writeFile(textures + "/Mat1.mat", "Texture1: sub/Inner\n");
        writeFile(textures + "/sub/Inner.dds", "");
        writeFile(textures + "/case.dds", "");
        writeFile(textures + "/Skin.dds", "");
        writeFile(route + "/SHAPES/beside.dds", "");
        writeFile(game + "/TEXTURES/MASZYNA/shared.dds", "");
        const QList<QByteArray> names = {"", "sub/Tex", "textures/sub/tex2", "Mat1", "beside", "shared", "Case", "dynamic/pkp/x/y"};
        QList<Record> parts;
        Record root;
        root.type = 256;
        root.child = 1;
        root.flags = 0;
        parts << root;
        for (int m = 1; m <= names.size(); ++m) {
            Record part;
            part.vertexCount = 3;
            part.material = m < names.size() ? m : -1;  // the last one takes the skin
            part.next = m < names.size() ? m + 1 : 0;
            parts << part;
        }
        const QString model = route + "/SHAPES/m.e3d";
        writeFile(model, file({records(parts), chunk("VNT2", triangle(0.0f, true)), chunk("TEX0", strings(names))}));
        MaszynaMaterial::forget();

        ShapeLib library;
        const int tsreId = library.addShape(model, textures);
        const int vehicleId = library.addShape(model, textures, VehicleLookups);
        test.check(tsreId != vehicleId, "one file in both modes: two shapes");
        auto *tsre = dynamic_cast<E3dShape *>(library.shape[tsreId]);
        auto *vehicleWay = dynamic_cast<E3dShape *>(library.shape[vehicleId]);
        if (tsre != nullptr && vehicleWay != nullptr) {
            tsre->load();
            vehicleWay->load();
            QStringList found, missingFiles;
            tsre->usedFiles(QString(), found, missingFiles);
            const auto has = [](const QStringList &list, const QString &path) { return list.contains(QDir::cleanPath(path)); };
            test.check(has(found, textures + "/sub/Tex.dds") && has(found, textures + "/sub/tex2.dds"),
                       "TSRE's rules: a name in the texture path, textures/ dropped: " + found.join(' '));
            test.check(has(found, textures + "/Mat1.mat") && has(found, textures + "/sub/Inner.dds"),
                       "a .mat in the texture path, its own textures there too, names as written");
            test.check(has(missingFiles, textures + "/beside.dds") && has(missingFiles, textures + "/shared.dds"),
                       "not beside the model, not in TEXTURES/MASZYNA: " + missingFiles.join(' '));
            test.check(has(missingFiles, textures + "/Case.dds") && !has(found, textures + "/case.dds"),
                       "names as written: no lower-casing (--contentcase repairs them)");
            test.check(has(missingFiles, textures + "/dynamic/pkp/x/y.dds"), "a vehicle's dynamic/ name is found nowhere");
            const unsigned skinned = tsre->newState();
            tsre->setRepaint(skinned, "Skin");
            found.clear();
            missingFiles.clear();
            tsre->usedFiles("Skin", found, missingFiles);
            test.check(has(found, textures + "/Skin.dds"), "a repaint in the texture path");
            found.clear();
            missingFiles.clear();
            tsre->usedFiles("skin", found, missingFiles);
            test.check(has(missingFiles, textures + "/skin.dds"), "a repaint as written");
            found.clear();
            missingFiles.clear();
            tsre->usedFiles("none", found, missingFiles);
            test.check(!has(found, textures + "/Skin.dds") && !has(missingFiles, textures + "/none.dds"), "MaSzyna's none: no repaint");
            tsre->setRepaint(skinned, "none");
            test.check(drawn(*tsre, skinned) == drawn(*tsre, 0), "a state with none draws as one without a repaint");

            found.clear();
            missingFiles.clear();
            vehicleWay->usedFiles(QString(), found, missingFiles);
            test.check(has(found, route + "/SHAPES/beside.dds") && has(found, game + "/TEXTURES/MASZYNA/shared.dds"),
                       "MaSzyna's lookups: beside the model, then TEXTURES/MASZYNA");
            const QVector<RenderItem *> a = drawn(*tsre, 0), b = drawn(*vehicleWay, 0);
            test.check(!a.isEmpty() && !b.isEmpty() && a[0]->mesh.handle == b[0]->mesh.handle, "and one model: the same mesh");
        }
    }

    // Its .sd and a placed model's own parts (task shapes 18): root (a
    // pivot at x 10) holding arm (at y 5), Light_On00, Light_Off00,
    // winter_variant and summer_variant, each its own triangle (told apart
    // by their first vertex: 0, 3, 6, 9, 12).
    {
        const QString folder = dir.path() + "/placed";
        QDir(folder).mkpath(folder + "/TEXTURES/WINTER");
        Record root;
        root.type = 256;
        root.name = 0;
        root.child = 1;
        root.flags = 0x8000;
        root.matrix = 0;
        QList<Record> list{root};
        for (int i = 1; i <= 5; ++i) {
            Record part;
            part.name = i;
            part.vertexCount = 3;
            part.vertexOffset = (i - 1) * 3;
            part.next = i < 5 ? i + 1 : 0;
            if (i == 1) {
                part.flags = 0x8010;
                part.matrix = 1;
            }
            list << part;
        }
        writeFile(folder + "/m.e3d", file({records(list),
                                          chunk("VNT2", triangle(0.0f, true) + triangle(1.0f, true) + triangle(2.0f, true)
                                                        + triangle(3.0f, true) + triangle(4.0f, true)),
                                          chunk("TRA0", matrix(10.0f, 0.0f, 0.0f) + matrix(0.0f, 5.0f, 0.0f)),
                                          chunk("NAM0", strings({"root", "arm", "Light_On00", "Light_Off00", "winter_variant",
                                                                 "summer_variant"}))}));
        writeFile(folder + "/m.sd",
                  "SIMISA@@@@@@@@@@JINX0t1t______\r\n\r\n"
                  "shape ( m.e3d\r\n"
                  "\tESD_Detail_Level ( 2 )\r\n"
                  "\tESD_Alternative_Texture ( 1 )\r\n"
                  "\tESD_Bounding_Box ( -1 0 -20 1 3 20 )\r\n"
                  "\tESD_Snapable ( )\r\n"
                  "\tESD_TSREDistanceLevelExtension ( None )\r\n"
                  "\tESD_TSRELightSchedule ( Always )\r\n"
                  "\tESD_TSRELight ( Name ( \"ON_ARM\" ) Type ( Point ) ShapeHierarchy ( \"arm\" ) Position ( 0 1 0 ) Intensity ( 10 ) )\r\n"
                  "\tESD_TSRELight ( Name ( \"AT_ORIGIN\" ) Type ( Point ) Position ( 0 2 0 ) Intensity ( 10 ) )\r\n"
                  "\tESD_TSREEmitter ( Name ( \"CHIMNEY\" ) Kind ( Exhaust ) Position ( 0 3 0 ) )\r\n"
                  "\tESD_TSREDisplay ( Document ( \"a.rml\" ) Image ( \"x.ace\" ) )\r\n"
                  ")\r\n");
        const auto firsts = [](const QVector<RenderItem *> &items) {
            QList<unsigned> out;
            for (const RenderItem *item : items)
                out << item->mesh.first;
            std::sort(out.begin(), out.end());
            return out;
        };
        const auto drawnLit = [](E3dShape &shape, unsigned state, bool lit) {
            PacketRecorder queue;
            queue.setLightsOn(lit);
            shape.pushRenderItem(queue, 0, state);
            QVector<RenderItem *> meshes;
            for (RenderItem *item : queue.packets)
                if (item->mesh.layout == RenderItem::PBR)
                    meshes << item;
            return meshes;
        };

        QScopedValueRollback<QString> seasonBack(Game::season, QString());
        E3dShape placed(folder + "/m.e3d", "m.e3d", folder + "/TEXTURES");
        placed.load();
        test.check(placed.isLoaded(), "a placed model with its .sd");
        test.check(placed.getEsdDetailLevel() == 2 && placed.isSnapable()
                           && placed.distanceLevelExtension() == ShapeLod::Extension::None
                           && placed.lightSchedule() == ShapeLights::Schedule::Always,
                   "the .sd's detail level, snapping, level extension and light schedule");
        test.check(placed.lights().size() == 2 && placed.emitters().size() == 1 && placed.displays().size() == 1,
                   QString("its lights, emitter and display: %1 %2 %3 (%4)").arg(placed.lights().size()).arg(placed.emitters().size())
                           .arg(placed.displays().size()).arg((placed.lightProblems() + placed.emitterProblems() + placed.displayProblems()).join("; ")));
        QVector<float> snap;
        placed.addSnapablePoints(snap);
        test.check(snap.size() == 6 && near(snap[2], -20.0f) && near(snap[5], 20.0f) && near(snap[0], 0.0f),
                   "snap points along the .sd's box, in the model's space");
        if (placed.lights().size() == 2) {
            const int armPart = ShapeLights::partOf(placed, placed.lights()[0]);
            test.check(armPart >= 0 && placed.partName(armPart) == "arm", "a light on a named part");
            test.check(ShapeLights::partOf(placed, placed.lights()[1]) == -1,
                       "one without ShapeHierarchy sits at the model's origin, not on its first record");
            const unsigned posed = placed.newState();
            float shift[16];
            Mat4::identity(shift);
            shift[12] = 3.0f;
            placed.setPartTransform(posed, armPart, shift);
            float out[16];
            test.check(placed.partMatrix(posed, armPart, out) && near(out[12], 13.0f), "and follows its part's pose");
            placed.releaseState(posed);
        }
        if (placed.emitters().size() == 1)
            test.check(ShapeEmitters::partOf(placed, placed.emitters()[0]) == -1, "an emitter without ShapeHierarchy too");

        const unsigned plainState = placed.newState();
        test.check(firsts(drawnLit(placed, plainState, false)) == QList<unsigned>({0, 6, 12}),
                   "by day: Light_Off00 shown, Light_On00 hidden, the summer variant alone (no season set)");
        test.check(firsts(drawnLit(placed, plainState, true)) == QList<unsigned>({0, 3, 12}),
                   "lights on: Light_On00 shown, Light_Off00 hidden");
        const unsigned other = placed.newState();
        test.check(drawnLit(placed, other, true) == drawnLit(placed, plainState, true)
                           && drawnLit(placed, other, false) == drawnLit(placed, plainState, false),
                   "lamps lit and unlit: two shared sets, objects still instance");

        // Another season: ESD_Alternative_Texture 1 puts textures in its
        // folder, the variant follows; the .sd read again on a reload.
        Game::season = "Winter";
        writeFile(folder + "/m.sd", "SIMISA@@@@@@@@@@JINX0t1t______\r\n\r\nshape ( m.e3d\r\n\tESD_Detail_Level ( 3 )\r\n)\r\n");
        placed.reload();
        placed.load();
        test.check(placed.getEsdDetailLevel() == 3 && placed.lights().isEmpty() && !placed.isSnapable(), "a reload reads the .sd again");
        test.check(firsts(drawnLit(placed, plainState, false)) == QList<unsigned>({0, 6, 9}), "in winter its winter variant");
        test.check(firsts(drawnLit(placed, plainState, true)) == QList<unsigned>({0, 3, 9}), "and its lamps");

        E3dShape vehiclePart(folder + "/m.e3d", "m.e3d", folder);
        vehiclePart.setLoadOptions(VehicleLookups);
        vehiclePart.load();
        test.check(firsts(drawnLit(vehiclePart, 0, true)) == QList<unsigned>({0, 6, 9, 12}),
                   "a vehicle's model (MaSzyna's lookups): Light_On hidden, no lamps or variants switched");
    }

    E3dShape missing(dir.path() + "/none.e3d", "none.e3d", dir.path());
    missing.load();
    test.check(missing.hasLoadFailed(), "a missing file fails without a crash");
    PacketRecorder queue;
    missing.pushRenderItem(queue, 0, 0);
    test.check(queue.packets.isEmpty(), "and draws nothing");
    return test.finish();
}

int TsreTests::runE3dCorpusSuite(bool verbose) {
    TokenTest::Suite test{"[tests:e3d-corpus]", verbose};
    const QString root = QDir::cleanPath(Game::root) + "/dynamic";
    if (!QFileInfo(root).isDir()) {
        qInfo() << "[tests:e3d-corpus] SKIP: no dynamic/ in the game root" << Game::root;
        return 0;
    }
    QStringList files;
    QDirIterator it(root, QStringList() << "*.e3d", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files << it.next();
    files.sort();

    struct Totals {
        int files = 0, kinds[4] = {0, 0, 0, 0}, vnt0 = 0, vnt2 = 0, idx1 = 0, idx2 = 0, idx4 = 0, unreachable = 0;
    } pkp;
    int failures = 0, mostLevels = 0, mostParts = 0;
    qint64 bytes = 0, vertices = 0;
    QElapsedTimer timer;
    timer.start();
    for (const QString &path : files) {
        QString error;
        const std::shared_ptr<const E3dModel> model = E3dFile::read(path, &error);
        if (model == nullptr) {
            if (++failures <= 20)
                qInfo().noquote() << "[tests:e3d-corpus] FAILED" << path << error;
            continue;
        }
        bytes += QFileInfo(path).size();
        vertices += model->vertexCount();
        mostLevels = std::max(mostLevels, int(model->levelLimits.size()));
        mostParts = std::max(mostParts, int(model->parts.size()));
        if (!path.startsWith(root + "/pkp/"))
            continue;
        ++pkp.files;
        for (int k = 0; k < 4; ++k)
            pkp.kinds[k] += model->fileKinds[k];
        pkp.vnt0 += model->fileVertexVariant == 0;
        pkp.vnt2 += model->fileVertexVariant == 2;
        pkp.idx1 += model->fileIndexBytes == 1;
        pkp.idx2 += model->fileIndexBytes == 2;
        pkp.idx4 += model->fileIndexBytes == 4;
        pkp.unreachable += model->unreachableParts;
    }
    const qint64 ms = timer.elapsed();
    test.check(failures == 0, QString("every file read (%1 failed of %2)").arg(failures).arg(files.size()));
    qInfo().noquote() << QString("[tests:e3d-corpus] %1 files, %2 MB, %3 vertices, %4 ms (%5 MB/s), at most %6 levels and %7 parts; "
                                 "pkp: %8 files, %9 meshes, %10 pivots, %11 spots, %12 other, VNT0 %13, VNT2 %14, IDX1 %15, IDX2 %16, IDX4 %17, not linked %18")
                         .arg(files.size()).arg(bytes / 1000000).arg(vertices).arg(ms)
                         .arg(ms > 0 ? bytes / 1000 / ms : 0).arg(mostLevels).arg(mostParts)
                         .arg(pkp.files).arg(pkp.kinds[0]).arg(pkp.kinds[1]).arg(pkp.kinds[2]).arg(pkp.kinds[3])
                         .arg(pkp.vnt0).arg(pkp.vnt2).arg(pkp.idx1).arg(pkp.idx2).arg(pkp.idx4).arg(pkp.unreachable);
    if (pkp.files > 0) {
        test.check(pkp.files == 5698, "pkp: the review's file count");
        test.check(pkp.kinds[0] == 243857 && pkp.kinds[1] == 11262 && pkp.kinds[2] == 5643 && pkp.kinds[3] == 0,
                   "pkp: the review's meshes, pivots and spot lights");
        test.check(pkp.vnt0 == 14 && pkp.vnt2 == 5684, "pkp: the review's vertex chunks");
        test.check(pkp.idx1 == 380 && pkp.idx2 == 4461 && pkp.idx4 == 843, "pkp: the review's index chunks");
    }
    return test.finish();
}

int TsreTests::runMaszynaVehicleSuite(bool verbose) {
    TokenTest::Suite test{"[tests:maszyna-vehicle]", verbose};
    QTemporaryDir dir;
    const QString root = dir.path();
    QDir(root).mkpath("TRAINS/TRAINSET/pkp/veh1");
    QDir(root).mkpath("TRAINS/TRAINSET/msts_a");
    QDir(root).mkpath("TRAINS/TRAINSET/group/msts_b");
    QDir(root).mkpath("TRAINS/CONSISTS");
    QDir(root).mkpath("TEXTURES/MASZYNA/tabor");
    QScopedValueRollback<QString> gameRoot(Game::root, root);
    const QString folder = root + "/TRAINS/TRAINSET/pkp/veh1";

    // MaszynaText.
    writeFile(folder + "/a.inc", "from_inc (p1) // a comment\n/* block\ncomment */ second(p2)x\n");
    writeFile(folder + "/t.mmd", "one;two // skip\n include a.inc v1 v2 end\n[ first other ] \"with space\" (p1) (p2) (p9) inCLUDE missing.inc end last");
    {
        MaszynaText text(folder, "t.mmd", {"P", "Q"});
        QStringList tokens;
        QByteArrayView token;
        while (text.next(token))
            tokens << MaszynaText::decode(token);
        test.check(tokens == QStringList({"one", "two", "from_inc", "v1", "secondv2x", "first", "with space", "P", "Q", "none", "last"}),
                   "tokens through includes, parameters, comments and sets: " + tokens.join('|'));
        test.check(text.problems().size() == 1 && text.problems()[0].endsWith("missing.inc"), "an include not found is told");
    }
    writeFile(folder + "/l.fiz", "  Param. M=1 // end\n\n# whole\ninclude b.inc 7 end\nlast line   \n");
    writeFile(folder + "/b.inc", "Dimensions: L=(p1)\n");
    {
        MaszynaText text(folder, "l.fiz");
        QStringList lines;
        QByteArrayView line;
        while (text.nextLine(line))
            lines << MaszynaText::decode(line);
        test.check(lines == QStringList({"Param. M=1", "# whole", "Dimensions: L=7", "last line"}), "lines: " + lines.join('|'));
    }
    test.check(MaszynaText::same("Models:", "models:") && !MaszynaText::same("models", "models:"), "any case, whole");

    // MaszynaPaths and MaszynaMaterial.
    const MaszynaPaths::Name name = MaszynaPaths::clean("\\Tabor\\Cien.DDS:st");
    test.check(name.text == "tabor/cien" && name.clampS && name.clampT, "a clean name, its traits apart");
    QStringList places = MaszynaPaths::places("textures/tabor/x", folder);
    test.check(places.size() == 2 && places[0] == folder + "/textures/tabor/x" && places[1] == root + "/TEXTURES/MASZYNA/tabor/x", "textures/ goes to TEXTURES/MASZYNA");
    places = MaszynaPaths::places("dynamic/pkp/other/x", folder);
    test.check(places.size() == 2 && places[1] == root + "/TRAINS/TRAINSET/pkp/other/x", "dynamic/ goes to the trainset");
    places = MaszynaPaths::places("tabor/x", folder);
    test.check(places.size() == 2 && places[1] == root + "/TEXTURES/MASZYNA/tabor/x", "a bare name: the shared folder after the vehicle's");
    // TSRE's rules (task shapes 18).
    const MaszynaPaths::Name asWritten = MaszynaPaths::clean("\\Tabor\\Cien.DDS:ST", MaszynaPaths::Lookup::Rules::Tsre);
    test.check(asWritten.text == "Tabor/Cien" && asWritten.clampS && asWritten.clampT,
               "TSRE's rules: a name as written, a known extension and traits in any case apart");
    const MaszynaPaths::Lookup tsreLookup = MaszynaPaths::Lookup::tsre(folder);
    test.check(MaszynaPaths::places("Textures/tabor/X", tsreLookup) == QStringList{folder + "/tabor/X"},
               "TSRE's rules: one place, textures/ dropped");
    test.check(MaszynaPaths::places("dynamic/pkp/x", tsreLookup).isEmpty(), "TSRE's rules: a dynamic/ name nowhere");
    test.check(tsreLookup.key() != MaszynaPaths::Lookup::maszyna(folder).key(), "the two rules cache apart");

    const MaszynaMaterial parsedMat = MaszynaMaterial::parse(
            "// c\ntexture1: [ a b ]\ntexture_normalmap: n:t\nopacity: 0.92\nselfillum: 2\nparam_glossiness: 150 1 2\n"
            "weird: { nested: { deeper: 1 } }\nwinter: { texture1: w\n opacity: 0.5 }\nrain: { texture1: r }\ntsre_metalness: 0.25\n", "winter:");
    test.check(parsedMat.baseName == "w", "the season's block over the plain value");
    test.check(parsedMat.normalName == "n:t", "texture_normalmap");
    test.check(near(parsedMat.opacity, 0.5f) && parsedMat.hasOpacity, "opacity by season");
    test.check(near(parsedMat.selfIllum, 2.0f), "selfillum");
    test.check(std::isnan(parsedMat.roughness) && parsedMat.family() == MaszynaMaterial::Family::Reflective
                       && parsedMat.maskName == "n:t" && near(parsedMat.reflectionStrength(), 1.0f)
                       && near(parsedMat.presetRoughness(), 0.15f),
               "two textures without a shader: MaSzyna's default_2, a reflection map from slot 1; glossiness no "
               "roughness (MaSzyna's own reflection a mirror's)");
    test.check(near(parsedMat.metalness, 0.25f), "TSRE's own key");
    const MaszynaMaterial summerMat = MaszynaMaterial::parse("texture1: [ a b ]\nwinter: { texture1: w }\n", "summer:");
    test.check(summerMat.baseName == "a", "another season's block is passed over; a set gives its first");
    const MaszynaMaterial second = MaszynaMaterial::parse("texture2: t\nshader: normalmap\n", "summer:");
    test.check(second.normalName == "t", "a normalmap shader takes texture2 as the normal map");
    const MaszynaMaterial plainSecond = MaszynaMaterial::parse("texture2: t\n", "summer:");
    test.check(plainSecond.normalName.isEmpty(), "texture2 without one is not a normal map");
    // MaSzyna's shader families and their PBR presets (task maszyna 04).
    using Family = MaszynaMaterial::Family;
    const auto familyOf = [](const char *text) { return MaszynaMaterial::parse(text, "summer:"); };
    const MaszynaMaterial oneTexture = familyOf("texture1: a\n"), colored = familyOf("shader: colored\n"),
                          normalMapped = familyOf("texture1: a\ntexture2: n\nshader: normalmap\n"),
                          sunless = familyOf("texture1: a\ntexture2: n\nshader: sunlessnormalmap\n"),
                          reflmap = familyOf("texture1: a\ntexture_reflmap: r\nshader: reflmap\n"),
                          lessReflection = familyOf("texture1: a\ntexture2: n\nshader: normalmap\nparam_reflection: 0.3\n"),
                          specgloss = familyOf("texture1: a\ntexture2: n\ntexture_specgloss: s\nshader: normalmap_specgloss\n");
    test.check(oneTexture.family() == Family::Diffuse && oneTexture.reflectionStrength() == 0.0f && oneTexture.maskName.isEmpty()
                       && colored.family() == Family::Colored && colored.reflectionStrength() == 0.0f
                       && sunless.family() == Family::MatteNormalMapped && sunless.reflectionStrength() == 0.0f,
               "no reflection: the default material, colour, sunless and shadowless normal maps");
    test.check(normalMapped.family() == Family::NormalMapped && normalMapped.maskName == "n"
                       && near(normalMapped.reflectionStrength(), 1.0f) && reflmap.family() == Family::Reflective
                       && reflmap.maskName == "r" && near(lessReflection.reflectionStrength(), 0.3f),
               "reflection masked by the normal map's or the reflmap's alpha, param_reflection its strength");
    test.check(specgloss.specGloss() && specgloss.family() == Family::NormalMapped && specgloss.specGlossName == "s",
               "a specgloss shader in its family, its map named");

    writeFile(root + "/TEXTURES/MASZYNA/tabor/shared.dds", "");
    writeFile(folder + "/own.dds", "");
    MaszynaMaterial::forget();
    auto shared = MaszynaMaterial::find("textures/tabor/shared:s", folder);
    test.check(shared->found && shared->base.endsWith("/TEXTURES/MASZYNA/tabor/shared.dds") && shared->clampS && !shared->clampT, "a texture found in the shared folder");
    test.check(MaszynaMaterial::find("own", folder)->base == folder + "/own.dds", "the vehicle's folder first");
    test.check(!MaszynaMaterial::find("nowhere", folder)->found, "a name found nowhere");
    test.check(MaszynaMaterial::find("own", folder) == MaszynaMaterial::find("own", folder), "each read once");

    // A vehicle.
    writeFile(folder + "/common.inc", "Dimensions: L=19.5 H=4 W=3\n");
    writeFile(folder + "/loco.fiz", "Param. Category=train M=120000 Vmax=125 PWR=3120 Type=et22\nParam. M=5\ninclude common.inc end\n"
                                    "Wheels: D=1.25\nBuffCoupl. CType=Screw\nEngine: EngineType=ElectricSeriesMotor\nDoors: DoorOpenMethod=Shift DoorMaxShiftL=0.65\n# Dimensions: L=1\n");
    writeFile(folder + "/loco.mmd", "models: \\Body/Loco.t3d#\nanimations: 12 0 0 4 2 2 -1\nlowpolyinterior: \\body/low.t3d\nattachments: { a1.t3d a2.t3d }\n"
                                    "loads: { x: y }\nanimwheelprefix: Wheel0\npantfactors: 1 2 3 4\ntsre_note: hello\nendmodels\nlocations:\n bogies: -5.25 5.25 end\nendlocations\n"
                                    "sounds:\n brake: { sound0: x.wav }\nendsounds\ninternaldata:\ncab1model: Cab1.t3d\n");
    writeFile(folder + "/wagon.fiz", "Param. Category=train M=40000 Vmax=120\nDimensions: L=26 H=4 W=2.8\nLoad: LoadAccepted=passengers\n");
    writeFile(folder + "/wagon.mmd", "models: wagon.t3d\nendmodels\n");
    writeFile(folder + "/truck.fiz", "Param. Category=road M=1\n");
    writeFile(folder + "/truck.mmd", "models: t.t3d\nendmodels\n");
    writeFile(folder + "/cab0.mmd", "only an include\n");
    writeFile(folder + "/textures.txt", "!=e,ET22,ET22-20xx,x\n*LOCO=?\n@LOCO=1\nSKIN-A.mat=LOCO,ET22,mini//v2,ET22-037,037,KR_Depot,1.1.2020,Comp,Auth,Ph\n"
                                        "skin-b.mat=loco,ET22\n!=A,x\ns1.mat=wagon,Coach//v2,Coach 1\nt1.mat=truck,T\nother.mat=gone,X\n");
    for (const char *mat : {"/skin-a.mat", "/skin-b.mat", "/s1.mat", "/t1.mat", "/unlisted.mat", "/skin-c,1.mat", "/skin-c,2.mat"})
        writeFile(folder + mat, "texture1: x\n");

    const auto vehicle = MaszynaVehicle::read(folder, "loco");
    test.check(vehicle != nullptr, "a type is read");
    if (vehicle != nullptr) {
        test.check(vehicle->category == "train" && near(float(vehicle->massKg), 120000.0f), "the first value of a key counts");
        test.check(near(vehicle->length, 19.5f) && near(vehicle->height, 4.0f) && near(vehicle->width, 3.0f), "dimensions through an include");
        test.check(near(vehicle->wheelDiameter, 1.25f) && vehicle->couplerType == "Screw" && vehicle->engineType == "ElectricSeriesMotor", "wheels, couplers, engine");
        test.check(vehicle->doorOpenMethod == "Shift" && near(vehicle->doorShiftLeft, 0.65f), "doors");
        test.check(vehicle->model == "body/loco.t3d", "the model, lower case, without its #");
        test.check(vehicle->animations[0] == 12 && vehicle->animations[5] == 2 && vehicle->animations[6] == -1, "animation counts up to -1");
        test.check(vehicle->lowpolyInterior == "body/low.t3d" && vehicle->attachments == QStringList({"a1.t3d", "a2.t3d"}), "extra models");
        test.check(vehicle->prefixes.value("animwheelprefix") == "Wheel0", "prefixes");
        test.check(vehicle->hasPantFactors && near(vehicle->pantFactors[2], 3.0f), "pantograph factors");
        test.check(vehicle->bogieOffsets.size() == 2 && near(vehicle->bogieOffsets[1], 5.25f), "bogie offsets");
        test.check(vehicle->cabModels == QStringList({"cab1.t3d"}), "cab model names");
        test.check(vehicle->tsre.value("tsre_note") == "hello", "TSRE's own keys");
    }
    test.check(MaszynaVehicle::read(folder, "loco") == vehicle, "read once per type");
    test.check(MaszynaVehicle::read(folder, "cab0") == nullptr, "an .mmd without a .fiz is no vehicle");

    const std::shared_ptr<const MaszynaCatalogue> heldCatalogue = MaszynaCatalogue::read(folder);
    const MaszynaCatalogue &catalogue = *heldCatalogue;
    test.check(catalogue.liveries.size() == 5 && catalogue.liveries[0].skin == "skin-a" && catalogue.liveries[0].type == "loco", "catalogue lines, lower case");
    test.check(catalogue.liveries[0].name == "ET22-037" && catalogue.liveries[0].keywords.contains("KR Depot"), "its metadata");
    test.check(catalogue.kinds.value("loco") == QLatin1Char('e') && catalogue.kinds.value("wagon") == QLatin1Char('A'), "each type's kind");

    int unlisted = -1;
    const QStringList rows = MaszynaVehicles::rows(folder, &unlisted);
    test.check(rows.contains("loco#skin-a.mmd") && rows.contains("loco#skin-b.mmd") && rows.contains("wagon#s1.mmd"), "a row per livery: " + rows.join(' '));
    test.check(!rows.contains("truck#t1.mmd"), "only railway vehicles");
    test.check(rows.size() == 3, "nothing else");
    QFile::remove(folder + "/s1.mat");
    test.check(!MaszynaVehicles::rows(folder).contains("wagon#s1.mmd") && MaszynaVehicles::rows(folder).contains("wagon#.mmd"),
               "a livery not copied has no row, its type a plain one");
    writeFile(folder + "/s1.mat", "texture1: x\n");
    // own, unlisted, skin-c (its slots counted once); the vehicles' other .mat files count too.
    test.check(unlisted == 2, QString("liveries not in the catalogue (%1)").arg(unlisted));
    test.check(MaszynaVehicles::firstRow(folder, "loco") == "loco#skin-a.mmd", "the first row of a type");

    writeFile(folder + "/named.fiz", "Param. Category=train M=1\n");
    writeFile(folder + "/named.mmd", "models: base/(p1).t3d\nendmodels\n");
    test.check(VehicleDefinition::read(folder, "named#a1.mmd", root, false)->shapePath == folder + "/base/a1.e3d", "a model named by the placed vehicle's name: its livery");

    const auto def = VehicleDefinition::read(folder, "loco#skin-a.mmd", root, false);
    test.check(def->ok() && def->document == nullptr && def->maszyna == vehicle, "a definition without a document");
    test.check(def->engName == "loco#skin-a" && def->skin == "skin-a", "the consist's name and its livery");
    test.check(def->displayName == "ET22-037 (pkp/veh1)", "its name with the folder: " + def->displayName);
    test.check(def->searchKeywords.contains("KR Depot"), "search keywords");
    test.check(def->wagonTypeId == 4 && def->typeHash == "Engine-Electric", "an electric engine");
    test.check(def->shapePath == folder + "/body/loco.e3d", "its model with .e3d: " + def->shapePath);
    test.check(def->freightAnims.size() == 3 && def->freightAnims[0].path == folder + "/body/low.e3d", "the extra shapes");
    test.check(near(def->size[0], 3.0f) && near(def->size[2], 19.5f) && near(def->length(), 19.5f), "its size and length");
    test.check(near(float(def->massKg), 120000.0f) && near(def->wheelRadius, 0.625f) && near(float(def->maxSpeedMps), 125.0f / 3.6f) && near(float(def->powerW), 3120000.0f), "mass, wheels, speed, power");
    test.check(def->resistanceFrom == VehicleDefinition::Resistance::Default && def->resistance[0] > 0.0, "the default resistance from the mass");
    test.check(def->number("wagon(mass", 0, 7.0) == 7.0 && def->text("wagon(type").isEmpty(), "document queries give their defaults");
    test.check(def->files().contains(folder + "/loco.mmd") && def->files().contains(folder + "/loco.fiz")
                       && def->files().contains(folder + "/common.inc") && def->files().contains(folder + "/textures.txt"),
               "its files: .fiz, .mmd, includes, the catalogue");
    test.check(VehicleDefinition::read(folder, "wagon#s1.mmd", root, false)->wagonTypeId == 1, "passengers: a carriage");
    test.check(!VehicleDefinition::read(folder, "nothing#x.mmd", root, false)->ok(), "a type not there");

    // The rig on a model in MaSzyna's frames: a root turning x' = -x, y' = z,
    // z' = y, below it its parts' z up.
    {
        const QString rigFolder = root + "/TRAINS/TRAINSET/pkp/rig";
        QDir(rigFolder).mkpath(".");
        writeFile(rigFolder + "/r.fiz", "Param. Category=train M=1\nWheels: D=1\nDoors: DoorOpenMethod=Shift DoorMaxShiftL=0.5\n");
        writeFile(rigFolder + "/r.mmd", "models: r.t3d\nanimations: 1 1 0 0 2 1 -1\nanimwheelprefix: wheel0\nanimdoorprefix: door\n"
                                        "animpantrd1prefix: low\nanimpantrd2prefix: low2_\nanimpantrg1prefix: up\nanimpantrg2prefix: up2_\nanimpantslprefix: sl\nendmodels\n");
        Record rootRecord;
        rootRecord.type = 256; rootRecord.name = 0; rootRecord.child = 1; rootRecord.flags = 0x8000; rootRecord.matrix = 0;
        Record bogie1; bogie1.name = 1; bogie1.next = 2; bogie1.child = 3; bogie1.flags = 0x8010; bogie1.matrix = 1; bogie1.vertexCount = 3;
        Record bogie2 = bogie1; bogie2.name = 2; bogie2.next = 4; bogie2.child = 0; bogie2.matrix = 2;
        Record wheel; wheel.name = 3; wheel.flags = 0x8010; wheel.matrix = 3; wheel.vertexCount = 3;
        Record low; low.type = 256; low.name = 4; low.next = 5; low.child = 6; low.flags = 0x8000; low.matrix = 4;
        Record door = wheel; door.name = 5; door.matrix = 3;
        Record up; up.type = 256; up.name = 6; up.child = 7; up.flags = 0x8000; up.matrix = 5;
        Record slip; slip.type = 256; slip.name = 7; slip.flags = 0x8000; slip.matrix = 6;
        QByteArray turned;
        for (float v : {-1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f})
            putF32(turned, v);
        writeFile(rigFolder + "/r.e3d", file({records({rootRecord, bogie1, bogie2, wheel, low, door, up, slip}), chunk("VNT2", triangle(0.0f, true)),
                                              chunk("TRA0", turned + matrix(0.0f, 5.0f, 0.8f) + matrix(0.0f, -5.0f, 0.8f) + matrix(0.0f, 1.0f, -0.2f)
                                                    + matrix(0.0f, 3.0f, 4.0f) + matrix(0.0f, -1.9f, 0.2f) + matrix(0.0f, 1.7f, 0.1f)),
                                              chunk("NAM0", strings({"banan", "bogie1", "bogie2", "wheel01", "low1", "door1", "up1", "sl1"}))}));
        const auto rigVehicle = MaszynaVehicle::read(rigFolder, "r");
        E3dShape rigShape(rigFolder + "/r.e3d", "r.e3d", rigFolder);
        rigShape.setLoadOptions(VehicleLookups);
        rigShape.load();
        test.check(rigShape.isLoaded() && rigVehicle != nullptr, "the rig's model");
        if (rigShape.isLoaded() && rigVehicle != nullptr) {
            CarAnimation rig(rigShape, *rigVehicle);
            test.check(rig.wheelCount() == 1, "wheels by their prefix");
            test.check(rig.bogies().size() == 2 && near(rig.bogies()[0].z, 5.0f) && near(rig.bogies()[1].z, -5.0f), "bogies and their pivots along the shape's z");
            test.check(rig.carriers().size() == 2 && near(rig.carriers()[0], -5.0f), "they carry it");
            test.check(rig.has(CarAnimation::Pantograph1) && !rig.has(CarAnimation::Pantograph2), "the pantograph found");
            test.check(rig.has(CarAnimation::DoorLeft) && !rig.has(CarAnimation::DoorRight), "an odd door is on the left");

            const auto partNamed = [&](const char *name) {
                for (int i = 0; i < rigShape.partCount(); ++i)
                    if (rigShape.partName(i) == name)
                        return i;
                return -1;
            };
            const unsigned state = rigShape.newState();
            rig.update(0.1f, 0.016f);
            rig.pose(rigShape, state, nullptr);
            float m[16];
            test.check(rigShape.partMatrix(state, partNamed("wheel01"), m), "the wheel posed");
            // The wheel's point on top: its local z (world y at rest), radius 0.5, travelled 0.1.
            const float theta = 0.1f / 0.5f;
            test.check(near(m[9], std::cos(theta)) && near(m[10], std::sin(theta)), "the top of a wheel rolls toward the shape's front");

            // A bogie turned 0.1 rad in MSTS's frame: the same turn in the drawing space.
            std::vector<float> turns(32, 0.0f);
            float *first = turns.data();
            first[0] = std::cos(0.1f); first[2] = -std::sin(0.1f);
            first[5] = 1.0f;
            first[8] = std::sin(0.1f); first[10] = std::cos(0.1f); first[15] = 1.0f;
            turns[16 + 15] = std::nanf("");
            rig.pose(rigShape, state, &turns);
            test.check(rigShape.partMatrix(state, partNamed("bogie1"), m), "the bogie posed");
            // Forward in drawing space: the bogie's local y; MSTS's +x is the drawing's -x.
            test.check(near(m[4], -std::sin(0.1f)) && near(m[6], std::cos(0.1f)), "a turned bogie keeps its turn in the drawing space");
            test.check(near(m[12], 0.0f) && near(m[14], 5.0f) && near(m[13], 0.8f), "and its place");
            test.check(near(m[9], 1.0f), "its up stays up");

            rig.set(CarAnimation::DoorLeft, true, true);
            rig.set(CarAnimation::Pantograph1, true, true);
            rig.pose(rigShape, state, nullptr);
            float doorRest[16];
            rigShape.partMatrix(0, partNamed("door1"), doorRest);
            rigShape.partMatrix(state, partNamed("door1"), m);
            test.check(near(m[13] - doorRest[13], 0.5f), "a sliding door shifts along its own z");
            float slipRest[16];
            rigShape.partMatrix(0, partNamed("sl1"), slipRest);
            rigShape.partMatrix(state, partNamed("sl1"), m);
            test.check(m[13] > slipRest[13] + 0.5f, QString("a raised pantograph lifts its head (%1 from %2)").arg(m[13]).arg(slipRest[13]));
            rig.set(CarAnimation::Pantograph1, false, true);
            rig.pose(rigShape, state, nullptr);
            rigShape.partMatrix(state, partNamed("sl1"), m);
            test.check(near(m[13], slipRest[13]), "and lowers it again");
        }
    }

    // Trainset subfolders.
    writeFile(root + "/TRAINS/TRAINSET/msts_a/a.eng", "");
    writeFile(root + "/TRAINS/TRAINSET/group/msts_b/b.wag", "");
    QDir(root).mkpath("TRAINS/TRAINSET/group/msts_b/deep/c");
    writeFile(root + "/TRAINS/TRAINSET/group/msts_b/deep/c/c.eng", "");
    QDir(root).mkpath("TRAINS/TRAINSET/empty/nothing/deep");
    writeFile(root + "/TRAINS/TRAINSET/empty/nothing/deep/d.eng", "");
    test.check(EngLib::consistFolder(root + "/TRAINS/TRAINSET/pkp/veh1") == "pkp/veh1", "a consist's folder is the path from TRAINSET");
    test.check(EngLib::consistFolder(root + "/TRAINS/TRAINSET/msts_a/") == "msts_a", "one level as before");
    test.check(EngLib::consistFolder("/elsewhere/veh") == "veh", "a folder elsewhere: its last");
    {
        EngLib lib;
        lib.loadAll(root);
        QStringList found;
        for (const auto &entry : lib.eng)
            if (entry.second != nullptr)
                found << EngLib::consistFolder(entry.second->path) + "/" + entry.second->name;
        found.sort();
        // Files without content do not load; rows do. What was tried is in the library.
        test.check(found.contains("pkp/veh1/loco#skin-a.mmd"), "MaSzyna rows from a group folder: " + found.join(' '));
        test.check(!found.join(' ').contains("deep/"), "no deeper than one group");
    }
    // A consist of such rows: read, saved and read back with its nested folder.
    {
        EngLib lib;
        QScopedValueRollback<EngLib *> current(Game::currentEngLib, &lib);
        const QString consists = root + "/TRAINS/CONSISTS";
        const QString consistText = QStringLiteral("SIMISA@@@@@@@@@@JINX0D0t______\n\nTrain (\n\tTrainCfg ( \"m\"\n\t\tName ( \"m\" )\n\t\tSerial ( 1 )\n\t\tMaxVelocity ( 40 1 )\n"
                                       "\t\tNextWagonUID ( 3 )\n\t\tDurability ( 1 )\n\t\tEngine (\n\t\t\tUiD ( 0 )\n\t\t\tEngineData ( \"loco#skin-a\" \"pkp/veh1\" )\n\t\t)\n"
                                       "\t\tWagon (\n\t\t\tWagonData ( \"wagon#s1\" \"pkp/veh1\" )\n\t\t\tUiD ( 1 )\n\t\t)\n\t)\n)\n");
        QByteArray utf16("\xFF\xFE", 2);
        utf16.append(reinterpret_cast<const char *>(consistText.utf16()), consistText.size() * 2);
        writeFile(consists + "/m.con", utf16);
        Consist con(consists, "m.con");
        test.check(con.loaded == 1, "the consist reads");
        QString cars;
        for (const auto &item : con.engItems)
            cars += QString(" [%1 %2 id %3 loaded %4]").arg(item.ename, item.epath).arg(item.eng).arg(item.eng >= 0 && lib.eng[item.eng] != nullptr ? lib.eng[item.eng]->loaded : -9);
        test.check(con.engItems.size() == 2 && !con.isBroken(), "its MaSzyna cars are found:" + cars);
        if (con.engItems.size() == 2) {
            test.check(lib.eng[con.engItems[0].eng]->definition->displayName == "ET22-037 (pkp/veh1)", "its first car is the row named");
            test.check(lib.eng[con.engItems[1].eng]->definition->wagonTypeId == 1, "a Wagon line reads the .mmd too");
            test.check(near(con.conLength, 19.5f + 26.0f), QString("its length (%1)").arg(con.conLength));
            con.name = "m2.con";
            con.save();
            Consist back(consists, "m2.con");
            test.check(back.loaded == 1 && back.engItems.size() == 2 && back.engItems[0].epath == "pkp/veh1" && back.engItems[0].ename == "loco#skin-a", "saved and read back");
            test.check(!back.isBroken(), "still whole");
        }
    }
    return test.finish();
}

int TsreTests::runMaszynaVehicleCorpusSuite(bool verbose) {
    TokenTest::Suite test{"[tests:maszyna-vehicle-corpus]", verbose};
    const QString root = QDir::cleanPath(Game::root) + "/dynamic";
    if (!QFileInfo(root).isDir()) {
        qInfo() << "[tests:maszyna-vehicle-corpus] SKIP: no dynamic/ in the game root" << Game::root;
        return 0;
    }
    QStringList folders;
    for (const QString &group : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        for (const QString &sub : QDir(root + "/" + group).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (MaszynaVehicles::holdsVehicle(root + "/" + group + "/" + sub))
                folders << root + "/" + group + "/" + sub;
    QElapsedTimer timer;
    timer.start();
    int pairs = 0, trains = 0, noModel = 0, rowsTotal = 0, unlistedTotal = 0, unmatched = 0, problems = 0, noMass = 0, noLength = 0;
    QStringList report;
    for (const QString &folder : folders) {
        const QDir dir(folder);
        for (const QString &mmd : dir.entryList(QStringList() << "*.mmd", QDir::Files)) {
            const QString type = mmd.chopped(4);
            if (!dir.exists(type + ".fiz"))
                continue;
            const auto vehicle = MaszynaVehicle::read(folder, type);
            if (vehicle == nullptr)
                continue;
            ++pairs;
            problems += vehicle->problems.size();
            for (const QString &p : vehicle->problems)
                report << "include: " + p;
            if (vehicle->category.compare("train", Qt::CaseInsensitive) != 0)
                continue;
            ++trains;
            const auto def = MaszynaVehicles::definition(folder, MaszynaVehicles::firstRow(folder, type));
            if (def->shapePath.isEmpty() || !QFile::exists(def->shapePath)) {
                ++noModel;
                report << "model: " + folder + "/" + type + " -> " + def->shapePath;
            }
            noMass += vehicle->massKg <= 0.0;
            noLength += vehicle->length <= 0.0f;
        }
        for (const MaszynaCatalogue::Livery &livery : MaszynaCatalogue::read(folder)->liveries)
            if (MaszynaVehicle::read(folder, livery.type) == nullptr) {
                ++unmatched;
                report << "catalogue: " + folder + ": " + livery.skin + " = " + livery.type;
            }
        int unlisted = 0;
        rowsTotal += MaszynaVehicles::rows(folder, &unlisted).size();
        unlistedTotal += unlisted;
    }
    const qint64 ms = timer.elapsed();
    qInfo().noquote() << QString("[tests:maszyna-vehicle-corpus] %1 folders, %2 pairs, %3 railway, %4 rows, %5 not in a catalogue, %6 ms; "
                                 "no model %7, catalogue lines without a type %8, includes not found %9, no mass %10, no length %11")
                         .arg(folders.size()).arg(pairs).arg(trains).arg(rowsTotal).arg(unlistedTotal).arg(ms)
                         .arg(noModel).arg(unmatched).arg(problems).arg(noMass).arg(noLength);
    const QString reportPath = qEnvironmentVariable("TSRE_MASZYNA_REPORT");
    if (!reportPath.isEmpty()) {
        QFile out(reportPath);
        out.open(QIODevice::WriteOnly | QIODevice::Text);
        out.write(report.join('\n').toUtf8());
    }
    test.check(pairs == 1451, QString("the review's pairs in vehicle folders (%1)").arg(pairs));
    // The whole pack as a trainset: the Consist Editor's start.
    const QString trainset = qEnvironmentVariable("TSRE_MASZYNA_TRAINSET_ROOT");
    if (!trainset.isEmpty()) {
        EngLib lib;
        QScopedValueRollback<EngLib *> current(Game::currentEngLib, &lib);
        QScopedValueRollback<QString> gameRoot(Game::root, trainset);
        MaszynaVehicle::forget();
        QElapsedTimer whole;
        whole.start();
        lib.loadAll(trainset);
        int loaded = 0, broken = 0;
        for (const auto &entry : lib.eng)
            (entry.second != nullptr && entry.second->loaded == 1 ? loaded : broken)++;
        qInfo().noquote() << QString("[tests:maszyna-vehicle-corpus] whole pack as a trainset: %1 rows loaded, %2 not, %3 ms").arg(loaded).arg(broken).arg(whole.elapsed());
        test.check(loaded > 3000, QString("the rows load (%1)").arg(loaded));
    }
    return test.finish();
}

int TsreTests::runVehicleFilesSuite(bool verbose) {
    TokenTest::Suite test{"[tests:vehicle-files]", verbose};
    // TSRE_VEHICLE_FILES_ROW=<folder from TRAINSET>/<row>: that vehicle of the
    // game root instead, its tree in the log, its dialog drawn into the image
    // TSRE_VEHICLE_FILES_GRAB.
    const QString wanted = qEnvironmentVariable("TSRE_VEHICLE_FILES_ROW");
    if (!wanted.isEmpty()) {
        EngLib engs;
        ShapeLib shapes;
        QScopedValueRollback<EngLib *> currentEngs(Game::currentEngLib, &engs);
        QScopedValueRollback<ShapeLib *> currentShapes(Game::currentShapeLib, &shapes);
        const int id = engs.addEng(QDir::cleanPath(Game::root) + "/TRAINS/TRAINSET/" + wanted.section('/', 0, -2), wanted.section('/', -1));
        const QVector<VehicleFiles::File> files = VehicleFiles::ofVehicle(*engs.eng[id]);
        qint64 total = 0;
        for (const VehicleFiles::Folder &folder : VehicleFiles::byFolder(files, Game::root)) {
            qInfo().noquote() << "[tests:vehicle-files]" << folder.name << folder.files.size() << "files" << folder.bytes / 1000000 << "MB," << folder.missing << "missing";
            total += folder.bytes;
        }
        qInfo().noquote() << "[tests:vehicle-files] total" << files.size() << "files" << total / 1000000 << "MB";
        const QString image = qEnvironmentVariable("TSRE_VEHICLE_FILES_GRAB");
        if (!image.isEmpty()) {
            VehicleFilesDialog dialog("Vehicle files: " + engs.eng[id]->definition->displayName, "a", files);
            dialog.grab().save(image);
        }
        return 0;
    }
    QTemporaryDir dir;
    const QString root = dir.path();
    QScopedValueRollback<QString> gameRoot(Game::root, root);
    EngLib engs;
    ShapeLib shapes;
    QScopedValueRollback<EngLib *> currentEngs(Game::currentEngLib, &engs);
    QScopedValueRollback<ShapeLib *> currentShapes(Game::currentShapeLib, &shapes);
    const QString folder = root + "/TRAINS/TRAINSET/pkp/veh";
    const QString other = root + "/TRAINS/TRAINSET/pkp/other";
    const QString shared = root + "/TEXTURES/MASZYNA/tabor";
    QDir(root).mkpath("TRAINS/TRAINSET/pkp/veh/model");
    QDir(root).mkpath("TRAINS/TRAINSET/pkp/veh/skins");
    QDir(root).mkpath("TRAINS/TRAINSET/pkp/other");
    QDir(root).mkpath("TEXTURES/MASZYNA/tabor");
    QDir(root).mkpath("TRAINS/CONSISTS");

    writeFile(folder + "/common.inc", "Dimensions: L=10 H=4 W=3\n");
    writeFile(folder + "/a.fiz", "Param. Category=train M=1000\ninclude common.inc end\n");
    writeFile(folder + "/a.mmd", "models: model/a.t3d\nendmodels\n");
    writeFile(folder + "/textures.txt", "!=e,X\nskin-a.mat=a,X\nskin-b.mat=a,X\n");
    writeFile(folder + "/skin-a.mat", "texture1: skins/skin-a\ntexture_normalmap: skins/skin-a_n\n");
    writeFile(folder + "/skins/skin-a.dds", "aaaa");
    writeFile(folder + "/skins/skin-b.dds", "bbbb");
    writeFile(folder + "/skin-b.mat", "texture1: skins/skin-b\n");
    writeFile(shared + "/shared.dds", "sss");
    writeFile(other + "/part.dds", "pp");
    writeFile(other + "/b.fiz", "Param. Category=train M=1\n");
    writeFile(other + "/b.mmd", "models: b.t3d\nendmodels\n");
    Record part;
    part.vertexCount = 3;
    part.material = 1;
    Record second = part;
    part.next = 1;
    second.material = 2;
    part.next = 1;
    Record third = part;
    third.material = 3;
    third.next = 0;
    second.next = 2;
    Record skinPart = part;
    skinPart.material = -1;
    skinPart.next = 0;
    third.next = 3;
    writeFile(folder + "/model/a.e3d", file({records({part, second, third, skinPart}), chunk("VNT2", triangle(0.0f, true)),
                                             chunk("TEX0", strings({"", "tabor/shared", "dynamic/pkp/other/part", "tabor/gone"}))}));

    const int id = engs.addEng(folder, "a#skin-a.mmd");
    test.check(id >= 0 && engs.eng[id]->loaded == 1, "the row loads");
    if (id < 0 || engs.eng[id]->loaded != 1)
        return test.finish();
    const QVector<VehicleFiles::File> files = VehicleFiles::ofVehicle(*engs.eng[id]);
    QStringList paths;
    for (const VehicleFiles::File &f : files)
        paths << f.path;
    const auto has = [&paths](const QString &p) { return paths.contains(p); };
    test.check(has(folder + "/a.fiz") && has(folder + "/a.mmd") && has(folder + "/common.inc") && has(folder + "/textures.txt"), "its text data");
    test.check(has(folder + "/model/a.e3d"), "its model");
    test.check(has(folder + "/skin-a.mat") && has(folder + "/skins/skin-a.dds"), "its livery's material and texture");
    test.check(!has(folder + "/skin-b.mat") && !has(folder + "/skins/skin-b.dds"), "not another livery's");
    test.check(has(shared + "/shared.dds") && has(other + "/part.dds"), "files outside its folder");
    const auto fileNamed = [&files](const QString &p) {
        for (const VehicleFiles::File &f : files)
            if (f.path == p)
                return f;
        return VehicleFiles::File();
    };
    test.check(fileNamed(shared + "/gone.dds").missing, "a texture not found: where it belongs, marked: " + paths.join(' '));
    test.check(fileNamed(folder + "/skins/skin-a_n.dds").missing, "in the vehicle's folder when that has its first folder");
    test.check(fileNamed(folder + "/a.mmd").text() && !fileNamed(folder + "/model/a.e3d").text() && !fileNamed(shared + "/shared.dds").text(), "text data is told");
    test.check(fileNamed(shared + "/shared.dds").bytes == 3, "sizes");

    const QVector<VehicleFiles::Folder> folders = VehicleFiles::byFolder(files, root);
    QStringList names;
    for (const VehicleFiles::Folder &f : folders)
        names << f.name;
    test.check(names == QStringList({"TRAINS/TRAINSET/pkp/veh", "TEXTURES/MASZYNA/tabor", "TRAINS/TRAINSET/pkp/other"}), "main folders, the vehicle's first: " + names.join(" | "));
    test.check(folders.size() == 3 && folders[1].missing == 1 && folders[1].bytes == 3, "a folder's size and what it misses");
    test.check(folders.size() == 3 && folders[0].files[0].text() && !folders[0].files.last().text(), "text data first");

    // MSTS: its OPENRAILS copy read, its own file listed as not read.
    const QString msts = root + "/TRAINS/TRAINSET/msts";
    QDir(root).mkpath("TRAINS/TRAINSET/msts/OPENRAILS");
    const auto utf16 = [](const QString &text) {
        QByteArray out("\xFF\xFE", 2);
        out.append(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
        return out;
    };
    const QString eng = "SIMISA@@@@@@@@@@JINX0D0t______\r\n\r\nWagon ( x\r\n Type ( Freight )\r\n Size ( 3m 4m 10m )\r\n Mass ( 10t )\r\n Name ( \"X\" )\r\n)\r\n";
    writeFile(msts + "/x.wag", utf16(eng));
    writeFile(msts + "/OPENRAILS/x.wag", utf16(eng));
    const auto read = VehicleDefinition::read(msts, "x.wag", root, true);
    test.check(read->path.contains("OPENRAILS"), "the copy is read");
    {
        EngLib local;
        Eng car(msts, "x.wag");
        car.definition = read;
        const QVector<VehicleFiles::File> carFiles = VehicleFiles::ofVehicle(car);
        bool copy = false, own = false;
        for (const VehicleFiles::File &f : carFiles) {
            copy = copy || (f.path == msts + "/OPENRAILS/x.wag" && !f.notRead);
            own = own || (f.path == msts + "/x.wag" && f.notRead);
        }
        test.check(copy && own, "the OPENRAILS copy read, the vehicle's own file told as not read");
    }

    // The train: its consist file and its cars', once each.
    const QString consistText = "SIMISA@@@@@@@@@@JINX0D0t______\n\nTrain (\n\tTrainCfg ( \"t\"\n\t\tName ( \"t\" )\n\t\tSerial ( 1 )\n\t\tMaxVelocity ( 40 1 )\n"
                                "\t\tNextWagonUID ( 3 )\n\t\tDurability ( 1 )\n\t\tEngine (\n\t\t\tUiD ( 0 )\n\t\t\tEngineData ( \"a#skin-a\" \"pkp/veh\" )\n\t\t)\n"
                                "\t\tEngine (\n\t\t\tUiD ( 1 )\n\t\t\tEngineData ( \"a#skin-b\" \"pkp/veh\" )\n\t\t)\n\t)\n)\n";
    writeFile(root + "/TRAINS/CONSISTS/t.con", utf16(consistText));
    Consist train(root + "/TRAINS/CONSISTS", "t.con");
    test.check(train.loaded == 1 && train.engItems.size() == 2, "the train reads");
    const QVector<VehicleFiles::File> trainFiles = VehicleFiles::ofTrain(train);
    QStringList trainPaths;
    for (const VehicleFiles::File &f : trainFiles)
        trainPaths << f.path;
    test.check(trainPaths.contains(root + "/TRAINS/CONSISTS/t.con") && trainPaths.contains(folder + "/skins/skin-a.dds") && trainPaths.contains(folder + "/skins/skin-b.dds"),
               "the train: its consist and both liveries");
    test.check(trainPaths.count(folder + "/a.mmd") == 1, "each file once");

    // The archive.
    const QString zipPath = root + "/out.zip";
    const VehicleFiles::Archived done = VehicleFiles::archive(files, root, zipPath);
    test.check(done.ok && done.left == 2, QString("written, the missing left out (%1 written, %2 left, %3)").arg(done.written).arg(done.left).arg(done.error));
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    test.check(mz_zip_reader_init_file(&zip, zipPath.toUtf8().constData(), 0), "it opens as a zip");
    QStringList entries;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
        char name[512];
        mz_zip_reader_get_filename(&zip, i, name, sizeof name);
        entries << QString::fromUtf8(name);
    }
    size_t size = 0;
    void *data = mz_zip_reader_extract_file_to_heap(&zip, "TEXTURES/MASZYNA/tabor/shared.dds", &size, 0);
    test.check(data != nullptr && QByteArray(static_cast<const char *>(data), int(size)) == "sss", "its content");
    if (data != nullptr)
        mz_free(data);
    mz_zip_reader_end(&zip);
    test.check(entries.contains("TRAINS/TRAINSET/pkp/veh/a.mmd") && entries.contains("TRAINS/TRAINSET/pkp/other/part.dds"), "paths from the game root: " + entries.join(' '));
    test.check(entries.size() == done.written, "one entry a file");
    const QString stoppedPath = root + "/stopped.zip";
    test.check(!VehicleFiles::archive(files, root, stoppedPath, [](int, int) { return false; }).ok && !QFile::exists(stoppedPath), "stopped: no archive left");

    // The dialog.
    VehicleFilesDialog dialog("t", "a", files);
    test.check(dialog.tree.topLevelItemCount() == 3 && dialog.tree.topLevelItem(0)->text(0) == "TRAINS/TRAINSET/pkp/veh", "the tree's main folders");
    QTreeWidgetItem *vehicle = dialog.tree.topLevelItem(0);
    QTreeWidgetItem *mmd = nullptr, *e3d = nullptr;
    for (int i = 0; i < vehicle->childCount(); ++i) {
        if (vehicle->child(i)->text(0) == "a.mmd")
            mmd = vehicle->child(i);
        if (vehicle->child(i)->text(0) == "model/a.e3d")
            e3d = vehicle->child(i);
    }
    test.check(mmd != nullptr && e3d != nullptr, "files by their paths in the folder");
    test.check(dialog.canOpen(mmd) && !dialog.canOpen(e3d) && !dialog.canOpen(vehicle), "only text data opens");
    dialog.tree.setCurrentItem(e3d);
    test.check(!dialog.openButton.isEnabled(), "the button follows the selection");
    dialog.tree.setCurrentItem(mmd);
    test.check(dialog.openButton.isEnabled(), "and is on for text data");
    QTreeWidgetItem *tabor = dialog.tree.topLevelItem(1);
    QTreeWidgetItem *gone = nullptr;
    for (int i = 0; i < tabor->childCount(); ++i)
        if (tabor->child(i)->text(0) == "gone.dds")
            gone = tabor->child(i);
    test.check(gone != nullptr && !dialog.canOpen(gone) && !gone->text(2).isEmpty(), "a missing file is marked");

    return test.finish();
}
