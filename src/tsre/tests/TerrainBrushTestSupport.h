#ifndef TERRAINBRUSHTESTSUPPORT_H
#define TERRAINBRUSHTESTSUPPORT_H
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainInfo.h>
#include <tsre/world/TerrainLibQt.h>
#include <tsre/world/QuadTree.h>
#include <QRect>
#include <QDataStream>
#include <QIODevice>
#include <cstring>
#include <vector>

namespace TsreTests {
class BrushTestLibrary : public TerrainLibQt {
public:
    QHash<Terrain*, QRect> dirty;
    QSet<Terrain*> updates;
    void clearDiagnostics() { dirty.clear(); updates.clear(); }
    void terrainSamplesChanged(Terrain *t, int x0, int z0, int x1, int z1,
                                unsigned int reasons) override {
        const QRect rect(x0, z0, x1 - x0 + 1, z1 - z0 + 1);
        dirty[t] = dirty.contains(t) ? dirty[t].united(rect) : rect;
        TerrainLibQt::terrainSamplesChanged(t, x0, z0, x1, z1, reasons);
    }
    void updateTerrainHeightmap(Terrain *t) override { updates.insert(t); }
    ~BrushTestLibrary() override {
        for (auto *registry : {&terrainQt, &terrainQtLo}) {
            for (auto *info : *registry) {
                if (info == nullptr) continue;
                delete info->t;
                delete info;
            }
            registry->clear();
        }
        delete quadTree;
        delete quadTreeLo;
    }
};
inline std::vector<float> brushHeights(Terrain &t) {
    const int side = t.getSampleCount() + 1;
    std::vector<float> result(std::size_t(side) * side);
    for (int z = 0; z < side; ++z)
        std::memcpy(result.data() + std::size_t(z) * side, t.terrainData[z], side * sizeof(float));
    return result;
}
inline bool sameBrushHeights(Terrain &t, const std::vector<float> &expected) {
    const int side = t.getSampleCount() + 1;
    if (expected.size() != std::size_t(side) * side) return false;
    for (int z = 0; z < side; ++z)
        if (std::memcmp(expected.data() + std::size_t(z) * side,
                        t.terrainData[z], side * sizeof(float)) != 0) return false;
    return true;
}
inline void restoreBrushHeights(Terrain &t, const std::vector<float> &heights) {
    const int side = t.getSampleCount() + 1;
    for (int z = 0; z < side; ++z)
        std::memcpy(t.terrainData[z], heights.data() + std::size_t(z) * side, side * sizeof(float));
    t.refreshAll();
    t.refreshModified();
    for (int i = 0; i < t.getGridLayout().patchRecordCount(); ++i) t.setPatchErrorBias(i, 19);
    for (auto &edge : t.adjacentEdges) edge.dirty = true;
    t.setModified(false);
}
inline QByteArray brushDescriptor(Terrain &t) {
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    t.saveTfileToStream(out);
    return bytes;
}
}
#endif
