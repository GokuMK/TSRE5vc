/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/tests/NewRouteTestSuite.h>

#include <routeEditor/NewRouteValidation.h>
#include <tsre/Game.h>
#include <tsre/coords/CoordsCountryPlaces.h>
#include <tsre/geo/GeoCoordinateText.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/geo/GeoPresetData.h>
#include <tsre/world/Trk.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <iterator>
#include <memory>
#include <miniz/miniz.h>

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

// A TRK as a text, UTF-16 with its byte order mark, as MSTS writes it.
bool writeTrkText(const QString &path, const QString &text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << text;
    out.flush();
    return file.error() == QFile::NoError;
}

QString readTrkText(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf16);
    return in.readAll();
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
            "1\tParis\tParis\tLutetia,Paname\t48.8566\t2.3522\tP\tPPLC\tFR\t\t11\t75\t751\t\t2138551\n"
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
    check(placeIndex.countryPlaces(QStringLiteral("fr")).size() == 1
                  && placeIndex.countryCodes().contains(QStringLiteral("PL")),
          "index places by country code");
    check(placeIndex.nearestCountry(52.2, 21.0) == QStringLiteral("PL"),
          "infer country from nearest place");

    const QString countryKmlPath = temporary.filePath(
            QStringLiteral("tsre-country-FR.kml"));
    error.clear();
    check(CoordsCountryPlaces::write(
                  countryKmlPath, QStringLiteral("FR"), placeIndex, &error),
          "write country places KML");
    QFile countryKmlFile(countryKmlPath);
    const bool countryKmlReadable = countryKmlFile.open(QIODevice::ReadOnly);
    const QByteArray countryKml = countryKmlReadable
            ? countryKmlFile.readAll() : QByteArray();
    check(countryKmlReadable
                  && countryKml.contains("urn:tsre5:geo-places:1")
                  && countryKml.contains("<tsre:alias>Lutetia</tsre:alias>")
                  && !countryKml.contains("<value>"),
          "country KML uses namespaced metadata without value wrappers");
    GeoMstsCoordinateConverter countryKmlConverter;
    GeoWorldCoordinateConverter *previousConverter = Game::GeoCoordConverter;
    Game::GeoCoordConverter = &countryKmlConverter;
    CoordsCountryPlaces countryPlaces(countryKmlPath);
    Game::GeoCoordConverter = previousConverter;
    const Coords::Marker *countryMarker = countryPlaces.markerAt(0);
    check(countryPlaces.loaded && countryPlaces.countryCode() == QStringLiteral("FR")
                  && countryMarker != nullptr
                  && countryMarker->sourceId == 1
                  && countryMarker->aliases.contains(QStringLiteral("Lutetia"))
                  && countryMarker->featureCode == QStringLiteral("PPLC")
                  && countryMarker->population == 2138551
                  && countryPlaces.search(QStringLiteral("lute")).value(0, -1) == 0,
          "reload country KML metadata (feature code, population) and alias search");

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
    check(GeoCoordinateText::parseLatitudeLongitude(
                  QStringLiteral("52.2297; 21.0122"), latitude, longitude)
                  && std::abs(latitude - 52.2297) < 1e-12
                  && std::abs(longitude - 21.0122) < 1e-12,
          "shared coordinate parser accepts semicolon pair");
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
    // Electrified, not mountainous, its line voltage (JAPAN2's as written:
    // a fraction kept).
    saved.electrified = 1;
    saved.mountains = 0;
    saved.maxLineVoltage = 1.554f;
    const QString trkPath = temporary.filePath(QStringLiteral("custom-name.trk"));
    check(writeTrk(trkPath, saved), "write variable-k0 TRK fixture");
    const QString previousRoot = Game::root;
    Game::root = temporary.path();
    Trk loaded;
    loaded.load(trkPath);
    Game::root = previousRoot;
    check(loaded.trkFileName == QStringLiteral("custom-name.trk"),
          "TRK owns its physical filename");
    check(loaded.electrified == 1 && loaded.mountains == 0 && std::abs(loaded.maxLineVoltage - 1.554f) < 1e-4f,
          "TRK keeps Electrified, Mountains and MaxLineVoltage");
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
    check(legacyLoaded.kept.isEmpty() && legacyLoaded.trackGauge == 0.0f
                  && legacyLoaded.trackGaugeText == QStringLiteral("0"),
          "TRK of TSRE's own keeps nothing, TrackGauge 0");

    // Open Rails' tokens TSRE does not read (OR_PSK_NEW's Prudnik.trk has
    // these) survive a save as written and in order (task tracks 16 step 0).
    const QString keptBlocks[] = {
        QStringLiteral("ORTSForceSuperElevation ( 1 )"),
        QStringLiteral("ORTSSuperElevation(\n        MaximumCant ( 0mm )\n"
                       "        MaximumSpeed ( 59km/h )\n    )"),
        QStringLiteral("Comment ( \"a (bracket) inside quotes\" )"),
        QStringLiteral("ORTSSuperElevation ( MaximumCant ( 100mm ) MinimumSpeed ( 101km/h ) )"),
        QStringLiteral("ORTSTrackSuperElevation ( 100 0.1 500 0.05 )"),
        QStringLiteral("Comment ( *** Dzwieki torowe - rozjazdy i luki *** )"),
        QStringLiteral("ORTSSwitchSMSNumber ( 31 )"),
    };
    QString orText = QStringLiteral("SIMISA@@@@@@@@@@JINX0r1t______\n\nTr_RouteFile (\n"
                                    "\tRouteID ( OR_KEPT )\n\tName ( \"Kept\" )\n"
                                    "\tTrackGauge ( 1435mm )\n\tSpeedLimit ( 44.4 )\n");
    for (const QString &block : keptBlocks)
        orText += QStringLiteral("     ") + block + QStringLiteral("\n");
    orText += QStringLiteral(")\n");
    const QString orPath = temporary.filePath(QStringLiteral("or-kept.trk"));
    check(writeTrkText(orPath, orText), "write Open Rails TRK fixture");
    Trk orLoaded;
    orLoaded.load(orPath);
    bool keptAsWritten = orLoaded.kept.size() == int(std::size(keptBlocks));
    for (int i = 0; keptAsWritten && i < orLoaded.kept.size(); ++i)
        keptAsWritten = orLoaded.kept[i] == keptBlocks[i];
    check(keptAsWritten, "TRK keeps the blocks it does not read, as written and in order");
    check(std::abs(orLoaded.trackGauge - 1.435f) < 1e-6f
                  && orLoaded.trackGaugeText == QStringLiteral("1435mm"),
          "TRK reads TrackGauge as a length with its unit");

    const QString orSaved = temporary.filePath(QStringLiteral("or-kept-saved.trk"));
    check(writeTrk(orSaved, orLoaded), "write the Open Rails TRK back");
    const QString savedText = readTrkText(orSaved);
    Trk orReloaded;
    orReloaded.load(orSaved);
    check(orReloaded.kept == orLoaded.kept
                  && savedText.contains(QStringLiteral("\tTrackGauge ( 1435mm )\n"))
                  && savedText.count(QStringLiteral("SuperElevation")) == 4,
          "a saved TRK reloads with the same kept blocks and TrackGauge as written");
    const QString orSavedAgain = temporary.filePath(QStringLiteral("or-kept-saved-again.trk"));
    check(writeTrk(orSavedAgain, orReloaded) && readTrkText(orSavedAgain) == savedText,
          "saving the reloaded TRK again writes the same text");

    orReloaded.trackGauge = 1.0f;
    const QString orEdited = temporary.filePath(QStringLiteral("or-kept-edited.trk"));
    check(writeTrk(orEdited, orReloaded)
                  && readTrkText(orEdited).contains(QStringLiteral("\tTrackGauge ( 1 )\n")),
          "a changed TrackGauge is written in metres");

    const QString metreGaugePath = temporary.filePath(QStringLiteral("gauge-metres.trk"));
    check(writeTrkText(metreGaugePath, QStringLiteral(
                  "SIMISA@@@@@@@@@@JINX0r1t______\n\nTr_RouteFile (\n"
                  "\tRouteID ( METRE )\n\tTrackGauge ( 1.067 )\n)\n")),
          "write a fractional TrackGauge fixture");
    Trk metreGauge;
    metreGauge.load(metreGaugePath);
    check(std::abs(metreGauge.trackGauge - 1.067f) < 1e-6f,
          "TRK keeps a fractional TrackGauge (metres without a unit)");

    // TsreRunningNoise (task maszyna 05): read in any case and written back
    // as TSRE writes it; absent: unset, not written; another word: unset.
    check(metreGauge.tsreRunningNoise == RunningNoise::Unset
                  && orLoaded.tsreRunningNoise == RunningNoise::Unset
                  && !savedText.contains(QStringLiteral("TsreRunningNoise")),
          "a TRK without TsreRunningNoise leaves it unset and does not write it");
    const QString noisePath = temporary.filePath(QStringLiteral("running-noise.trk"));
    check(writeTrkText(noisePath, QStringLiteral(
                  "SIMISA@@@@@@@@@@JINX0r1t______\n\nTr_RouteFile (\n"
                  "\tRouteID ( NOISE )\n\tTsreRunningNoise ( regions )\n)\n")),
          "write a TsreRunningNoise fixture");
    Trk noise;
    noise.load(noisePath);
    const QString noiseSaved = temporary.filePath(QStringLiteral("running-noise-saved.trk"));
    check(noise.tsreRunningNoise == RunningNoise::Regions && noise.kept.isEmpty()
                  && writeTrk(noiseSaved, noise)
                  && readTrkText(noiseSaved).contains(QStringLiteral("\tTsreRunningNoise ( Regions )\n")),
          "TRK reads TsreRunningNoise and writes it back");
    const QString badNoisePath = temporary.filePath(QStringLiteral("running-noise-bad.trk"));
    check(writeTrkText(badNoisePath, QStringLiteral(
                  "SIMISA@@@@@@@@@@JINX0r1t______\n\nTr_RouteFile (\n"
                  "\tRouteID ( NOISE )\n\tTsreRunningNoise ( \"Bridges\" )\n\tSpeedLimit ( 30 )\n)\n")),
          "write an unknown TsreRunningNoise fixture");
    Trk badNoise;
    badNoise.load(badNoisePath);
    check(badNoise.tsreRunningNoise == RunningNoise::Unset && std::abs(badNoise.speedLimit - 30.0f) < 1e-6f,
          "an unknown TsreRunningNoise is unset, the keys after it read");

    qInfo() << "[tests:new-route] cases=" << passed + failed
            << "passed=" << passed << "failed=" << failed;
    return failed ? 1 : 0;
}
