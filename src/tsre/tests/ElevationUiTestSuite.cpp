#include <tsre/tests/ElevationUiTestSuite.h>
#include <tsre/geo/HeightWindow.h>
#include <tsre/geo/GeoCoordinates.h>
#include <settings/SettingsAccess.h>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>

namespace {
class TestCoordinates : public GeoWorldCoordinateConverter {
public:
    IghCoordinate *ConvertToInternal(PreciseTileCoordinate *p, IghCoordinate *out) override {
        out->Line = p->Z; out->Sample = p->X; return out;
    }
    LatitudeLongitudeCoordinate *ConvertToLatLon(IghCoordinate *p, LatitudeLongitudeCoordinate *out) override {
        out->Latitude = 52.75-p->Line*.5; out->Longitude = 19.25+p->Sample*.5; return out;
    }
};
}
int TsreTests::runElevationUiSuite(const QString &capturePath, bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const char *name) {
        if (value) { ++passed; if (verbose) qInfo() << "[tests:elevation-ui] PASS" << name; }
        else { ++failed; qWarning() << "[tests:elevation-ui] FAIL" << name; }
    };
    TestCoordinates coordinates;
    QScopedValueRollback<GeoWorldCoordinateConverter*> rollback(Game::GeoCoordConverter,&coordinates);
    auto &settings = SettingsManager::instance();
    const auto root = Settings::string("core.paths.geoData",SettingType::Directory);
    const auto source = Settings::string("geo.elevation.source",SettingType::Enum);
    QTemporaryDir temp;
    check(settings.setSessionValue("core.paths.geoData",temp.path()),"set isolated geodata directory");
    check(settings.setSessionValue("geo.elevation.source",QString()),"select local HGT");
    QFile file(temp.filePath("N52E019.hgt"));
    QByteArray bytes(5*5*2,Qt::Uninitialized);
    for (int i = 0; i < 25; ++i) qToBigEndian<qint16>(100+i*5,bytes.data()+2*i);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),"write isolated HGT fixture");
    file.close();
    HeightWindow window;
    window.tileX = window.tileZ = 0;
    window.terrainResolution = 16; window.terrainSize = 2048;
    auto *selector = window.findChild<QComboBox*>();
    check(selector && selector->count() == 5,"HGT, Polish 1 m and Czech 5 m / 2 m datasets are selectable");
    QPushButton *apply = nullptr;
    for (auto *button : window.findChildren<QPushButton*>())
        if (button->text() == qtTrId("geo.elevation.apply")) apply = button;
    check(apply != nullptr,"translated apply button exists");
    QTimer::singleShot(0,&window,[&] {
        window.load(true);
        check(!window.ok && apply && apply->isEnabled(),"preview prepares heights without accepting them");
        check(window.terrainData && window.terrainData[0][0] != window.terrainData[15][15],"preview samples actual HGT gradient");
        if (!capturePath.isEmpty()) {
            QDir().mkpath(QFileInfo(capturePath).absolutePath());
            check(window.grab().save(capturePath),"capture populated dialog");
        }
        window.reject();
    });
    check(window.exec() == QDialog::Rejected && !window.ok,"close after preview does not apply");
    QTimer::singleShot(0,&window,[&] {
        window.load(true);
        window.hOffsetEnabled(QStringLiteral("10"));
        check(apply && !apply->isEnabled(),"offset change invalidates preview");
        window.load(true);
        window.accept();
    });
    check(window.exec() == QDialog::Accepted && window.ok,"explicit apply accepts a complete tile");
    const float acceptedHeight = window.terrainData[0][0];
    check(acceptedHeight > 10,"applied data are finite heights");
    window.terrainResolution = 512;
    QTimer::singleShot(0,&window,[&] {
        QTimer::singleShot(0,&window,[&] {
            auto *progress = window.findChild<QProgressDialog*>();
            if (progress) if (auto *button = progress->findChild<QPushButton*>()) button->click();
        });
        window.load(true);
        check(HeightWindow::lastLoadWasCancelled() && !window.ok && apply && !apply->isEnabled(),
              "cancelled worker does not apply and exposes cancellation to batch caller");
        window.reject();
    });
    window.exec();
    window.terrainResolution = 16;
    for (int i = 0; i < 25; ++i) qToBigEndian<qint16>(-32768,bytes.data()+2*i);
    check(file.open(QIODevice::WriteOnly|QIODevice::Truncate) && file.write(bytes) == bytes.size(),"replace fixture with HGT voids");
    file.close();
    QTimer::singleShot(0,&window,[&] {
        window.load(true);
        check(!window.ok && apply && !apply->isEnabled(),"failed reload cannot apply stale prepared data");
        window.reject();
    });
    window.exec();
    settings.setSessionValue("core.paths.geoData",root);
    settings.setSessionValue("geo.elevation.source",source);
    qInfo() << "[tests:elevation-ui] cases=" << passed+failed << "passed=" << passed << "failed=" << failed;
    return failed ? 1 : 0;
}
