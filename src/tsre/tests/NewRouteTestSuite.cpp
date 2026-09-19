/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/tests/NewRouteTestSuite.h>

#include <routeEditor/NewRouteValidation.h>
#include <tsre/Game.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/geo/GeoPresetData.h>
#include <tsre/world/Trk.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <memory>
#include <mzip/miniz/miniz.h>

namespace {
bool writeFile(const QString &path, const QByteArray &contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly)
            && file.write(contents) == contents.size();
}

bool writeZip(const QString &path, const QByteArray &contents) {
    mz_zip_archive archive = {};
    if (!mz_zip_writer_init_heap(&archive, 0, 0)) return false;
    bool ok = mz_zip_writer_add_mem(
            &archive, "geo_cities_presets.txt", contents.constData(),
            size_t(contents.size()), MZ_BEST_COMPRESSION);
    void *zipData = nullptr;
    size_t zipSize = 0;
    if (ok) ok = mz_zip_writer_finalize_heap_archive(
            &archive, &zipData, &zipSize);
    if (ok) {
        QFile file(path);
        ok = file.open(QIODevice::WriteOnly)
                && file.write(static_cast<const char *>(zipData),
                              qint64(zipSize)) == qint64(zipSize);
    }
    mz_zip_writer_end(&archive);
    mz_free(zipData);
    return ok;
}

bool writeTrk(const QString &path, Trk &trk) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);
    out.setRealNumberPrecision(12);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << "SIMISA@@@@@@@@@@JINX0r1t______\n\n";
    trk.saveToStream(out);
    out.flush();
    return file.error() == QFile::NoError;
}
}

int TsreTests::runNewRouteSuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    const auto check = [&](bool value, const char *name) {
        if (value) {
            ++passed;
            if (verbose) qInfo() << "[tests:new-route] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:new-route] FAIL" << name;
        }
    };

    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary workspace");
    const QByteArray places =
            "1\tParis\tParis\tLutetia,Paname\t48.8566\t2.3522\tP\tPPLC\tFR\n"
            "2\tWarszawa\tWarsaw\tVarsovie\t52.2297\t21.0122\tP\tPPLC\tPL\n";
    const QString placesPath = temporary.filePath("places.txt");
    check(writeFile(placesPath, places), "write place fixture");

    GeoPlacePresetIndex placeIndex;
    QString error;
    check(placeIndex.load(placesPath, &error) && placeIndex.placeCount() == 2,
          "parse place presets");
    const QVector<int> aliasMatches = placeIndex.search(QStringLiteral("lute"));
    check(aliasMatches.size() == 1
                  && placeIndex.place(aliasMatches.first()).name == QStringLiteral("Paris"),
          "normalized alias prefix lookup");

    const QString extractedPath = temporary.filePath("assets/geo/geo_cities_presets.txt");
    const QString zipPath = temporary.filePath("geo_cities_presets.zip");
    check(writeZip(zipPath, places), "write compressed place fixture");
    GeoPlacePresetIndex fallbackIndex;
    error.clear();
    check(fallbackIndex.load(extractedPath, zipPath, &error)
                  && QFile::exists(extractedPath)
                  && fallbackIndex.placeCount() == 2,
          "extract missing place asset from ZIP and parse it");

    const QByteArray projections = R"json([
        {"countryCode":"FR","countryName":"France","name":"Paris West","lat":48.8,"lon":2.0,"k0":0.9996},
        {"countryCode":"FR","countryName":"France","name":"Paris East","lat":48.8,"lon":3.0,"k0":0.9999},
        {"countryCode":"AD","countryName":"Andorra","name":"Andorra","lat":42.5,"lon":1.5,"k0":1.0}
    ])json";
    const QString projectionPath = temporary.filePath("projections.json");
    check(writeFile(projectionPath, projections), "write projection fixture");
    GeoProjectionPresetList projectionList;
    error.clear();
    check(projectionList.load(projectionPath, &error)
                  && projectionList.count() == 3,
          "parse projection presets");
    const int nearestFrance = projectionList.nearest(48.85, 2.85,
                                                      QStringLiteral("FR"));
    check(nearestFrance >= 0
                  && projectionList.preset(nearestFrance).name
                        == QStringLiteral("Paris East"),
          "country-restricted nearest projection");
    check(nearestFrance >= 0
                  && std::abs(projectionList.preset(nearestFrance).scaleFactor
                              - 0.9999) < 1e-12,
          "retain projection k0");

    QDir().mkpath(temporary.filePath("ROUTES/Paris"));
    using NameError = NewRouteValidation::NameError;
    check(NewRouteValidation::validateName(QStringLiteral("paris"), temporary.path())
                  == NameError::AlreadyExists,
          "route name collision is case-insensitive");
    check(NewRouteValidation::validateName(QStringLiteral("../Paris"), temporary.path())
                  == NameError::InvalidCharacter,
          "route name rejects path characters");
    check(NewRouteValidation::validateName(QStringLiteral("CON"), temporary.path())
                  == NameError::Reserved,
          "route name rejects reserved device names");
    check(NewRouteValidation::validateName(QStringLiteral("New Route_2"), temporary.path())
                  == NameError::None,
          "route name accepts supported characters");

    double latitude = 0.0;
    double longitude = 0.0;
    check(NewRouteValidation::coordinatePair(QStringLiteral("48.8566, 2.3522"),
                                              latitude, longitude)
                  && std::abs(latitude - 48.8566) < 1e-12
                  && std::abs(longitude - 2.3522) < 1e-12,
          "parse pasted coordinate pair");
    check(!NewRouteValidation::coordinatePair(QStringLiteral("91, 2"),
                                               latitude, longitude),
          "reject out-of-range coordinate pair");

    GeoProjectionParameters projection;
    projection.originLatitude = 48.8566;
    projection.originLongitude = 2.3522;
    projection.offsetX = 2.0 * 2048.0;
    projection.offsetZ = 4.0 * 2048.0;
    std::unique_ptr<GeoWorldCoordinateConverter> converter(
            GeoWorldCoordinateConverter::Create(
                    GeoProjectionType::TransverseMercator, &projection));
    IghCoordinate projected;
    PreciseTileCoordinate tile;
    converter->ConvertToInternal(48.8566, 2.3522, &projected);
    converter->ConvertToTile(&projected, &tile);
    check(tile.TileX == 2 && tile.TileZ == 3,
          "projection origin maps to requested tile offset");

    // EPSG:32631 without its 500000 m false easting: Paris (2.3522 E,
    // 48.8566 N) is approximately -47517.47 m, 5411717.18 m on GRS80.
    GeoProjectionParameters utm31;
    utm31.originLongitude = 3.0;
    utm31.scaleFactor = 0.9996;
    std::unique_ptr<GeoWorldCoordinateConverter> scaledConverter(
            GeoWorldCoordinateConverter::Create(
                    GeoProjectionType::TransverseMercator, &utm31));
    IghCoordinate parisProjected;
    scaledConverter->ConvertToInternal(48.8566, 2.3522, &parisProjected);
    check(std::abs(parisProjected.Sample - (-47517.47)) < 0.5
                  && std::abs(parisProjected.Line - 5411717.18) < 0.5,
          "TM k0 known-reference projection");
    LatitudeLongitudeCoordinate parisRoundTrip;
    scaledConverter->ConvertToLatLon(&parisProjected, &parisRoundTrip);
    check(std::abs(parisRoundTrip.Latitude - 48.8566) < 1e-9
                  && std::abs(parisRoundTrip.Longitude - 2.3522) < 1e-9,
          "TM non-unit k0 round trip");

    GeoProjectionParameters unitScale = utm31;
    unitScale.scaleFactor = 1.0;
    std::unique_ptr<GeoWorldCoordinateConverter> unitConverter(
            GeoWorldCoordinateConverter::Create(
                    GeoProjectionType::TransverseMercator, &unitScale));
    IghCoordinate unitProjected;
    unitConverter->ConvertToInternal(48.8566, 2.3522, &unitProjected);
    check(std::abs(parisProjected.Sample / unitProjected.Sample - 0.9996) < 1e-12
                  && std::abs(parisProjected.Line / unitProjected.Line - 0.9996) < 1e-12,
          "TM k0 scales both projection axes");

    std::unique_ptr<Trk> defaults =
            Trk::createNewRouteTemplate(QStringLiteral("DEFAULT_ROUTE"));
    check(defaults->startTileX == -5000 && defaults->startTileZ == 15000
                  && defaults->geoProjectionType
                        == GeoProjectionType::InterruptedGoodeHomolosine
                  && !defaults->geoProjection.has_value(),
          "automatic route template uses safe legacy defaults");

    Trk saved;
    saved.idName = QStringLiteral("K0_ROUTE");
    saved.displayName = QStringLiteral("K0 route");
    saved.routeName = QStringLiteral("K0_ROUTE");
    saved.geoProjectionType = GeoProjectionType::TransverseMercator;
    saved.geoProjection = utm31;
    const QString trkPath = temporary.filePath(QStringLiteral("custom-name.trk"));
    check(writeTrk(trkPath, saved), "write variable-k0 TRK fixture");
    const QString previousRoot = Game::root;
    Game::root = temporary.path();
    Trk loaded;
    loaded.load(trkPath);
    Game::root = previousRoot;
    check(loaded.trkFileName == QStringLiteral("custom-name.trk"),
          "TRK owns its physical filename");
    check(loaded.geoProjection.has_value(),
          "reload TRK projection parameters");
    check(loaded.geoProjection.has_value()
                  && std::abs(loaded.geoProjection->scaleFactor - 0.9996) < 1e-6,
          "reload TRK projection k0");

    saved.geoProjection->scaleFactor = 1.0;
    const QString legacyTrkPath = temporary.filePath(QStringLiteral("legacy.trk"));
    check(writeTrk(legacyTrkPath, saved), "write k0=1 TRK fixture");
    Game::root = temporary.path();
    Trk legacyLoaded;
    legacyLoaded.load(legacyTrkPath);
    Game::root = previousRoot;
    check(legacyLoaded.geoProjection.has_value()
                  && legacyLoaded.geoProjection->scaleFactor == 1.0,
          "TRK without scale token defaults k0 to one");

    qInfo() << "[tests:new-route] cases=" << passed + failed
            << "passed=" << passed << "failed=" << failed;
    return failed ? 1 : 0;
}
