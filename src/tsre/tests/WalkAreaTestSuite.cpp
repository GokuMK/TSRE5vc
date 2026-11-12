/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Walk areas (task humans 04) on made-up stations: a platform 0.55 m above
// the rail with a bench and a shelter roof, over terrain; a halt with
// terrain only. The roadmap's rules: seeds on the platform, bench tops,
// under benches and roofs out, ground closed, the edge zone, ground strips,
// the track's band.

#include "WalkAreaTestSuite.h"
#include "TokenTestSupport.h"
#include <tsre/humans/ShapeTriangles.h>
#include <tsre/humans/WalkArea.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/shape/ShapeMetadata.h>
#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <cmath>
#include <memory>

namespace {

using Humans::WalkArea;

// A box's twelve triangles, its role.
void addBox(WalkArea::Input &in, float x0, float y0, float z0, float x1, float y1, float z1, WalkArea::Role role) {
    const int base = int(in.vertices.size() / 3);
    const float corners[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1},
                                 {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}};
    for (const auto &c : corners)
        in.vertices.insert(in.vertices.end(), c, c + 3);
    const int faces[6][4] = {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1}, {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}};
    for (const auto &f : faces) {
        for (int t : {0, 1, 2, 0, 2, 3})
            in.triangles.push_back(base + f[t]);
        in.roles.push_back(role);
        in.roles.push_back(role);
    }
}

// A level square of terrain, `cells` a side.
void addGround(WalkArea::Input &in, float x0, float z0, float x1, float z1, float y, int cells) {
    const int base = int(in.vertices.size() / 3);
    for (int j = 0; j <= cells; ++j)
        for (int i = 0; i <= cells; ++i) {
            in.vertices.push_back(x0 + (x1 - x0) * float(i) / float(cells));
            in.vertices.push_back(y);
            in.vertices.push_back(z0 + (z1 - z0) * float(j) / float(cells));
        }
    for (int j = 0; j < cells; ++j)
        for (int i = 0; i < cells; ++i) {
            const int a = base + j * (cells + 1) + i, b = a + 1, c = a + cells + 1, d = c + 1;
            for (int v : {a, c, b, b, c, d})
                in.triangles.push_back(v);
            in.roles.push_back(WalkArea::Role::Ground);
            in.roles.push_back(WalkArea::Role::Ground);
        }
}

// What the mesh has at a point: the walked area there (none: -1), looking
// `reach` across and half a metre up and down.
int areaAt(const WalkArea::Built &built, float x, float y, float z, float reach = 0.1f) {
    if (!built.mesh)
        return -1;
    dtNavMeshQuery query;
    if (dtStatusFailed(query.init(built.mesh.get(), 256)))
        return -1;
    dtQueryFilter filter;
    filter.setIncludeFlags(WalkArea::FlagWalk);
    const float point[3] = {x, y, z}, extents[3] = {reach, 0.5f, reach};
    dtPolyRef ref = 0;
    float nearest[3];
    if (dtStatusFailed(query.findNearestPoly(point, extents, &filter, &ref, nearest)) || ref == 0)
        return -1;
    // The nearest point must be the point itself (not one beside it).
    if (std::fabs(nearest[0] - x) > reach || std::fabs(nearest[2] - z) > reach)
        return -1;
    unsigned char area = 0;
    built.mesh->getPolyArea(ref, &area);
    return area;
}

}

namespace TsreTests {

int runWalkAreaSuite(bool verbose) {
    TokenTest::Suite test{"[tests:walk-areas]", verbose};

    // A station: the track along x at the rail's top (y 0), the platform on
    // its +z side 1.6 to 6.6 m off it, 0.55 m high, 80 m long; a bench
    // (seat 0.45 m above the platform, 5 cm thick, open below), a shelter
    // roof 2.6 m above it; terrain 0.3 m below the rail all around.
    {
        WalkArea::Input in;
        addBox(in, -40.0f, -0.3f, 1.6f, 40.0f, 0.55f, 6.6f, WalkArea::Role::Floor);
        addBox(in, -0.8f, 1.0f, 3.8f, 0.8f, 1.05f, 4.25f, WalkArea::Role::Floor);
        addBox(in, 10.0f, 3.15f, 2.5f, 20.0f, 3.25f, 6.0f, WalkArea::Role::Floor);
        addGround(in, -60.0f, -30.0f, 60.0f, 30.0f, -0.3f, 24);
        WalkArea::Platform platform;
        platform.line = {-40.0f, 0.0f, 0.0f, 40.0f, 0.0f, 0.0f};
        platform.side = 1.0f;
        in.platforms.push_back(platform);
        in.tracks.push_back({-60.0f, 0.0f, 0.0f, 60.0f, 0.0f, 0.0f});
        const WalkArea::Built built = WalkArea::build(in);
        test.check(built.ok, QString("built: %1 polygons, %2 reached, %3 x %4 cells, %5 ms %6").arg(built.polygons)
                   .arg(built.reached).arg(built.cells[0]).arg(built.cells[1]).arg(built.milliseconds, 0, 'f', 1)
                   .arg(built.error));
        const WalkArea::Built::PlatformSeeds seeds = built.platforms.empty() ? WalkArea::Built::PlatformSeeds()
                                                                              : built.platforms.front();
        bool heights = true;
        for (const WalkArea::Seed &seed : built.seeds)
            heights = heights && std::fabs(seed.position[1] - 0.55f) < 0.15f;
        test.check(seeds.floor == 40 && seeds.ground == 0 && seeds.missed == 0 && heights,
                   QString("40 seeds on the platform's top (floor %1, ground %2, missed %3)")
                   .arg(seeds.floor).arg(seeds.ground).arg(seeds.missed));
        test.check(areaAt(built, -20.0f, 0.55f, 4.5f) == WalkArea::AreaFloor, "the platform: floor");
        test.check(areaAt(built, -20.0f, 0.55f, 2.0f) == WalkArea::AreaEdge, "1.95 m off the track: the edge zone");
        test.check(areaAt(built, -20.0f, 0.55f, 1.3f) < 0, "over the platform's edge: nothing");
        test.check(areaAt(built, 0.0f, 0.55f, 4.0f, 0.05f) < 0, "under the bench: nothing (no headroom)");
        test.check(areaAt(built, 0.0f, 1.05f, 4.0f, 0.05f) < 0, "the bench's seat: not walked (too high a step)");
        test.check(areaAt(built, 15.0f, 3.25f, 4.0f) < 0, "the shelter's roof: not walked (no seed reaches it)");
        test.check(areaAt(built, 15.0f, 0.55f, 4.0f) == WalkArea::AreaFloor, "under the shelter: floor");
        test.check(areaAt(built, -20.0f, -0.3f, 12.0f) < 0, "the ground behind the platform: closed");
        test.check(areaAt(built, -20.0f, -0.3f, -5.0f) < 0 && areaAt(built, -20.0f, 0.0f, 0.0f, 0.5f) < 0,
                   "the track and beyond it: closed");
        int dropped = 0;
        for (unsigned char area : built.areas)
            dropped += (area & WalkArea::DroppedFlag) != 0;
        test.check(dropped > 0 && built.reached < built.polygons,
                   QString("the bench's seat and the roof are dropped (%1 triangles)").arg(dropped));
    }

    // A halt: terrain only, 0.3 m below the rail. Its seeds land on the
    // ground; a strip 1.7 to 4.2 m off the track is walked, nothing beyond.
    {
        WalkArea::Input in;
        addGround(in, -60.0f, -30.0f, 60.0f, 30.0f, -0.3f, 24);
        WalkArea::Platform platform;
        platform.line = {-20.0f, 0.0f, 0.0f, 20.0f, 0.0f, 0.0f};
        platform.side = 1.0f;
        in.platforms.push_back(platform);
        in.tracks.push_back({-60.0f, 0.0f, 0.0f, 60.0f, 0.0f, 0.0f});
        const WalkArea::Built built = WalkArea::build(in);
        const WalkArea::Built::PlatformSeeds seeds = built.platforms.empty() ? WalkArea::Built::PlatformSeeds()
                                                                              : built.platforms.front();
        test.check(built.ok && seeds.ground == 20 && seeds.floor == 0,
                   QString("a halt: %1 seeds on the ground, %2 ms").arg(seeds.ground).arg(built.milliseconds, 0, 'f', 1));
        test.check(areaAt(built, 0.0f, -0.3f, 3.0f) == WalkArea::AreaStrip, "its strip: walked");
        test.check(areaAt(built, 0.0f, -0.3f, 7.0f) < 0 && areaAt(built, 30.0f, -0.3f, 3.0f) < 0,
                   "past the strip and past the platform's end: closed");
    }

    // The other side: the same platform on the track's -z side (side -1).
    {
        WalkArea::Input in;
        addBox(in, -40.0f, -0.3f, -6.6f, 40.0f, 0.55f, -1.6f, WalkArea::Role::Floor);
        addGround(in, -60.0f, -30.0f, 60.0f, 30.0f, -0.3f, 24);
        WalkArea::Platform platform;
        platform.line = {-40.0f, 0.0f, 0.0f, 40.0f, 0.0f, 0.0f};
        platform.side = -1.0f;
        in.platforms.push_back(platform);
        in.tracks.push_back({-60.0f, 0.0f, 0.0f, 60.0f, 0.0f, 0.0f});
        const WalkArea::Built built = WalkArea::build(in);
        test.check(built.ok && !built.platforms.empty() && built.platforms.front().floor == 40
                   && areaAt(built, 0.0f, 0.55f, -4.5f) == WalkArea::AreaFloor,
                   "a platform on the other side: found and walked");
    }

    // A platform with its side set wrong: its seeds find the platform on
    // the other side and take it.
    {
        WalkArea::Input in;
        addBox(in, -40.0f, -0.3f, 1.6f, 40.0f, 0.55f, 6.6f, WalkArea::Role::Floor);
        addGround(in, -60.0f, -30.0f, 60.0f, 30.0f, -0.3f, 24);
        WalkArea::Platform platform;
        platform.line = {-40.0f, 0.0f, 0.0f, 40.0f, 0.0f, 0.0f};
        platform.side = -1.0f;
        in.platforms.push_back(platform);
        in.tracks.push_back({-60.0f, 0.0f, 0.0f, 60.0f, 0.0f, 0.0f});
        const WalkArea::Built built = WalkArea::build(in);
        test.check(built.ok && !built.platforms.empty() && built.platforms.front().swapped
                   && built.platforms.front().floor == 40 && areaAt(built, 0.0f, 0.55f, 4.5f) == WalkArea::AreaFloor,
                   "a platform with its side set wrong: the other side taken");
    }

    // A shape's own role: ESD_TSREWalk in its .sd; TSRE_walk in a glTF
    // scene's extras, its triangles read on the CPU with their nodes'
    // places.
    {
        QTemporaryDir dir;
        QFile sd(dir.filePath("bench.sd"));
        if (sd.open(QIODevice::WriteOnly))
            sd.write("SIMISA@@@@@@@@@@JINX0t1t______\n\nShape ( bench.s\n\tESD_Detail_Level ( 0 )\n"
                     "\tESD_TSREWalk ( Solid )\n)\n");
        sd.close();
        ShapeMetadata metadata;
        ShapeMetadata::read(dir.filePath("bench.s"), metadata);
        test.check(metadata.walk == "Solid", QString("ESD_TSREWalk read: %1").arg(metadata.walk));

        // One triangle, its node 10 m up and 5 m along x.
        const float points[9] = {0, 0, 0, 1, 0, 0, 0, 0, 1};
        const QByteArray data = QByteArray(reinterpret_cast<const char *>(points), sizeof(points)).toBase64();
        const QByteArray gltf = QByteArray("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
                "\"scenes\":[{\"nodes\":[0],\"extras\":{\"TSRE_walk\":\"Ignore\"}}],"
                "\"nodes\":[{\"mesh\":0,\"translation\":[5,10,0]}],"
                "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
                "\"buffers\":[{\"byteLength\":36,\"uri\":\"data:application/octet-stream;base64,") + data
                + "\"}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":36}],"
                "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
                "\"min\":[0,0,0],\"max\":[1,0,1]}]}";
        QFile file(dir.filePath("roof.gltf"));
        if (file.open(QIODevice::WriteOnly))
            file.write(gltf);
        file.close();
        std::vector<float> triangles;
        QString walk;
        const bool read = GltfShape::readTriangles(dir.filePath("roof.gltf"), dir.path(), triangles, &walk);
        float lowY = 1e9f, lowX = 1e9f;
        for (size_t i = 0; i + 2 < triangles.size(); i += 3) {
            lowX = std::min(lowX, triangles[i]);
            lowY = std::min(lowY, triangles[i + 1]);
        }
        test.check(read && triangles.size() == 9 && walk == "Ignore" && std::fabs(lowY - 10.0f) < 1e-4f
                   && std::fabs(std::fabs(lowX) - 5.0f) < 1e-4f,
                   QString("a glTF file's triangles on the CPU: %1 floats, its node's place (%2, %3), TSRE_walk %4")
                   .arg(triangles.size()).arg(double(lowX)).arg(double(lowY)).arg(walk));
        const std::shared_ptr<const Humans::ShapeTriangles> cached =
                Humans::shapeTriangles(dir.filePath("roof.gltf"), dir.path());
        test.check(cached->read && cached->triangles.size() == 9 && cached->walk == "Ignore"
                   && Humans::shapeTriangles(dir.filePath("roof.gltf"), dir.path()) == cached,
                   "the triangle reader keeps a file once read");
        Humans::forgetShapeTriangles();
    }
    return test.finish();
}

} // namespace TsreTests
