#include <tsre/world/TerrainLib.h>
#include <tsre/world/Terrain.h>
#include <algorithm>
#include <cmath>
#include <QDebug>

namespace {
int edgePatch(const TerrainGridLayout &layout, TerrainEdgeSide side, int along) {
    const int p = along / layout.patchWorldSize;
    switch (side) {
    case TerrainEdgeSide::LocalX0: return layout.patchIndex(p, 0);
    case TerrainEdgeSide::LocalXMax: return layout.patchIndex(p, layout.patchesPerSide - 1);
    case TerrainEdgeSide::LocalZ0: return layout.patchIndex(0, p);
    case TerrainEdgeSide::LocalZMax: return layout.patchIndex(layout.patchesPerSide - 1, p);
    }
    return -1;
}
quint8 edgeBit(TerrainEdgeSide side) { return quint8(1u << unsigned(side)); }
}

void TerrainLib::prepareTerrainLod(const QVector<Terrain*> &terrains,
                                   const QVector<TerrainLodLevel> &levels,
                                   double cameraX, double cameraZ) {
    clearPreparedTerrainLod();
    if (!std::isfinite(cameraX) || !std::isfinite(cameraZ)) return;
    QVector<Terrain*> participants;
    QVector<TerrainLodTileState> states;
    QHash<Terrain*, int> tileIndex;
    for (Terrain *terrain : terrains) {
        if (terrain == nullptr || !terrain->loaded || terrain->lowTile
                || terrain->terrainData == nullptr || tileIndex.contains(terrain))
            continue;
        const auto &layout = terrain->getGridLayout();
        if (layout.patchWorldSize <= 0) continue;
        const auto bounds = TerrainPhysicalBounds::of(*terrain);
        TerrainLodTileState state;
        state.layout = layout;
        state.gaps = terrain->getPatchGapState();
        bool profileViolation = false;
        state.patches = TerrainLod::buildTileState(layout, levels,
                          float(cameraX - bounds.minX), float(cameraZ - bounds.minZ),
                          state.gaps, &profileViolation, false);
        if (profileViolation && !terrainLodWarnings.contains(terrain->name + "/profile")) {
            terrainLodWarnings.insert(terrain->name + "/profile");
            qWarning() << "Terrain LOD profile creates a greater-than-2:1 patch transition;"
                          " applying conservative refinement" << terrain->name;
        }
        tileIndex.insert(terrain, participants.size());
        participants.append(terrain);
        states.append(std::move(state));
    }

    QVector<TerrainLodConnection> connections;
    for (int index = 0; index < participants.size(); ++index) {
        Terrain &owner = *participants[index];
        const auto &layout = owner.getGridLayout();
        const auto bounds = TerrainPhysicalBounds::of(owner);
        for (auto side : {TerrainEdgeSide::LocalX0, TerrainEdgeSide::LocalXMax,
                          TerrainEdgeSide::LocalZ0, TerrainEdgeSide::LocalZMax}) {
            // Camera movement does not rebuild these vectors. Resolve validates
            // cached identities, rebuilding only for dirty/load/edit changes.
            const auto &edge = resolveAdjacentEdge(owner, side,
                                                    TerrainEdgeDiscovery::LoadedOnly);
            const bool xEdge = side == TerrainEdgeSide::LocalX0
                    || side == TerrainEdgeSide::LocalXMax;
            for (const auto &section : edge.sections) {
                Terrain *other = edgeTerrainAt(section.source.worldX,
                                  section.source.worldZ, false, false);
                const auto found = tileIndex.constFind(other);
                if (found == tileIndex.constEnd() || found.value() <= index)
                    continue; // each shared span once, no recursive neighbour chase
                const auto &otherLayout = other->getGridLayout();
                const auto otherBounds = TerrainPhysicalBounds::of(*other);
                const qint64 originDelta = xEdge ? bounds.minZ - otherBounds.minZ
                                                : bounds.minX - otherBounds.minX;
                int at = std::max(0, section.firstAlongM());
                const int end = std::min(layout.terrainWorldSize, section.lastAlongM());
                // Native sections may cross several patch boundaries, on either
                // side. Split arithmetically; no per-point lookup or stored map.
                while (at < end) {
                    const int firstPatch = edgePatch(layout, side, at);
                    const int secondPatch = section.sourcePatchAt(owner, *other, at);
                    if (!layout.isPatchIndexValid(firstPatch)
                            || !otherLayout.isPatchIndexValid(secondPatch)) break;
                    connections.append({index, firstPatch, edgeBit(side),
                                        found.value(), secondPatch,
                                        edgeBit(section.source.sourceSide)});
                    const qint64 nextOwner = (at / layout.patchWorldSize + 1)
                            * layout.patchWorldSize;
                    const qint64 nextOther = ((at + originDelta) / otherLayout.patchWorldSize + 1)
                            * otherLayout.patchWorldSize - originDelta;
                    const int next = int(std::min({qint64(end), nextOwner, nextOther}));
                    if (next <= at) break;
                    at = next;
                }
            }
        }
    }
    TerrainLod::connectTileStates(states, connections);
    for (int index = 0; index < participants.size(); ++index) {
        const QString key = participants[index]->name + "/boundary";
        if (states[index].bestEffortBoundary && !terrainLodWarnings.contains(key)) {
            terrainLodWarnings.insert(key);
            qWarning() << "Terrain LOD boundary uses best-effort 2:1 stitching:"
                          " larger native ratio or mixed neighbour levels" << participants[index]->name;
        }
        preparedTerrainLod.insert(participants[index], std::move(states[index].patches));
    }
}
