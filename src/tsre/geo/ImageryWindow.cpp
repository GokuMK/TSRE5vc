/* TSRE5 - Copyright (C) 2016 Piotr Gadecki. GPL-3.0-or-later. */
#include <tsre/geo/ImageryWindow.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/geo/ImagerySource.h>
#include <tsre/geo/MapWindow.h>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>

#include <QComboBox>
#include <QEventLoop>
#include <QGridLayout>
#include <QLabel>
#include <QOpenGLContext>
#include <QProgressDialog>
#include <QPushButton>
#include <QTextBrowser>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>

namespace {
const char *SourceSetting = "geo.imagery.source";

QString link(const QString &label,const QUrl &url) {
    return QStringLiteral("<b>%1:</b> <a href=\"%2\">%3</a><br>")
        .arg(label.toHtmlEscaped(),url.toString(QUrl::FullyEncoded).toHtmlEscaped(),
             url.toString().toHtmlEscaped());
}

QString sourceInformation(const Imagery::Dataset &dataset) {
    QString html=QStringLiteral("<b>%1:</b> %2<br>")
        //% "Source"
        .arg(qtTrId("geo.imagery.info.source").toHtmlEscaped(),dataset.name.toHtmlEscaped());
    QString catalogueOrigin;
    if(dataset.userDefined) {
        //% "User-defined"
        catalogueOrigin=qtTrId("geo.imagery.info.user.defined");
    } else {
        //% "Built-in"
        catalogueOrigin=qtTrId("geo.imagery.info.built.in");
    }
    html+=QStringLiteral("<b>%1:</b> %2<br>")
        //% "Catalogue"
        .arg(qtTrId("geo.imagery.info.catalogue").toHtmlEscaped(),
             catalogueOrigin.toHtmlEscaped());
    html+=QStringLiteral("<b>%1:</b> %2 m<br>")
        //% "Native resolution"
        .arg(qtTrId("geo.imagery.info.resolution").toHtmlEscaped())
        .arg(dataset.nativeResolution,0,'g',8);
    if(!dataset.persistentCache) html+=QStringLiteral("<b>%1:</b> %2<br>")
        //% "Cache"
        .arg(qtTrId("geo.imagery.info.cache").toHtmlEscaped(),
             //% "Disabled by source definition"
             qtTrId("geo.imagery.info.cache.disabled").toHtmlEscaped());
    if(!dataset.attribution.isEmpty()) html+=QStringLiteral("<b>%1:</b> %2<br>")
        //% "Attribution"
        .arg(qtTrId("geo.imagery.info.attribution").toHtmlEscaped(),dataset.attribution.toHtmlEscaped());
    if(!dataset.license.isEmpty()) html+=QStringLiteral("<b>%1:</b> %2<br>")
        //% "License"
        .arg(qtTrId("geo.imagery.info.license").toHtmlEscaped(),dataset.license.toHtmlEscaped());
    if(!dataset.information.isEmpty()) html+=QStringLiteral("<p>%1</p>").arg(dataset.information.toHtmlEscaped());
    //% "Attribution information"
    if(!dataset.attributionUrl.isEmpty()) html+=link(qtTrId("geo.imagery.info.attribution.link"),dataset.attributionUrl);
    //% "More information"
    if(!dataset.informationUrl.isEmpty()) html+=link(qtTrId("geo.imagery.info.more"),dataset.informationUrl);
    return html;
}

QString summary(const Imagery::Result &result) {
    QString text;
    if(result.report.zoom>=0) {
        //% "Zoom level: %1; required images: %2; cache hits: %3; downloads: %4 (%5 MiB)"
        text=qtTrId("geo.imagery.report.summary")
            .arg(result.report.zoom).arg(result.report.tiles).arg(result.report.cacheHits)
            .arg(result.report.downloads).arg(result.report.downloadedBytes/1024.0/1024.0,0,'f',2);
    } else {
        //% "Source images: %1; cache hits: %2; downloads: %3 (%4 MiB)"
        text=qtTrId("geo.imagery.report.image.summary")
            .arg(result.report.tiles).arg(result.report.cacheHits).arg(result.report.downloads)
            .arg(result.report.downloadedBytes/1024.0/1024.0,0,'f',2);
    }
    //% "Output spacing: %1 m/pixel; selected source spacing: %2 m/pixel."
    text+='\n'+qtTrId("geo.imagery.report.resolution")
        .arg(result.report.targetMetresPerPixel,0,'f',2)
        .arg(result.report.sourceMetresPerPixel,0,'f',2);
    //% "Cancelled. No imagery was applied."
    if(result.cancelled) text+='\n'+qtTrId("geo.imagery.report.cancelled");
    if(!result.error.isEmpty()) text+='\n'+result.error;
    if(!result.report.issues.isEmpty()) text+='\n'+result.report.issues.join('\n');
    return text;
}
}

ImageryWindow::ImageryWindow(QWidget *parent) : QDialog(parent) {
    //% "Terrain imagery"
    setWindowTitle(qtTrId("geo.imagery.title"));
    sourceBox=new QComboBox(this);
    sourceBox->setObjectName(QStringLiteral("imagerySourceBox"));
    sourceBox->setStyleSheet(QStringLiteral("combobox-popup: 0;"));
    //% "Select an imagery source for this location"
    sourceBox->setPlaceholderText(qtTrId("geo.imagery.source.select.local"));
    resolutionBox=new QComboBox(this);
    resolutionBox->setObjectName(QStringLiteral("imageryResolutionBox"));
    resolutionBox->setStyleSheet(QStringLiteral("combobox-popup: 0;"));
    resolutionBox->setMaximumWidth(100);
    resolutionLabel=new QLabel(this);
    resolutionLabel->setWordWrap(true);
    imageLabel=new QLabel(this);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setMinimumSize(560,360);
    reportText=new QTextBrowser(this);
    reportText->setReadOnly(true);
    reportText->setOpenExternalLinks(true);
    reportText->setMaximumHeight(190);
    //% "Load preview"
    loadButton=new QPushButton(qtTrId("geo.imagery.preview"),this);
    //% "Apply"
    applyButton=new QPushButton(qtTrId("geo.imagery.apply"),this);
    applyButton->setEnabled(false);
    //% "Close"
    auto *closeButton=new QPushButton(qtTrId("geo.imagery.close"),this);

    auto *top=new QGridLayout;
    //% "Source:"
    top->addWidget(new QLabel(qtTrId("geo.imagery.source"),this),0,0);
    top->addWidget(sourceBox,0,1);
    //% "Res.:"
    top->addWidget(new QLabel(qtTrId("geo.imagery.resolution.short"),this),0,2);
    top->addWidget(resolutionBox,0,3);
    top->setColumnStretch(1,1);
    auto *buttons=new QHBoxLayout;
    buttons->addWidget(loadButton);buttons->addStretch();
    buttons->addWidget(applyButton);buttons->addWidget(closeButton);
    auto *layout=new QVBoxLayout(this);
    layout->addLayout(top);layout->addWidget(resolutionLabel);
    layout->addWidget(imageLabel,1);layout->addWidget(reportText);layout->addLayout(buttons);
    resize(860,760);

    connect(loadButton,&QPushButton::clicked,this,&ImageryWindow::loadPreview);
    connect(applyButton,&QPushButton::clicked,this,&ImageryWindow::apply);
    connect(closeButton,&QPushButton::clicked,this,&QDialog::reject);
    connect(sourceBox,&QComboBox::currentIndexChanged,this,[this] {
        invalidatePreview();
        loadButton->setEnabled(sourceBox->currentIndex()>=0);
        if(sourceBox->currentIndex()<0)return;
        updateRequestSizes();
        if(!distantTerrain) {
            QString error;
            if(!SettingsManager::instance().setSessionValue(QString::fromLatin1(SourceSetting),
                                                             sourceBox->currentData(),&error)) {
                reportText->setPlainText(error);
                return;
            }
        }
        showSourceInformation();
    });
    connect(resolutionBox,&QComboBox::currentIndexChanged,this,[this] {
        if(sourceBox->currentIndex()>=0 && resolutionBox->currentIndex()>=0)
            selectedRequestSizes.insert(sourceBox->currentData().toString(),
                                        resolutionBox->currentData().toInt());
        invalidatePreview();
    });
}

void ImageryWindow::invalidatePreview() {
    preparedImage={};
    imageLabel->clear();
    applyButton->setEnabled(false);
}

void ImageryWindow::showSourceInformation() {
    QString error;
    const auto catalogue=Imagery::datasets(error);
    const QString id=sourceBox->currentData().toString();
    for(const auto &dataset:catalogue)if(dataset.id==id){
        QString html=sourceInformation(dataset);
        if(!error.isEmpty())html+=QStringLiteral("<p>%1</p>").arg(error.toHtmlEscaped());
        reportText->setHtml(html);
        return;
    }
    reportText->setPlainText(error);
}

void ImageryWindow::updateRequestSizes() {
    const QSignalBlocker blocker(resolutionBox);
    resolutionBox->clear();
    QString error;
    const auto catalogue=Imagery::datasets(error);
    const QString id=sourceBox->currentData().toString();
    for(const auto &dataset:catalogue)if(dataset.id==id){
        if(dataset.requestSizes.isEmpty()) {
            //% "Auto"
            resolutionBox->addItem(qtTrId("geo.imagery.resolution.auto"),0);
            resolutionBox->setEnabled(false);
            return;
        }
        for(const int size:dataset.requestSizes)
            resolutionBox->addItem(QString::number(size),size);
        int selected=selectedRequestSizes.value(id,dataset.defaultRequestSize);
        int index=resolutionBox->findData(selected);
        if(index<0)index=resolutionBox->findData(dataset.defaultRequestSize);
        if(index<0)index=0;
        resolutionBox->setCurrentIndex(index);
        selectedRequestSizes.insert(id,resolutionBox->currentData().toInt());
        resolutionBox->setEnabled(resolutionBox->count()>1);
        return;
    }
    resolutionBox->setEnabled(false);
}

int ImageryWindow::exec() {
    QPointer<QOpenGLContext> previousContext=QOpenGLContext::currentContext();
    QSurface *previousSurface=previousContext?previousContext->surface():nullptr;
    invalidatePreview();
    //% "Terrain imagery - tile %1 %2"
    setWindowTitle(qtTrId("geo.imagery.tile.title").arg(tileX).arg(-tileZ));
    const int outputSize=Settings::integer("core.maps.imageResolution");
    //% "Output: %1 x %1 pixels, approximately %2 m/pixel for this tile."
    resolutionLabel->setText(qtTrId("geo.imagery.output.summary").arg(outputSize)
        .arg(terrainSize>0?double(terrainSize)/outputSize:0,0,'f',2));
    QString error;
    const auto catalogue=Imagery::datasets(error);
    QString selected=Settings::string(SourceSetting,SettingType::Enum);
    const auto approved=[&](const QString &id){
        for(const auto &dataset:catalogue)if(dataset.id==id)
            return distantTerrain?dataset.distantTerrainApproved:dataset.detailedTerrainApproved;
        return false;
    };
    if(!approved(selected)) selected=distantTerrain
        ?Imagery::defaultDistantSourceId(catalogue):Imagery::defaultDetailedSourceId(catalogue);
    QVector<Imagery::GeographicPoint> area;
    if(Game::GeoCoordConverter&&terrainSize>0){
        PreciseTileCoordinate coordinate;
        coordinate.TileX=tileX;coordinate.TileZ=-tileZ;
        IghCoordinate internal;LatitudeLongitudeCoordinate geographic;
        for(int row=0;row<3;++row)for(int column=0;column<3;++column){
            coordinate.setWxyzU(float(column*terrainSize*.5),0,float(row*terrainSize*.5));
            Game::GeoCoordConverter->ConvertToInternal(&coordinate,&internal);
            Game::GeoCoordConverter->ConvertToLatLon(&internal,&geographic);
            if(std::isfinite(geographic.Latitude)&&std::isfinite(geographic.Longitude))
                area.push_back({geographic.Latitude,geographic.Longitude});
        }
    }
    {
        const QSignalBlocker blocker(sourceBox);
        sourceBox->clear();
        for(const auto &dataset:catalogue){
            const bool domain=distantTerrain?dataset.distantTerrainApproved:dataset.detailedTerrainApproved;
            if(domain&&Imagery::nearDataset(dataset,area))sourceBox->addItem(dataset.name,dataset.id);
        }
        int selectedIndex=sourceBox->findData(selected);
        if(selectedIndex<0){
            const QString domainDefault=distantTerrain
                ?Imagery::defaultDistantSourceId(catalogue):Imagery::defaultDetailedSourceId(catalogue);
            selectedIndex=sourceBox->findData(domainDefault);
        }
        if(selectedIndex<0&&sourceBox->count()>0)selectedIndex=0;
        sourceBox->setCurrentIndex(selectedIndex);
    }
    updateRequestSizes();
    loadButton->setEnabled(sourceBox->currentIndex()>=0);
    if(!error.isEmpty())reportText->setPlainText(error);else showSourceInformation();
    const int result=QDialog::exec();
    if(previousContext&&previousSurface)previousContext->makeCurrent(previousSurface);
    return result;
}

void ImageryWindow::done(int result) {
    if(loading)return;
    QDialog::done(result);
}

void ImageryWindow::loadPreview() {
    if(loading||sourceBox->currentIndex()<0||!Game::GeoCoordConverter)return;
    invalidatePreview();
    const int outputSize=Settings::integer("core.maps.imageResolution");
    if(outputSize<64||outputSize>16384||terrainSize<=0){
        //% "Invalid imagery output size or terrain tile."
        reportText->setPlainText(qtTrId("geo.imagery.grid.invalid"));
        return;
    }
    Imagery::Request request;
    request.root=Settings::string("core.paths.geoData",SettingType::Directory);
    request.datasetId=sourceBox->currentData().toString();
    request.sourcePixels=resolutionBox->currentData().toInt();
    QString secretCatalogueError;
    for(const auto &dataset:Imagery::datasets(secretCatalogueError))
        if(dataset.id==request.datasetId&&!dataset.apiKeySecret.isEmpty()){
            request.secrets.insert(dataset.apiKeySecret,
                SettingsManager::instance().secretValue(dataset.apiKeySecret));
            break;
        }
    request.width=request.height=outputSize;
    request.controlColumns=request.controlRows=33;
    request.terrainSizeMetres=terrainSize;
    request.controlPoints.reserve(request.controlColumns*request.controlRows);
    PreciseTileCoordinate coordinate;
    coordinate.TileX=tileX;coordinate.TileZ=-tileZ;
    IghCoordinate internal;LatitudeLongitudeCoordinate geographic;
    for(int row=0;row<request.controlRows;++row)for(int column=0;column<request.controlColumns;++column){
        coordinate.setWxyzU(float(double(column)*terrainSize/(request.controlColumns-1)),0,
                            float(double(row)*terrainSize/(request.controlRows-1)));
        Game::GeoCoordConverter->ConvertToInternal(&coordinate,&internal);
        Game::GeoCoordConverter->ConvertToLatLon(&internal,&geographic);
        request.controlPoints.push_back({geographic.Latitude,geographic.Longitude});
    }
    loading=true;
    loadButton->setEnabled(false);sourceBox->setEnabled(false);resolutionBox->setEnabled(false);
    //% "Preparing terrain imagery"
    QProgressDialog progress(qtTrId("geo.imagery.prepare"),
        //% "Cancel"
        qtTrId("geo.imagery.cancel"),0,0,this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setAutoClose(false);progress.setAutoReset(false);
    std::atomic_bool cancel{false};
    QEventLoop loop;
    Imagery::Result result;
    connect(&progress,&QProgressDialog::canceled,&loop,[&]{
        cancel=true;
        //% "Cancelling imagery load"
        progress.setLabelText(qtTrId("geo.imagery.cancelling"));
        progress.setCancelButton(nullptr);
    });
    QThread *worker=QThread::create([&]{
        try{
            result=Imagery::generate(request,cancel,[&](int done,int total,const QString &message){
                QMetaObject::invokeMethod(&progress,[&,done,total,message]{
                    progress.setLabelText(message);progress.setRange(0,total);progress.setValue(done);
                },Qt::QueuedConnection);
            });
        }catch(const std::exception &exception){
            //% "Imagery load failed: %1"
            result.error=qtTrId("geo.imagery.load.failed").arg(QString::fromUtf8(exception.what()));
        }
    });
    connect(worker,&QThread::finished,&loop,&QEventLoop::quit);
    progress.show();worker->start();loop.exec();worker->wait();delete worker;progress.hide();
    if(cancel){result.cancelled=true;result.image={};}
    loading=false;loadButton->setEnabled(true);sourceBox->setEnabled(true);
    resolutionBox->setEnabled(resolutionBox->count()>1);
    QString information;
    QString catalogueError;
    for(const auto &dataset:Imagery::datasets(catalogueError))
        if(dataset.id==request.datasetId){information=sourceInformation(dataset);break;}
    reportText->setHtml(information+QStringLiteral("<pre>%1</pre>").arg(summary(result).toHtmlEscaped()));
    if(!result.success())return;
    preparedImage=std::move(result.image);
    imageLabel->setPixmap(QPixmap::fromImage(preparedImage).scaled(800,500,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    applyButton->setEnabled(true);
}

void ImageryWindow::apply() {
    if(preparedImage.isNull()||loading)return;
    QImage image;
    if(MapWindow::isAlpha>0){
        image=preparedImage.convertToFormat(QImage::Format_RGBA8888);
        const uchar alpha=uchar(255-MapWindow::isAlpha);
        for(int y=0;y<image.height();++y){
            uchar *line=image.scanLine(y);
            for(int x=0;x<image.width();++x)line[x*4+3]=alpha;
        }
    }else image=preparedImage.convertToFormat(QImage::Format_RGB888);
    MapWindow::setTileImage(tileX,tileZ,image);
    QDialog::accept();
}
