/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/maszyna/E3dFile.h>
#include <tsre/maszyna/Cp1250.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/shape/MstsMaterials.h>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

constexpr quint32 fourCc(char a, char b, char c, char d) {
    return quint32(quint8(a)) | quint32(quint8(b)) << 8 | quint32(quint8(c)) << 16 | quint32(quint8(d)) << 24;
}
constexpr quint32 IdMask = 0x00FFFFFF;
constexpr int RecordBytes = 256;
constexpr int RecordsPerVariant = 64;
constexpr int VertexFloatsPlain = 8;
constexpr int TangentFloats = 4;
constexpr int UserDataFloats = 4;
// The file's flags (task review): a matrix counts only with one of these.
constexpr quint32 FlagsMatrix = 0xC000;

inline quint32 u32At(const char *p) {
    quint32 v;
    std::memcpy(&v, p, sizeof v);
    return v;
}
inline qint32 i32At(const char *p) {
    return qint32(u32At(p));
}
inline float f32At(const char *p) {
    float v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

struct Chunk {
    const char *data = nullptr;
    qsizetype size = 0;
    int variant = -1;
    bool present() const { return data != nullptr; }
};

struct Chunks {
    Chunk records, matrices, vertices, indices, materials, names;
    bool doubleMatrices = false;
};

QString tooShort() {
    //% "The file ends inside a chunk."
    return qtTrId("maszyna.e3d.error.size");
}

bool assign(Chunk &chunk, const char *data, qsizetype size, int variant, QString *error) {
    if (chunk.present()) {
        //% "A chunk is repeated."
        *error = qtTrId("maszyna.e3d.error.repeated");
        return false;
    }
    chunk.data = data;
    chunk.size = size;
    chunk.variant = variant;
    return true;
}

// The chunks inside the file's E3D0, by kind; those it does not know passed
// over, as MaSzyna does.
bool readChunks(const QByteArray &bytes, Chunks &out, QString *error) {
    if (bytes.size() < 8 || u32At(bytes.constData()) != fourCc('E', '3', 'D', '0')) {
        //% "Not an E3D model."
        *error = qtTrId("maszyna.e3d.error.format");
        return false;
    }
    const qsizetype outer = u32At(bytes.constData() + 4);
    if (outer < 8 || outer > bytes.size()) {
        *error = tooShort();
        return false;
    }
    const char *begin = bytes.constData();
    qsizetype at = 8;
    while (at < outer) {
        if (at + 8 > outer) {
            *error = tooShort();
            return false;
        }
        const quint32 type = u32At(begin + at);
        const qsizetype size = u32At(begin + at + 4);
        if (size < 8 || at + size > outer) {
            *error = tooShort();
            return false;
        }
        const char *data = begin + at + 8;
        const qsizetype payload = size - 8;
        const int digit = int((type >> 24) & 0xFF) - '0';
        bool ok = true;
        if ((type & IdMask) == (fourCc('S', 'U', 'B', 0) & IdMask))
            ok = assign(out.records, data, payload, digit, error);
        else if ((type & IdMask) == (fourCc('V', 'N', 'T', 0) & IdMask))
            ok = assign(out.vertices, data, payload, digit, error);
        else if ((type & IdMask) == (fourCc('I', 'D', 'X', 0) & IdMask))
            ok = assign(out.indices, data, payload, digit, error);
        else if (type == fourCc('T', 'R', 'A', '0') || type == fourCc('T', 'R', 'A', '1')) {
            ok = assign(out.matrices, data, payload, digit, error);
            out.doubleMatrices = type == fourCc('T', 'R', 'A', '1');
        } else if (type == fourCc('T', 'E', 'X', '0'))
            ok = assign(out.materials, data, payload, digit, error);
        else if (type == fourCc('N', 'A', 'M', '0'))
            ok = assign(out.names, data, payload, digit, error);
        if (!ok)
            return false;
        at += size;
    }
    if (!out.records.present() || out.records.variant < 0) {
        //% "The model has no submodels."
        *error = qtTrId("maszyna.e3d.error.nosubmodels");
        return false;
    }
    return true;
}

QStringList stringTable(const Chunk &chunk, bool slashes) {
    QStringList table;
    const char *p = chunk.data, *end = chunk.data + chunk.size;
    while (p < end) {
        const char *zero = static_cast<const char *>(std::memchr(p, 0, size_t(end - p)));
        const char *stop = zero != nullptr ? zero : end;
        QString text = Maszyna::fromCp1250(p, int(stop - p));
        if (slashes)
            text.replace(QLatin1Char('\\'), QLatin1Char('/'));
        table.push_back(text);
        p = stop + 1;
    }
    return table;
}

inline quint32 indexAt(const char *p, int size, qsizetype i) {
    switch (size) {
    case 1:
        return quint8(p[i]);
    case 2: {
        quint16 v;
        std::memcpy(&v, p + i * 2, sizeof v);
        return v;
    }
    default:
        return u32At(p + i * 4);
    }
}

// The file's vertices to the PBR layout in one pass: position, normal, uv,
// alpha 1, tangent (xyz, handedness), second uv 0, colour 1.
bool readVertices(const Chunk &chunk, E3dModel &model, bool &hasTangents, QString *error) {
    hasTangents = false;
    if (!chunk.present())
        return true;
    const int variant = chunk.variant;
    if (variant < 0 || (variant & 3) == 1 || (variant & 3) == 3) {
        //% "The model's vertices are in a format not read."
        *error = qtTrId("maszyna.e3d.error.vertices");
        return false;
    }
    hasTangents = (variant & 3) == 2;
    const int floats = VertexFloatsPlain + (hasTangents ? TangentFloats : 0) + ((variant & 4) != 0 ? UserDataFloats : 0);
    const qsizetype stride = floats * qsizetype(sizeof(float));
    const qsizetype count = chunk.size / stride;
    model.fileVertexVariant = variant;
    model.vertices.resize(size_t(count) * E3dModel::VertexFloats);
    float *out = model.vertices.data();
    const char *p = chunk.data;
    float v[VertexFloatsPlain + TangentFloats];
    const size_t copied = (VertexFloatsPlain + (hasTangents ? TangentFloats : 0)) * sizeof(float);
    for (qsizetype i = 0; i < count; ++i, p += stride, out += E3dModel::VertexFloats) {
        std::memcpy(v, p, copied);
        std::copy(v, v + VertexFloatsPlain, out);
        out[8] = 1.0f;
        if (hasTangents)
            std::copy(v + VertexFloatsPlain, v + VertexFloatsPlain + TangentFloats, out + 9);
        else
            std::fill(out + 9, out + 13, 0.0f);
        out[13] = out[14] = 0.0f;
        std::fill(out + 15, out + 19, 1.0f);
    }
    return true;
}

bool readIndices(const Chunk &chunk, E3dModel &model, QString *error) {
    if (!chunk.present())
        return true;
    const int size = chunk.variant;
    if (size != 1 && size != 2 && size != 4) {
        //% "The model's indices are in a format not read."
        *error = qtTrId("maszyna.e3d.error.indices");
        return false;
    }
    model.fileIndexBytes = size;
    const qsizetype count = chunk.size / size;
    if (size == 1) {
        model.indexBytes = 2;
        model.indices.resize(count * 2);
        char *to = model.indices.data();
        for (qsizetype i = 0; i < count; ++i) {
            const quint16 v = quint8(chunk.data[i]);
            std::memcpy(to + i * 2, &v, sizeof v);
        }
    } else {
        model.indexBytes = size;
        model.indices = QByteArray(chunk.data, count * size);
    }
    return true;
}

void identity(float *m) {
    std::fill(m, m + 16, 0.0f);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

bool startsHidden(const QString &name) {
    return name.startsWith(QLatin1String("Light_On")) || name.endsWith(QLatin1String("_on"));
}

E3dModel::Kind kindOf(qint32 type) {
    switch (type) {
    case 4:
        return E3dModel::Kind::Mesh;
    case 256:
        return E3dModel::Kind::Pivot;
    case 257:
        return E3dModel::Kind::Spot;
    default:
        return E3dModel::Kind::Other;
    }
}

unsigned char passesOf(quint32 flags) {
    unsigned char passes = 0;
    if ((flags & 0x10) != 0)
        passes |= E3dModel::Opaque;
    if ((flags & 0x20) != 0)
        passes |= E3dModel::Blended;
    if ((flags & 0x0F) != 0)
        passes |= E3dModel::BySlot;
    return passes;
}

struct Raw {
    qint32 next, child;
    quint32 flags;
    qint32 matrix;
};

QString badGeometry() {
    //% "A submodel's geometry lies outside the file's vertices or indices."
    return qtTrId("maszyna.e3d.error.geometry");
}

// One record to a part; its tree links are filled by the walk.
bool readRecord(const char *r, const Chunks &chunks, E3dModel &model, E3dModel::Part &part, Raw &raw, QString *error) {
    raw.next = i32At(r);
    raw.child = i32At(r + 4);
    raw.flags = u32At(r + 20);
    raw.matrix = i32At(r + 24);

    part.kind = kindOf(i32At(r + 8));
    ++model.fileKinds[int(part.kind)];
    part.name = model.names.isEmpty() ? -1 : i32At(r + 12);
    if (part.name >= model.names.size()) {
        //% "A submodel names an entry past the file's names."
        *error = qtTrId("maszyna.e3d.error.name");
        return false;
    }

    const qint32 vertexCount = i32At(r + 28);
    const qint32 vertexOffset = i32At(r + 32);
    const qint32 indexCount = i32At(r + 156);
    const qint32 indexOffset = i32At(r + 160);
    const qint32 material = i32At(r + 36);
    part.material = material > 0 && material < model.materialNames.size() ? material : std::min(material, 0);
    part.passes = passesOf(raw.flags);
    part.slotMask = (unsigned char)(raw.flags & 0x0F);
    part.lightThreshold = f32At(r + 44);
    for (int c = 0; c < 4; ++c)
        part.diffuse[c] = f32At(r + 64 + c * 4);
    const float multiplier = f32At(r + 164);
    if (multiplier > 0.0f)
        for (int c = 0; c < 3; ++c)
            part.diffuse[c] /= multiplier;
    const float maxSquared = f32At(r + 116);
    const float minSquared = f32At(r + 120);
    part.rangeMax = maxSquared > 0.0f ? std::sqrt(maxSquared) : 0.0f;
    part.rangeMin = minSquared > 0.0f ? std::sqrt(minSquared) : 0.0f;

    const qint64 vertices = model.vertexCount();
    if (part.kind == E3dModel::Kind::Mesh && vertexCount > 0) {
        if (vertexOffset < 0 || qint64(vertexOffset) + vertexCount > vertices) {
            *error = badGeometry();
            return false;
        }
        part.firstVertex = unsigned(vertexOffset);
        part.vertexCount = unsigned(vertexCount);
        // Without an index chunk a record's count means nothing (old files).
        if (indexCount > 0 && chunks.indices.present()) {
            const qint64 indices = chunks.indices.size / chunks.indices.variant;
            if (indexOffset < 0 || qint64(indexOffset) + indexCount > indices) {
                *error = badGeometry();
                return false;
            }
            for (qsizetype i = 0; i < indexCount; ++i)
                if (indexAt(chunks.indices.data, chunks.indices.variant, qsizetype(indexOffset) + i) >= quint32(vertexCount)) {
                    *error = badGeometry();
                    return false;
                }
            part.firstIndex = unsigned(indexOffset);
            part.indexCount = unsigned(indexCount);
        }
    }
    return true;
}

// The tree, walked as MaSzyna's name search goes (self, next sibling, then
// children): it comes out parents first, and the first of a repeated name
// found. Checks the links; every part reached once.
bool walkTree(std::vector<E3dModel::Part> &records, const std::vector<Raw> &raw, E3dModel &model, QString *error) {
    const int count = int(records.size());
    std::vector<int> newIndex(size_t(count), -1);
    struct Visit { int index; int parent; };
    std::vector<Visit> stack;
    stack.push_back({0, -1});
    std::vector<int> order;
    order.reserve(size_t(count));
    const auto badTree = [&] {
        //% "The model's submodel tree is broken."
        *error = qtTrId("maszyna.e3d.error.tree");
        return false;
    };
    std::vector<int> parents;
    parents.reserve(size_t(count));
    while (!stack.empty()) {
        const Visit visit = stack.back();
        stack.pop_back();
        if (newIndex[size_t(visit.index)] >= 0)
            return badTree();
        newIndex[size_t(visit.index)] = int(order.size());
        order.push_back(visit.index);
        parents.push_back(visit.parent);
        const Raw &r = raw[size_t(visit.index)];
        if (r.child >= count || r.next >= count)
            return badTree();
        if (r.child > 0)
            stack.push_back({r.child, visit.index});
        if (r.next > 0)
            stack.push_back({r.next, visit.parent});
    }

    model.unreachableParts = count - int(order.size());
    model.parts.reserve(order.size());
    for (size_t i = 0; i < order.size(); ++i) {
        E3dModel::Part part = records[size_t(order[i])];
        const Raw &r = raw[size_t(order[i])];
        part.parent = parents[i] >= 0 ? newIndex[size_t(parents[i])] : -1;
        part.firstChild = r.child > 0 ? newIndex[size_t(r.child)] : -1;
        part.nextSibling = r.next > 0 ? newIndex[size_t(r.next)] : -1;
        model.parts.push_back(part);
    }
    return true;
}

void readMatrices(const Chunks &chunks, std::vector<float> &out) {
    if (!chunks.matrices.present())
        return;
    const qsizetype size = chunks.doubleMatrices ? 128 : 64;
    const qsizetype count = chunks.matrices.size / size;
    out.resize(size_t(count) * 16);
    if (!chunks.doubleMatrices) {
        std::memcpy(out.data(), chunks.matrices.data, size_t(count) * 64);
        return;
    }
    for (qsizetype i = 0; i < count * 16; ++i) {
        double v;
        std::memcpy(&v, chunks.matrices.data + i * 8, sizeof v);
        out[size_t(i)] = float(v);
    }
}

void computeTangents(E3dModel &model) {
    const int count = model.vertexCount();
    std::vector<int> triangles;
    for (const E3dModel::Part &part : model.parts) {
        if (part.kind != E3dModel::Kind::Mesh || part.vertexCount == 0)
            continue;
        if (part.indexCount > 0)
            for (unsigned i = 0; i < part.indexCount; ++i)
                triangles.push_back(int(part.firstVertex + indexAt(model.indices.constData(), model.indexBytes, part.firstIndex + i)));
        else
            for (unsigned i = 0; i < part.vertexCount; ++i)
                triangles.push_back(int(part.firstVertex + i));
    }
    const float *vertices = model.vertices.data();
    const MstsMaterials::VertexReader reader = [vertices, count](int vertex, float *position, float *normal, float *uv) {
        if (vertex < 0 || vertex >= count)
            return false;
        const float *v = vertices + size_t(vertex) * E3dModel::VertexFloats;
        std::copy(v, v + 3, position);
        std::copy(v + 3, v + 6, normal);
        std::copy(v + 6, v + 8, uv);
        return true;
    };
    std::vector<float> tangents;
    MstsMaterials::tangents(count, reader, triangles, tangents);
    for (int i = 0; i < count; ++i)
        std::copy(tangents.data() + size_t(i) * 4, tangents.data() + size_t(i) * 4 + 4,
                  model.vertices.data() + size_t(i) * E3dModel::VertexFloats + 9);
}

void computeBounds(E3dModel &model) {
    const float inf = std::numeric_limits<float>::infinity();
    float low[3] = {inf, inf, inf}, high[3] = {-inf, -inf, -inf};
    bool any = false;
    for (E3dModel::Part &part : model.parts) {
        if (part.kind != E3dModel::Kind::Mesh || part.vertexCount == 0)
            continue;
        const float *v = model.vertices.data() + size_t(part.firstVertex) * E3dModel::VertexFloats;
        float partLow[3] = {inf, inf, inf}, partHigh[3] = {-inf, -inf, -inf};
        const float *m = part.world;
        for (unsigned i = 0; i < part.vertexCount; ++i, v += E3dModel::VertexFloats) {
            for (int c = 0; c < 3; ++c) {
                partLow[c] = std::min(partLow[c], v[c]);
                partHigh[c] = std::max(partHigh[c], v[c]);
                const float w = m[c] * v[0] + m[4 + c] * v[1] + m[8 + c] * v[2] + m[12 + c];
                low[c] = std::min(low[c], w);
                high[c] = std::max(high[c], w);
            }
        }
        any = true;
        float r = 0.0f;
        for (int c = 0; c < 3; ++c) {
            part.boundCenter[c] = 0.5f * (partLow[c] + partHigh[c]);
            const float e = 0.5f * (partHigh[c] - partLow[c]);
            r += e * e;
        }
        part.boundRadius = std::sqrt(r);
    }
    if (!any)
        return;
    float diagonal = 0.0f;
    for (int c = 0; c < 3; ++c) {
        model.bound[c * 2] = high[c];
        model.bound[c * 2 + 1] = low[c];
        const float e = high[c] - low[c];
        diagonal += e * e;
    }
    model.radius = 0.5f * std::sqrt(diagonal);
}

// The levels are the distances where something changes: each part's range,
// narrowed by its parents' (a part out of range hides its children).
void computeLevels(E3dModel &model) {
    const int count = int(model.parts.size());
    std::vector<float> low(static_cast<size_t>(count)), high(static_cast<size_t>(count));
    std::vector<float> limits;
    for (int i = 0; i < count; ++i) {
        const E3dModel::Part &part = model.parts[size_t(i)];
        low[size_t(i)] = part.parent >= 0 ? std::max(low[size_t(part.parent)], part.rangeMin) : part.rangeMin;
        high[size_t(i)] = part.parent >= 0 ? std::min(high[size_t(part.parent)], part.rangeMax) : part.rangeMax;
        if (part.kind != E3dModel::Kind::Mesh || part.vertexCount == 0 || low[size_t(i)] >= high[size_t(i)])
            continue;
        limits.push_back(high[size_t(i)]);
        if (low[size_t(i)] > 0.0f)
            limits.push_back(low[size_t(i)]);
    }
    std::sort(limits.begin(), limits.end());
    limits.erase(std::unique(limits.begin(), limits.end()), limits.end());
    model.levelLimits = limits;
    model.levelParts.assign(limits.size(), {});
    for (size_t level = 0; level < limits.size(); ++level) {
        const float from = level > 0 ? limits[level - 1] : 0.0f;
        const float to = limits[level];
        for (int i = 0; i < count; ++i) {
            const E3dModel::Part &part = model.parts[size_t(i)];
            if (part.kind == E3dModel::Kind::Mesh && part.vertexCount > 0 && low[size_t(i)] <= from && to <= high[size_t(i)])
                model.levelParts[level].push_back(i);
        }
    }
}

// MaSzyna's lit sections (TDynamicObject::init_sections): cab0 to cab2, then
// the chains corridor, korytarz, compartment, przedzial and external_load,
// counted from 0 or 1, below 10 also with two digits. A section's light
// reaches everything under it.
void findSections(E3dModel &model) {
    const auto take = [&model](const QString &name) {
        const auto found = model.byName.constFind(name);
        if (found == model.byName.constEnd())
            return false;
        E3dModel::Part &part = model.parts[size_t(found.value())];
        if (part.section < 0) {
            part.section = int(model.sections.size());
            model.sections << name;
        }
        return true;
    };
    for (const char *cab : {"cab0", "cab1", "cab2"})
        take(QLatin1String(cab));
    for (const char *chain : {"corridor", "korytarz", "compartment", "przedzial", "external_load"}) {
        const QString prefix = QLatin1String(chain);
        bool found = false;
        int index = 0;
        do {
            const QString number = QString::number(index);
            found = take(prefix + number) || (index < 10 && take(prefix + QLatin1Char('0') + number));
            ++index;
        } while (found || index < 2);
    }
    if (model.sections.isEmpty())
        return;
    for (E3dModel::Part &part : model.parts)
        if (part.section < 0 && part.parent >= 0)
            part.section = model.parts[size_t(part.parent)].section;
}

}

namespace E3dFile {

std::shared_ptr<E3dModel> parse(const QByteArray &bytes, QString *error) {
    Chunks chunks;
    if (!readChunks(bytes, chunks, error))
        return nullptr;

    auto model = std::make_shared<E3dModel>();
    if (chunks.materials.present())
        model->materialNames = stringTable(chunks.materials, true);
    if (chunks.names.present())
        model->names = stringTable(chunks.names, false);

    bool hasTangents = false;
    if (!readVertices(chunks.vertices, *model, hasTangents, error) || !readIndices(chunks.indices, *model, error))
        return nullptr;

    const qsizetype recordSize = RecordBytes + RecordsPerVariant * chunks.records.variant;
    const qsizetype count = chunks.records.size / recordSize;
    if (count <= 0) {
        *error = qtTrId("maszyna.e3d.error.nosubmodels");
        return nullptr;
    }
    std::vector<E3dModel::Part> records(static_cast<size_t>(count));
    std::vector<Raw> raw(static_cast<size_t>(count));
    for (qsizetype i = 0; i < count; ++i)
        if (!readRecord(chunks.records.data + i * recordSize, chunks, *model, records[size_t(i)], raw[size_t(i)], error))
            return nullptr;

    std::vector<float> matrices;
    readMatrices(chunks, matrices);
    const qint32 matrixCount = qint32(matrices.size() / 16);
    for (qsizetype i = 0; i < count; ++i) {
        E3dModel::Part &part = records[size_t(i)];
        identity(part.rest);
        const Raw &r = raw[size_t(i)];
        if ((r.flags & FlagsMatrix) == 0 || r.matrix < 0)
            continue;
        if (r.matrix >= matrixCount) {
            //% "A submodel names a matrix past the file's matrices."
            *error = qtTrId("maszyna.e3d.error.matrix");
            return nullptr;
        }
        std::copy(matrices.data() + size_t(r.matrix) * 16, matrices.data() + size_t(r.matrix) * 16 + 16, part.rest);
    }

    if (!walkTree(records, raw, *model, error))
        return nullptr;

    for (size_t i = 0; i < model->parts.size(); ++i) {
        E3dModel::Part &part = model->parts[i];
        if (part.parent >= 0)
            Mat4::multiply(part.world, model->parts[size_t(part.parent)].world, part.rest);
        else
            std::copy(part.rest, part.rest + 16, part.world);
        if (part.name >= 0) {
            const QString &name = model->names[part.name];
            part.startsHidden = startsHidden(name);
            if (!name.isEmpty()) {
                const QString key = name.toLower();
                if (!model->byName.contains(key))
                    model->byName.insert(key, int(i));
            }
        }
    }

    if (!hasTangents && !model->vertices.empty())
        computeTangents(*model);
    computeBounds(*model);
    computeLevels(*model);
    findSections(*model);
    return model;
}

std::shared_ptr<const E3dModel> read(const QString &path, QString *error, bool fresh) {
    struct Entry {
        std::weak_ptr<const E3dModel> model;
        QDateTime modified;
    };
    static QMutex mutex;
    static QHash<QString, Entry> cache;

    const QDateTime modified = QFileInfo(path).lastModified();
    if (!fresh) {
        QMutexLocker lock(&mutex);
        const auto found = cache.constFind(path);
        if (found != cache.constEnd() && found->modified == modified)
            if (std::shared_ptr<const E3dModel> model = found->model.lock())
                return model;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        //% "Cannot open the file."
        *error = qtTrId("maszyna.e3d.error.open");
        return nullptr;
    }
    const QByteArray bytes = file.readAll();
    std::shared_ptr<const E3dModel> model = parse(bytes, error);
    if (model != nullptr) {
        QMutexLocker lock(&mutex);
        cache.insert(path, Entry{model, modified});
    }
    return model;
}

}
