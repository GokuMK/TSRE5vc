#include <tsre/tests/MapViewTestSuite.h>

#include <QDebug>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tsre/Game.h>
#include <tsre/map/ActivityMapLayer.h>
#include <tsre/map/MapOverlayFade.h>
#include <tsre/map/MapLabelLayer.h>
#include <tsre/map/MapLabelSources.h>
#include <tsre/map/MapMeasureLayer.h>
#include <tsre/map/MapScaleBar.h>
#include <tsre/coords/Coords.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityEvent.h>
#include <tsre/map/MapPalette.h>
#include <tsre/map/OsmMapLayer.h>
#include <tsre/geo/GeoCoordinates.h>
#include <memory>
#include <tsre/map/TerrainMapLayer.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/map/MapView.h>
#include <tsre/map/TrackItemMapLayer.h>
#include <tsre/map/TrackMapLayer.h>
#include <tsre/world/objects/WorldObj.h>
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
    MapPalette oldFade, newFade;
    check(MapPalette::fromJson("{\"terrainFade\": 0.5}", oldFade) && near(oldFade.overlayFade, 0.5f)
                  && MapPalette::fromJson("{\"terrainFade\": 0.5, \"overlayFade\": 0.2}", newFade)
                  && near(newFade.overlayFade, 0.2f) && near(MapPalette::light().overlayFade, 0.55f) && near(MapPalette::dark().overlayFade, 0.55f),
          "palettes: overlayFade, read from the former terrainFade too");

    // Layer order: terrain textures, OSM data (50 to 79), terrain aids, the
    // Faded Overlay, then the route's own data.
    check(TerrainMapLayer::OverlayHeight < 50.0f && TerrainMapLayer::MissingHeight > 79.0f
                  && TerrainMapLayer::QuadHeight > TerrainMapLayer::MissingHeight
                  && TerrainMapLayer::BorderHeight > TerrainMapLayer::QuadHeight
                  && TerrainMapLayer::HighlightHeight > TerrainMapLayer::BorderHeight
                  && MapOverlayFade::Height > TerrainMapLayer::HighlightHeight
                  && MapOverlayFade::Height < TrackMapLayer::RoadHeight,
          "layers: terrain, OSM, terrain aids, Faded Overlay, route data");
    {
        MapView fadeView;
        fadeView.width = 200;
        fadeView.height = 100;
        fadeView.metresPerPixel = 2.0f;
        std::vector<float> fadeSquare;
        MapOverlayFade::appendView(fadeSquare, fadeView, MapOverlayFade::Height);
        float lowX = 1e9f, highX = -1e9f, lowZ = 1e9f, highZ = -1e9f;
        for (size_t i = 0; i + 2 < fadeSquare.size(); i += 3) {
            lowX = std::min(lowX, fadeSquare[i]);
            highX = std::max(highX, fadeSquare[i]);
            lowZ = std::min(lowZ, fadeSquare[i + 2]);
            highZ = std::max(highZ, fadeSquare[i + 2]);
        }
        check(fadeSquare.size() == 18 && near(highX - lowX, 400.0f) && near(highZ - lowZ, 200.0f)
                      && near(fadeSquare[1], MapOverlayFade::Height),
              "Faded Overlay covers the ground in view");
    }

    // Labels: placement without overlaps, ranking, the atlas.
    {
        using C = MapLabelLayer::Candidate;
        const auto placed = MapLabelLayer::place({C{100, 100, 60, 16}, C{100, 100, 60, 16}, C{130, 100, 60, 16},
                                                  C{-50, 100, 60, 16}, C{300, 5, 60, 16}},
                                                 400, 300, 5.0f, 2.0f);
        check(placed[0] == QPoint(70, 77) && placed[1].x() < 0 && placed[2].x() >= 0 && placed[2] != QPoint(100, 77)
                      && placed[3].x() < 0 && placed[4].y() > 5,
              "labels: the first name above its dot; one on the same dot left out; a neighbour moved aside; "
              "off screen left out; at the top edge placed below");
        C kraków{100, 100, 60, 16, 7}, sameName{100, 160, 60, 16, 7}, other{100, 220, 60, 16, 9}, far{350, 100, 40, 16, 7};
        const auto deduped = MapLabelLayer::place({kraków, sameName, other, far}, 500, 300, 5.0f, 2.0f, 120.0f);
        check(deduped[0].x() >= 0 && deduped[1].x() < 0 && deduped[2].x() >= 0 && deduped[3].x() >= 0,
              "labels: the same name within 120 pixels is placed once; other names and distant ones are placed");
        bool major = false, minor = true;
        const double capital = placeLabelPriority("PPLC", 1000, &major), seat = placeLabelPriority("PPLA", 5000000);
        const double big = placeLabelPriority("PPL", 900000, &minor), small = placeLabelPriority("PPL", 20000);
        check(capital > seat && seat > big && big > small && small > placeLabelPriority("PPLX", 5000000) && major && !minor,
              "labels: capitals, region seats, then by population; city sections last; capitals and seats major");
        MapLabelAtlas atlas;
        const MapLabelAtlas::Entry *a1 = atlas.get("Wrocław", false);
        const QRect r1 = a1 ? a1->rect : QRect();
        const MapLabelAtlas::Entry *b1 = atlas.get("Kędzierzyn-Koźle", false);
        const MapLabelAtlas::Entry *a2 = atlas.get("Wrocław", false);
        const MapLabelAtlas::Entry *major1 = atlas.get("Wrocław", true);
        check(a1 && b1 && a2 && major1 && a2->rect == r1 && !b1->rect.intersects(r1) && !major1->rect.intersects(r1)
                      && b1->rect.width() > r1.width() && major1->rect.height() > r1.height() && atlas.pageCount() == 1
                      && atlas.measure("Wrocław", false) == r1.size(),
              "label atlas: a name is painted once and reused; names do not overlap; bold ones are larger");
        const int generation = atlas.generation();
        atlas.setStyle(Qt::white, Qt::black, 2.0f);
        const MapLabelAtlas::Entry *again = atlas.get("Wrocław", false);
        check(atlas.generation() == generation + 1 && again && again->rect.height() > r1.height(),
              "label atlas: a new style or pixel ratio starts again, at the new size");

        // Sources: a location event and a marker into the view's tile convention.
        Activity activity;
        ActivityEvent event(1, ActivityEvent::CategoryLocation);
        event.name = "Pick up";
        const float location[5] = {-6120, 15094, 100, 200, 25};
        std::copy(location, location + 5, event.location);
        activity.event.push_back(event);
        Coords markers;
        Coords::Marker marker;
        marker.name = "Kraków";
        marker.featureCode = "PPLA";
        marker.population = 800000;
        marker.tileX.push_back(5);
        marker.tileZ.push_back(7);
        marker.x.push_back(10);
        marker.y.push_back(0);
        marker.z.push_back(-20);
        markers.markerList.push_back(marker);
        markers.loaded = true;
        std::vector<MapLabel> sourced;
        MapLabelSources::appendActivity(sourced, &activity);
        MapLabelSources::appendMarkers(sourced, &markers);
        check(sourced.size() == 2 && sourced[0].text == "Pick up" && sourced[0].tileX == -6120 && sourced[0].tileZ == -15094
                      && near(sourced[0].x, 100) && near(sourced[0].z, -200) && sourced[0].kind == MapLabelKind::Event
                      && sourced[1].tileZ == -7 && near(sourced[1].z, -20) && sourced[1].major
                      && sourced[0].priority > sourced[1].priority,
              "label sources: events and markers in the view's tile convention, events ranked above places");
        const MapPalette lightPalette = MapPalette::light();
        check(MapLabelLayer::dotColour(lightPalette, MapLabelKind::Siding) == lightPalette.siding
                      && MapLabelLayer::dotColour(lightPalette, MapLabelKind::Station) == lightPalette.platform
                      && MapLabelLayer::dotColour(lightPalette, MapLabelKind::Event) == lightPalette.event,
              "label dots take the colour of what they name");
    }

    // OSM data: its band of heights, and placing latitude and longitude on the ground.
    check(OsmMapLayer::BaseHeight > TerrainMapLayer::OverlayHeight
                  && OsmMapLayer::BaseHeight + 40.0f * OsmMapLayer::HeightStep
                             <= TerrainMapLayer::MissingHeight,
          "OSM: drawn between the terrain textures and the terrain aids");
    for (GeoProjectionType type : {GeoProjectionType::TransverseMercator,
                                   GeoProjectionType::InterruptedGoodeHomolosine}) {
        GeoProjectionParameters projection;
        projection.originLatitude = 52.0;
        projection.originLongitude = 20.0;
        std::unique_ptr<GeoWorldCoordinateConverter> converter(
                GeoWorldCoordinateConverter::Create(type, &projection));
        // A view tile and a point 5 km away in both directions.
        int tileX = 3, tileZ = -2;
        if (type == GeoProjectionType::InterruptedGoodeHomolosine) {
            IghCoordinate igh;
            PreciseTileCoordinate tile;
            converter->ConvertToInternal(52.0, 20.0, &igh);
            converter->ConvertToTile(&igh, &tile);
            tileX = tile.TileX;
            tileZ = -tile.TileZ;
        }
        double lat = 0, lon = 0;
        float x = 0, z = 0;
        OsmMapLayer::toLatLon(converter.get(), tileX, tileZ, 5000.0, -5000.0, lat, lon);
        OsmMapLayer::toGround(converter.get(), lat, lon, tileX, tileZ, x, z);
        double latEast = 0, lonEast = 0, latSouth = 0, lonSouth = 0;
        OsmMapLayer::toLatLon(converter.get(), tileX, tileZ, 1000.0, 0.0, latEast, lonEast);
        OsmMapLayer::toLatLon(converter.get(), tileX, tileZ, 0.0, 1000.0, latSouth, lonSouth);
        double lat0 = 0, lon0 = 0;
        OsmMapLayer::toLatLon(converter.get(), tileX, tileZ, 0.0, 0.0, lat0, lon0);
        const float rect[4] = {-2000.0f, 3000.0f, -1000.0f, 4000.0f};
        const Osm::Box area = OsmMapLayer::areaOf(converter.get(), tileX, tileZ, rect);
        bool inside = true;
        for (float cx : {rect[0], rect[1]})
            for (float cz : {rect[2], rect[3]}) {
                double clat, clon;
                OsmMapLayer::toLatLon(converter.get(), tileX, tileZ, cx, cz, clat, clon);
                const Osm::Location l = Osm::Location::fromDegrees(clon, clat);
                inside = inside && l.x >= area.minX && l.x <= area.maxX && l.y >= area.minY
                        && l.y <= area.maxY;
            }
        check(near(x, 5000.0f, 0.05f) && near(z, -5000.0f, 0.05f) && lonEast > lon0
                      && latSouth < lat0 && inside,
              type == GeoProjectionType::TransverseMercator
                      ? "OSM: ground and latitude/longitude round trip, x east, z south (TM)"
                      : "OSM: ground and latitude/longitude round trip, x east, z south (IGH)");
    }

    // Track objects.
    check(TrackItemMapLayer::kindOfItemType("SignalItem") == TrackItemMapLayer::Signal
                  && TrackItemMapLayer::kindOfItemType("hazzarditem") == TrackItemMapLayer::Hazard
                  && TrackItemMapLayer::kindOfItemType("levelcritem")
                          == TrackItemMapLayer::LevelCrossing
                  && TrackItemMapLayer::kindOfItemType("crossoveritem") == -1
                  && TrackItemMapLayer::kindOfItemType("emptyitem") == -1,
          "track objects: database item types, crossovers and empty items skipped");
    check(TrackItemMapLayer::kindOfObjectType(WorldObj::signal) == TrackItemMapLayer::Signal
                  && TrackItemMapLayer::kindOfObjectType(WorldObj::carspawner)
                          == TrackItemMapLayer::CarSpawner
                  && TrackItemMapLayer::kindOfObjectType(WorldObj::soundregion)
                          == TrackItemMapLayer::SoundRegion
                  && TrackItemMapLayer::kindOfObjectType(WorldObj::sstatic) == -1,
          "track objects: world object types, other objects skipped");
    const MapPalette itemPalette = MapPalette::light();
    check(TrackItemMapLayer::colour(itemPalette, TrackItemMapLayer::Signal) == QColor(255, 0, 0)
                  && TrackItemMapLayer::colour(itemPalette, TrackItemMapLayer::Platform)
                          == QColor(0, 255, 0)
                  && MapPalette::dark().itemBorder.lightness() > itemPalette.itemBorder.lightness(),
          "track objects: the 3D view's colours, a border that stands out of the background");
    MapView items;
    items.width = 1000;
    items.height = 600;
    items.metresPerPixel = 6.0f;
    const bool closeDrawsObjects = TrackItemMapLayer::drawsWorldObjects(items);
    items.metresPerPixel = 6.2f;
    check(closeDrawsObjects && !TrackItemMapLayer::drawsWorldObjects(items),
          "track objects: world objects below three tiles across, the database above");
    // Activity: vehicle footprints.
    const float wagon[MapFeatures::VehicleFloats] = {10.0f, 20.0f, 1.0f, 0.0f, 20.0f, 3.0f, 0.0f};
    auto extent = [](const std::vector<float> &v, int axis, float &low, float &high) {
        low = high = v[axis];
        for (size_t i = axis; i < v.size(); i += 3) {
            low = std::min(low, v[i]);
            high = std::max(high, v[i]);
        }
    };
    std::vector<float> footprint;
    ActivityMapLayer::appendVehicle(footprint, wagon, 1.0f, 0.0f, 0.0f);
    float xLow, xHigh, zLow, zHigh;
    extent(footprint, 0, xLow, xHigh);
    extent(footprint, 2, zLow, zHigh);
    check(footprint.size() == 18 && near(xLow, 0.0f) && near(xHigh, 20.0f) && near(zLow, 18.5f)
                  && near(zHigh, 21.5f),
          "activity: a wagon's footprint is its length along the track and its width across");
    footprint.clear();
    ActivityMapLayer::appendVehicle(footprint, wagon, 1.0f, 0.5f, 5.0f);
    extent(footprint, 0, xLow, xHigh);
    extent(footprint, 2, zLow, zHigh);
    check(near(xLow, -0.5f) && near(xHigh, 20.5f) && near(zLow, 17.0f) && near(zHigh, 23.0f),
          "activity: footprints keep a smallest size and grow by the border");
    check(ActivityMapLayer::colour(itemPalette, ActivityMapLayer::Engines) == itemPalette.engine
                  && ActivityMapLayer::colour(itemPalette, ActivityMapLayer::Event)
                          == QColor(255, 0, 0)
                  && MapPalette::dark().path != itemPalette.path,
          "activity: colours from the palette, events red as in 3D");

    // Positions into their tile, however far (the map's pointer).
    {
        int tx = 0, tz = 0;
        float px = 11210.0f, pz = -8023.0f;
        Game::check_coords(tx, tz, px, pz);
        const bool far = tx == 5 && near(px, 970.0f) && tz == -4 && near(pz, 169.0f);
        int ex = 0, ez = 0;
        float edgeX = 1024.0f, edgeZ = -1024.0f;
        Game::check_coords(ex, ez, edgeX, edgeZ);
        const bool edges = ex == 1 && near(edgeX, -1024.0f) && ez == 0 && near(edgeZ, -1024.0f);
        int nx = 0, nz = 0;
        float bad = std::numeric_limits<float>::quiet_NaN(), small = -1025.0f;
        Game::check_coords(nx, nz, bad, small);
        check(far && edges && nx == 0 && std::isnan(bad) && nz == -1 && near(small, 1023.0f),
              "tile coordinates: positions far off normalize into their tile");
    }

    // Terrain levels by the view's longer side.
    MapView ground;
    ground.width = 1600;
    ground.height = 900;
    ground.metresPerPixel = 10.24f;
    const bool patchesAt16 = TerrainMapLayer::drawsDetailedPatches(ground);
    ground.metresPerPixel = 10.3f;
    const bool bordersAbove16 = !TerrainMapLayer::drawsDetailedPatches(ground);
    ground.metresPerPixel = 3.8f;
    const bool proceduralBelow = TerrainMapLayer::drawsProcedural(ground);
    ground.metresPerPixel = 3.84f;
    check(patchesAt16 && bordersAbove16 && proceduralBelow
                  && !TerrainMapLayer::drawsProcedural(ground),
          "terrain: patches up to 16 km across, procedural shading below 6144 m");
    // A patch square from Terrain::mapPatchCorners order: two triangles of
    // VT vertices, wound like the ribbons (clockwise seen from above in x, z).
    const float patchCorners[16] = {0, 0, 0, 0,  128, 0, 1, 0,  128, 128, 1, 1,  0, 128, 0, 1};
    std::vector<float> square;
    TerrainMapLayer::appendPatch(square, patchCorners, 20.0f);
    std::vector<float> lineRibbon;
    const float lineSegment[6] = {0, 0, 0, 100, 0, 0};
    TrackMapLayer::appendRibbons(lineRibbon, lineSegment, 1, 10.0f);
    auto winding = [](const float *a, const float *b, const float *c) {
        return (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0]);
    };
    const float squareWinding = winding(square.data(), square.data() + 6, square.data() + 12);
    const float ribbonWinding = winding(lineRibbon.data(), lineRibbon.data() + 3,
                                        lineRibbon.data() + 6);
    check(square.size() == 36 && near(square[1], 20.0f) && near(square[5], 1.0f)
                  && squareWinding * ribbonWinding > 0.0f,
          "terrain: a patch is two textured triangles wound like the ribbons");

    items.metresPerPixel = 2.0f;
    items.x = 900.0f;
    int tiles[4];
    items.visibleTiles(tiles[0], tiles[1], tiles[2], tiles[3]);
    check(tiles[0] == 0 && tiles[1] == 1 && tiles[2] == 0 && tiles[3] == 0,
          "visible tiles: a 2 km by 1.2 km view across a tile edge");

    // Measure Distance: WGS84 geodesics (Vincenty's own example, Flinders Peak to
    // Buninyong, 54972.271 m; one degree of latitude at the equator, 110574.389 m).
    const double flinders = MapMeasureLayer::geodesicMetres(-(37 + 57 / 60.0 + 3.72030 / 3600.0),
            144 + 25 / 60.0 + 29.52440 / 3600.0, -(37 + 39 / 60.0 + 10.15610 / 3600.0),
            143 + 55 / 60.0 + 35.38390 / 3600.0);
    check(std::abs(flinders - 54972.271) < 0.01
                  && std::abs(MapMeasureLayer::geodesicMetres(0, 0, 1, 0) - 110574.389) < 0.01
                  && MapMeasureLayer::geodesicMetres(52, 20, 52, 20) == 0.0,
          "measure: geodesic lengths on WGS84");
    MapMeasureLayer measure;
    measure.set({0, 0, 1000.0f, 0.0f}, {1, 0, -1000.0f, 0.0f});
    const uint64_t measured = measure.version();
    check(measure.shown() && std::abs(measure.gameLength() - 48.0) < 1e-6 && measure.geoLength(nullptr) < 0.0,
          "measure: the game length across a tile edge; no geo length without a reference");
    measure.set({0, 0, 5.0f, 5.0f}, {0, 0, 5.0f, 5.0f});
    check(!measure.shown() && measure.version() != measured, "measure: a click (no length) shows nothing");
    measure.clear();
    check(!measure.shown(), "measure: cleared");
    const QString one = MapMeasureLayer::text(1234.4, 1234.3), two = MapMeasureLayer::text(1234.4, 1236.6);
    check(one.contains(QLatin1String("1234")) && !one.contains(QLatin1String("1237"))
                  && two.contains(QLatin1String("1234")) && two.contains(QLatin1String("1237"))
                  && MapMeasureLayer::text(10.0, -1.0) == MapMeasureLayer::text(10.0, 10.2)
                  && !MapMeasureLayer::text(524.79, 524.3).contains(QLatin1String("524")),
          "measure: one length when they differ by under a metre (no switching at roundings), else both");

    // Scale bar: the longest round length (1, 2 or 5 times a power of ten) that fits.
    check(MapScaleBar::roundLength(120.0) == 100.0 && MapScaleBar::roundLength(240.0) == 200.0
                  && MapScaleBar::roundLength(2.4) == 2.0 && MapScaleBar::roundLength(7.0) == 5.0
                  && MapScaleBar::roundLength(1000.0) == 1000.0 && MapScaleBar::roundLength(60000.0) == 50000.0
                  && MapScaleBar::roundLength(0.0) == 0.0,
          "scale bar: round lengths");
    check(MapScaleBar::text(2000.0).contains(QLatin1String("2 km")) && MapScaleBar::text(500.0).contains(QLatin1String("500 m")),
          "scale bar: metres below a kilometre, kilometres from it");

    // Terrain tile overlays below full opacity (F3 Opacity): blended, the
    // terrain's own overlay packets keep their terrain pass.
    {
        RenderItem item;
        item.material.surface = RenderItem::SURFACE_OPAQUE;
        const bool opaque = item.drawSurface() == RenderItem::SURFACE_OPAQUE;
        item.material.opacity = 0.5f;
        const bool blended = item.drawSurface() == RenderItem::SURFACE_BLENDED;
        item.material.surface = RenderItem::SURFACE_TERRAIN;
        check(opaque && blended && item.drawSurface() == RenderItem::SURFACE_TERRAIN,
              "an opacity below 1 draws a surface blended, terrain packets stay terrain");
    }

    qInfo().noquote() << "[tests:map-view] cases=" << passed + failed << "passed=" << passed
                      << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
