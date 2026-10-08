#include <tsre/tests/OsmDataTestSuite.h>
#include <tsre/geo/osm/OsmConversionUi.h>
#include <tsre/geo/osm/OsmDirectory.h>
#include <tsre/geo/osm/SortedPbfStore.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/geo/MapDataOSM.h>
#include <tsre/Game.h>
#include <routeEditor/AboutWindow.h>
#include <QLabel>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QImage>
#include <QApplication>
#include <QDebug>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTimer>
#include <QWidget>
#include <QFile>
#include <QTemporaryDir>
#include <memory>

namespace TsreTests {

namespace {

// A small download near (lon, lat) in the Geofabrik layout.
bool writeDownload(const QString &path, double lon, double lat) {
    using namespace Osm;
    HeaderInfo h;
    h.bbox = Box::fromDegrees(lon - 0.5, lat - 0.5, lon + 0.5, lat + 0.5);
    h.requiredFeatures << "OsmSchema-V0.6" << "DenseNodes";
    h.optionalFeatures << "Sort.Type_then_ID";
    BlockBuilder nodes, ways;
    const Tag none[] = {{"", ""}};
    for (int i = 0; i < 4; ++i) nodes.addNode(i + 1, Location::fromDegrees(lon + i * 0.001, lat), none, 0);
    const Tag rail[] = {{"railway", "rail"}};
    const int64_t refs[] = {1, 2, 3, 4};
    ways.addWay(7, rail, 1, refs, nullptr, 4);
    std::string a, b;
    nodes.build(a); ways.build(b);
    const std::string bytes = encodeBlobFrame("OSMHeader", encodeHeaderBlock(h), {}, 6) + encodeBlobFrame("OSMData", a, {}, 6) + encodeBlobFrame("OSMData", b, {}, 6);
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes.data(), qint64(bytes.size())) == qint64(bytes.size());
}


// Tile under (lat, lon) set up like MapWindow::load (2048 m tile, level 1).
void setupTile(MapDataOSM &m, double lat, double lon) {
    GeoWorldCoordinateConverter *c = Game::GeoCoordConverter;
    IghCoordinate igh;
    PreciseTileCoordinate tile;
    c->ConvertToInternal(lat, lon, &igh);
    c->ConvertToTile(&igh, &tile);
    m.tileX = tile.TileX; m.tileZ = tile.TileZ;
    m.level = 1; m.tileSize = 2048;
    m.minlat = m.minlon = 999; m.maxlat = m.maxlon = -999;
    for (int k = 0; k < 4; ++k) {
        PreciseTileCoordinate corner;
        corner.set(tile.TileX, tile.TileZ, k & 1, k >> 1);
        IghCoordinate ci;
        LatitudeLongitudeCoordinate ll;
        c->ConvertToInternal(&corner, &ci);
        c->ConvertToLatLon(&ci, &ll);
        m.minlat = std::min<float>(m.minlat, ll.Latitude); m.maxlat = std::max<float>(m.maxlat, ll.Latitude);
        m.minlon = std::min<float>(m.minlon, ll.Longitude); m.maxlon = std::max<float>(m.maxlon, ll.Longitude);
    }
}

// Pixel of (lat, lon) on a tile image of the given size, as MapDataOSM projects it.
QPoint pixelOf(const MapDataOSM &m, double lat, double lon, int size) {
    IghCoordinate igh;
    PreciseTileCoordinate t;
    Game::GeoCoordConverter->ConvertToInternal(lat, lon, &igh);
    Game::GeoCoordConverter->ConvertToTile(&igh, &t);
    return QPoint(int((t.X + (t.TileX - m.tileX)) * size), int((t.Z - (t.TileZ - m.tileZ)) * size));
}

// Lat/lon of a point at tile fraction (fx, fz) of the tile under (lat, lon).
std::pair<double, double> tilePoint(double lat, double lon, double fx, double fz) {
    GeoWorldCoordinateConverter *c = Game::GeoCoordConverter;
    IghCoordinate igh;
    PreciseTileCoordinate tile;
    c->ConvertToInternal(lat, lon, &igh);
    c->ConvertToTile(&igh, &tile);
    PreciseTileCoordinate p;
    p.set(tile.TileX, tile.TileZ, fx, fz);
    LatitudeLongitudeCoordinate ll;
    c->ConvertToInternal(&p, &igh);
    c->ConvertToLatLon(&igh, &ll);
    return {ll.Latitude, ll.Longitude};
}

bool writeBytes(const QString &path, const std::string &bytes) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes.data(), qint64(bytes.size())) == qint64(bytes.size());
}

// Presses the dialog button with the given role once the modal question appears; optionally
// saves a snapshot (TSRE_OSM_UI_SNAPSHOTS=<dir>) for visual review.
void answerQuestion(QDialogButtonBox::ButtonRole role, const QString &snapshot, bool &seen) {
    QTimer::singleShot(200, [=, &seen] {
        QWidget *w = QApplication::activeModalWidget();
        auto *box = w ? w->findChild<QDialogButtonBox *>() : nullptr;
        if (!box) return;
        seen = true;
        const QString dir = qEnvironmentVariable("TSRE_OSM_UI_SNAPSHOTS");
        if (!dir.isEmpty()) w->grab().save(dir + "/" + snapshot);
        for (QAbstractButton *b : box->buttons())
            if (box->buttonRole(b) == role) { b->click(); return; }
    });
}

}

int runOsmDataSuite(bool verbose) {
    using namespace Osm;
    int passed = 0, failed = 0;
    auto check = [&](bool condition, const char *name) {
        if (condition) { ++passed; if (verbose) qInfo() << "[tests:osm-data] PASS" << name; }
        else { ++failed; qWarning() << "[tests:osm-data] FAIL" << name; }
    };
    QTemporaryDir tmp;
    check(writeDownload(tmp.filePath("gdansk.osm.pbf"), 18.6, 54.35) && writeDownload(tmp.filePath("warszawa.osm.pbf"), 21.0, 52.23),
          "write downloads");
    const Box tczew = Box::fromDegrees(18.78, 54.08, 18.81, 54.10), far = Box::fromDegrees(10.0, 40.0, 10.1, 40.1);

    EnsureResult none = ensureConverted(nullptr, tmp.filePath("missing"), false, tczew, EnsureMode::AcceptAll);
    check(!none.hasDirectory && none.converted == 0, "no directory: nothing to do");
    EnsureResult nothing = ensureConverted(nullptr, tmp.path(), false, far, EnsureMode::AcceptAll);
    check(nothing.hasDirectory && nothing.converted == 0, "an area no download covers converts nothing");
    EnsureResult first = ensureConverted(nullptr, tmp.path(), false, tczew, EnsureMode::AcceptAll);
    check(first.converted == 1 && first.errors.isEmpty() && QFile::exists(tmp.filePath("gdansk.tsre.osm.pbf"))
              && !QFile::exists(tmp.filePath("warszawa.tsre.osm.pbf")) && QFile::exists(tmp.filePath("gdansk.osm.pbf")),
          "first use converts only the download covering the area and keeps it");
    EnsureResult again = ensureConverted(nullptr, tmp.path(), false, tczew, EnsureMode::AcceptAll);
    check(again.converted == 0 && again.declined == 0, "a converted area is not converted again");
    const Box warsaw = Box::fromDegrees(20.9, 52.1, 21.1, 52.3);
    EnsureResult del = ensureConverted(nullptr, tmp.path(), true, warsaw, EnsureMode::AcceptAll);
    check(del.converted == 1 && QFile::exists(tmp.filePath("warszawa.tsre.osm.pbf")) && !QFile::exists(tmp.filePath("warszawa.osm.pbf")),
          "delete mode removes the download after converting");
    OsmDirectory dir;
    QString error;
    check(dir.scan(tmp.path(), error) && dir.convertedFiles().size() == 2 && dir.pendingConversions().empty(), "directory is fully converted");
    check(availableMemoryBytes() >= 0, "available memory query does not fail");

    // The question: "Not now" postpones for the session, "Convert now" converts.
    QTemporaryDir ui;
    writeDownload(ui.filePath("pomorskie.osm.pbf"), 18.6, 54.35);
    bool seen = false;
    answerQuestion(QDialogButtonBox::RejectRole, "osm-question.png", seen);
    EnsureResult later = ensureConverted(nullptr, ui.path(), false, tczew, EnsureMode::Ask);
    check(seen && later.declined == 1 && later.converted == 0 && !QFile::exists(ui.filePath("pomorskie.tsre.osm.pbf")), "declining the question converts nothing");
    seen = false;
    EnsureResult silent = ensureConverted(nullptr, ui.path(), false, tczew, EnsureMode::Ask);
    check(!seen && silent.declined == 1 && silent.converted == 0, "a declined file is not offered again in the same session");
    QTemporaryDir ui2;
    writeDownload(ui2.filePath("pomorskie.osm.pbf"), 18.6, 54.35);
    answerQuestion(QDialogButtonBox::AcceptRole, "osm-question-accept.png", seen);
    EnsureResult accepted = ensureConverted(nullptr, ui2.path(), false, tczew, EnsureMode::Ask);
    check(seen && accepted.converted == 1 && QFile::exists(ui2.filePath("pomorskie.tsre.osm.pbf")), "accepting the question converts");

    // Tile map from the store: a primary road, a building, a lake with an island, a rail bridge.
    GeoWorldCoordinateConverter *previous = Game::GeoCoordConverter;
    std::unique_ptr<GeoWorldCoordinateConverter> igh(GeoWorldCoordinateConverter::Create(GeoProjectionType::InterruptedGoodeHomolosine));
    Game::GeoCoordConverter = igh.get();
    {
        const double clat = 54.0925, clon = 18.7964;
        auto at = [&](double fx, double fz) { return Location::fromDegrees(tilePoint(clat, clon, fx, fz).second, tilePoint(clat, clon, fx, fz).first); };
        HeaderInfo h;
        h.requiredFeatures << "OsmSchema-V0.6" << "DenseNodes";
        h.optionalFeatures << "Sort.Type_then_ID";
        BlockBuilder nodes, ways, rels;
        const Tag none[] = {{"", ""}};
        int64_t nextNode = 1;
        std::vector<std::vector<int64_t>> wayRefs;
        auto polyline = [&](std::vector<std::pair<double, double>> pts) {
            std::vector<int64_t> refs;
            for (auto p : pts) { nodes.addNode(nextNode, at(p.first, p.second), none, 0); refs.push_back(nextNode++); }
            return refs;
        };
        auto ring = [&](double x0, double z0, double x1, double z1) {
            auto r = polyline({{x0, z0}, {x1, z0}, {x1, z1}, {x0, z1}});
            r.push_back(r.front());
            return r;
        };
        const auto road = polyline({{0.1, 0.3}, {0.9, 0.3}});
        const auto building = ring(0.15, 0.4, 0.25, 0.5);
        const auto outer = ring(0.4, 0.4, 0.8, 0.8), island = ring(0.55, 0.55, 0.65, 0.65);
        const auto rail = polyline({{0.3, 0.1}, {0.3, 0.9}});
        const Tag primary[] = {{"highway", "primary"}}, house[] = {{"building", "house"}}, bridge[] = {{"railway", "rail"}, {"bridge", "yes"}};
        ways.addWay(1, primary, 1, road.data(), nullptr, road.size());
        ways.addWay(2, house, 1, building.data(), nullptr, building.size());
        ways.addWay(3, none, 0, outer.data(), nullptr, outer.size());
        ways.addWay(4, none, 0, island.data(), nullptr, island.size());
        ways.addWay(5, bridge, 2, rail.data(), nullptr, rail.size());
        const Tag lake[] = {{"type", "multipolygon"}, {"natural", "water"}};
        const BlockBuilder::MemberIn members[] = {{3, ItemType::Way, "outer"}, {4, ItemType::Way, "inner"}};
        rels.addRelation(10, lake, 2, members, 2);
        std::string a, b, c;
        nodes.build(a); ways.build(b); rels.build(c);
        QTemporaryDir map;
        writeBytes(map.filePath("tile.osm.pbf"), encodeBlobFrame("OSMHeader", encodeHeaderBlock(h), {}, 6) + encodeBlobFrame("OSMData", a, {}, 6)
                                                      + encodeBlobFrame("OSMData", b, {}, 6) + encodeBlobFrame("OSMData", c, {}, 6));
        ConvertOptions options;
        ConvertStats stats;
        QString error;
        SortedPbfStore store;
        check(convertPbf(map.filePath("tile.osm.pbf"), map.filePath("tile.tsre.osm.pbf"), options, stats, error)
                  && store.open(QStringList{map.filePath("tile.tsre.osm.pbf")}, error),
              "tile fixture converts and opens");
        MapDataOSM m;
        setupTile(m, clat, clon);
        check(m.loadFrom(store) == 4, "the tile has the road, the building, the lake and the rail bridge");
        const int size = 1024;
        QImage image(size, size, QImage::Format_RGB32);
        check(m.draw(&image), "the tile draws");
        auto colorAt = [&](double fx, double fz) {
            const auto ll = tilePoint(clat, clon, fx, fz);
            return QColor(image.pixel(pixelOf(m, ll.first, ll.second, size))).name();
        };
        check(colorAt(0.6, 0.3) == QColor(228, 109, 113).name(), "the primary road is drawn in its line colour");
        check(colorAt(0.2, 0.45) == QColor(190, 173, 173).name(), "the building is filled");
        check(colorAt(0.45, 0.7) == QColor(181, 208, 208).name(), "the lake multipolygon is filled");
        check(colorAt(0.6, 0.6) == QColor(241, 238, 232).name(), "the island stays a hole");
        check(colorAt(0.3, 0.7) == QColor(90, 90, 90).name(), "the rail bridge line is drawn");
        check(colorAt(0.9, 0.9) == QColor(241, 238, 232).name(), "empty ground shows the background");
        // The OSM API fallback draws the same way from api/0.6/map XML.
        {
            QByteArray xml = "<?xml version=\"1.0\"?><osm version=\"0.6\">";
            const double fz = 0.3;
            int id = 1;
            QByteArray refs;
            for (double fx : {0.1, 0.5, 0.9}) {
                const auto ll = tilePoint(clat, clon, fx, fz);
                xml += QStringLiteral("<node id=\"%1\" lat=\"%2\" lon=\"%3\"/>").arg(id).arg(ll.first, 0, 'f', 7).arg(ll.second, 0, 'f', 7).toUtf8();
                refs += QStringLiteral("<nd ref=\"%1\"/>").arg(id++).toUtf8();
            }
            xml += "<way id=\"7\">" + refs + "<tag k=\"highway\" v=\"primary\"/></way><way id=\"8\">" + refs + "</way></osm>";
            MapDataOSM api;
            setupTile(api, clat, clon);
            QImage apiImage(size, size, QImage::Format_RGB32);
            check(api.loadFromApiXml({xml, xml}) == 1 && api.draw(&apiImage), "API responses load: overlapping responses give one way, untagged ways are skipped");
            const auto ll = tilePoint(clat, clon, 0.7, fz);
            check(QColor(apiImage.pixel(pixelOf(api, ll.first, ll.second, size))).name() == QColor(228, 109, 113).name(), "the API road is drawn like the local one");
            const QString apiDir = qEnvironmentVariable("TSRE_OSM_API_XML_DIR");
            const QString shots = qEnvironmentVariable("TSRE_OSM_UI_SNAPSHOTS");
            if (!apiDir.isEmpty()) {
                QList<QByteArray> responses;
                for (const QFileInfo &f : QDir(apiDir).entryInfoList({"tczew_*.osm"}, QDir::Files)) { QFile x(f.filePath()); if (x.open(QIODevice::ReadOnly)) responses << x.readAll(); }
                MapDataOSM saved;
                setupTile(saved, 54.0925, 18.7964);
                QElapsedTimer t;
                t.start();
                const size_t n = saved.loadFromApiXml(responses);
                const qint64 loadMs = t.restart();
                QImage big(4096, 4096, QImage::Format_RGB32);
                saved.draw(&big);
                qInfo().noquote() << QStringLiteral("[tests:osm-data] tczew from %1 API responses: %2 items, parse %3 ms, draw %4 ms").arg(responses.size()).arg(n).arg(loadMs).arg(t.elapsed());
                if (!shots.isEmpty()) big.save(shots + "/osm-tile-tczew-api.png");
            }
        }
        const QString snapshots = qEnvironmentVariable("TSRE_OSM_UI_SNAPSHOTS");
        if (!snapshots.isEmpty()) image.save(snapshots + "/osm-tile-fixture.png");

        // Opt-in: real converted files in TSRE_OSM_TEST_DIR, 4096 px tiles with timings.
        const QString realDir = qEnvironmentVariable("TSRE_OSM_TEST_DIR");
        if (!realDir.isEmpty()) {
            OsmDirectory dir;
            SortedPbfStore real;
            QStringList files;
            if (dir.scan(realDir, error)) for (const auto *e : dir.convertedFiles()) files << e->path;
            check(real.open(files, error), "real converted files open");
            const struct { const char *name; double lat, lon; } places[] = {{"tczew", 54.0925, 18.7964}, {"gdansk", 54.3557, 18.6440}, {"warszawa", 52.2297, 21.0122}};
            for (const auto &place : places) {
                MapDataOSM tile;
                setupTile(tile, place.lat, place.lon);
                QElapsedTimer t;
                t.start();
                const size_t n = tile.loadFrom(real);
                const qint64 loadMs = t.restart();
                QImage big(4096, 4096, QImage::Format_RGB32);
                tile.draw(&big);
                const qint64 drawMs = t.restart();
                tile.draw(&big);
                const qint64 drawAgainMs = t.elapsed();
                qInfo().noquote() << QStringLiteral("[tests:osm-data] %1: %2 items, load %3 ms, draw %4 ms (again %5 ms)")
                                             .arg(place.name).arg(n).arg(loadMs).arg(drawMs).arg(drawAgainMs);
                if (!snapshots.isEmpty()) big.save(snapshots + QStringLiteral("/osm-tile-%1.png").arg(place.name));
            }
        }
    }
    Game::GeoCoordConverter = previous;

    // OSM attribution in the About window.
    AboutWindow about(nullptr);
    bool attributed = false;
    for (QLabel *label : about.findChildren<QLabel *>()) attributed |= label->text().contains("openstreetmap.org/copyright");
    check(attributed, "the About window credits OpenStreetMap contributors");
    const QString aboutShots = qEnvironmentVariable("TSRE_OSM_UI_SNAPSHOTS");
    if (!aboutShots.isEmpty()) about.grab().save(aboutShots + "/about.png");


    qInfo().noquote() << QStringLiteral("[tests:osm-data] %1 passed, %2 failed").arg(passed).arg(failed);
    return failed ? 1 : 0;
}

}
