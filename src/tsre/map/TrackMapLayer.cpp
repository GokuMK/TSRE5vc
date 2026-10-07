/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "TrackMapLayer.h"
#include "MapPalette.h"
#include "MapView.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QOpenGLFunctions>
#include <algorithm>
#include <cmath>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRnode.h>

namespace {

// Geometry is rebuilt when the scale changed by more than this factor, so
// lines keep about their width in pixels.
constexpr float RebuildScale = 1.25f;

// x, y, z of a database position relative to a view tile (the editor's tile
// convention: z tiles are negated).
void relative(float *out, int tileX, int tileZ, float x, float y, float z, int viewTileX,
              int viewTileZ) {
    out[0] = float(tileX - viewTileX) * 2048.0f + x;
    out[1] = y;
    out[2] = float(-tileZ - viewTileZ) * 2048.0f - z;
}

void setColour(OglObj &object, const QColor &colour) {
    object.setMaterial(float(colour.redF()), float(colour.greenF()), float(colour.blueF()));
}

}

struct TrackMapLayer::Geometry {
    std::vector<float> lines;
    std::vector<float> junctions;
    std::vector<float> ends;
};

TrackMapLayer::TrackMapLayer()
    : trackLines(std::make_unique<OglObj>()), roadLines(std::make_unique<OglObj>()),
      junctions(std::make_unique<OglObj>()), ends(std::make_unique<OglObj>()) {}

TrackMapLayer::~TrackMapLayer() = default;

void TrackMapLayer::appendRibbons(std::vector<float> &out, const float *segments, int segmentCount,
                                  float width) {
    const float half = 0.5f * width;
    for (int i = 0; i < segmentCount; ++i) {
        const float *a = segments + i * 6;
        const float *b = a + 3;
        float dx = b[0] - a[0];
        float dz = b[2] - a[2];
        const float length = std::sqrt(dx * dx + dz * dz);
        if (length < 1e-4f)
            continue;
        // Sideways on the ground, half the width.
        const float sx = -dz / length * half;
        const float sz = dx / length * half;
        const float quad[4][3] = {{a[0] - sx, a[1], a[2] - sz},
                                  {a[0] + sx, a[1], a[2] + sz},
                                  {b[0] + sx, b[1], b[2] + sz},
                                  {b[0] - sx, b[1], b[2] - sz}};
        for (int corner : {0, 1, 2, 0, 2, 3})
            out.insert(out.end(), quad[corner], quad[corner] + 3);
    }
}

void TrackMapLayer::appendSquare(std::vector<float> &out, float x, float y, float z, float side,
                                 float rx, float rz, float ux, float uz) {
    const float h = 0.5f * side;
    const float corners[4][3] = {{x + (-rx - ux) * h, y, z + (-rz - uz) * h},
                                 {x + (rx - ux) * h, y, z + (rz - uz) * h},
                                 {x + (rx + ux) * h, y, z + (rz + uz) * h},
                                 {x + (-rx + ux) * h, y, z + (-rz + uz) * h}};
    for (int corner : {0, 1, 2, 0, 2, 3})
        out.insert(out.end(), corners[corner], corners[corner] + 3);
}

void TrackMapLayer::appendOctagon(std::vector<float> &out, float x, float y, float z, float width) {
    // Corners at 22.5 degrees and every 45 degrees from there, the flats a
    // width apart.
    const float radius = 0.5f * width / std::cos(0.39269908f);
    float corners[8][3];
    for (int i = 0; i < 8; ++i) {
        const float angle = 0.39269908f + i * 0.78539816f;
        corners[i][0] = x + radius * std::cos(angle);
        corners[i][1] = y;
        corners[i][2] = z + radius * std::sin(angle);
    }
    for (int i = 1; i < 7; ++i)
        for (int corner : {0, i, i + 1})
            out.insert(out.end(), corners[corner], corners[corner] + 3);
}

void TrackMapLayer::buildDatabase(Geometry &geometry, const MapView &view, TDB *database,
                                  bool detail, int minTileX, int maxTileX, int minTileZ,
                                  int maxTileZ, float lineHeight) {
    if (database == nullptr || !database->loaded)
        return;
    const float width = LinePixels * view.metresPerPixel;
    const float marker = MarkerPixels * view.metresPerPixel;
    std::vector<float> segments;
    auto inView = [&](int tileX, int tileZ) {
        // Database tiles are not negated; the view's are.
        return tileX >= minTileX && tileX <= maxTileX && -tileZ >= minTileZ && -tileZ <= maxTileZ;
    };
    for (int id = 1; id <= database->iTRnodes; ++id) {
        auto found = database->trackNodes.find(id);
        if (found == database->trackNodes.end() || found->second == nullptr)
            continue;
        const TRnode *node = found->second;
        if (node->typ == 0 || node->typ == 2) {
            float p[3];
            relative(p, node->uid.tileX, node->uid.tileZ, node->uid.x, node->uid.y, node->uid.z,
                     view.tileX, view.tileZ);
            appendOctagon(node->typ == 2 ? geometry.junctions : geometry.ends, p[0],
                          node->typ == 2 ? JunctionHeight : EndHeight, p[2], marker);
            continue;
        }
        if (node->typ != 1 || node->iTrv < 1 || node->trVectorSection == nullptr)
            continue;
        bool nodeInView = false;
        if (detail)
            for (int i = 0; i < node->iTrv && !nodeInView; ++i)
                nodeInView = inView(node->trVectorSection[i].tileX, node->trVectorSection[i].tileZ);
        segments.clear();
        if (nodeInView) {
            // The curves of the whole node: pairs of points of six floats.
            float *buffer = nullptr;
            int length = 0;
            database->getVectorSectionLine(buffer, length, view.tileX, view.tileZ, id);
            for (int i = 0; i + 12 <= length; i += 12) {
                segments.insert(segments.end(), buffer + i, buffer + i + 3);
                segments.insert(segments.end(), buffer + i + 6, buffer + i + 9);
            }
            delete[] buffer;
        } else {
            float a[3], b[3];
            for (int i = 0; i < node->iTrv; ++i) {
                const TrackVectorSection &s = node->trVectorSection[i];
                relative(a, s.tileX, s.tileZ, s.x, s.y, s.z, view.tileX, view.tileZ);
                if (i + 1 < node->iTrv) {
                    const TrackVectorSection &n = node->trVectorSection[i + 1];
                    relative(b, n.tileX, n.tileZ, n.x, n.y, n.z, view.tileX, view.tileZ);
                } else {
                    auto next = database->trackNodes.find(node->pins[1].link);
                    if (node->pins[1].link == 0 || next == database->trackNodes.end()
                            || next->second == nullptr)
                        continue;
                    const TrackNodeUid &uid = next->second->uid;
                    relative(b, uid.tileX, uid.tileZ, uid.x, uid.y, uid.z, view.tileX, view.tileZ);
                }
                segments.insert(segments.end(), a, a + 3);
                segments.insert(segments.end(), b, b + 3);
            }
        }
        for (size_t i = 1; i < segments.size(); i += 3)
            segments[i] = lineHeight;
        appendRibbons(geometry.lines, segments.data(), int(segments.size() / 6), width);
    }
}

void TrackMapLayer::build(const MapView &view, const MapPalette &palette, TDB *track, TDB *road) {
    static const bool trace = qEnvironmentVariableIsSet("TSRE_MAP_TRACE");
    QElapsedTimer timer;
    timer.start();
    const bool detail = view.metresPerPixel <= DetailMetresPerPixel;
    // Tiles in view, with one more around, in the view's tile convention.
    float corners[4][2];
    const float screen[4][2] = {{0, 0}, {float(view.width), 0}, {0, float(view.height)},
                                {float(view.width), float(view.height)}};
    for (int c = 0; c < 4; ++c)
        view.groundAt(screen[c][0], screen[c][1], corners[c][0], corners[c][1]);
    float lowX = corners[0][0], highX = lowX, lowZ = corners[0][1], highZ = lowZ;
    for (int c = 1; c < 4; ++c) {
        lowX = std::min(lowX, corners[c][0]);
        highX = std::max(highX, corners[c][0]);
        lowZ = std::min(lowZ, corners[c][1]);
        highZ = std::max(highZ, corners[c][1]);
    }
    auto tileOf = [](float metres) { return int(std::floor((metres + 1024.0f) / 2048.0f)); };
    const int tiles[4] = {view.tileX + tileOf(lowX) - 1, view.tileX + tileOf(highX) + 1,
                          view.tileZ + tileOf(lowZ) - 1, view.tileZ + tileOf(highZ) + 1};

    Geometry trackGeometry, roadGeometry;
    buildDatabase(trackGeometry, view, track, detail, tiles[0], tiles[1], tiles[2], tiles[3],
                  TrackHeight);
    buildDatabase(roadGeometry, view, road, detail, tiles[0], tiles[1], tiles[2], tiles[3],
                  RoadHeight);
    trackGeometry.junctions.insert(trackGeometry.junctions.end(), roadGeometry.junctions.begin(),
                                   roadGeometry.junctions.end());
    trackGeometry.ends.insert(trackGeometry.ends.end(), roadGeometry.ends.begin(),
                              roadGeometry.ends.end());
    auto upload = [](OglObj &object, std::vector<float> &vertices, const QColor &colour) {
        setColour(object, colour);
        object.init(vertices.data(), int(vertices.size()), RenderItem::V, GL_TRIANGLES);
    };
    const qint64 geometryNs = timer.nsecsElapsed();
    const size_t floats = trackGeometry.lines.size() + roadGeometry.lines.size()
            + trackGeometry.junctions.size() + trackGeometry.ends.size();
    upload(*trackLines, trackGeometry.lines, palette.track);
    upload(*roadLines, roadGeometry.lines, palette.road);
    upload(*junctions, trackGeometry.junctions, palette.junction);
    upload(*ends, trackGeometry.ends, palette.end);

    valid = true;
    builtTileX = view.tileX;
    builtTileZ = view.tileZ;
    builtMetresPerPixel = view.metresPerPixel;
    builtDetail = detail;
    std::copy(tiles, tiles + 4, builtTiles);
    builtPalette = palette.name;
    if (trace)
        qInfo().noquote() << "map-trace build" << (detail ? "detail" : "chords") << "m/px"
                          << view.metresPerPixel << "geometry ms" << geometryNs / 1e6 << "total ms"
                          << timer.nsecsElapsed() / 1e6 << "vertices" << floats / 3;
}

void TrackMapLayer::pushRenderItems(RenderQueue &queue, const MapView &view,
                                    const MapPalette &palette, TDB *track, TDB *road) {
    const float scale = view.metresPerPixel / std::max(builtMetresPerPixel, 1e-6f);
    const bool detail = view.metresPerPixel <= DetailMetresPerPixel;
    bool rebuild = !valid || view.tileX != builtTileX || view.tileZ != builtTileZ
            || scale > RebuildScale || scale < 1.0f / RebuildScale
            || detail != builtDetail
            || palette.name != builtPalette;
    if (!rebuild && detail) {
        // Zoomed in, the curves follow the tiles in view.
        float gx, gz;
        view.groundAt(0.5f * view.width, 0.5f * view.height, gx, gz);
        const int centreTileX = view.tileX + int(std::floor((gx + 1024.0f) / 2048.0f));
        const int centreTileZ = view.tileZ + int(std::floor((gz + 1024.0f) / 2048.0f));
        rebuild = centreTileX <= builtTiles[0] || centreTileX >= builtTiles[1]
                || centreTileZ <= builtTiles[2] || centreTileZ >= builtTiles[3];
    }
    if (rebuild)
        build(view, palette, track, road);
    for (OglObj *object : {roadLines.get(), trackLines.get(), ends.get(), junctions.get()})
        object->pushRenderItem(queue);
}
