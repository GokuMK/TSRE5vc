#include "LocalLightsTestSuite.h"

#include <QDebug>
#include <QImage>
#include <cmath>
#include <tsre/renderer/EmissiveEmitters.h>
#include <tsre/renderer/LightGrid.h>

namespace {

// Two triangles of a w x h quad in the y = 0 plane at x0, z0, with texture
// coordinates across it (stride 5: position, then u, v).
void addQuad(std::vector<float> &vertices, float x0, float z0, float w, float h) {
    const float corners[4][5] = {{x0, 0, z0, 0, 0}, {x0 + w, 0, z0, 1, 0},
                                 {x0 + w, 0, z0 + h, 1, 1}, {x0, 0, z0 + h, 0, 1}};
    for (int index : {0, 1, 2, 0, 2, 3})
        vertices.insert(vertices.end(), corners[index], corners[index] + 5);
}

float totalIntensity(const QVector<RenderItem::Light> &lights) {
    float total = 0.0f;
    for (const RenderItem::Light &light : lights)
        total += light.intensity * std::max({light.color[0], light.color[1], light.color[2]});
    return total;
}

}

int TsreTests::runLocalLightsSuite(bool verbose) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name) {
        if (ok) {
            ++passed;
            if (verbose) qInfo() << "[tests:local-lights] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:local-lights] FAIL" << name;
        }
    };

    // Range: where pi * brightness / d^2 falls to 1/512.
    check(std::abs(LightGrid::rangeFor(1.0f, 0.0f) - std::sqrt(512.0f * float(M_PI))) < 1e-3f,
          "a light of brightness 1 reaches about 40 m");
    check(LightGrid::rangeFor(1e6f, 0.0f) == 250.0f && LightGrid::rangeFor(0.0f, 0.0f) == 0.0f,
          "ranges are limited, and dark lights reach nowhere");
    // The default reach caps strong lights only: a weak glow fades sooner.
    check(LightGrid::rangeFor(1e6f, 0.0f, LightGrid::DefaultReach) == LightGrid::DefaultReach
          && LightGrid::rangeFor(0.05f, 0.33f, LightGrid::DefaultReach) == LightGrid::rangeFor(0.05f, 0.33f),
          "the default reach caps strong lights and leaves weak ones");

    const float eye[3] = {1000.0f, 50.0f, -2000.0f};
    auto light = [](float x, float y, float z, float brightness, float range) {
        LightGrid::Light result;
        result.position[0] = x; result.position[1] = y; result.position[2] = z;
        result.color[0] = result.color[1] = result.color[2] = brightness;
        result.range = range;
        return result;
    };
    LightGrid grid;
    grid.build({light(1010.0f, 50.0f, -1990.0f, 1.0f, 20.0f)}, eye);
    const float at[3] = {1010.0f, 50.0f, -1990.0f};
    const float near[3] = {1025.0f, 50.0f, -1990.0f};
    const float far[3] = {1050.0f, 50.0f, -1990.0f};
    auto count = [&](const float *position) {
        const int cell = grid.cellAt(position);
        int first = 0, listed = 0;
        if (cell >= 0)
            LightGrid::unpackCell(grid.cellTexels[size_t(cell)], first, listed);
        return cell < 0 ? -1 : listed;
    };
    check(grid.lightCount() == 1 && count(at) == 1 && count(near) == 1 && count(far) == 0,
          "a light is listed in the cells within its range only");
    check(grid.lightTexels.size() == 16 && grid.lightTexels[3] == 20.0f
          && grid.indexTexels.size() % LightGrid::IndexWidth == 0,
          "light data and indices are laid out in texture rows");
    check(std::fmod(grid.origin[0], LightGrid::Step) == 0.0f && grid.horizontalCell == LightGrid::CellSize
                  && grid.origin[0] == std::floor((eye[0] - LightGrid::MaxExtent / 2) / LightGrid::Step) * LightGrid::Step
                  && grid.cellTexels.size() == size_t(LightGrid::CellsWidth) * LightGrid::CellRows,
          "16 m cells around the camera, moved in steps");
    {
        int first = -1, listed = -1;
        LightGrid::unpackCell(LightGrid::packCell(4194303, 64), first, listed);
        const float packed = LightGrid::packCell(0, 1);
        int none = -1, zero = -1;
        LightGrid::unpackCell(LightGrid::packCell(5, 0), none, zero);
        check(first == 4194303 && listed == 64 && std::isnormal(packed) && packed > 0.0f
                      && std::isnormal(LightGrid::packCell(4194303, 127)) && zero == 0,
              "a cell's first index and count packed in an ordinary float");
    }

    grid.build({light(1010.0f, 50.0f, -1990.0f, 1.0f, 20.0f),
                light(1012.0f, 50.0f, -1990.0f, 5.0f, 20.0f)}, eye);
    const int cell = grid.cellAt(at);
    int first = 0, listed = 0;
    if (cell >= 0)
        LightGrid::unpackCell(grid.cellTexels[size_t(cell)], first, listed);
    check(cell >= 0 && listed == 2 && int(grid.indexTexels[size_t(first)]) == 1,
          "the brightest light of a cell comes first");

    std::vector<LightGrid::Light> crowd;
    for (int i = 0; i < 100; ++i)
        crowd.push_back(light(1010.0f, 50.0f, -1990.0f, 1.0f + i, 10.0f));
    crowd.push_back(light(eye[0] + 5000.0f, 50.0f, eye[2], 1.0f, 10.0f));
    // Lights 1.5 km either side, and one 3.5 km away: the cells stay 16 m
    // (task 45: a far light made those by the camera wider).
    std::vector<LightGrid::Light> spread = {light(eye[0] - 1500.0f, 50.0f, eye[2], 1.0f, 10.0f),
                                            light(eye[0] + 1500.0f, 50.0f, eye[2], 1.0f, 10.0f),
                                            light(1010.0f, 50.0f, -1990.0f, 1.0f, 20.0f),
                                            light(eye[0] + 3500.0f, 50.0f, eye[2], 1.0f, 10.0f)};
    grid.build(spread, eye);
    const float west[3] = {eye[0] - 1500.0f, 50.0f, eye[2]};
    const float east[3] = {eye[0] + 1500.0f, 50.0f, eye[2]};
    const float farEast[3] = {eye[0] + 3500.0f, 50.0f, eye[2]};
    check(grid.horizontalCell == LightGrid::CellSize && count(west) == 1 && count(east) == 1 && count(farEast) == 1
                  && count(at) == 1 && count(near) == 1 && count(far) == 0,
          "far lights listed in their own 16 m cells, the cells by the camera unchanged");
    grid.build(crowd, eye);
    check(grid.lightCount() == 100 && count(at) == LightGrid::MaxLightsPerCell,
          "cells keep the brightest 64 lights; lights off the grid (5 km away) are left out");

    grid.build({}, eye);
    check(grid.empty() && grid.indexTexels.empty(), "no lights, an empty grid");

    // Emissive surfaces.
    std::vector<float> quad;
    addQuad(quad, 0.0f, 0.0f, 1.0f, 1.0f);
    EmissiveEmitters::Surface surface;
    surface.vertices = quad.data();
    surface.vertexCount = int(quad.size() / 5);
    surface.stride = 5;
    surface.uvOffset = 3;
    surface.emissive[0] = surface.emissive[1] = surface.emissive[2] = 1.0f;
    QVector<RenderItem::Light> lights = EmissiveEmitters::extract(surface);
    float centre[3] = {0.0f, 0.0f, 0.0f}, weight = 0.0f;
    for (const RenderItem::Light &emitter : lights) {
        for (int c = 0; c < 3; ++c)
            centre[c] += emitter.position[c] * emitter.intensity;
        weight += emitter.intensity;
    }
    check(!lights.isEmpty() && lights.first().emissive
          && std::abs(totalIntensity(lights) - 1.0f / (4.0f * float(M_PI))) < 1e-5f,
          "a white square metre of emission carries M A / (4 pi)");
    check(weight > 0.0f && std::abs(centre[0] / weight - 0.5f) < 1e-4f
          && std::abs(centre[2] / weight - 0.5f) < 1e-4f,
          "emitters sit at the centroid of the emission");

    surface.emissive[1] = 0.0f;
    surface.emissive[2] = 0.0f;
    lights = EmissiveEmitters::extract(surface);
    check(!lights.isEmpty() && lights.first().color[0] == 1.0f && lights.first().color[1] == 0.0f,
          "emitters take the emissive colour");
    surface.emissive[0] = 0.0f;
    check(EmissiveEmitters::extract(surface).isEmpty(), "no emission, no emitters");

    // A map black on its left half: quads on the left emit nothing.
    std::vector<float> strip;
    for (int i = 0; i < 4; ++i) {
        addQuad(strip, float(i), 0.0f, 1.0f, 1.0f);
        // Each quad's texture coordinates cover a quarter of the map.
        for (size_t v = strip.size() - 30; v < strip.size(); v += 5)
            strip[v + 3] = (i + strip[v + 3]) / 4.0f;
    }
    QImage map(8, 8, QImage::Format_RGB32);
    map.fill(Qt::white);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 4; ++x)
            map.setPixel(x, y, qRgb(0, 0, 0));
    surface.vertices = strip.data();
    surface.vertexCount = int(strip.size() / 5);
    surface.emissive[0] = surface.emissive[1] = surface.emissive[2] = 1.0f;
    surface.map = &map;
    lights = EmissiveEmitters::extract(surface);
    bool rightOnly = !lights.isEmpty();
    for (const RenderItem::Light &emitter : lights)
        rightOnly = rightOnly && emitter.position[0] > 2.0f;
    check(rightOnly && std::abs(totalIntensity(lights) - 2.0f / (4.0f * float(M_PI))) < 1e-4f,
          "the emissive map masks the emission");
    surface.map = nullptr;

    // A long strip of emission splits into several emitters, at most 64.
    std::vector<float> road;
    for (int i = 0; i < 400; ++i)
        addQuad(road, float(i), 0.0f, 1.0f, 1.0f);
    surface.vertices = road.data();
    surface.vertexCount = int(road.size() / 5);
    lights = EmissiveEmitters::extract(surface);
    check(lights.size() > 1 && lights.size() <= EmissiveEmitters::MaxEmitters
          && std::abs(totalIntensity(lights) - 400.0f / (4.0f * float(M_PI))) < 1e-2f,
          "large surfaces split into at most 64 emitters keeping their power");

    qInfo() << "[tests:local-lights] cases=" << (passed + failed) << "passed=" << passed
            << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
