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
const char *FallbackSetting = "geo.elevation.fallback";
QPointer<QPlainTextEdit> automaticReport;
bool lastElevationCancelled = false;
bool distantSourceApproved(const QVector<Elevation::Dataset> &catalogue,
                           const QString &id,bool fallback) {
    for(const auto &dataset:catalogue)if(dataset.id==id)
        return dataset.distantTerrainApproved&&(!fallback||dataset.fallbackApproved);
    return false;
}
QString summarize(const Elevation::Result &result, const QString &source) {
    const auto &r = result.report;
    //% "Source: %1\nSource samples: %2; fallback samples: %3\nCached blocks: %4; downloads: %5"
    QString text = qtTrId("geo.elevation.report.summary")
        .arg(source).arg(r.primarySamples).arg(r.fallbackSamples).arg(r.cacheHits).arg(r.downloads);
    if (r.filledPixels) {
        //% "\nFilled source NoData pixels: %1 (estimated from neighbouring heights)."
        text += qtTrId("geo.elevation.report.filled").arg(r.filledPixels);
    }
    if (r.fallbackSamples || r.noDataSamples || r.outsideSamples || r.unavailableSamples) {
        //% "\nFallback (%1): %2 samples. Missing/zero data: %3; outside coverage: %4; unavailable data: %5."
        text += qtTrId("geo.elevation.report.fallback")
            .arg(r.fallbackSourceName.isEmpty() ? QStringLiteral("none") : r.fallbackSourceName)
            .arg(r.fallbackSamples).arg(r.noDataSamples).arg(r.outsideSamples).arg(r.unavailableSamples);
    }
    if (r.fallbackSamples) {
        //% "\nSource and fallback heights may use different vertical datums. No vertical datum conversion is applied."
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
QString sourceInformation(const Elevation::Dataset &dataset,const QString &root) {
    const auto escaped=[](const QString &value){return value.toHtmlEscaped();};
    const auto link=[&](const QString &label,const QUrl &url){
        const QString href=url.toString(QUrl::FullyEncoded).toHtmlEscaped();
        return QStringLiteral("<b>%1:</b> <a href=\"%2\">%3</a><br>")
            .arg(label.toHtmlEscaped(),href,escaped(url.toString()));
    };
    //% "Source"
    QString html=QStringLiteral("<b>%1:</b> %2<br>")
        .arg(qtTrId("geo.elevation.info.source").toHtmlEscaped(),escaped(dataset.name));
    //% "Catalogue"
    const QString catalogueLabel=qtTrId("geo.elevation.info.catalogue").toHtmlEscaped();
    //% "User-defined"
    const QString catalogueOrigin=dataset.userDefined
        ?qtTrId("geo.elevation.info.user.defined").toHtmlEscaped()
        //% "Built-in"
        :qtTrId("geo.elevation.info.built.in").toHtmlEscaped();
    html+=QStringLiteral("<b>%1:</b> %2<br>").arg(catalogueLabel,catalogueOrigin);
    if(dataset.resolution>0) {
        //% "Native/request resolution"
        html+=QStringLiteral("<b>%1:</b> %2 m<br>")
            .arg(qtTrId("geo.elevation.info.resolution").toHtmlEscaped())
            .arg(dataset.resolution,0,'g',10);
    }
    if(!dataset.verticalDatum.isEmpty()) {
        //% "Vertical datum"
        html+=QStringLiteral("<b>%1:</b> %2<br>")
            .arg(qtTrId("geo.elevation.info.datum").toHtmlEscaped(),escaped(dataset.verticalDatum));
    }
    if(!dataset.attribution.isEmpty()) {
        //% "Attribution"
        html+=QStringLiteral("<b>%1:</b> %2<br>")
            .arg(qtTrId("geo.elevation.info.attribution").toHtmlEscaped(),escaped(dataset.attribution));
    }
    if(!dataset.license.isEmpty()) {
        //% "License"
        html+=QStringLiteral("<b>%1:</b> %2<br>")
            .arg(qtTrId("geo.elevation.info.license").toHtmlEscaped(),escaped(dataset.license));
    }
    if(dataset.fileGrid=="directory") {
        //% "User-managed files"
        html+=QStringLiteral("<b>%1:</b> %2<br>")
            .arg(qtTrId("geo.elevation.info.mode").toHtmlEscaped(),
                 qtTrId("geo.elevation.info.manual").toHtmlEscaped());
        //% "Local directory"
        html+=QStringLiteral("<b>%1:</b> %2<br>")
            .arg(qtTrId("geo.elevation.info.directory").toHtmlEscaped(),
                 escaped(QDir::toNativeSeparators(QDir(root).filePath(dataset.directory))));
    }
    if(!dataset.information.isEmpty())html+=QStringLiteral("<p>%1</p>").arg(escaped(dataset.information));
    const QUrl more=dataset.informationUrl.isEmpty()?dataset.attributionUrl:dataset.informationUrl;
    //% "More information"
    if(!more.isEmpty())html+=link(qtTrId("geo.elevation.info.more"),more);
    //% "Download data"
    if(!dataset.downloadPage.isEmpty())html+=link(qtTrId("geo.elevation.info.download"),dataset.downloadPage);
    return html;
}
}

HeightWindow::HeightWindow() : QDialog() {
    //% "Terrain elevation"
    setWindowTitle(qtTrId("geo.elevation.title"));
    sourceBox = new QComboBox(this);
    sourceBox->setObjectName(QStringLiteral("elevationSourceBox"));
    sourceBox->setStyleSheet(QStringLiteral("combobox-popup: 0;"));
    //% "Select an elevation source for this location"
    sourceBox->setPlaceholderText(qtTrId("geo.elevation.source.select.local"));
    const auto *sourceDefinition = SettingsManager::instance().registry().definition(SourceSetting);
    if (sourceDefinition)
        for (const auto &option : sourceDefinition->resolvedOptions())
            sourceBox->addItem(option.displayName(),option.value);
    fallbackBox = new QComboBox(this);
    fallbackBox->setObjectName(QStringLiteral("elevationFallbackBox"));
    fallbackBox->setStyleSheet(QStringLiteral("combobox-popup: 0;"));
    const auto *fallbackDefinition = SettingsManager::instance().registry().definition(FallbackSetting);
    if (fallbackDefinition)
        for (const auto &option : fallbackDefinition->resolvedOptions())
            fallbackBox->addItem(option.displayName(),option.value);
    sourceOffsetEdit = new QLineEdit(QStringLiteral("0"),this);
    sourceOffsetEdit->setObjectName(QStringLiteral("elevationSourceOffset"));
    sourceOffsetEdit->setMaximumWidth(70);
    fallbackOffsetEdit = new QLineEdit(QStringLiteral("0"),this);
    fallbackOffsetEdit->setObjectName(QStringLiteral("elevationFallbackOffset"));
    fallbackOffsetEdit->setMaximumWidth(70);
    for (auto *edit : {sourceOffsetEdit,fallbackOffsetEdit}) {
        auto *validator = new QDoubleValidator(-9999,9999,2,edit);
        validator->setNotation(QDoubleValidator::StandardNotation);
        edit->setValidator(validator);
    }
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
    reportText = new QTextBrowser(this);
    reportText->setReadOnly(true);
    reportText->setOpenExternalLinks(true);
    reportText->setMaximumHeight(150);
    auto *top = new QGridLayout;
    //% "Main:"
    top->addWidget(new QLabel(qtTrId("geo.elevation.source.main"),this),0,0);
    top->addWidget(sourceBox,0,1);
    //% "Y (m):"
    top->addWidget(new QLabel(qtTrId("geo.elevation.offset.short"),this),0,2);
    top->addWidget(sourceOffsetEdit,0,3);
    //% "Fallback:"
    top->addWidget(new QLabel(qtTrId("geo.elevation.source.fallback"),this),1,0);
    top->addWidget(fallbackBox,1,1);
    top->addWidget(new QLabel(qtTrId("geo.elevation.offset.short"),this),1,2);
    top->addWidget(fallbackOffsetEdit,1,3);
    top->setColumnStretch(1,1);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(loadButton); buttons->addStretch();
    buttons->addWidget(applyButton); buttons->addWidget(closeButton);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top); layout->addWidget(imageLabel,1);
    //% "Source resolution depends on the selected dataset. Output spacing follows this terrain tile. Missing coverage and NoData use the selected fallback."
    auto *note = new QLabel(qtTrId("geo.elevation.source.note"),this);
    note->setWordWrap(true); layout->addWidget(note);
    layout->addWidget(reportText); layout->addLayout(buttons);
    resize(820,720);
    connect(loadButton,&QPushButton::clicked,this,[this] { load(true); });
    connect(applyButton,&QPushButton::clicked,this,&QDialog::accept);
    connect(closeButton,&QPushButton::clicked,this,&QDialog::reject);
    connect(sourceOffsetEdit,&QLineEdit::textEdited,this,&HeightWindow::offsetsChanged);
    connect(fallbackOffsetEdit,&QLineEdit::textEdited,this,&HeightWindow::offsetsChanged);
    connect(sourceBox,&QComboBox::currentIndexChanged,this,[this] {
        prepared = ok = false; applyButton->setEnabled(false);
        loadButton->setEnabled(sourceBox->currentIndex() >= 0 && fallbackBox->currentIndex() >= 0);
        if (sourceBox->currentIndex() < 0) return;
        QString error;
        if (!SettingsManager::instance().setSessionValue(QString::fromLatin1(SourceSetting),sourceBox->currentData(),&error))
            reportText->setPlainText(error);
        else showSourceInformation();
    });
    connect(fallbackBox,&QComboBox::currentIndexChanged,this,[this] {
        prepared = ok = false; applyButton->setEnabled(false);
        loadButton->setEnabled(sourceBox->currentIndex() >= 0 && fallbackBox->currentIndex() >= 0);
        if (fallbackBox->currentIndex() < 0) return;
        QString error;
        if (!SettingsManager::instance().setSessionValue(QString::fromLatin1(FallbackSetting),fallbackBox->currentData(),&error))
            reportText->setPlainText(error);
        else showSourceInformation();
    });
}
void HeightWindow::showSourceInformation() {
    if(sourceBox->currentIndex()<0){reportText->clear();return;}
    QString catalogueError;
    const auto catalog=Elevation::datasets(catalogueError);
    const QString id=sourceBox->currentData().toString();
    for(const auto &dataset:catalog)if(dataset.id==id){
        QString html=sourceInformation(dataset,Settings::string("core.paths.geoData",SettingType::Directory));
        const QString fallbackId=fallbackBox->currentData().toString();
        for(const auto &fallback:catalog)if(fallback.id==fallbackId){
            //% "Fallback"
            html+=QStringLiteral("<p><b>%1:</b> %2</p>")
                .arg(qtTrId("geo.elevation.info.fallback").toHtmlEscaped(),fallback.name.toHtmlEscaped());
            break;
        }
        if(!catalogueError.isEmpty())html+=QStringLiteral("<p>%1</p>").arg(catalogueError.toHtmlEscaped());
        reportText->setHtml(html);return;
    }
    reportText->setPlainText(catalogueError);
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
    QString catalogueError;
    const auto catalog = Elevation::datasets(catalogueError);
    if (!catalogueError.isEmpty()) reportText->setPlainText(catalogueError);
    //% "Terrain elevation - tile %1 %2"
    setWindowTitle(qtTrId("geo.elevation.tile.title").arg(tileX).arg(-tileZ));
    QString selected = Settings::string(SourceSetting,SettingType::Enum);
    if (selected.isEmpty()) selected = Elevation::defaultFileSourceId(catalog);
    QString selectedFallback = Settings::string(FallbackSetting,SettingType::Enum);
    if (selectedFallback.isEmpty()) selectedFallback = Elevation::defaultFallbackSourceId(catalog);
    if(distantTerrain){
        if(!distantSourceApproved(catalog,selected,false))
            selected=Elevation::defaultDistantTerrainSourceId(catalog);
        if(!distantSourceApproved(catalog,selectedFallback,true))
            selectedFallback=Elevation::defaultDistantTerrainFallbackSourceId(catalog);
    }
    QVector<Elevation::Point> area;
    if (Game::GeoCoordConverter && terrainSize > 0) {
        PreciseTileCoordinate coordinate;
        coordinate.TileX = tileX; coordinate.TileZ = tileZ;
        IghCoordinate internal;
        LatitudeLongitudeCoordinate geographic;
        for (int y=0;y<3;++y) for (int x=0;x<3;++x) {
            coordinate.setWxyzU(float(x*terrainSize*.5),0,float(y*terrainSize*.5));
            Game::GeoCoordConverter->ConvertToInternal(&coordinate,&internal);
            Game::GeoCoordConverter->ConvertToLatLon(&internal,&geographic);
            if (std::isfinite(geographic.Latitude) && std::isfinite(geographic.Longitude)
                    && std::abs(geographic.Latitude) <= 90 && std::abs(geographic.Longitude) <= 180)
                area.push_back({geographic.Latitude,geographic.Longitude});
        }
    }
    { const QSignalBlocker blocker(sourceBox);
      sourceBox->clear();
      bool known = false;
      for (const auto &dataset : catalog) {
          if(distantTerrain&&!dataset.distantTerrainApproved)continue;
          known |= dataset.id == selected;
          if (Elevation::nearDataset(dataset,area)) sourceBox->addItem(dataset.name,dataset.id);
      }
      if (!known&&!distantTerrain)
          sourceBox->addItem(qtTrId("settings.dialog.text.widget").arg(selected),selected);
      sourceBox->setCurrentIndex(sourceBox->findData(selected)); }
    { const QSignalBlocker blocker(fallbackBox);
      fallbackBox->clear();
      for (const auto &dataset : catalog)
          if (dataset.fallbackApproved&&(!distantTerrain||dataset.distantTerrainApproved)
                  &&Elevation::nearDataset(dataset,area))
              fallbackBox->addItem(dataset.name,dataset.id);
      fallbackBox->setCurrentIndex(fallbackBox->findData(selectedFallback)); }
    loadButton->setEnabled(sourceBox->currentIndex() >= 0 && fallbackBox->currentIndex() >= 0);
    showSourceInformation();
    const int result = QDialog::exec();
    if (previousContext && previousSurface) previousContext->makeCurrent(previousSurface);
    return result;
}
void HeightWindow::done(int result) {
    if (loading) return;
    ok = result == QDialog::Accepted && prepared;
    QDialog::done(result);
}
void HeightWindow::offsetsChanged() {
    bool valid;
    sourceOffset = sourceOffsetEdit->locale().toFloat(sourceOffsetEdit->text(),&valid);
    if (!valid) sourceOffset = 0;
    fallbackOffset = fallbackOffsetEdit->locale().toFloat(fallbackOffsetEdit->text(),&valid);
    if (!valid) fallbackOffset = 0;
    prepared = ok = false; applyButton->setEnabled(false);
}
void HeightWindow::load(bool gui) {
    if (loading) return;
    if (gui && (sourceBox->currentIndex() < 0 || fallbackBox->currentIndex() < 0)) return;
    resetLoadCancellation();
    ok = prepared = false; applyButton->setEnabled(false);
    if (terrainResolution < 1 || terrainResolution > 4096 || terrainSize < 1 || !Game::GeoCoordConverter) {
        //% "Invalid terrain grid or coordinate converter."
        reportText->setPlainText(qtTrId("geo.elevation.grid.invalid")); return;
    }
    loading = true;
    QPointer<QOpenGLContext> previousContext = QOpenGLContext::currentContext();
    QSurface *previousSurface = previousContext ? previousContext->surface() : nullptr;
    loadButton->setEnabled(false); sourceBox->setEnabled(false); fallbackBox->setEnabled(false);
    sourceOffsetEdit->setEnabled(false); fallbackOffsetEdit->setEnabled(false);
    const QString root = Settings::string("core.paths.geoData",SettingType::Directory);
    QString dataset = gui ? sourceBox->currentData().toString() : Settings::string(SourceSetting,SettingType::Enum);
    QString fallbackDataset = gui ? fallbackBox->currentData().toString() : Settings::string(FallbackSetting,SettingType::Enum);
    QString catalogueError;
    const auto catalog = Elevation::datasets(catalogueError);
    if (dataset.isEmpty()) dataset = Elevation::defaultFileSourceId(catalog);
    if (fallbackDataset.isEmpty()) fallbackDataset = Elevation::defaultFallbackSourceId(catalog);
    if(distantTerrain){
        if(!distantSourceApproved(catalog,dataset,false))
            dataset=Elevation::defaultDistantTerrainSourceId(catalog);
        if(!distantSourceApproved(catalog,fallbackDataset,true))
            fallbackDataset=Elevation::defaultDistantTerrainFallbackSourceId(catalog);
    }
    const int sourceIndex = sourceBox->findData(dataset);
    QString sourceName = sourceIndex < 0 ? dataset : sourceBox->itemText(sourceIndex);
    // Snapshot only the selected primary/fallback secrets on the UI thread.
    QMap<QString,QString> secrets;
    for (const auto &entry : catalog) if (entry.id == dataset || entry.id == fallbackDataset) {
        if (entry.id == dataset) sourceName = entry.name;
        if (!entry.apiKeySecret.isEmpty())
            secrets.insert(entry.apiKeySecret,SettingsManager::instance().secretValue(entry.apiKeySecret));
        if (!entry.basicUsernameSecret.isEmpty())
            secrets.insert(entry.basicUsernameSecret,SettingsManager::instance().secretValue(entry.basicUsernameSecret));
        if (!entry.basicPasswordSecret.isEmpty())
            secrets.insert(entry.basicPasswordSecret,SettingsManager::instance().secretValue(entry.basicPasswordSecret));
    }
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
    const float primaryOffset = sourceOffset, secondaryOffset = fallbackOffset;
    QThread *worker = QThread::create([&] {
        try {
            result = Elevation::generate(root,dataset,points,step,primaryOffset,secondaryOffset,cancel,[&](int done,int total,const QString &message) {
                    if (!gui) return; // setValue() can otherwise auto-show the dialog.
                QMetaObject::invokeMethod(&progress,[&,done,total,message] {
                    progress.setLabelText(message); progress.setRange(0,total); progress.setValue(done);
                },Qt::QueuedConnection);
            },secrets,fallbackDataset);
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
    loadButton->setEnabled(true); sourceBox->setEnabled(true); fallbackBox->setEnabled(true);
    sourceOffsetEdit->setEnabled(true); fallbackOffsetEdit->setEnabled(true);
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
    QString catalogueError;
    const auto catalog = Elevation::datasets(catalogueError);
    QString fallbackId = Settings::string(FallbackSetting,SettingType::Enum);
    if (fallbackId.isEmpty()) fallbackId = Elevation::defaultFallbackSourceId(catalog);
    QString selected = Settings::string(SourceSetting,SettingType::Enum);
    if (selected.isEmpty()) selected = Elevation::defaultFileSourceId(catalog);
    const Elevation::Dataset *fileSource = nullptr;
    for (const auto &dataset : catalog)
        if ((dataset.id == selected || dataset.id == fallbackId)
                && dataset.provider == "file" && dataset.format == "hgt") {
            fileSource = &dataset;break;
        }
    if (!fileSource) {
        QMessageBox::information(nullptr,qtTrId("geo.elevation.data.title"),
            qtTrId("geo.elevation.hgt.fallback.check"));
        return;
    }
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
            if (Elevation::findHgtFile(root,*fileSource,lat,lon).isEmpty()) missing.insert(Elevation::hgtFileName(lat,lon));
    }
    QStringList names = missing.values(); names.sort();
    QString message = selected == fileSource->id
        //% "Elevation file-source check"
        ? qtTrId("geo.elevation.hgt.check")
        //% "Fallback file-source check. Elevation data is prepared when terrain is loaded."
        : qtTrId("geo.elevation.hgt.fallback.check");
    //% "\nAll checked elevation files are present."
    message += names.isEmpty() ? qtTrId("geo.elevation.hgt.present") : //% "\nMissing local elevation files (downloaded automatically when supported):\n%1"
        qtTrId("geo.elevation.hgt.missing").arg(names.join('\n'));
    //% "Elevation data"
    QMessageBox::information(nullptr,qtTrId("geo.elevation.data.title"),message);
}
