#include <tsre/geo/ImagerySource.h>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <cmath>
#include <functional>

void runImageryTests(const std::function<void(bool,const char*)> &check) {
    QString error;
    const auto catalogue=Imagery::builtInDatasets(error);
    check(error.isEmpty()&&catalogue.size()==7,"imagery built-in catalogue parses");
    const Imagery::Dataset *poland=nullptr,*world=nullptr,*usa=nullptr,*usaHigh=nullptr;
    const Imagery::Dataset *czechia=nullptr,*netherlands=nullptr,*slovakia=nullptr;
    for(const auto &dataset:catalogue){
        if(dataset.id=="pl.gugik.orto.standard")poland=&dataset;
        if(dataset.id=="world.esa.worldcover-s2-2021")world=&dataset;
        if(dataset.id=="us.usgs.imagery-only")usa=&dataset;
        if(dataset.id=="us.usgs.naip-plus")usaHigh=&dataset;
        if(dataset.id=="cz.cuzk.ortofoto")czechia=&dataset;
        if(dataset.id=="nl.pdok.luchtfoto-rgb-25cm")netherlands=&dataset;
        if(dataset.id=="sk.gku.ortofotomozaika")slovakia=&dataset;
    }
    check(poland&&world&&usa&&usaHigh&&czechia&&netherlands&&slovakia
          &&poland->detailedTerrainApproved&&!poland->distantTerrainApproved
          &&world->distantTerrainApproved&&usa->detailedTerrainApproved
          &&!usa->distantTerrainApproved&&usaHigh->detailedTerrainApproved
          &&!usaHigh->distantTerrainApproved&&czechia->detailedTerrainApproved
          &&!czechia->distantTerrainApproved&&netherlands->detailedTerrainApproved
          &&!netherlands->distantTerrainApproved&&slovakia->detailedTerrainApproved
          &&!slovakia->distantTerrainApproved,"imagery terrain-domain approvals");
    if(!poland||!world||!usa||!usaHigh||!czechia||!netherlands||!slovakia)return;
    check(poland->requestSizes==QVector<int>({4096,2048,1024})
          &&poland->defaultRequestSize==4096&&world->requestSizes.isEmpty()
          &&czechia->requestSizes==QVector<int>({4096,2048,1024})
          &&netherlands->requestSizes==QVector<int>({4096,2048,1024})
          &&poland->requestBlockPixels==2048&&czechia->requestBlockPixels==2048
          &&usaHigh->requestBlockPixels==2048
          &&netherlands->maxRequestPixels==2500
          &&netherlands->requestBlockPixels==2048
          &&slovakia->requestSizes==QVector<int>({4096,2048,1024})
          &&slovakia->defaultRequestSize==4096
          &&slovakia->requestBlockPixels==2048,
          "imagery source-specific request sizes and default");

    const QUrl polandUrl=Imagery::arcGisMapUrl(*poland,637000,486000,639048,488048,
                                               2048,2048);
    const QUrlQuery polandQuery(polandUrl);
    check(polandQuery.queryItemValue("bboxSR")=="2180"
          &&polandQuery.queryItemValue("bbox")=="637000.000,486000.000,639048.000,488048.000"
          &&polandQuery.queryItemValue("size")=="2048,2048"
          &&polandQuery.queryItemValue("layers")=="show:3",
          "Poland MapServer projected export request");
    const QUrl worldUrl=Imagery::tileUrl(*world,{14,9148,5394});
    const QUrlQuery worldQuery(worldUrl);
    check(worldQuery.queryItemValue("TILEMATRIX")=="14"
          &&worldQuery.queryItemValue("TIME")=="2021-01-01",
          "WorldCover WMTS matrix and time dimension");
    check(Imagery::chooseZoom(*world,52.0,.5)==13,
          "imagery zoom selection observes native resolution");
    const QUrl usaUrl=Imagery::tileUrl(*usa,{16,13655,24872});
    const QUrlQuery usaQuery(usaUrl);
    check(usaQuery.queryItemValue("LAYER")=="USGSImageryOnly"
          &&usaQuery.queryItemValue("TILEMATRIXSET")=="GoogleMapsCompatible"
          &&usaQuery.queryItemValue("TILEMATRIX")=="16"
          &&usaQuery.queryItemValue("FORMAT")=="image/jpeg"
          &&Imagery::chooseZoom(*usa,39.7392,.5)==16,
          "USGS WMTS request and published zoom ceiling");
    const QUrl usaHighUrl=Imagery::arcGisImageUrl(*usaHigh,-11688798.399,4826783.931,
                                                  -11686135.070,4829447.261,4000,4000);
    const QUrlQuery usaHighQuery(usaHighUrl);
    check(usaHighUrl.path().endsWith("/ImageServer/exportImage")
          &&usaHighQuery.queryItemValue("bboxSR")=="3857"
          &&usaHighQuery.queryItemValue("size")=="4000,4000"
          &&usaHighQuery.queryItemValue("adjustAspectRatio")=="false"
          &&usaHighQuery.queryItemValue("renderingRule").contains("NaturalColor")
          &&usaHigh->requestSizes==QVector<int>({4000,2048,1024})
          &&usaHigh->defaultRequestSize==4000,
          "USGS NAIP Plus ImageServer request and selectable resolutions");
    check(Imagery::nearDataset(*usa,{{39.7392,-104.9903}},0)
          &&!Imagery::nearDataset(*usa,{{52.2297,21.0122}},0),
          "US imagery source is limited to contiguous-US locations");
    const QUrl czechUrl=Imagery::arcGisMapUrl(*czechia,1606000,6445000,
                                              1609200,6448200,4096,4096);
    const QUrlQuery czechQuery(czechUrl);
    check(czechQuery.queryItemValue("bboxSR")=="3857"
          &&czechQuery.queryItemValue("size")=="4096,4096"
          &&!czechQuery.hasQueryItem("layers"),
          "Czech Ortofoto single-image ArcGIS export request");
    const QUrl netherlandsUrl=Imagery::wmsUrl(*netherlands,569000,6810000,
                                              571000,6812000,2048,2048);
    const QUrlQuery netherlandsQuery(netherlandsUrl);
    check(netherlandsQuery.queryItemValue("LAYERS")=="Actueel_ortho25"
          &&netherlandsQuery.queryItemValue("CRS")=="EPSG:3857"
          &&netherlandsQuery.queryItemValue("WIDTH")=="2048"
          &&netherlandsQuery.queryItemValue("HEIGHT")=="2048",
          "PDOK current 25 cm WMS block request");
    check(Imagery::nearDataset(*czechia,{{50.0755,14.4378}},0)
          &&Imagery::nearDataset(*netherlands,{{52.0907,5.1214}},0)
          &&!Imagery::nearDataset(*netherlands,{{50.0755,14.4378}},0),
          "Czech and Dutch imagery bounds filter unrelated locations");
    const QUrl slovakiaUrl=Imagery::arcGisMapUrl(*slovakia,1903000,6129976,
                                                 1904024,6131000,2048,2048);
    const QUrlQuery slovakiaQuery(slovakiaUrl);
    check(slovakiaQuery.queryItemValue("bboxSR")=="3857"
          &&slovakiaQuery.queryItemValue("bbox")=="1903000.000,6129976.000,1904024.000,6131000.000"
          &&slovakiaQuery.queryItemValue("size")=="2048,2048"
          &&!slovakiaQuery.hasQueryItem("layers")
          &&Imagery::nearDataset(*slovakia,{{48.1486,17.1077}},0)
          &&!Imagery::nearDataset(*slovakia,{{52.0907,5.1214}},0),
          "Slovak Ortofotomozaika export and national bounds");
    const QPointF origin=Imagery::webMercatorPixel({0,0},0,256);
    check(std::abs(origin.x()-128)<1e-9&&std::abs(origin.y()-128)<1e-9,
          "Web Mercator origin pixel");
    check(Imagery::cacheRelativePath(*world,{13,1,2})
              .endsWith("world_esa_worldcover_s2_2021/2021_v2/EPSG_3857/13/1/2.png"),
          "imagery cache path is readable and Windows-safe");

    QFile builtIn(":/geo/imagery-datasets.json");
    check(builtIn.open(QIODevice::ReadOnly),"open built-in imagery catalogue fixture");
    const QByteArray user=R"json({"version":1,"datasets":[
      {"id":"broken","name":"Broken","provider":"unknown"},
      {"id":"pl.gugik.orto.standard","name":"Custom Poland","provider":"wmts-kvp-webmercator",
       "endpoint":"https://example.invalid/wmts","layer":"ortho","style":"default","format":"image/jpeg",
       "tileMatrixSet":"EPSG:3857","tileMatrixTemplate":"{zoom}","tilePixels":256,
       "minZoom":0,"maxZoom":19,"nativeResolution":1,"boundsWgs84":[14,49,24,55],
       "detailedTerrainApproved":true,"directory":"custom_poland","revision":"v1"}
    ]})json";
    const auto merged=Imagery::mergeDatasets(builtIn.readAll(),user,error);
    bool custom=false,broken=false,worldRetained=false;
    for(const auto &dataset:merged){
        custom|=dataset.id=="pl.gugik.orto.standard"&&dataset.name=="Custom Poland"&&dataset.userDefined;
        broken|=dataset.id=="broken";
        worldRetained|=dataset.id==world->id;
    }
    check(custom&&!broken&&worldRetained&&error.contains("broken"),
          "invalid user imagery object is isolated while valid override merges");

    QTemporaryDir temporary;
    Imagery::Request unsupported;
    unsupported.root=temporary.path();unsupported.datasetId=poland->id;
    unsupported.width=unsupported.height=64;
    unsupported.controlColumns=unsupported.controlRows=2;
    unsupported.terrainSizeMetres=64;unsupported.sourcePixels=3072;
    unsupported.controlPoints={{52.0000,21.0000},{52.0000,21.0002},
                               {51.9998,21.0000},{51.9998,21.0002}};
    std::atomic_bool unsupportedCancel{false};
    const auto unsupportedResult=Imagery::generate(unsupported,unsupportedCancel);
    check(!unsupportedResult.success()&&unsupportedResult.error.contains("Unsupported imagery request size"),
          "imagery provider rejects request sizes outside the dataset set");

    Imagery::Request request;
    request.root=temporary.path();request.datasetId=world->id;
    request.width=request.height=64;request.controlColumns=request.controlRows=2;
    request.terrainSizeMetres=64;
    request.controlPoints={{52.0000,21.0000},{52.0000,21.0002},
                           {51.9998,21.0000},{51.9998,21.0002}};
    const int zoom=Imagery::chooseZoom(*world,52,1);
    int minColumn=INT_MAX,maxColumn=INT_MIN,minRow=INT_MAX,maxRow=INT_MIN;
    for(const auto point:request.controlPoints){
        const QPointF pixel=Imagery::webMercatorPixel(point,zoom,world->tilePixels);
        minColumn=std::min(minColumn,int(std::floor((pixel.x()-1)/world->tilePixels)));
        maxColumn=std::max(maxColumn,int(std::floor((pixel.x()+1)/world->tilePixels)));
        minRow=std::min(minRow,int(std::floor((pixel.y()-1)/world->tilePixels)));
        maxRow=std::max(maxRow,int(std::floor((pixel.y()+1)/world->tilePixels)));
    }
    QImage tile(world->tilePixels,world->tilePixels,QImage::Format_RGB888);
    tile.fill(QColor(20,40,60));
    bool cacheReady=true;
    for(int row=minRow;row<=maxRow;++row)for(int column=minColumn;column<=maxColumn;++column){
        const QString path=QDir(temporary.path()).filePath(
            Imagery::cacheRelativePath(*world,{zoom,column,row}));
        cacheReady&=QDir().mkpath(QFileInfo(path).absolutePath())&&tile.save(path,"PNG");
    }
    std::atomic_bool cancel{false};
    const auto generated=Imagery::generate(request,cancel);
    const QColor centre=generated.image.isNull()?QColor():generated.image.pixelColor(32,32);
    check(cacheReady&&generated.success()&&generated.report.downloads==0
          &&generated.report.cacheHits>0&&centre==QColor(20,40,60),
          "cached WMTS tiles compose into terrain imagery without network");
}
