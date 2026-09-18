/* TSRE5 - Copyright (C) 2016 Piotr Gadecki. GPL-3.0-or-later. */
#include <tsre/geo/HeightWindow.h>
#include <tsre/geo/GeoCoordinates.h>
#include <settings/SettingsAccess.h>
#include <QThread>
#include <QPointer>
#include <QOpenGLContext>
#include <algorithm>
#include <cmath>
#include <exception>

namespace {
const char *SourceSetting = "geo.elevation.source";
QPointer<QPlainTextEdit> automaticReport;
bool lastElevationCancelled = false;
QString summarize(const Elevation::Result &result, const QString &source) {
    const auto &r = result.report;
    //% "Source: %1\nSource samples: %2; HGT samples: %3\nCache blocks: %4; downloaded blocks: %5"
    QString text = qtTrId("geo.elevation.report.summary")
        .arg(source).arg(r.primarySamples).arg(r.hgtSamples).arg(r.cacheHits).arg(r.downloads);
    if (r.fallbackSamples || r.noDataSamples || r.outsideSamples || r.unavailableSamples) {
        //% "\nHGT fallback: %1 samples. Missing/zero data: %2; outside coverage: %3; unavailable blocks: %4."
        text += qtTrId("geo.elevation.report.fallback")
            .arg(r.fallbackSamples).arg(r.noDataSamples).arg(r.outsideSamples).arg(r.unavailableSamples);
        //% "\nSource and HGT heights may use different vertical datums. No vertical datum conversion is applied."
        text += qtTrId("geo.elevation.report.datum");
    }
    //% "\nCancelled. No elevation heights were applied."
    if (result.cancelled) text += qtTrId("geo.elevation.report.cancelled");
    if (!result.error.isEmpty()) text += "\n" + result.error;
    if (!r.issues.isEmpty()) text += "\n" + r.issues.join('\n');
    return text;
}
void showAutomaticReport(const QString &text) {
    if (!automaticReport) {
        automaticReport = new QPlainTextEdit;
        automaticReport->setAttribute(Qt::WA_DeleteOnClose);
        automaticReport->setAttribute(Qt::WA_ShowWithoutActivating);
        //% "Terrain elevation report"
        automaticReport->setWindowTitle(qtTrId("geo.elevation.report.title"));
        automaticReport->setReadOnly(true);
        automaticReport->setMaximumBlockCount(1000);
        automaticReport->resize(680,360);
    }
    automaticReport->appendPlainText(text+"\n");
    automaticReport->show();
}
}

HeightWindow::HeightWindow() : QDialog() {
    //% "Terrain elevation"
    setWindowTitle(qtTrId("geo.elevation.title"));
    sourceBox = new QComboBox(this);
    //% "Local HGT files"
    sourceBox->addItem(qtTrId("geo.elevation.source.hgt"),QString());
    QString catalogError;
    for (const auto &dataset : Elevation::datasets(catalogError)) sourceBox->addItem(dataset.name,dataset.id);
    offsetEdit = new QLineEdit(QStringLiteral("0"),this);
    offsetEdit->setMaximumWidth(90);
    auto *validator = new QDoubleValidator(-9999,9999,2,offsetEdit);
    validator->setNotation(QDoubleValidator::StandardNotation);
    offsetEdit->setValidator(validator);
    //% "Load preview"
    loadButton = new QPushButton(qtTrId("geo.elevation.preview"),this);
    //% "Apply"
    applyButton = new QPushButton(qtTrId("geo.elevation.apply"),this);
    applyButton->setEnabled(false);
    //% "Close"
    auto *closeButton = new QPushButton(qtTrId("geo.elevation.close"),this);
    imageLabel = new QLabel(this);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setMinimumSize(480,360);
    reportText = new QPlainTextEdit(this);
    reportText->setReadOnly(true);
    reportText->setMaximumHeight(150);
    if (!catalogError.isEmpty()) reportText->setPlainText(catalogError);
    auto *top = new QHBoxLayout;
    top->addWidget(sourceBox,1);
    //% "Y offset (m):"
    top->addWidget(new QLabel(qtTrId("geo.elevation.offset"),this));
    top->addWidget(offsetEdit);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(loadButton); buttons->addStretch();
    buttons->addWidget(applyButton); buttons->addWidget(closeButton);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top); layout->addWidget(imageLabel,1);
    //% "Source resolution depends on the selected dataset. Output spacing follows this terrain tile. Missing coverage and NoData use HGT fallback."
    auto *note = new QLabel(qtTrId("geo.elevation.source.note"),this);
    note->setWordWrap(true); layout->addWidget(note);
    layout->addWidget(reportText); layout->addLayout(buttons);
    resize(820,720);
    connect(loadButton,&QPushButton::clicked,this,[this] { load(true); });
    connect(applyButton,&QPushButton::clicked,this,&QDialog::accept);
    connect(closeButton,&QPushButton::clicked,this,&QDialog::reject);
    connect(offsetEdit,&QLineEdit::textEdited,this,&HeightWindow::hOffsetEnabled);
    connect(sourceBox,&QComboBox::currentIndexChanged,this,[this] {
        prepared = ok = false; applyButton->setEnabled(false);
        SettingsManager::instance().setSessionValue(QString::fromLatin1(SourceSetting),sourceBox->currentData());
    });
}
void HeightWindow::clearData() {
    for (int i = 0; i < allocatedTerrainResolution; ++i) delete[] terrainData[i];
    delete[] terrainData;
    terrainData = nullptr; allocatedTerrainResolution = 0;
}
HeightWindow::~HeightWindow() { clearData(); }
bool HeightWindow::lastLoadWasCancelled() { return lastElevationCancelled; }
void HeightWindow::resetLoadCancellation() { lastElevationCancelled = false; }
int HeightWindow::exec() {
    QPointer<QOpenGLContext> previousContext = QOpenGLContext::currentContext();
    QSurface *previousSurface = previousContext ? previousContext->surface() : nullptr;
    ok = prepared = false; applyButton->setEnabled(false);
    imageLabel->clear(); reportText->clear();
    //% "Terrain elevation - tile %1 %2"
    setWindowTitle(qtTrId("geo.elevation.tile.title").arg(tileX).arg(-tileZ));
    const QString selected = Settings::string(SourceSetting,SettingType::Enum);
    { const QSignalBlocker blocker(sourceBox);
      sourceBox->setCurrentIndex(std::max(0,sourceBox->findData(selected))); }
    const int result = QDialog::exec();
    if (previousContext && previousSurface) previousContext->makeCurrent(previousSurface);
    return result;
}
void HeightWindow::done(int result) {
    if (loading) return;
    ok = result == QDialog::Accepted && prepared;
    QDialog::done(result);
}
void HeightWindow::hOffsetEnabled(QString value) {
    bool valid;
    yOffset = offsetEdit->locale().toFloat(value,&valid);
    if (!valid) yOffset = 0;
    prepared = ok = false; applyButton->setEnabled(false);
}
void HeightWindow::load(bool gui) {
    if (loading) return;
    resetLoadCancellation();
    ok = prepared = false; applyButton->setEnabled(false);
    if (terrainResolution < 1 || terrainResolution > 4096 || terrainSize < 1 || !Game::GeoCoordConverter) {
        //% "Invalid terrain grid or coordinate converter."
        reportText->setPlainText(qtTrId("geo.elevation.grid.invalid")); return;
    }
    loading = true;
    QPointer<QOpenGLContext> previousContext = QOpenGLContext::currentContext();
    QSurface *previousSurface = previousContext ? previousContext->surface() : nullptr;
    loadButton->setEnabled(false); sourceBox->setEnabled(false); offsetEdit->setEnabled(false);
    const QString root = Settings::string("core.paths.geoData",SettingType::Directory);
    const QString dataset = gui ? sourceBox->currentData().toString() : Settings::string(SourceSetting,SettingType::Enum);
    const QString sourceName = sourceBox->itemText(std::max(0,sourceBox->findData(dataset)));
    QVector<Elevation::Point> points;
    points.reserve(qsizetype(terrainResolution)*terrainResolution);
    PreciseTileCoordinate coordinate;
    coordinate.TileX = tileX; coordinate.TileZ = tileZ;
    IghCoordinate internal;
    LatitudeLongitudeCoordinate geographic;
    const double step = double(terrainSize)/terrainResolution;
    for (int i = 0; i < terrainResolution; ++i) for (int j = 0; j < terrainResolution; ++j) {
        coordinate.setWxyzU(float(i*step),0,float(j*step));
        Game::GeoCoordConverter->ConvertToInternal(&coordinate,&internal);
        Game::GeoCoordConverter->ConvertToLatLon(&internal,&geographic);
        points.push_back({geographic.Latitude,geographic.Longitude});
    }
    //% "Preparing terrain elevation"
    QProgressDialog progress(qtTrId("geo.elevation.prepare"),//% "Cancel"
        qtTrId("geo.elevation.cancel"),0,0,this);
    progress.setWindowModality(gui ? Qt::ApplicationModal : Qt::NonModal);
    progress.setAutoClose(false); progress.setAutoReset(false);
    if (!gui) progress.reset(); // Stop any automatic-show timer.
    std::atomic_bool cancel{false};
    QEventLoop loop;
    Elevation::Result result;
    connect(&progress,&QProgressDialog::canceled,&loop,[&] {
        cancel = true;
        //% "Cancelling elevation load"
        progress.setLabelText(qtTrId("geo.elevation.cancelling"));
        progress.setCancelButton(nullptr); progress.show();
    });
    const float offset = yOffset;
    QThread *worker = QThread::create([&] {
        try {
            result = Elevation::generate(root,dataset,points,step,offset,cancel,[&](int done,int total,const QString &message) {
                    if (!gui) return; // setValue() can otherwise auto-show the dialog.
                QMetaObject::invokeMethod(&progress,[&,done,total,message] {
                    progress.setLabelText(message); progress.setRange(0,total); progress.setValue(done);
                },Qt::QueuedConnection);
            });
        } catch (const std::exception &e) {
            //% "Elevation load failed: %1"
            result.error = qtTrId("geo.elevation.load.failed").arg(QString::fromUtf8(e.what()));
        }
    });
    connect(worker,&QThread::finished,&loop,&QEventLoop::quit);
    if (gui) progress.show();
    worker->start(); loop.exec(); worker->wait(); delete worker;
    progress.hide();
    if (cancel) { result.cancelled = true; result.heights.clear(); }
    lastElevationCancelled = result.cancelled;
    if (previousContext && previousSurface) previousContext->makeCurrent(previousSurface);
    loading = false;
    loadButton->setEnabled(true); sourceBox->setEnabled(true); offsetEdit->setEnabled(true);
    const QString report = summarize(result,sourceName);
    reportText->setPlainText(report);
    if (!gui && (!result.success() || result.report.fallbackSamples))
        //% "Tile %1 %2\n%3"
        showAutomaticReport(qtTrId("geo.elevation.tile.report").arg(tileX).arg(-tileZ).arg(report));
    if (!result.success()) return;
    clearData();
    terrainData = new float*[terrainResolution];
    for (int i = 0; i < terrainResolution; ++i) {
        terrainData[i] = new float[terrainResolution];
        ++allocatedTerrainResolution;
        std::copy_n(result.heights.constData()+qsizetype(i)*terrainResolution,terrainResolution,terrainData[i]);
    }
    prepared = true;
    if (!gui) { ok = true; return; }
    const auto extremes = std::minmax_element(result.heights.cbegin(),result.heights.cend());
    const float low = *extremes.first, span = *extremes.second-low;
    QImage preview(terrainResolution,terrainResolution,QImage::Format_RGB32);
    for (int i = 0; i < terrainResolution; ++i) for (int j = 0; j < terrainResolution; ++j) {
        const int value = span > 0 ? std::clamp(int((terrainData[i][j]-low)*255/span),0,255) : 128;
        preview.setPixel(i,j,qRgb(value,value,value));
    }
    imageLabel->setPixmap(QPixmap::fromImage(preview).scaled(760,480,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    applyButton->setEnabled(true);
}

void HeightWindow::CheckForMissingGeodataFiles(QMap<int,QPair<int,int>*> &tiles) {
    if (!Game::GeoCoordConverter) return;
    const QString root = Settings::string("core.paths.geoData",SettingType::Directory);
    QSet<QString> missing;
    for (auto it = tiles.cbegin(); it != tiles.cend(); ++it) {
        if (!it.value()) continue;
        PreciseTileCoordinate coordinate;
        coordinate.TileX = it.value()->first; coordinate.TileZ = it.value()->second;
        IghCoordinate internal;
        LatitudeLongitudeCoordinate geographic;
        QSet<int> latitudes, longitudes;
        for (int x : {0,1024,2048}) for (int y : {0,1024,2048}) {
            coordinate.setWxyzU(x,0,y);
            Game::GeoCoordConverter->ConvertToInternal(&coordinate,&internal);
            Game::GeoCoordConverter->ConvertToLatLon(&internal,&geographic);
            latitudes.insert(int(std::floor(geographic.Latitude)));
            longitudes.insert(int(std::floor(geographic.Longitude)));
        }
        for (int lat : latitudes) for (int lon : longitudes)
            if (Elevation::findHgtFile(root,lat,lon).isEmpty()) missing.insert(Elevation::hgtFileName(lat,lon));
    }
    QStringList names = missing.values(); names.sort();
    QString message = Settings::string(SourceSetting,SettingType::Enum).isEmpty()
        //% "Local HGT file check"
        ? qtTrId("geo.elevation.hgt.check")
        //% "HGT fallback file check. Elevation blocks are prepared when terrain is loaded."
        : qtTrId("geo.elevation.hgt.fallback.check");
    //% "\nAll checked HGT files are present."
    message += names.isEmpty() ? qtTrId("geo.elevation.hgt.present") : //% "\nMissing HGT files:\n%1"
        qtTrId("geo.elevation.hgt.missing").arg(names.join('\n'));
    //% "Elevation data"
    QMessageBox::information(nullptr,qtTrId("geo.elevation.data.title"),message);
}
