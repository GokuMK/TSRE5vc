/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/GltfShape.h>

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSet>

#include <shapeViewer/ContentHierarchyInfo.h>
#include <shapeViewer/ShapeHierarchyInfo.h>
#include <shapeViewer/ShapeTextureInfo.h>

#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>

#ifndef __APPLE__
#include <GL/gl.h>
#else
#include <OpenGL/gl.h>
#endif

namespace {

static QString normalizePathId(QString pathid) {
    if (Game::caseInsensitiveFS) {
        pathid = pathid.toLower();
    }
    pathid.replace("\\", "/");
    pathid.replace("//", "/");
    return pathid;
}

static QString cleanJoinPath(const QString& baseDir, const QString& uriOrPath) {
    if (uriOrPath.isEmpty()) {
        return normalizePathId(baseDir);
    }
    QString out;
    if (QDir::isAbsolutePath(uriOrPath)) {
        out = uriOrPath;
    } else {
        out = QDir(baseDir).filePath(uriOrPath);
    }
    out = QDir::cleanPath(out);
    return normalizePathId(out);
}

static bool decodeDataUri(const QString& uri, QByteArray& outBytes, QString& outMimeType) {
    if (!uri.startsWith("data:", Qt::CaseInsensitive)) {
        return false;
    }

    const int comma = uri.indexOf(',');
    if (comma < 0) {
        return false;
    }

    const QString meta = uri.mid(5, comma - 5);
    outMimeType = meta.section(';', 0, 0);
    const bool isBase64 = meta.contains(";base64", Qt::CaseInsensitive);
    const QByteArray payload = uri.mid(comma + 1).toLatin1();

    if (isBase64) {
        outBytes = QByteArray::fromBase64(payload);
        return !outBytes.isEmpty();
    }

    outBytes = QByteArray::fromPercentEncoding(payload);
    return !outBytes.isEmpty();
}

static bool readUint32LE(const QByteArray& data, int offset, quint32& out) {
    if (offset < 0 || offset + 4 > data.size()) {
        return false;
    }
    const auto* p = reinterpret_cast<const unsigned char*>(data.constData() + offset);
    out = quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
    return true;
}

static bool readGlbFile(const QString& path, QByteArray& outJsonChunk, QByteArray& outBinChunk, QString& outError) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        outError = QString("GLB: file not found or unreadable: %1").arg(path);
        return false;
    }

    const QByteArray bytes = file.readAll();
    if (bytes.size() < 12) {
        outError = QString("GLB: file too small: %1").arg(path);
        return false;
    }

    quint32 magic = 0;
    quint32 version = 0;
    quint32 length = 0;
    if (!readUint32LE(bytes, 0, magic) || !readUint32LE(bytes, 4, version) || !readUint32LE(bytes, 8, length)) {
        outError = QString("GLB: failed to read header: %1").arg(path);
        return false;
    }

    if (magic != 0x46546C67u /* 'glTF' */) {
        outError = QString("GLB: invalid magic in: %1").arg(path);
        return false;
    }
    if (version != 2u) {
        outError = QString("GLB: unsupported version %1 in: %2").arg(version).arg(path);
        return false;
    }
    if (length > quint32(bytes.size())) {
        outError = QString("GLB: declared length exceeds file size in: %1").arg(path);
        return false;
    }

    int offset = 12;
    while (offset + 8 <= bytes.size()) {
        quint32 chunkLength = 0;
        quint32 chunkType = 0;
        if (!readUint32LE(bytes, offset, chunkLength) || !readUint32LE(bytes, offset + 4, chunkType)) {
            break;
        }
        offset += 8;
        if (offset + int(chunkLength) > bytes.size()) {
            outError = QString("GLB: chunk exceeds file size in: %1").arg(path);
            return false;
        }

        const QByteArray chunkData = bytes.mid(offset, int(chunkLength));
        if (chunkType == 0x4E4F534Au /* JSON */) {
            outJsonChunk = chunkData;
        } else if (chunkType == 0x004E4942u /* BIN\\0 */) {
            outBinChunk = chunkData;
        }

        offset += int(chunkLength);
    }

    if (outJsonChunk.isEmpty()) {
        outError = QString("GLB: missing JSON chunk: %1").arg(path);
        return false;
    }

    return true;
}

static int componentCountForType(const QString& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT2") return 4;
    if (type == "MAT3") return 9;
    if (type == "MAT4") return 16;
    return 0;
}

static int componentByteSize(int componentType) {
    switch (componentType) {
        case 5120: return 1; // BYTE
        case 5121: return 1; // UNSIGNED_BYTE
        case 5122: return 2; // SHORT
        case 5123: return 2; // UNSIGNED_SHORT
        case 5125: return 4; // UNSIGNED_INT
        case 5126: return 4; // FLOAT
        default: return 0;
    }
}

static float readComponentAsFloat(const char* p, int componentType, bool normalized) {
    if (p == nullptr) {
        return 0.0f;
    }

    switch (componentType) {
        case 5126: { // FLOAT
            float v = 0.0f;
            std::memcpy(&v, p, sizeof(float));
            return v;
        }
        case 5121: { // UNSIGNED_BYTE
            const uint8_t v = *reinterpret_cast<const uint8_t*>(p);
            return normalized ? float(v) / 255.0f : float(v);
        }
        case 5123: { // UNSIGNED_SHORT
            uint16_t v = 0;
            std::memcpy(&v, p, sizeof(uint16_t));
            return normalized ? float(v) / 65535.0f : float(v);
        }
        case 5120: { // BYTE
            const int8_t v = *reinterpret_cast<const int8_t*>(p);
            if (!normalized) return float(v);
            const float f = float(v) / 127.0f;
            return f < -1.0f ? -1.0f : f;
        }
        case 5122: { // SHORT
            int16_t v = 0;
            std::memcpy(&v, p, sizeof(int16_t));
            if (!normalized) return float(v);
            const float f = float(v) / 32767.0f;
            return f < -1.0f ? -1.0f : f;
        }
        case 5125: { // UNSIGNED_INT
            uint32_t v = 0;
            std::memcpy(&v, p, sizeof(uint32_t));
            return float(v);
        }
        default:
            return 0.0f;
    }
}

static unsigned int readComponentAsUInt(const char* p, int componentType) {
    if (p == nullptr) {
        return 0;
    }

    switch (componentType) {
        case 5121: { // UNSIGNED_BYTE
            return unsigned(*reinterpret_cast<const uint8_t*>(p));
        }
        case 5123: { // UNSIGNED_SHORT
            uint16_t v = 0;
            std::memcpy(&v, p, sizeof(uint16_t));
            return unsigned(v);
        }
        case 5125: { // UNSIGNED_INT
            uint32_t v = 0;
            std::memcpy(&v, p, sizeof(uint32_t));
            return unsigned(v);
        }
        default:
            return 0;
    }
}

struct AccessorView {
    const char* data = nullptr;
    int stride = 0;
    int componentCount = 0;
    int componentType = 0;
    bool normalized = false;
    int count = 0;
};

struct GltfBuffer {
    QByteArray data;
};

struct GltfBufferView {
    int buffer = 0;
    int byteOffset = 0;
    int byteLength = 0;
    int byteStride = 0;
};

struct GltfAccessor {
    int bufferView = -1;
    int byteOffset = 0;
    int componentType = 0;
    int count = 0;
    QString type;
    bool normalized = false;
    bool hasSparse = false;
    QVector<float> min;
    QVector<float> max;
};

struct GltfImage {
    QString uri;
    int bufferView = -1;
    QString mimeType;
};

struct GltfTexture {
    int source = -1;
};

struct GltfMaterial {
    QString name;
    int baseColorTexture = -1; // texture index
    float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    QString alphaMode = "OPAQUE";
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};

struct GltfPrimitive {
    int position = -1;
    int normal = -1;
    int texcoord0 = -1;
    int indices = -1;
    int material = -1;
    int mode = 4; // TRIANGLES
};

struct GltfMesh {
    QString name;
    QVector<GltfPrimitive> primitives;
};

struct GltfNode {
    QString name;
    int mesh = -1;
    QVector<int> children;
    QVector<float> matrix;
    QVector<float> translation;
    QVector<float> rotation;
    QVector<float> scale;
};

struct GltfScene {
    QVector<int> nodes;
};

struct GltfModel {
    QVector<GltfBuffer> buffers;
    QVector<GltfBufferView> bufferViews;
    QVector<GltfAccessor> accessors;
    QVector<GltfImage> images;
    QVector<GltfTexture> textures;
    QVector<GltfMaterial> materials;
    QVector<GltfMesh> meshes;
    QVector<GltfNode> nodes;
    QVector<GltfScene> scenes;
    int defaultScene = 0;
};

static bool makeAccessorView(const GltfModel& model, int accessorIndex, AccessorView& out, QString& outError) {
    if (accessorIndex < 0 || accessorIndex >= model.accessors.size()) {
        outError = QString("Accessor index out of range: %1").arg(accessorIndex);
        return false;
    }

    const GltfAccessor& accessor = model.accessors[accessorIndex];
    if (accessor.hasSparse) {
        outError = QString("Accessor uses sparse encoding (unsupported)");
        return false;
    }

    if (accessor.bufferView < 0 || accessor.bufferView >= model.bufferViews.size()) {
        outError = QString("Accessor missing/invalid bufferView (unsupported)");
        return false;
    }

    const GltfBufferView& view = model.bufferViews[accessor.bufferView];
    if (view.buffer < 0 || view.buffer >= model.buffers.size()) {
        outError = QString("BufferView has invalid buffer index");
        return false;
    }

    const GltfBuffer& buffer = model.buffers[view.buffer];
    const int compCount = componentCountForType(accessor.type);
    const int compSize = componentByteSize(accessor.componentType);
    if (compCount <= 0 || compSize <= 0) {
        outError = QString("Accessor has unsupported type/componentType");
        return false;
    }

    const int stride = view.byteStride > 0 ? view.byteStride : compCount * compSize;
    const int baseOffset = view.byteOffset + accessor.byteOffset;
    if (baseOffset < 0 || baseOffset >= buffer.data.size()) {
        outError = QString("Accessor data offset out of buffer range");
        return false;
    }

    const qint64 lastByte = qint64(baseOffset) + qint64(accessor.count - 1) * qint64(stride) + qint64(compCount * compSize);
    if (lastByte > buffer.data.size()) {
        outError = QString("Accessor data exceeds buffer size");
        return false;
    }

    out.data = buffer.data.constData() + baseOffset;
    out.stride = stride;
    out.componentCount = compCount;
    out.componentType = accessor.componentType;
    out.normalized = accessor.normalized;
    out.count = accessor.count;
    return true;
}

static QVector<float> jsonFloatArray(const QJsonArray& arr) {
    QVector<float> out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        out.push_back(float(v.toDouble()));
    }
    return out;
}

static QVector<int> jsonIntArray(const QJsonArray& arr) {
    QVector<int> out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        out.push_back(v.toInt());
    }
    return out;
}

static void buildNodeLocalMatrix(const GltfNode& node, float out[16]) {
    if (node.matrix.size() == 16) {
        for (int i = 0; i < 16; i++) {
            out[i] = node.matrix[i];
        }
        return;
    }

    float t[3] = {0.0f, 0.0f, 0.0f};
    float q[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // x,y,z,w
    float s[3] = {1.0f, 1.0f, 1.0f};

    if (node.translation.size() == 3) {
        t[0] = node.translation[0];
        t[1] = node.translation[1];
        t[2] = node.translation[2];
    }
    if (node.rotation.size() == 4) {
        q[0] = node.rotation[0];
        q[1] = node.rotation[1];
        q[2] = node.rotation[2];
        q[3] = node.rotation[3];
    }
    if (node.scale.size() == 3) {
        s[0] = node.scale[0];
        s[1] = node.scale[1];
        s[2] = node.scale[2];
    }

    Mat4::fromRotationTranslation(out, q, t);
    // Apply scale (T * R * S): scale the basis vectors (first 3 columns).
    out[0] *= s[0]; out[1] *= s[0]; out[2] *= s[0];
    out[4] *= s[1]; out[5] *= s[1]; out[6] *= s[1];
    out[8] *= s[2]; out[9] *= s[2]; out[10] *= s[2];
}

static void computeSceneWorldMatrices(const GltfModel& model, const QVector<int>& roots, QVector<float*>& outWorld) {
    const int nodeCount = model.nodes.size();
    outWorld.resize(nodeCount);
    QVector<char> visited;
    visited.fill(0, nodeCount);

    for (int i = 0; i < nodeCount; i++) {
        outWorld[i] = new float[16];
        Mat4::identity(outWorld[i]);
    }

    std::function<void(int, const float*)> dfs = [&](int nodeIndex, const float* parentWorld) {
        if (nodeIndex < 0 || nodeIndex >= nodeCount) {
            return;
        }
        if (visited[nodeIndex]) {
            return;
        }

        visited[nodeIndex] = 1;

        float local[16];
        buildNodeLocalMatrix(model.nodes[nodeIndex], local);

        if (parentWorld == nullptr) {
            std::memcpy(outWorld[nodeIndex], local, sizeof(float) * 16);
        } else {
            Mat4::multiply(outWorld[nodeIndex], const_cast<float*>(parentWorld), local);
        }

        for (int child : model.nodes[nodeIndex].children) {
            dfs(child, outWorld[nodeIndex]);
        }
    };

    for (int root : roots) {
        dfs(root, nullptr);
    }

    for (int i = 0; i < nodeCount; i++) {
        if (visited[i]) continue;
        dfs(i, nullptr);
    }
}

static bool parseJsonFile(const QString& path, QByteArray& outBytes, QString& outError) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        outError = QString("glTF: file not found or unreadable: %1").arg(path);
        return false;
    }
    outBytes = file.readAll();
    if (outBytes.isEmpty()) {
        outError = QString("glTF: empty file: %1").arg(path);
        return false;
    }
    return true;
}

static bool parseGltfJson(const QByteArray& jsonBytes, QJsonObject& outRoot, QString& outError) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
    if (err.error != QJsonParseError::NoError) {
        outError = QString("glTF JSON parse error at %1: %2").arg(err.offset).arg(err.errorString());
        return false;
    }
    if (!doc.isObject()) {
        outError = QString("glTF JSON root is not an object");
        return false;
    }
    outRoot = doc.object();
    return true;
}

static void parseModelFromRoot(const QJsonObject& root, GltfModel& model) {
    model.defaultScene = root.value("scene").toInt(0);

    const QJsonArray buffers = root.value("buffers").toArray();
    model.buffers.resize(buffers.size());

    const QJsonArray bufferViews = root.value("bufferViews").toArray();
    model.bufferViews.resize(bufferViews.size());
    for (int i = 0; i < bufferViews.size(); i++) {
        const QJsonObject o = bufferViews[i].toObject();
        model.bufferViews[i].buffer = o.value("buffer").toInt(0);
        model.bufferViews[i].byteOffset = o.value("byteOffset").toInt(0);
        model.bufferViews[i].byteLength = o.value("byteLength").toInt(0);
        model.bufferViews[i].byteStride = o.value("byteStride").toInt(0);
    }

    const QJsonArray accessors = root.value("accessors").toArray();
    model.accessors.resize(accessors.size());
    for (int i = 0; i < accessors.size(); i++) {
        const QJsonObject o = accessors[i].toObject();
        model.accessors[i].bufferView = o.value("bufferView").toInt(-1);
        model.accessors[i].byteOffset = o.value("byteOffset").toInt(0);
        model.accessors[i].componentType = o.value("componentType").toInt(0);
        model.accessors[i].count = o.value("count").toInt(0);
        model.accessors[i].type = o.value("type").toString();
        model.accessors[i].normalized = o.value("normalized").toBool(false);
        model.accessors[i].hasSparse = o.contains("sparse");
        if (o.contains("min")) {
            model.accessors[i].min = jsonFloatArray(o.value("min").toArray());
        }
        if (o.contains("max")) {
            model.accessors[i].max = jsonFloatArray(o.value("max").toArray());
        }
    }

    const QJsonArray images = root.value("images").toArray();
    model.images.resize(images.size());
    for (int i = 0; i < images.size(); i++) {
        const QJsonObject o = images[i].toObject();
        model.images[i].uri = o.value("uri").toString();
        model.images[i].bufferView = o.value("bufferView").toInt(-1);
        model.images[i].mimeType = o.value("mimeType").toString();
    }

    const QJsonArray textures = root.value("textures").toArray();
    model.textures.resize(textures.size());
    for (int i = 0; i < textures.size(); i++) {
        const QJsonObject o = textures[i].toObject();
        model.textures[i].source = o.value("source").toInt(-1);
    }

    const QJsonArray materials = root.value("materials").toArray();
    model.materials.resize(materials.size());
    for (int i = 0; i < materials.size(); i++) {
        const QJsonObject o = materials[i].toObject();
        model.materials[i].name = o.value("name").toString();
        model.materials[i].alphaMode = o.value("alphaMode").toString("OPAQUE");
        model.materials[i].alphaCutoff = float(o.value("alphaCutoff").toDouble(0.5));
        model.materials[i].doubleSided = o.value("doubleSided").toBool(false);

        const QJsonObject pbr = o.value("pbrMetallicRoughness").toObject();
        if (pbr.contains("baseColorFactor")) {
            const QVector<float> factor = jsonFloatArray(pbr.value("baseColorFactor").toArray());
            if (factor.size() >= 4) {
                model.materials[i].baseColorFactor[0] = factor[0];
                model.materials[i].baseColorFactor[1] = factor[1];
                model.materials[i].baseColorFactor[2] = factor[2];
                model.materials[i].baseColorFactor[3] = factor[3];
            }
        }
        const QJsonObject baseColorTex = pbr.value("baseColorTexture").toObject();
        if (baseColorTex.contains("index")) {
            model.materials[i].baseColorTexture = baseColorTex.value("index").toInt(-1);
        }
    }

    const QJsonArray meshes = root.value("meshes").toArray();
    model.meshes.resize(meshes.size());
    for (int i = 0; i < meshes.size(); i++) {
        const QJsonObject o = meshes[i].toObject();
        model.meshes[i].name = o.value("name").toString();

        const QJsonArray prims = o.value("primitives").toArray();
        for (int p = 0; p < prims.size(); p++) {
            const QJsonObject po = prims[p].toObject();
            GltfPrimitive prim;
            prim.indices = po.value("indices").toInt(-1);
            prim.material = po.value("material").toInt(-1);
            prim.mode = po.value("mode").toInt(4);

            const QJsonObject attrs = po.value("attributes").toObject();
            prim.position = attrs.value("POSITION").toInt(-1);
            prim.normal = attrs.value("NORMAL").toInt(-1);
            prim.texcoord0 = attrs.value("TEXCOORD_0").toInt(-1);

            model.meshes[i].primitives.push_back(prim);
        }
    }

    const QJsonArray nodes = root.value("nodes").toArray();
    model.nodes.resize(nodes.size());
    for (int i = 0; i < nodes.size(); i++) {
        const QJsonObject o = nodes[i].toObject();
        model.nodes[i].name = o.value("name").toString();
        model.nodes[i].mesh = o.value("mesh").toInt(-1);
        model.nodes[i].children = jsonIntArray(o.value("children").toArray());
        if (o.contains("matrix")) model.nodes[i].matrix = jsonFloatArray(o.value("matrix").toArray());
        if (o.contains("translation")) model.nodes[i].translation = jsonFloatArray(o.value("translation").toArray());
        if (o.contains("rotation")) model.nodes[i].rotation = jsonFloatArray(o.value("rotation").toArray());
        if (o.contains("scale")) model.nodes[i].scale = jsonFloatArray(o.value("scale").toArray());
    }

    const QJsonArray scenes = root.value("scenes").toArray();
    model.scenes.resize(scenes.size());
    for (int i = 0; i < scenes.size(); i++) {
        const QJsonObject o = scenes[i].toObject();
        model.scenes[i].nodes = jsonIntArray(o.value("nodes").toArray());
    }
}

static bool accessorMinMaxVec3(const GltfModel& model, int accessorIndex, float outMin[3], float outMax[3], QString& outError) {
    if (accessorIndex < 0 || accessorIndex >= model.accessors.size()) {
        outError = QString("Invalid accessor index for POSITION");
        return false;
    }
    const GltfAccessor& accessor = model.accessors[accessorIndex];
    if (componentCountForType(accessor.type) != 3 || accessor.count <= 0) {
        outError = QString("POSITION accessor must be VEC3 with count > 0");
        return false;
    }
    if (accessor.componentType != 5126) {
        outError = QString("POSITION accessor must be FLOAT (5126)");
        return false;
    }

    if (accessor.min.size() >= 3 && accessor.max.size() >= 3) {
        outMin[0] = accessor.min[0];
        outMin[1] = accessor.min[1];
        outMin[2] = accessor.min[2];
        outMax[0] = accessor.max[0];
        outMax[1] = accessor.max[1];
        outMax[2] = accessor.max[2];
        return true;
    }

    AccessorView view;
    if (!makeAccessorView(model, accessorIndex, view, outError)) {
        return false;
    }

    outMin[0] = outMin[1] = outMin[2] = 1e30f;
    outMax[0] = outMax[1] = outMax[2] = -1e30f;

    for (int i = 0; i < view.count; i++) {
        const char* p = view.data + i * view.stride;
        const float x = readComponentAsFloat(p + 0 * 4, 5126, false);
        const float y = readComponentAsFloat(p + 1 * 4, 5126, false);
        const float z = readComponentAsFloat(p + 2 * 4, 5126, false);
        if (x < outMin[0]) outMin[0] = x;
        if (y < outMin[1]) outMin[1] = y;
        if (z < outMin[2]) outMin[2] = z;
        if (x > outMax[0]) outMax[0] = x;
        if (y > outMax[1]) outMax[1] = y;
        if (z > outMax[2]) outMax[2] = z;
    }

    return true;
}

static QVector<unsigned int> readIndices(const GltfModel& model, int accessorIndex, QString& outError) {
    QVector<unsigned int> out;
    if (accessorIndex < 0) {
        return out;
    }

    AccessorView view;
    if (!makeAccessorView(model, accessorIndex, view, outError)) {
        return out;
    }

    if (view.componentCount != 1) {
        outError = QString("Indices accessor must be SCALAR");
        return out;
    }

    if (view.componentType != 5121 && view.componentType != 5123 && view.componentType != 5125) {
        outError = QString("Indices accessor has unsupported componentType");
        return out;
    }

    out.resize(view.count);
    for (int i = 0; i < view.count; i++) {
        out[i] = readComponentAsUInt(view.data + i * view.stride, view.componentType);
    }
    return out;
}

} // namespace

GltfShape::GltfShape(QString pathid, QString name, QString texPath) {
    this->pathid = normalizePathId(pathid);
    this->name = name;
    this->texPath = normalizePathId(texPath);
    this->isinit = 1;
    this->loaded = 0;
    state.push_back(State());
}

GltfShape::~GltfShape() {
    cleanupRenderItems();
    cleanupGpu();
    cleanupNodeMatrices();
}

bool GltfShape::getBoxPoints(QVector<float>& points) {
    if (!isLoaded()) {
        return false;
    }

    for (int i = 0; i < 2; i++) {
        for (int j = 4; j < 6; j++) {
            points.push_back(bound[i]);
            points.push_back(bound[2]);
            points.push_back(bound[j]);
            points.push_back(bound[i]);
            points.push_back(bound[3]);
            points.push_back(bound[j]);
        }
    }

    for (int i = 0; i < 2; i++) {
        for (int j = 2; j < 4; j++) {
            points.push_back(bound[i]);
            points.push_back(bound[j]);
            points.push_back(bound[4]);
            points.push_back(bound[i]);
            points.push_back(bound[j]);
            points.push_back(bound[5]);
        }
    }

    for (int i = 4; i < 6; i++) {
        for (int j = 2; j < 4; j++) {
            points.push_back(bound[0]);
            points.push_back(bound[j]);
            points.push_back(bound[i]);
            points.push_back(bound[1]);
            points.push_back(bound[j]);
            points.push_back(bound[i]);
        }
    }

    return true;
}

void GltfShape::load() {
    if (!parseAndBuild()) {
        loaded = 2;
        return;
    }
    loaded = 1;
    requiresUpdate = true;
}

void GltfShape::reload() {
    invalidateRenderState(true);
    cleanupGpu();
    cleanupNodeMatrices();
    loaded = 0;
}

unsigned int GltfShape::newState() {
    state.push_back(State());
    return state.size() - 1;
}

void GltfShape::setAnimated(unsigned int stateId, bool animated) {
    if(stateId >= (unsigned int)state.size())
        return;
    state[stateId].animated = animated;
}

void GltfShape::updateSim(float deltaTime, unsigned int stateId) {
    (void)deltaTime;
    (void)stateId;
}

void GltfShape::render() {
    render(0, 0);
}

void GltfShape::render(int selectionColor, unsigned int stateId) {
    (void)stateId;

    if (isinit != 1 || loaded == 2) {
        return;
    }

    if (loaded == 0) {
        if (Game::allowObjLag < 1) return;
        Game::allowObjLag -= 2;
        loaded = 2;
        load();
        return;
    }

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (ctx == nullptr) {
        return;
    }
    QOpenGLFunctions* f = ctx->functions();
    if (f == nullptr) {
        return;
    }

    GLUU* gluu = GLUU::get();
    if (gluu == nullptr || gluu->currentShader == nullptr) {
        return;
    }

    if (selectionColor == 0) {
        syncTextureAddresses();
        gluu->enableTextures();
    } else {
        gluu->disableTextures(selectionColor);
    }
    gluu->enableNormals();
    gluu->setBrightness(1.0f);

    for (const DrawUnit& unit : drawUnits) {
        if (!unit.enabled) continue;
        if (unit.nodeIndex < 0 || unit.nodeIndex >= nodeWorldMatrices.size()) continue;
        if (unit.meshIndex < 0 || unit.meshIndex >= meshes.size()) continue;

        MeshGpu* mesh = meshes[unit.meshIndex];
        if (mesh == nullptr) continue;
        if (unit.primitiveIndex < 0 || unit.primitiveIndex >= mesh->primitives.size()) continue;

        MeshPrimitiveGpu* prim = mesh->primitives[unit.primitiveIndex];
        if (prim == nullptr) continue;
        if (!prim->VAO.isCreated()) continue;

        float* msMatrix = nodeWorldMatrices[unit.nodeIndex];
        if (msMatrix != nullptr) {
            gluu->currentShader->setUniformValue(gluu->currentShader->msMatrixUniform,
                                                 *reinterpret_cast<float(*)[4][4]>(msMatrix));
            // Keep legacy SFile matrix upload cache in sync with direct uniform writes.
            // Without this, the next shape can skip uploading its own matrix and inherit
            // the last glTF node transform.
            gluu->currentMsMatrinxHash = 0;
        }

        if (selectionColor == 0) {
            if (!prim->material.hasTexture || prim->material.texId < 0) {
                gluu->disableTextures(prim->material.color[0], prim->material.color[1], prim->material.color[2], prim->material.color[3]);
            } else if (prim->material.texAddr >= 0 && TexLib::disabledTextures.value(prim->material.texAddr, 0) != 1) {
                gluu->bindTexture(f, (unsigned int)prim->material.texAddr);
            } else {
                gluu->disableTextures(1.0f, 0.0f, 1.0f, 1.0f);
            }
        }

        QOpenGLVertexArrayObject::Binder vaoBinder(&prim->VAO);
        f->glDrawArrays(GL_TRIANGLES, 0, prim->vertCount);

        if (selectionColor == 0) {
            gluu->enableTextures();
        }
    }

    gluu->setBrightness(1.0f);
}

void GltfShape::pushRenderItem() {
    pushRenderItem(0, 0);
}

void GltfShape::pushRenderItem(int selectionColor, unsigned int stateId) {
    if (isinit != 1 || loaded == 2) {
        return;
    }
    if (Game::currentRenderer == nullptr) {
        return;
    }
    if (stateId >= (unsigned int)state.size()) {
        return;
    }

    if (loaded == 0) {
        if (Game::allowObjLag < 1) return;
        Game::allowObjLag -= 2;
        loaded = 2;
        load();
        return;
    }

    if (selectionColor == 0) {
        syncTextureAddresses();
        const unsigned long long textureStateHash = getTextureStateHash();
        if (renderItemsTextureHash.value(stateId, 0ULL) != textureStateHash) {
            requiresUpdate = true;
        }
    }

    if (renderItems[stateId].size() == 0 || requiresUpdate) {
        const bool globalInvalidateRequested = requiresUpdate;
        requiresUpdate = false;
        renderItemsTextureHash.remove(stateId);
        renderItems[stateId].clear();

        for (int u = 0; u < drawUnits.size(); u++) {
            const DrawUnit& unit = drawUnits[u];
            if (!unit.enabled) continue;
            if (unit.nodeIndex < 0 || unit.nodeIndex >= nodeWorldMatrices.size()) continue;
            if (unit.meshIndex < 0 || unit.meshIndex >= meshes.size()) continue;

            MeshGpu* mesh = meshes[unit.meshIndex];
            if (mesh == nullptr) continue;
            if (unit.primitiveIndex < 0 || unit.primitiveIndex >= mesh->primitives.size()) continue;

            MeshPrimitiveGpu* prim = mesh->primitives[unit.primitiveIndex];
            if (prim == nullptr) continue;
            if (!prim->VAO.isCreated()) continue;

            RenderItem* r = new RenderItem();
            r->msMatrix = nodeWorldMatrices[unit.nodeIndex];
            r->VBO = &prim->VBO;
            r->VAO = &prim->VAO;
            r->vertOffset = 0;
            r->vertCount = prim->vertCount;
            r->itemType = GL_TRIANGLES;
            r->vertexAttr = RenderItem::VNTA;
            r->shared = true;
            r->normalsEnabled = 1;
            r->brightness = 1.0f;

            if (selectionColor != 0) {
                r->disableTextures(selectionColor);
            } else if (!prim->material.hasTexture || prim->material.texId < 0) {
                r->disableTextures(prim->material.color[0], prim->material.color[1], prim->material.color[2], prim->material.color[3]);
            } else if (prim->material.texAddr >= 0 && TexLib::disabledTextures.value(prim->material.texAddr, 0) != 1) {
                r->enableTextures((unsigned int)prim->material.texAddr);
            } else {
                r->disableTextures(1.0f, 0.0f, 1.0f, 1.0f);
            }

            renderItems[stateId].push_back(r);
        }

        if (globalInvalidateRequested) {
            for (auto it = renderItems.begin(); it != renderItems.end(); ++it) {
                if (it.key() == stateId) continue;
                it.value().clear();
                renderItemsTextureHash.remove(it.key());
            }
        }
        renderItemsTextureHash[stateId] = getTextureStateHash();
    }

    if (selectionColor != 0) {
        for (int i = 0; i < renderItems[stateId].size(); i++) {
            RenderItem* baseItem = renderItems[stateId][i];
            if (baseItem == nullptr) continue;

            RenderItem* selectionItem = new RenderItem(*baseItem);
            selectionItem->shared = false;
            selectionItem->disableTextures(selectionColor);
            selectionItem->lineWidth = 0;
            Game::currentRenderer->pushItem(selectionItem, Game::currentRenderer->mvMatrix);
        }
        return;
    }

    if (renderItems[stateId].size() > 0) {
        Game::currentRenderer->pushItemsVNTA(renderItems[stateId], Game::currentRenderer->mvMatrix);
    }
}

void GltfShape::invalidateRenderState(bool invalidateMatrixCache) {
    (void)invalidateMatrixCache;
    requiresUpdate = true;
    cleanupRenderItems();
}

void GltfShape::enablePart(unsigned int uid, unsigned int stateId) {
    (void)stateId;
    if (uid >= (unsigned int)drawUnits.size()) {
        return;
    }
    drawUnits[(int)uid].enabled = true;
    invalidateRenderState(false);
}

void GltfShape::disablePart(unsigned int uid, unsigned int stateId) {
    (void)stateId;
    if (uid >= (unsigned int)drawUnits.size()) {
        return;
    }
    drawUnits[(int)uid].enabled = false;
    invalidateRenderState(false);
}

void GltfShape::fillShapeTextureInfo(QHash<int, ShapeTextureInfo*>& list, unsigned int stateId) {
    (void)stateId;
    if (!isLoaded()) return;

    QSet<int> uniqueTexIds;
    for (const MeshGpu* mesh : meshes) {
        if (mesh == nullptr) continue;
        for (const MeshPrimitiveGpu* prim : mesh->primitives) {
            if (prim == nullptr) continue;
            if (!prim->material.hasTexture) continue;
            if (prim->material.texId < 0) continue;
            uniqueTexIds.insert(prim->material.texId);
        }
    }

    for (int texId : uniqueTexIds) {
        ShapeTextureInfo* tInfo = new ShapeTextureInfo();
        tInfo->shapeName = name;
        tInfo->textureId = texId;

        auto it = TexLib::mtex.find(texId);
        Texture* ttex = (it == TexLib::mtex.end()) ? nullptr : it->second;

        if (ttex == nullptr) {
            tInfo->textureName = QString("tex:%1").arg(texId);
            tInfo->loaded = "MISSING";
            list[texId] = tInfo;
            continue;
        }

        tInfo->textureName = ttex->pathid;

        if (!ttex->loaded && !ttex->missing && !ttex->error) {
            tInfo->loading = true;
            list[texId] = tInfo;
            continue;
        }
        if (ttex->missing) {
            tInfo->loaded = "MISSING";
            list[texId] = tInfo;
            continue;
        }
        if (ttex->error) {
            tInfo->loaded = "ERROR";
            list[texId] = tInfo;
            continue;
        }

        if (!ttex->glLoaded) {
            tInfo->loading = true;
            list[texId] = tInfo;
            continue;
        }

        tInfo->loaded = "YES";
        tInfo->resolution = QString::number(ttex->width) + "x" + QString::number(ttex->height);
        if (ttex->gpuIsCompressed()) {
            tInfo->format = "GPU compressed";
        } else {
            tInfo->format = (ttex->bytesPerPixel == 4) ? "RGBA" : "RGB";
        }

        list[texId] = tInfo;
    }
}

void GltfShape::fillShapeHierarchyInfo(ShapeHierarchyInfo* info, unsigned int stateId) {
    (void)stateId;
    if (info == nullptr) return;
    if (!isLoaded()) return;

    info->hierarchy.clear();
    info->matrices.clear();
    info->parts.clear();

    info->hierarchy.reserve(nodeParents.size());
    for (int p : nodeParents) {
        info->hierarchy.push_back(p);
    }

    info->matrices.reserve(nodeNames.size());
    for (const QString& n : nodeNames) {
        info->matrices.push_back(n);
    }

    for (int u = 0; u < drawUnits.size(); u++) {
        const DrawUnit& unit = drawUnits[u];
        if (unit.nodeIndex < 0) continue;
        if (unit.meshIndex < 0 || unit.meshIndex >= meshes.size()) continue;
        MeshGpu* mesh = meshes[unit.meshIndex];
        if (mesh == nullptr) continue;
        if (unit.primitiveIndex < 0 || unit.primitiveIndex >= mesh->primitives.size()) continue;
        MeshPrimitiveGpu* prim = mesh->primitives[unit.primitiveIndex];
        if (prim == nullptr) continue;

        ShapeHierarchyInfo::ShapePart part;
        part.matrixId = unit.nodeIndex;
        part.uid = (unsigned int)u;
        part.enabled = unit.enabled;
        part.polyCount = prim->vertCount / 3;
        if (prim->material.hasTexture && prim->material.texId >= 0) {
            auto it = TexLib::mtex.find(prim->material.texId);
            Texture* t = (it == TexLib::mtex.end()) ? nullptr : it->second;
            if (t != nullptr) {
                part.textureName = t->pathid;
            }
        }
        info->parts.push_back(part);
    }
}

void GltfShape::fillContentHierarchyInfo(QVector<ContentHierarchyInfo*>& list, int parent) {
    if (!isLoaded()) return;

    ContentHierarchyInfo* info = new ContentHierarchyInfo();
    info->parent = parent;
    info->name = name.isEmpty() ? QFileInfo(pathid).fileName() : name;
    info->distanceLevelId = -1;
    info->shape = this;
    info->type = "shape";
    list.push_back(info);
}

void GltfShape::cleanupGpu() {
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    const bool hasGl = ctx != nullptr;

    for (MeshGpu* mesh : meshes) {
        if (mesh == nullptr) continue;
        for (MeshPrimitiveGpu* prim : mesh->primitives) {
            if (prim == nullptr) continue;
            if (hasGl) {
                if (prim->VBO.isCreated()) prim->VBO.destroy();
                if (prim->VAO.isCreated()) prim->VAO.destroy();
            }
            delete prim;
        }
        mesh->primitives.clear();
        delete mesh;
    }

    meshes.clear();
    drawUnits.clear();
}

void GltfShape::cleanupNodeMatrices() {
    for (float* m : nodeWorldMatrices) {
        delete[] m;
    }
    nodeWorldMatrices.clear();
    nodeNames.clear();
    nodeParents.clear();
}

void GltfShape::cleanupRenderItems() {
    renderItems.clear();
    renderItemsTextureHash.clear();
}

bool GltfShape::parseAndBuild() {
    cleanupRenderItems();
    cleanupGpu();
    cleanupNodeMatrices();

    const QString ext = pathid.section('.', -1, -1).toLower();
    const QString assetDir = QFileInfo(pathid).absolutePath();

    QByteArray jsonBytes;
    QByteArray binBytes;
    QString error;

    if (ext == "glb") {
        if (!readGlbFile(pathid, jsonBytes, binBytes, error)) {
            qDebug() << error;
            return false;
        }
    } else {
        if (!parseJsonFile(pathid, jsonBytes, error)) {
            qDebug() << error;
            return false;
        }
    }

    QJsonObject root;
    if (!parseGltfJson(jsonBytes, root, error)) {
        qDebug() << "glTF:" << pathid << ":" << error;
        return false;
    }

    GltfModel model;
    parseModelFromRoot(root, model);

    // Load buffers (binary payloads).
    const QJsonArray jsonBuffers = root.value("buffers").toArray();
    for (int i = 0; i < jsonBuffers.size(); i++) {
        const QJsonObject bo = jsonBuffers[i].toObject();
        const QString uri = bo.value("uri").toString();

        if (uri.isEmpty()) {
            if (ext == "glb" && i == 0) {
                model.buffers[i].data = binBytes;
                continue;
            }
            qDebug() << "glTF:" << pathid << "buffer missing uri (unsupported) idx:" << i;
            return false;
        }

        QByteArray bytes;
        QString mime;
        if (decodeDataUri(uri, bytes, mime)) {
            model.buffers[i].data = bytes;
            continue;
        }

        const QString bufferPath = cleanJoinPath(assetDir, uri);
        QFile f(bufferPath);
        if (!f.open(QIODevice::ReadOnly)) {
            qDebug() << "glTF:" << pathid << "buffer file not found:" << bufferPath;
            return false;
        }
        model.buffers[i].data = f.readAll();
        if (model.buffers[i].data.isEmpty()) {
            qDebug() << "glTF:" << pathid << "buffer empty:" << bufferPath;
            return false;
        }
    }

    // Node names + parents (for viewer hierarchy).
    nodeNames.clear();
    nodeParents.clear();
    nodeNames.reserve(model.nodes.size());
    nodeParents.fill(-1, model.nodes.size());
    for (int i = 0; i < model.nodes.size(); i++) {
        QString n = model.nodes[i].name;
        if (n.isEmpty()) {
            n = QString("Node %1").arg(i);
        }
        nodeNames.push_back(n);
    }
    for (int i = 0; i < model.nodes.size(); i++) {
        for (int child : model.nodes[i].children) {
            if (child < 0 || child >= model.nodes.size()) continue;
            if (nodeParents[child] < 0) {
                nodeParents[child] = i;
            }
        }
    }

    // Determine scene roots.
    QVector<int> roots;
    if (!model.scenes.isEmpty() && model.defaultScene >= 0 && model.defaultScene < model.scenes.size()) {
        roots = model.scenes[model.defaultScene].nodes;
    } else if (!model.nodes.isEmpty()) {
        roots.push_back(0);
    }

    // Compute world matrices (shape-local).
    computeSceneWorldMatrices(model, roots, nodeWorldMatrices);

    // Texture caching per glTF texture index.
    QVector<int> textureToTexLibId;
    textureToTexLibId.fill(-999, model.textures.size());

    auto texRoot = [&]() -> QString {
        if (!this->texPath.isEmpty()) {
            return this->texPath;
        }
        return normalizePathId(assetDir);
    };

    auto registerEmbeddedImage = [&](const QByteArray& encodedBytes, const QString& mimeType, const QString& debugName) -> int {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(encodedBytes);
        const QByteArray hex = hash.result().toHex();
        const QString key = QString("gltfimg:sha256:%1:len:%2:mime:%3")
                                    .arg(QString::fromLatin1(hex))
                                    .arg(encodedBytes.size())
                                    .arg(mimeType.isEmpty() ? "unknown" : mimeType);

        Texture* texture = new Texture(key);
        texture->editable = true;

        QString decodeError;
        if (!TexLib::decodeFromBytes(texture, encodedBytes, &decodeError)) {
            texture->error = true;
            qDebug() << "glTF:" << pathid << "embedded image decode failed:" << debugName << decodeError;
        }
        if (texture->error || texture->missing) {
            texture->loaded = false;
        }

        return TexLib::addTex(texture);
    };

    auto resolveTextureIndexToTexLibId = [&](int textureIndex) -> int {
        if (textureIndex < 0 || textureIndex >= model.textures.size()) {
            return -1;
        }
        if (textureToTexLibId[textureIndex] != -999) {
            return textureToTexLibId[textureIndex];
        }

        const int imageIndex = model.textures[textureIndex].source;
        if (imageIndex < 0 || imageIndex >= model.images.size()) {
            textureToTexLibId[textureIndex] = -1;
            return -1;
        }

        const GltfImage& img = model.images[imageIndex];
        const QString debugName = QString("texture[%1] image[%2]").arg(textureIndex).arg(imageIndex);

        if (!img.uri.isEmpty()) {
            QByteArray bytes;
            QString mime;
            if (decodeDataUri(img.uri, bytes, mime)) {
                textureToTexLibId[textureIndex] = registerEmbeddedImage(bytes, mime, debugName);
                return textureToTexLibId[textureIndex];
            }

            const QString resolved = cleanJoinPath(texRoot(), img.uri);
            textureToTexLibId[textureIndex] = TexLib::addTex(resolved);
            return textureToTexLibId[textureIndex];
        }

        if (img.bufferView >= 0 && img.bufferView < model.bufferViews.size()) {
            const GltfBufferView& bv = model.bufferViews[img.bufferView];
            if (bv.buffer < 0 || bv.buffer >= model.buffers.size()) {
                qDebug() << "glTF:" << pathid << "invalid image buffer index" << debugName;
                textureToTexLibId[textureIndex] = -1;
                return -1;
            }
            const GltfBuffer& buf = model.buffers[bv.buffer];
            if (bv.byteOffset < 0 || bv.byteLength <= 0 || bv.byteOffset + bv.byteLength > buf.data.size()) {
                qDebug() << "glTF:" << pathid << "invalid image bufferView range" << debugName;
                textureToTexLibId[textureIndex] = -1;
                return -1;
            }
            const char* ptr = buf.data.constData() + bv.byteOffset;
            const QByteArray slice = QByteArray::fromRawData(ptr, bv.byteLength);
            textureToTexLibId[textureIndex] = registerEmbeddedImage(slice, img.mimeType, debugName);
            return textureToTexLibId[textureIndex];
        }

        qDebug() << "glTF:" << pathid << "image has no uri/bufferView (unsupported)" << debugName;
        textureToTexLibId[textureIndex] = -1;
        return -1;
    };

    // Build meshes + GPU buffers (requires an active OpenGL context).
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (ctx == nullptr) {
        qDebug() << "glTF:" << pathid << "no OpenGL context";
        return false;
    }
    QOpenGLFunctions* f = ctx->functions();
    if (f == nullptr) {
        qDebug() << "glTF:" << pathid << "no OpenGL functions";
        return false;
    }

    GLUU* gluu = GLUU::get();
    const float defaultAlphaTest = gluu != nullptr ? gluu->alphaTest : 0.3f;

    meshes.reserve(model.meshes.size());
    for (int mi = 0; mi < model.meshes.size(); mi++) {
        MeshGpu* mesh = new MeshGpu();
        meshes.push_back(mesh);

        // Compute mesh local bounds from POSITION accessors.
        float meshMin[3] = {1e30f, 1e30f, 1e30f};
        float meshMax[3] = {-1e30f, -1e30f, -1e30f};
        bool meshHasBounds = false;

        for (const GltfPrimitive& prim : model.meshes[mi].primitives) {
            if (prim.position < 0) continue;
            float aMin[3], aMax[3];
            QString boundsError;
            if (!accessorMinMaxVec3(model, prim.position, aMin, aMax, boundsError)) {
                qDebug() << "glTF:" << pathid << "mesh" << mi << "bounds failed:" << boundsError;
                continue;
            }
            meshHasBounds = true;
            for (int k = 0; k < 3; k++) {
                if (aMin[k] < meshMin[k]) meshMin[k] = aMin[k];
                if (aMax[k] > meshMax[k]) meshMax[k] = aMax[k];
            }
        }

        if (meshHasBounds) {
            mesh->hasBounds = true;
            mesh->min[0] = meshMin[0]; mesh->min[1] = meshMin[1]; mesh->min[2] = meshMin[2];
            mesh->max[0] = meshMax[0]; mesh->max[1] = meshMax[1]; mesh->max[2] = meshMax[2];
        }

        for (int pi = 0; pi < model.meshes[mi].primitives.size(); pi++) {
            const GltfPrimitive& prim = model.meshes[mi].primitives[pi];
            if (prim.mode != 4) {
                qDebug() << "glTF:" << pathid << "mesh" << mi << "primitive" << pi << "unsupported mode" << prim.mode;
                continue;
            }
            if (prim.position < 0) {
                qDebug() << "glTF:" << pathid << "mesh" << mi << "primitive" << pi << "missing POSITION";
                continue;
            }

            AccessorView posView;
            QString viewErr;
            if (!makeAccessorView(model, prim.position, posView, viewErr)) {
                qDebug() << "glTF:" << pathid << "POSITION view error:" << viewErr;
                continue;
            }
            if (posView.componentType != 5126 || posView.componentCount != 3) {
                qDebug() << "glTF:" << pathid << "POSITION must be VEC3 float";
                continue;
            }

            AccessorView normView;
            const bool hasNormals = prim.normal >= 0 && makeAccessorView(model, prim.normal, normView, viewErr) && normView.componentType == 5126 && normView.componentCount == 3;
            AccessorView uvView;
            const bool hasUV = prim.texcoord0 >= 0 && makeAccessorView(model, prim.texcoord0, uvView, viewErr) && uvView.componentCount == 2;

            QVector<unsigned int> indices;
            if (prim.indices >= 0) {
                indices = readIndices(model, prim.indices, viewErr);
                if (!viewErr.isEmpty() && indices.isEmpty()) {
                    qDebug() << "glTF:" << pathid << "indices error:" << viewErr;
                    continue;
                }
            }

            const int vertexSourceCount = posView.count;
            const int outVertCount = !indices.isEmpty() ? indices.size() : vertexSourceCount;
            if (outVertCount <= 0) {
                continue;
            }
            if (outVertCount % 3 != 0) {
                qDebug() << "glTF:" << pathid << "primitive vertex count not divisible by 3 (TRIANGLES):" << outVertCount;
            }

            // Material mapping
            MaterialRuntime mat;
            if (prim.material >= 0 && prim.material < model.materials.size()) {
                const GltfMaterial& srcMat = model.materials[prim.material];
                mat.debugName = srcMat.name;
                mat.color[0] = srcMat.baseColorFactor[0];
                mat.color[1] = srcMat.baseColorFactor[1];
                mat.color[2] = srcMat.baseColorFactor[2];
                mat.color[3] = srcMat.baseColorFactor[3];
                mat.doubleSided = srcMat.doubleSided;

                const QString alphaMode = srcMat.alphaMode.toUpper();
                if (alphaMode == "OPAQUE") {
                    mat.alphaAttr = 1.0f;
                } else if (alphaMode == "MASK") {
                    float cutoff = srcMat.alphaCutoff;
                    if (cutoff < 0.0f) cutoff = 0.0f;
                    if (cutoff > 1.0f) cutoff = 1.0f;
                    mat.alphaAttr = -cutoff;
                } else if (alphaMode == "BLEND") {
                    mat.alphaAttr = -defaultAlphaTest;
                } else {
                    mat.alphaAttr = 1.0f;
                }

                if (srcMat.baseColorTexture >= 0) {
                    mat.texId = resolveTextureIndexToTexLibId(srcMat.baseColorTexture);
                    mat.hasTexture = mat.texId >= 0;
                }
            } else {
                mat.alphaAttr = 1.0f;
                mat.color[0] = mat.color[1] = mat.color[2] = 1.0f;
                mat.color[3] = 1.0f;
            }

            // Generate normals if missing.
            QVector<float> generatedNormals;
            if (!hasNormals) {
                generatedNormals.fill(0.0f, vertexSourceCount * 3);

                auto readPos = [&](unsigned int idx, float outPos[3]) {
                    if ((int)idx < 0 || (int)idx >= vertexSourceCount) {
                        outPos[0] = outPos[1] = outPos[2] = 0.0f;
                        return;
                    }
                    const char* p = posView.data + int(idx) * posView.stride;
                    outPos[0] = readComponentAsFloat(p + 0 * 4, 5126, false);
                    outPos[1] = readComponentAsFloat(p + 1 * 4, 5126, false);
                    outPos[2] = readComponentAsFloat(p + 2 * 4, 5126, false);
                };

                auto addNorm = [&](unsigned int idx, float nx, float ny, float nz) {
                    if ((int)idx < 0 || (int)idx >= vertexSourceCount) return;
                    generatedNormals[int(idx) * 3 + 0] += nx;
                    generatedNormals[int(idx) * 3 + 1] += ny;
                    generatedNormals[int(idx) * 3 + 2] += nz;
                };

                auto processTriangle = [&](unsigned int i0, unsigned int i1, unsigned int i2) {
                    float p0[3], p1[3], p2[3];
                    readPos(i0, p0);
                    readPos(i1, p1);
                    readPos(i2, p2);
                    const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
                    const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
                    const float nx = e1[1] * e2[2] - e1[2] * e2[1];
                    const float ny = e1[2] * e2[0] - e1[0] * e2[2];
                    const float nz = e1[0] * e2[1] - e1[1] * e2[0];
                    addNorm(i0, nx, ny, nz);
                    addNorm(i1, nx, ny, nz);
                    addNorm(i2, nx, ny, nz);
                };

                if (!indices.isEmpty()) {
                    for (int i = 0; i + 2 < indices.size(); i += 3) {
                        processTriangle(indices[i], indices[i + 1], indices[i + 2]);
                    }
                } else {
                    for (int i = 0; i + 2 < vertexSourceCount; i += 3) {
                        processTriangle(unsigned(i), unsigned(i + 1), unsigned(i + 2));
                    }
                }

                for (int i = 0; i < vertexSourceCount; i++) {
                    float nx = generatedNormals[i * 3 + 0];
                    float ny = generatedNormals[i * 3 + 1];
                    float nz = generatedNormals[i * 3 + 2];
                    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                    if (len > 1e-6f) {
                        nx /= len;
                        ny /= len;
                        nz /= len;
                    } else {
                        nx = 0.0f;
                        ny = 1.0f;
                        nz = 0.0f;
                    }
                    generatedNormals[i * 3 + 0] = nx;
                    generatedNormals[i * 3 + 1] = ny;
                    generatedNormals[i * 3 + 2] = nz;
                }
            }

            float* vertices = new float[outVertCount * 9];
            for (int outIndex = 0; outIndex < outVertCount; outIndex++) {
                const unsigned int srcIndex = !indices.isEmpty() ? indices[outIndex] : unsigned(outIndex);

                float px = 0.0f, py = 0.0f, pz = 0.0f;
                if ((int)srcIndex >= 0 && (int)srcIndex < posView.count) {
                    const char* p = posView.data + int(srcIndex) * posView.stride;
                    px = readComponentAsFloat(p + 0 * 4, 5126, false);
                    py = readComponentAsFloat(p + 1 * 4, 5126, false);
                    pz = readComponentAsFloat(p + 2 * 4, 5126, false);
                }

                float nx = 0.0f, ny = 1.0f, nz = 0.0f;
                if (hasNormals && (int)srcIndex >= 0 && (int)srcIndex < normView.count) {
                    const char* p = normView.data + int(srcIndex) * normView.stride;
                    nx = readComponentAsFloat(p + 0 * 4, 5126, false);
                    ny = readComponentAsFloat(p + 1 * 4, 5126, false);
                    nz = readComponentAsFloat(p + 2 * 4, 5126, false);
                } else if (!hasNormals && (int)srcIndex >= 0 && (int)srcIndex < vertexSourceCount) {
                    nx = generatedNormals[int(srcIndex) * 3 + 0];
                    ny = generatedNormals[int(srcIndex) * 3 + 1];
                    nz = generatedNormals[int(srcIndex) * 3 + 2];
                }

                float u = 0.0f, v = 0.0f;
                if (hasUV && (int)srcIndex >= 0 && (int)srcIndex < uvView.count) {
                    const char* p = uvView.data + int(srcIndex) * uvView.stride;
                    const int uvCompSize = componentByteSize(uvView.componentType);
                    const bool uvNormalized = uvView.normalized || uvView.componentType != 5126;
                    u = readComponentAsFloat(p + 0 * uvCompSize, uvView.componentType, uvNormalized);
                    v = readComponentAsFloat(p + 1 * uvCompSize, uvView.componentType, uvNormalized);
                }
                // TSRE's texture upload path keeps Qt's scanline order (top-to-bottom).
                // This matches glTF sample assets without any extra UV flipping here.

                vertices[outIndex * 9 + 0] = px;
                vertices[outIndex * 9 + 1] = py;
                vertices[outIndex * 9 + 2] = pz;
                vertices[outIndex * 9 + 3] = nx;
                vertices[outIndex * 9 + 4] = ny;
                vertices[outIndex * 9 + 5] = nz;
                vertices[outIndex * 9 + 6] = u;
                vertices[outIndex * 9 + 7] = v;
                vertices[outIndex * 9 + 8] = mat.alphaAttr;
            }

            MeshPrimitiveGpu* gpuPrim = new MeshPrimitiveGpu();
            gpuPrim->material = mat;
            gpuPrim->vertCount = outVertCount;

            gpuPrim->VAO.create();
            QOpenGLVertexArrayObject::Binder vaoBinder(&gpuPrim->VAO);
            gpuPrim->VBO.create();
            gpuPrim->VBO.bind();
            gpuPrim->VBO.allocate(vertices, outVertCount * 9 * sizeof(GLfloat));
            f->glEnableVertexAttribArray(0);
            f->glEnableVertexAttribArray(1);
            f->glEnableVertexAttribArray(2);
            f->glEnableVertexAttribArray(3);
            f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), 0);
            f->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), reinterpret_cast<void*>(3 * sizeof(GLfloat)));
            f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), reinterpret_cast<void*>(6 * sizeof(GLfloat)));
            f->glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), reinterpret_cast<void*>(8 * sizeof(GLfloat)));
            gpuPrim->VBO.release();

            delete[] vertices;

            mesh->primitives.push_back(gpuPrim);
        }
    }

    // Build draw units from nodes referencing meshes.
    drawUnits.clear();
    for (int ni = 0; ni < model.nodes.size(); ni++) {
        const int meshIndex = model.nodes[ni].mesh;
        if (meshIndex < 0 || meshIndex >= meshes.size()) continue;
        MeshGpu* mesh = meshes[meshIndex];
        if (mesh == nullptr) continue;
        for (int pi = 0; pi < mesh->primitives.size(); pi++) {
            DrawUnit unit;
            unit.nodeIndex = ni;
            unit.meshIndex = meshIndex;
            unit.primitiveIndex = pi;
            unit.enabled = true;
            drawUnits.push_back(unit);
        }
    }

    // Compute world-space (shape-local) bounds by transforming mesh AABB corners by node world matrices.
    float globalMin[3] = {1e30f, 1e30f, 1e30f};
    float globalMax[3] = {-1e30f, -1e30f, -1e30f};
    bool hasAnyBounds = false;

    for (int ni = 0; ni < model.nodes.size(); ni++) {
        const int meshIndex = model.nodes[ni].mesh;
        if (meshIndex < 0 || meshIndex >= meshes.size()) continue;
        MeshGpu* mesh = meshes[meshIndex];
        if (mesh == nullptr || !mesh->hasBounds) continue;
        if (ni < 0 || ni >= nodeWorldMatrices.size()) continue;
        float* m = nodeWorldMatrices[ni];
        if (m == nullptr) continue;

        const float minx = mesh->min[0], miny = mesh->min[1], minz = mesh->min[2];
        const float maxx = mesh->max[0], maxy = mesh->max[1], maxz = mesh->max[2];

        const float corners[8][3] = {
            {minx, miny, minz}, {minx, miny, maxz}, {minx, maxy, minz}, {minx, maxy, maxz},
            {maxx, miny, minz}, {maxx, miny, maxz}, {maxx, maxy, minz}, {maxx, maxy, maxz}
        };

        for (int c = 0; c < 8; c++) {
            float in[3] = {corners[c][0], corners[c][1], corners[c][2]};
            float out[3];
            Vec3::transformMat4(out, in, m);
            hasAnyBounds = true;
            if (out[0] < globalMin[0]) globalMin[0] = out[0];
            if (out[1] < globalMin[1]) globalMin[1] = out[1];
            if (out[2] < globalMin[2]) globalMin[2] = out[2];
            if (out[0] > globalMax[0]) globalMax[0] = out[0];
            if (out[1] > globalMax[1]) globalMax[1] = out[1];
            if (out[2] > globalMax[2]) globalMax[2] = out[2];
        }
    }

    if (!hasAnyBounds) {
        globalMin[0] = globalMin[1] = globalMin[2] = 0.0f;
        globalMax[0] = globalMax[1] = globalMax[2] = 0.0f;
    }

    bound[0] = globalMax[0];
    bound[1] = globalMin[0];
    bound[2] = globalMax[1];
    bound[3] = globalMin[1];
    bound[4] = globalMax[2];
    bound[5] = globalMin[2];

    const float dx = bound[0] - bound[1];
    const float dy = bound[2] - bound[3];
    const float dz = bound[4] - bound[5];
    size = std::sqrt(dx * dx + dy * dy + dz * dz);

    return true;
}

void GltfShape::syncTextureAddresses() {
    if (loaded != 1) {
        return;
    }

    for (MeshGpu* mesh : meshes) {
        if (mesh == nullptr) continue;
        for (MeshPrimitiveGpu* prim : mesh->primitives) {
            if (prim == nullptr) continue;
            if (!prim->material.hasTexture) continue;
            if (prim->material.texId < 0) continue;

            auto it = TexLib::mtex.find(prim->material.texId);
            Texture* t = (it == TexLib::mtex.end()) ? nullptr : it->second;
            if (t == nullptr) {
                requiresUpdate = true;
                prim->material.texAddr = -1;
                continue;
            }

            if (t->missing || t->error) {
                requiresUpdate = true;
                prim->material.texAddr = -1;
                continue;
            }

            if (!t->loaded) {
                requiresUpdate = true;
                continue;
            }

            if (!t->glLoaded) {
                t->GLTextures();
                requiresUpdate = true;
            }

            if (t->glLoaded && t->tex != nullptr) {
                const unsigned int addr = t->tex[0];
                if (prim->material.texAddr != (int)addr) {
                    prim->material.texAddr = (int)addr;
                    requiresUpdate = true;
                }
            }
        }
    }
}

unsigned long long GltfShape::getTextureStateHash() const {
    unsigned long long hash = 1469598103934665603ULL; // FNV-1a offset basis
    const unsigned long long prime = 1099511628211ULL;

    hash ^= (unsigned long long)(unsigned int)meshes.size();
    hash *= prime;

    for (const MeshGpu* mesh : meshes) {
        if (mesh == nullptr) continue;
        hash ^= (unsigned long long)(unsigned int)mesh->primitives.size();
        hash *= prime;
        for (const MeshPrimitiveGpu* prim : mesh->primitives) {
            if (prim == nullptr) continue;
            hash ^= (unsigned long long)(unsigned int)prim->material.texId;
            hash *= prime;
            hash ^= (unsigned long long)(unsigned int)prim->material.texAddr;
            hash *= prime;
            hash ^= (unsigned long long)(unsigned int)(prim->material.hasTexture ? 1 : 0);
            hash *= prime;
        }
    }

    return hash;
}
