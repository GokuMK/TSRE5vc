#include <tsre/geo/ImagerySource.h>
#include <tsre/geo/CogImagerySource.h>

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
    check(error.isEmpty()&&catalogue.size()==20,"imagery built-in catalogue parses");
    const Imagery::Dataset *poland=nullptr,*world=nullptr,*usa=nullptr,*usaHigh=nullptr;
    const Imagery::Dataset *czechia=nullptr,*netherlands=nullptr,*slovakia=nullptr,*france=nullptr;
    const Imagery::Dataset *flanders=nullptr,*wallonia=nullptr,*spain=nullptr,*lithuania=nullptr;
    const Imagery::Dataset *estonia=nullptr,*croatia=nullptr,*slovenia=nullptr,*luxembourg=nullptr;
    const Imagery::Dataset *portugal=nullptr,*denmark=nullptr,*austria=nullptr,*switzerland=nullptr;
    for(const auto &dataset:catalogue){
        if(dataset.id=="pl.gugik.orto.standard")poland=&dataset;
        if(dataset.id=="world.esa.worldcover-s2-2021")world=&dataset;
        if(dataset.id=="us.usgs.imagery-only")usa=&dataset;
        if(dataset.id=="us.usgs.naip-plus")usaHigh=&dataset;
        if(dataset.id=="cz.cuzk.ortofoto")czechia=&dataset;
        if(dataset.id=="nl.pdok.luchtfoto-rgb-25cm")netherlands=&dataset;
        if(dataset.id=="sk.gku.ortofotomozaika")slovakia=&dataset;
        if(dataset.id=="fr.ign.bd-ortho")france=&dataset;
        if(dataset.id=="be.vlaanderen.ortho-winter-latest")flanders=&dataset;
        if(dataset.id=="be.wallonia.ortho-2025-spring")wallonia=&dataset;
        if(dataset.id=="es.ign.pnoa-latest")spain=&dataset;
        if(dataset.id=="lt.geoportal.ort10lt")lithuania=&dataset;
        if(dataset.id=="ee.maaamet.orthophoto-latest")estonia=&dataset;
        if(dataset.id=="hr.dgu.orthophoto-2023-2024")croatia=&dataset;
        if(dataset.id=="si.gurs.dof-latest")slovenia=&dataset;
        if(dataset.id=="lu.geoportail.ortho-2025-summer")luxembourg=&dataset;
        if(dataset.id=="pt.dgt.orthophoto-2025")portugal=&dataset;
        if(dataset.id=="dk.datafordeler.ortofoto-foraar")denmark=&dataset;
        if(dataset.id=="at.bev.dop-20220128")austria=&dataset;
        if(dataset.id=="ch.swisstopo.swissimage-dop10")switzerland=&dataset;
    }
    check(poland&&world&&usa&&usaHigh&&czechia&&netherlands&&slovakia&&france
          &&flanders&&wallonia&&spain&&lithuania
          &&estonia&&croatia&&slovenia&&luxembourg&&portugal&&denmark&&austria&&switzerland
          &&poland->detailedTerrainApproved&&!poland->distantTerrainApproved
          &&world->distantTerrainApproved&&usa->detailedTerrainApproved
          &&!usa->distantTerrainApproved&&usaHigh->detailedTerrainApproved
          &&!usaHigh->distantTerrainApproved&&czechia->detailedTerrainApproved
          &&!czechia->distantTerrainApproved&&netherlands->detailedTerrainApproved
          &&!netherlands->distantTerrainApproved&&slovakia->detailedTerrainApproved
          &&!slovakia->distantTerrainApproved&&france->detailedTerrainApproved
          &&!france->distantTerrainApproved&&flanders->detailedTerrainApproved
          &&!flanders->distantTerrainApproved&&wallonia->detailedTerrainApproved
          &&!wallonia->distantTerrainApproved&&spain->detailedTerrainApproved
          &&!spain->distantTerrainApproved&&lithuania->detailedTerrainApproved
          &&!lithuania->distantTerrainApproved&&estonia->detailedTerrainApproved
          &&!estonia->distantTerrainApproved&&croatia->detailedTerrainApproved
          &&!croatia->distantTerrainApproved&&slovenia->detailedTerrainApproved
          &&!slovenia->distantTerrainApproved&&luxembourg->detailedTerrainApproved
          &&!luxembourg->distantTerrainApproved&&portugal->detailedTerrainApproved
          &&!portugal->distantTerrainApproved&&denmark->detailedTerrainApproved
          &&!denmark->distantTerrainApproved&&austria->detailedTerrainApproved
          &&!austria->distantTerrainApproved&&switzerland->detailedTerrainApproved
          &&!switzerland->distantTerrainApproved,"imagery terrain-domain approvals");
    if(!poland||!world||!usa||!usaHigh||!czechia||!netherlands||!slovakia||!france
       ||!flanders||!wallonia||!spain||!lithuania
       ||!estonia||!croatia||!slovenia||!luxembourg||!portugal||!denmark||!austria||!switzerland)return;
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
          &&slovakia->requestBlockPixels==2048
          &&france->requestSizes==QVector<int>({4096,2048,1024})
          &&france->defaultRequestSize==2048
          &&france->requestBlockPixels==0
          &&flanders->requestSizes==QVector<int>({4096,2048,1024})
          &&flanders->defaultRequestSize==4096&&flanders->maxRequestPixels==2048
          &&flanders->requestBlockPixels==2048
          &&wallonia->requestSizes==QVector<int>({4096,2048,1024})
          &&wallonia->defaultRequestSize==4096&&wallonia->requestBlockPixels==0
          &&spain->requestSizes==QVector<int>({4096,2048,1024})
          &&spain->defaultRequestSize==4096&&spain->requestBlockPixels==2048
          &&lithuania->requestSizes==QVector<int>({4096,2048,1024})
          &&lithuania->defaultRequestSize==4096&&lithuania->requestBlockPixels==2048
          &&estonia->defaultRequestSize==4096&&estonia->requestBlockPixels==2048
          &&croatia->defaultRequestSize==4096&&croatia->requestBlockPixels==2048
          &&slovenia->defaultRequestSize==4096&&slovenia->requestBlockPixels==2048
          &&luxembourg->defaultRequestSize==4096&&luxembourg->requestBlockPixels==2048
          &&portugal->requestSizes==QVector<int>({4096})
          &&portugal->defaultRequestSize==4096&&portugal->requestBlockPixels==0
          &&denmark->requestSizes==QVector<int>({4096,2048,1024})
          &&denmark->defaultRequestSize==2048
          &&austria->requestSizes==QVector<int>({4096,2048,1024})
          &&austria->defaultRequestSize==4096&&austria->requestBlockPixels==0
          &&switzerland->requestSizes==QVector<int>({4096,2048,1024})
          &&switzerland->defaultRequestSize==4096&&switzerland->requestBlockPixels==0,
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
    const QUrl franceUrl=Imagery::wmsUrl(*france,651445,6861011,
                                         653493,6863059,2048,2048);
    const QUrlQuery franceQuery(franceUrl);
    check(franceQuery.queryItemValue("LAYERS")=="HR.ORTHOIMAGERY.ORTHOPHOTOS"
          &&franceQuery.queryItemValue("CRS")=="EPSG:2154"
          &&franceQuery.queryItemValue("BBOX")=="651445.000,6861011.000,653493.000,6863059.000"
          &&franceQuery.queryItemValue("WIDTH")=="2048"
          &&franceQuery.queryItemValue("HEIGHT")=="2048"
          &&Imagery::nearDataset(*france,{{48.8566,2.3522}},0)
          &&!Imagery::nearDataset(*france,{{52.0907,5.1214}},0),
          "French BD ORTHO native Lambert-93 WMS request and bounds");
    const QUrl flandersUrl=Imagery::wmsUrl(*flanders,648304,664238,
                                            650352,666286,2048,2048);
    const QUrlQuery flandersQuery(flandersUrl);
    check(flandersQuery.queryItemValue("LAYERS")=="Ortho"
          &&flandersQuery.queryItemValue("CRS")=="EPSG:3812"
          &&flandersQuery.queryItemValue("WIDTH")=="2048"
          &&Imagery::nearDataset(*flanders,{{51.2194,4.4025}},0),
          "Flanders orthophoto WMS request and bounds");
    const QUrl walloniaUrl=Imagery::wmsUrl(*wallonia,667700,593400,
                                           669748,595448,4096,4096);
    const QUrlQuery walloniaQuery(walloniaUrl);
    check(walloniaQuery.queryItemValue("LAYERS")=="0"
          &&walloniaQuery.queryItemValue("CRS")=="EPSG:3812"
          &&walloniaQuery.queryItemValue("WIDTH")=="4096"
          &&Imagery::nearDataset(*wallonia,{{50.4674,4.8718}},0),
          "Wallonia 2025 orthophoto WMS request and bounds");
    const QUrl spainUrl=Imagery::wmsUrl(*spain,-413024,4925976,
                                        -410976,4928024,2048,2048);
    const QUrlQuery spainQuery(spainUrl);
    check(spainQuery.queryItemValue("LAYERS")=="OI.OrthoimageCoverage"
          &&spainQuery.queryItemValue("CRS")=="EPSG:3857"
          &&spainQuery.queryItemValue("WIDTH")=="2048"
          &&Imagery::nearDataset(*spain,{{40.4168,-3.7038}},0),
          "Spain PNOA WMS request and bounds");
    const QUrl lithuaniaUrl=Imagery::arcGisMapUrl(*lithuania,2812000,7306000,
                                                  2814048,7308048,2048,2048);
    const QUrlQuery lithuaniaQuery(lithuaniaUrl);
    check(lithuaniaQuery.queryItemValue("bboxSR")=="3857"
          &&lithuaniaQuery.queryItemValue("size")=="2048,2048"
          &&!lithuaniaQuery.hasQueryItem("layers")
          &&Imagery::nearDataset(*lithuania,{{54.6872,25.2797}},0)
          &&!Imagery::nearDataset(*lithuania,{{40.4168,-3.7038}},0),
          "Lithuania ORT10LT export and bounds");
    const QUrl estoniaUrl=Imagery::wmsUrl(*estonia,541500,6588000,
                                          543548,6590048,2048,2048);
    const QUrlQuery estoniaQuery(estoniaUrl);
    check(estoniaQuery.queryItemValue("LAYERS")=="EESTIFOTO"
          &&estoniaQuery.queryItemValue("CRS")=="EPSG:3301"
          &&estoniaQuery.queryItemValue("BBOX")=="6588000.000,541500.000,6590048.000,543548.000"
          &&Imagery::nearDataset(*estonia,{{59.437,24.7536}},0),
          "Estonia orthophoto WMS native axis order and bounds");
    const QUrl croatiaUrl=Imagery::wmsUrl(*croatia,457000,5073000,
                                          459048,5075048,2048,2048);
    const QUrlQuery croatiaQuery(croatiaUrl);
    check(croatiaQuery.queryItemValue("LAYERS")=="OI.OrthoimageCoverage"
          &&croatiaQuery.queryItemValue("CRS")=="EPSG:3765"
          &&croatiaQuery.queryItemValue("BBOX")=="457000.000,5073000.000,459048.000,5075048.000"
          &&Imagery::nearDataset(*croatia,{{45.815,15.9819}},0),
          "Croatia complete-cycle orthophoto WMS request and bounds");
    const QUrl sloveniaUrl=Imagery::wmsUrl(*slovenia,460000,99000,
                                           462048,101048,2048,2048);
    const QUrlQuery sloveniaQuery(sloveniaUrl);
    check(sloveniaQuery.queryItemValue("LAYERS")=="SI.GURS.ZPDZ:OI.OrthoimageCoverage"
          &&sloveniaQuery.queryItemValue("CRS")=="EPSG:3794"
          &&Imagery::nearDataset(*slovenia,{{46.0569,14.5058}},0),
          "Slovenia DOF native D96/TM WMS request and bounds");
    const QUrl luxembourgUrl=Imagery::wmsUrl(*luxembourg,680000,6370000,
                                             682048,6372048,2048,2048);
    const QUrlQuery luxembourgQuery(luxembourgUrl);
    check(luxembourgQuery.queryItemValue("LAYERS")=="3207"
          &&luxembourgQuery.queryItemValue("CRS")=="EPSG:3857"
          &&Imagery::nearDataset(*luxembourg,{{49.6116,6.1319}},0)
          &&!Imagery::nearDataset(*luxembourg,{{50.4674,4.8718}},0),
          "Luxembourg summer 2025 orthophoto WMS request and bounds");
    const QUrl portugalUrl=Imagery::wmsUrl(*portugal,-90000,-106000,
                                           -87952,-103952,4096,4096);
    const QUrlQuery portugalQuery(portugalUrl);
    check(portugalQuery.queryItemValue("LAYERS")=="Ortos2025-RGB"
          &&portugalQuery.queryItemValue("CRS")=="EPSG:3763"
          &&portugalQuery.queryItemValue("BBOX")=="-90000.000,-106000.000,-87952.000,-103952.000"
          &&portugalQuery.queryItemValue("WIDTH")=="4096"
          &&Imagery::nearDataset(*portugal,{{38.7223,-9.1393}},0)
          &&!Imagery::nearDataset(*portugal,{{40.4168,-3.7038}},0),
          "Portugal 2025 orthophoto native PT-TM06 WMS request and bounds");
    const QUrl denmarkUrl=Imagery::tileUrl(*denmark,{18,135019,80639});
    const QUrlQuery denmarkQuery(denmarkUrl);
    check(denmark->apiKeySecret=="geo.elevation.dk.datafordeler.apiKey"
          &&denmark->apiKeyParameter=="apikey"
          &&denmarkQuery.queryItemValue("LAYER")=="orto_foraar_webm"
          &&denmarkQuery.queryItemValue("TILEMATRIXSET")=="DFD_GoogleMapsCompatible"
          &&denmarkQuery.queryItemValue("TILEMATRIX")=="18"
          &&!denmarkQuery.hasQueryItem("apikey")
          &&Imagery::nearDataset(*denmark,{{55.6761,12.5683}},0),
          "Denmark WMTS uses a secret reference without exposing the key");
    check(austria->provider=="projected-cog-image"&&austria->crs==3035
          &&std::abs(austria->fileTileSize-50000)<1e-9
          &&Imagery::projectedCogUrl(*austria,2800000,4750000)
            ==QUrl("https://data.bev.gv.at/download/DOP/20220128/DOP_CRS3035RES50000mN2800000E4750000_20220128.tif")
          &&Imagery::nearDataset(*austria,{{48.2082,16.3738}},0),
          "Austria uses the predictable 50 km projected COG grid");
    check(switzerland->provider=="stac-cog-image"
          &&switzerland->endpoint==QUrl("https://data.geo.admin.ch/api/stac/v1/")
          &&switzerland->layer=="ch.swisstopo.swissimage-dop10"
          &&switzerland->format=="image/tiff"&&switzerland->crs==2056
          &&Imagery::nearDataset(*switzerland,{{46.9480,7.4474}},0)
          &&Imagery::nearDataset(*switzerland,{{47.1410,9.5210}},0)
          &&!Imagery::nearDataset(*switzerland,{{48.2082,16.3738}},0),
          "Switzerland and Liechtenstein STAC COG source and bounds");
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
       "detailedTerrainApproved":true,"directory":"custom_poland","revision":"v1"},
      {"id":"user.google.satellite","name":"Google satellite","provider":"static-map-url",
       "urlTemplate":"https://maps.googleapis.com/maps/api/staticmap?center={lat},{lon}&zoom={zoom}&size={res}x{res}&maptype=satellite&format=png",
       "format":"image/png","tilePixels":640,"minZoom":0,"maxZoom":21,"nativeResolution":0.1,
       "requestSizes":[4096,2048,1024],"defaultRequestSize":4096,
       "boundsWgs84":[-180,-85,180,85],"detailedTerrainApproved":true,
       "persistentCache":false,"directory":"google_static_satellite","revision":"current",
       "authentication":{"type":"query-api-key","secret":"maps.imageryApiKey","parameter":"key"}}
    ]})json";
    const auto merged=Imagery::mergeDatasets(builtIn.readAll(),user,error);
    bool custom=false,broken=false,worldRetained=false;
    const Imagery::Dataset *google=nullptr;
    for(const auto &dataset:merged){
        custom|=dataset.id=="pl.gugik.orto.standard"&&dataset.name=="Custom Poland"&&dataset.userDefined;
        broken|=dataset.id=="broken";
        worldRetained|=dataset.id==world->id;
        if(dataset.id=="user.google.satellite")google=&dataset;
    }
    check(custom&&!broken&&worldRetained&&google&&google->userDefined
          &&google->provider=="static-map-url"&&google->tilePixels==640
          &&!google->persistentCache&&google->apiKeySecret=="maps.imageryApiKey"
          &&error.contains("broken"),
          "invalid user imagery object is isolated while valid override merges");
    if(google){
        const QUrl googleUrl=Imagery::staticMapUrl(*google,{52.2297,21.0122},18);
        const QUrlQuery googleQuery(googleUrl);
        check(googleQuery.queryItemValue("center")=="52.22970000,21.01220000"
              &&googleQuery.queryItemValue("zoom")=="18"
              &&googleQuery.queryItemValue("size")=="640x640"
              &&!googleQuery.hasQueryItem("key")
              &&Imagery::chooseZoom(*google,52.2297,.5)==18,
              "static-map URL placeholders and source resolution are generic");
    }

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

    Imagery::Request missingKey=unsupported;
    missingKey.datasetId=denmark->id;missingKey.sourcePixels=0;
    missingKey.controlPoints={{55.6761,12.5683},{55.6761,12.5693},
                              {55.6755,12.5683},{55.6755,12.5693}};
    std::atomic_bool missingKeyCancel{false};
    const auto missingKeyResult=Imagery::generate(missingKey,missingKeyCancel);
    check(!missingKeyResult.success()
          &&missingKeyResult.error.contains("geo.elevation.dk.datafordeler.apiKey"),
          "authenticated imagery reports a missing secret before network access");

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
