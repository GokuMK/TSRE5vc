#ifndef TSRE_TERRAIN_ADJACENT_EDGE_H
#define TSRE_TERRAIN_ADJACENT_EDGE_H

#include <QVector>
#include <QString>
#include <QtGlobal>

class Terrain;

enum class TerrainEdgeSide { LocalX0, LocalXMax, LocalZ0, LocalZMax };
enum class TerrainEdgeDiscovery { LoadedOnly, LoadDirectNeighbor };

// Absolute metre coordinates. World files still use the fixed 2048 m lattice.
struct TerrainPhysicalBounds {
    qint64 minX, minZ, maxX, maxZ;
    static TerrainPhysicalBounds of(const Terrain &terrain);
};

struct TerrainEdgePoint {
    int alongM = 0;
    float height = 0;
};

struct TerrainSourceLocator {
    int worldX = 0;
    int worldZ = 0;
    QString name;
    bool distantDomain = false;
    TerrainEdgeSide sourceSide = TerrainEdgeSide::LocalX0;
};

struct TerrainEdgeSection {
    TerrainSourceLocator source;
    QVector<TerrainEdgePoint> points;
    int firstAlongM() const { return points.first().alongM; }
    int lastAlongM() const { return points.last().alongM; }
    int nativeSpacing() const;
    // Use a point inside the requested patch interval (half-open ownership).
    int sourcePatchAt(const Terrain &owner, const Terrain &sourceTerrain,
                      int alongM) const;
};

struct TerrainAdjacentEdge {
    enum class Status { Unresolved, Missing, Partial, Complete, Conflict };
    struct Cursor { int section = 0; int point = 0; };
    TerrainEdgeSide side = TerrainEdgeSide::LocalX0;
    int ownerLengthM = 0;
    QVector<TerrainEdgeSection> sections;
    Status status = Status::Unresolved;
    bool dirty = true;
    TerrainEdgeDiscovery discovery = TerrainEdgeDiscovery::LoadedOnly;

    void finish();
    bool sampleHeight(int alongM, float &height) const;
    // Requests must be nondecreasing when reusing a cursor.
    bool sampleHeight(int alongM, float &height, Cursor &cursor) const;
};

#endif
