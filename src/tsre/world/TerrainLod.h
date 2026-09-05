/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef TERRAINLOD_H
#define TERRAINLOD_H

#include <QVector>
#include <QString>
#include <QtGlobal>
#include <tsre/world/TerrainGridLayout.h>

struct TerrainGridLayout;

struct TerrainLodLevel {
    int sampleSpacing = 0;
    int preferredEndDistance = 0;

    bool operator==(const TerrainLodLevel &other) const {
        return sampleSpacing == other.sampleSpacing
                && preferredEndDistance == other.preferredEndDistance;
    }
};

struct TerrainPatchLodState {
    int sourceStep = 1;
    int effectiveSampleSpacing = 0;
    quint8 edgeMask = 0;
};

// Transient selection state: no height/vertex storage and no camera-dependent
// changes to the native adjacent-edge cache.
struct TerrainLodTileState {
    TerrainGridLayout layout;
    QVector<TerrainPatchLodState> patches;
    QVector<quint8> gaps;
    bool bestEffortBoundary = false;
};

struct TerrainLodConnection {
    int firstTile, firstPatch;
    quint8 firstEdge;
    int secondTile, secondPatch;
    quint8 secondEdge;
};

class TerrainLod {
public:
    enum Edge : quint8 {
        LocalX0 = 1u << 0,
        LocalXMax = 1u << 1,
        LocalZ0 = 1u << 2,
        LocalZMax = 1u << 3
    };

    static constexpr int MaximumSampleSpacing = 32;

    static const QVector<TerrainLodLevel> &defaultProfile();
    static QString profileSummary(const QVector<TerrainLodLevel> &levels);
    static bool validateProfile(const QVector<TerrainLodLevel> &levels,
                                QString *error = nullptr,
                                bool allowEmpty = true);
    static int requestedSampleSpacing(
            const QVector<TerrainLodLevel> &levels,
            float squaredDistance);
    static QVector<int> availableSourceSteps(const TerrainGridLayout &layout);
    static int sourceStepForRequest(const TerrainGridLayout &layout,
                                    int requestedSpacing);
    static QVector<TerrainPatchLodState> buildTileState(
            const TerrainGridLayout &layout,
            const QVector<TerrainLodLevel> &levels,
            float cameraLocalX, float cameraLocalZ,
            const QVector<quint8> &patchHasGap,
            bool *profileViolation = nullptr,
            bool pinOuterRing = true);
    static bool refineTileState(const TerrainGridLayout &layout,
                                QVector<TerrainPatchLodState> &patches);
    static void connectTileStates(QVector<TerrainLodTileState> &tiles,
                                 const QVector<TerrainLodConnection> &connections);
};

#endif // TERRAINLOD_H
