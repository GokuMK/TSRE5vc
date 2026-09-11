#pragma once
// Private implementation shared by the CPU and GL translation units.
#include "SFileComplex.h"
#include "SFileDocument.h"
#include <QMap>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLVertexArrayObject>
#include <QPointer>
#include <QQuaternion>
#include <QSet>
#include <QVector2D>
#include <QVector3D>
#include <array>
#include <tsre/renderer/RenderItem.h>
#include <vector>
struct SFileComplex::Data {
    struct Matrix {
        QString name;
        QMatrix4x4 transform;
    };
    struct Image {
        QString name;
        int id = -1;
        int address = -1;
    };
    struct Material {
        int image = -1, matrix = 0, light = 0;
        float alpha = 0;
        bool alphaTest = false;
    };
    struct Vertex {
        int point = -1, normal = -1, uv = -1;
    };
    struct Part {
        int material = 0;
        unsigned int uid = 0;
        std::vector<int> indices;
        int offset = 0, count = 0;
        unsigned int mode = 0x0004;
    };
    struct Gpu {
        QOpenGLBuffer vbo;
        QOpenGLVertexArrayObject vao;
        int bytes = 0;
    };
    struct Mesh {
        int subobject = 0, vertexCount = 0;
        std::vector<int> geometryMap;
        std::vector<Vertex> vertices;
        std::vector<Part> parts;
        std::unique_ptr<Gpu> gpu;
    };
    struct Lod {
        float distance = 0;
        int control = 0, sourceLevel = 0;
        std::vector<int> parents;
        std::vector<int> matrixOrder;
        std::vector<Mesh> meshes;
    };
    struct Key {
        float frame = 0;
        QVector3D position;
        QQuaternion rotation;
    };
    struct Channel {
        std::vector<Key> position, rotation;
    };
    struct Animation {
        float frames = 0, fps = 0;
        std::vector<Channel> channels;
    };
    struct State {
        bool animated = false, dirty = true, namesDirty = true;
        float frame = 0;
        int lod = 0;
        quint32 enabled = 0xffffffff;
        QMap<QString, bool> names;
        QSet<unsigned int> disabledParts;
        QSet<int> disabledSubs;
        std::vector<QMatrix4x4> matrices;
        std::vector<std::unique_ptr<RenderItem>> packets;
    };
    QString path, name, textureRoot, texturePath;
    LoadOptions options;
    qint64 loadDocumentBytes = 0, sourceBlocks = 0, sourceScalars = 0, skippedSourceBlocks = 0;
    double readMs = 0, extractMs = 0, metadataMs = 0, cleanupMs = 0;
    bool sourceAvailable = false; // Required CPU arrays survive until successful GL upload.
    bool loaded = false, attempted = false, snap = false, edited = false;
    Retention retention = Retention::Unloaded;
    Health health = Health::Valid;
    GpuState gpuState = GpuState::NotInitialized;
    int detail = -1, alternative = 0;
    float size = 0;
    float bound[6] = {};
    std::vector<std::array<float, 6>> boxes;
    QStringList diagnostics;
    std::unique_ptr<SFileDetail::Document> document, metadata;
    std::vector<QVector3D> points, normals;
    std::vector<QVector2D> uvs;
    std::vector<Matrix> matrices;
    std::vector<Image> images;
    std::vector<Material> materials;
    std::vector<Lod> lods;
    std::vector<Animation> animations;
    std::vector<State> states{1};
    QPointer<QOpenGLContext> context;
};
