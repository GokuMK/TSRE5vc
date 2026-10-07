#include <tsre/tests/MapViewTestSuite.h>

#include <QDebug>
#include <cmath>
#include <tsre/map/MapPalette.h>
#include <tsre/map/MapView.h>
#include <tsre/map/TrackMapLayer.h>
#include <tsre/math3d/GLMatrix.h>
#include <vector>

namespace {
bool near(float a, float b, float tolerance = 1e-3f) {
    return std::fabs(a - b) <= tolerance;
}
}

int TsreTests::runMapViewSuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    auto check = [&](bool condition, const char *name) {
        if (condition) {
            ++passed;
            if (verbose)
                qInfo() << "[tests:map-view] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:map-view] FAIL" << name;
        }
    };

    MapView view;
    view.width = 800;
    view.height = 600;
    view.x = 100.0f;
    view.z = -50.0f;
    view.metresPerPixel = 2.0f;
    float gx, gz, px, py;
    view.groundAt(400.0f, 300.0f, gx, gz);
    check(near(gx, 100.0f) && near(gz, -50.0f), "the screen centre is the map centre");
    // North up: up the screen is -z (north), right is +x (east).
    view.groundAt(400.0f, 200.0f, gx, gz);
    check(near(gx, 100.0f) && near(gz, -250.0f), "north up: up the screen is north");
    view.groundAt(500.0f, 300.0f, gx, gz);
    check(near(gx, 300.0f) && near(gz, -50.0f), "north up: right of the screen is east");

    view.heading = MapView::NorthUp - 0.7f;
    view.groundAt(123.0f, 456.0f, gx, gz);
    view.screenAt(gx, gz, px, py);
    check(near(px, 123.0f) && near(py, 456.0f), "screen to ground and back, rotated");

    // The view and projection matrices put a ground point where screenAt
    // does.
    float viewMatrix[16], projection[16], combined[16];
    view.viewMatrix(viewMatrix);
    view.projection(projection);
    Mat4::multiply(combined, projection, viewMatrix);
    view.groundAt(650.0f, 120.0f, gx, gz);
    const float point[4] = {gx, 37.0f, gz, 1.0f};
    float clip[4];
    for (int r = 0; r < 4; ++r)
        clip[r] = combined[r] * point[0] + combined[4 + r] * point[1] + combined[8 + r] * point[2]
                + combined[12 + r] * point[3];
    const float sx = (clip[0] / clip[3] * 0.5f + 0.5f) * view.width;
    const float sy = (0.5f - clip[1] / clip[3] * 0.5f) * view.height;
    check(near(sx, 650.0f, 0.05f) && near(sy, 120.0f, 0.05f) && std::fabs(clip[2] / clip[3]) < 1.0f,
          "the matrices draw a ground point under its screen point, inside the depth range");

    view.groundAt(700.0f, 100.0f, gx, gz);
    view.zoomAt(700.0f, 100.0f, 0.5f);
    float hx, hz;
    view.groundAt(700.0f, 100.0f, hx, hz);
    check(near(view.metresPerPixel, 1.0f) && near(gx, hx, 0.01f) && near(gz, hz, 0.01f),
          "zooming keeps the ground under the mouse");
    view.zoomAt(0, 0, 1e9f);
    check(near(view.metresPerPixel, MapView::MaxMetresPerPixel), "zoom stops at the limit");
    view.metresPerPixel = 1.0f;

    view.tileX = view.tileZ = 0;
    view.x = 1000.0f;
    view.z = 0.0f;
    view.groundAt(300.0f, 300.0f, gx, gz);
    view.pan(-140.0f, -25.0f);
    view.groundAt(160.0f, 275.0f, hx, hz);
    // The ground point keeps its place, also when the centre changes tile.
    const float wx = hx + 2048.0f * view.tileX, wz = hz + 2048.0f * view.tileZ;
    check(view.tileX == 1 && near(gx, wx, 0.01f) && near(gz, wz, 0.01f),
          "a drag moves the ground with the mouse");

    view.x = 1500.0f;
    view.z = -3000.0f;
    view.tileX = 4;
    view.tileZ = 7;
    view.normalize();
    check(view.tileX == 5 && near(view.x, -548.0f) && view.tileZ == 6 && near(view.z, -952.0f),
          "the centre moves to the next tiles as the camera's does");

    view.heading = 3.0f;
    view.rotate(0.5f);
    check(near(view.heading, 3.5f - 2.0f * 3.14159265f), "rotation wraps around");

    std::vector<float> ribbon;
    const float segment[6] = {0, 5, 0, 10, 5, 0};
    TrackMapLayer::appendRibbons(ribbon, segment, 1, 2.0f);
    bool flat = ribbon.size() == 18;
    float minZ = 1e9f, maxZ = -1e9f;
    for (size_t i = 0; i + 2 < ribbon.size(); i += 3) {
        flat = flat && near(ribbon[i + 1], 5.0f);
        minZ = std::min(minZ, ribbon[i + 2]);
        maxZ = std::max(maxZ, ribbon[i + 2]);
    }
    check(flat && near(minZ, -1.0f) && near(maxZ, 1.0f),
          "a segment becomes two triangles of the line's width");

    std::vector<float> octagon;
    TrackMapLayer::appendOctagon(octagon, 10.0f, 3.0f, 20.0f, 4.0f);
    float lowX = 1e9f, highX = -1e9f;
    for (size_t i = 0; i + 2 < octagon.size(); i += 3) {
        lowX = std::min(lowX, octagon[i]);
        highX = std::max(highX, octagon[i]);
    }
    check(octagon.size() == 54 && near(lowX, 8.0f) && near(highX, 12.0f),
          "a marker is an octagon of its width, six triangles");

    const MapPalette dark = MapPalette::named("dark");
    MapPalette custom;
    const bool parsed = MapPalette::fromJson("{\"background\": \"#102030\", \"track\": \"#ffffff\"}", custom);
    check(dark.background.lightness() < 64 && MapPalette::light().background == QColor(Qt::white)
          && parsed && custom.background == QColor("#102030") && custom.road == MapPalette::light().road,
          "palettes: light and dark built in, custom files over the light one");
    check(MapPalette::named("no-such-palette").background == QColor(Qt::white),
          "an unknown palette falls back to light");

    qInfo().noquote() << "[tests:map-view] cases=" << passed + failed << "passed=" << passed
                      << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
