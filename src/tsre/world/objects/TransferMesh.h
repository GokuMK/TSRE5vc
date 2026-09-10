#pragma once
#include <QPointer>
#include <QVector>
#include <tsre/world/TerrainLod.h>
#include <array>

class Terrain;
class TerrainLib;

// CPU-only clipping and dependency cache, independent of texture and GL state.
class TransferMesh {
public:
    struct Vertex { double x, y, z, nx=0, ny=1, nz=0, u=0, v=0; };
    struct Rectangle {
        double width=0, height=0, cosine=1, sine=0;
        static Rectangle fromQuaternion(double width, double height, const float *q);
        bool valid() const;
        void uv(Vertex &vertex) const;
    };
    static void clipTriangle(const Rectangle &rect, std::array<Vertex,3> triangle,
                             float alpha, QVector<float> &vertices, QVector<float> &outline);
    // Disjoint ground/hole outputs, in the same VNTA format. On a cache hit
    // returns false and leaves both caller-owned vectors untouched.
    bool update(TerrainLib *lib, int worldX, int worldZ, const float *position,
                double width, double height, const float *quaternion, float alpha,
                QVector<float> &vertices, QVector<float> &holeVertices,
                bool respectTerrainHoles = false);
    void invalidate() { validCache=false; }
    const QVector<float> &outline() const { return boundary; }
private:
    struct Patch {
        QPointer<Terrain> terrain;
        quint64 revision;
        int id;
        float originX, originZ;
        TerrainPatchLodState lod;
        bool hidden;
        bool operator==(const Patch &other) const;
    };
    bool validCache=false;
    QVector<Patch> sources;
    std::array<double,10> placement{};
    QVector<float> boundary;
};
