#include <tsre/geo/ElevationRaster.h>
#include <tsre/geo/ElevationSource.h>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QUrlQuery>
#include <QtEndian>
#include <QElapsedTimer>
#include <cmath>
#include <iostream>

using namespace Elevation;
namespace {
int checks = 0, failures = 0;
void check(bool condition, const char *name) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}
bool near(double a, double b, double tolerance = 1e-4) { return std::abs(a-b) <= tolerance; }
QByteArray fixture(const char *name) {
    QFile f(QStringLiteral(TSRE_GEO_FIXTURES)+"/"+QString::fromLatin1(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}
bool write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
QByteArray hgt(int side, qint16 value) {
    QByteArray bytes(side*side*2,Qt::Uninitialized);
    for (int i = 0; i < side*side; ++i) qToBigEndian<qint16>(value,bytes.data()+i*2);
    return bytes;
}
void mutateTag(QByteArray &bytes, quint16 tag, quint32 value) {
    const auto ifd = qFromLittleEndian<quint32>(bytes.constData()+4);
    const auto count = qFromLittleEndian<quint16>(bytes.constData()+ifd);
    for (int i = 0; i < count; ++i) {
        const auto p = ifd+2+12*i;
        if (qFromLittleEndian<quint16>(bytes.constData()+p) == tag) qToLittleEndian<quint32>(value,bytes.data()+p+8);
    }
}
}
void runDownloadTests(const std::function<void(bool,const char*)> &check);
void runArcGisImageServerTests(const std::function<void(bool,const char*)> &check);
void runCzechWcsTests(const std::function<void(bool,const char*)> &check);
void runNoDataFillTests(const std::function<void(bool,const char*)> &check);
int main(int argc, char **argv) {
    QCoreApplication app(argc,argv);
    const auto args = app.arguments();
    if (args.size() >= 4 && (args[1] == "--live" || args[1] == "--live-area" || args[1] == "--live-at")) {
        // Explicit opt-in only; normal ctest is fully offline.
        std::atomic_bool cancel{false};
        QVector<Point> points{{52.0,19.0},{52.00005,19.00005}};
        if (args[1] == "--live-at") {
            bool latOk = false, lonOk = false;
            const double lat = args.value(4).toDouble(&latOk), lon = args.value(5).toDouble(&lonOk);
            if (!latOk || !lonOk || args.size() != 6) return 2;
            points = {{lat,lon},{lat+.00005,lon+.00005}};
        }
        if (args[1] == "--live-area") {
            points.clear();
            for (int y=0; y<16; ++y) for (int x=0; x<16; ++x)
                points.push_back({52.0+y*.0184/15,19.0+x*.0299/15});
        }
        QElapsedTimer timer; timer.start();
        const auto result = generate(args[2],args[3],points,1.0,0,cancel);
        std::cout << "success=" << result.success() << " primary=" << result.report.primarySamples
                  << " fallback=" << result.report.fallbackSamples << " downloads=" << result.report.downloads
                  << " nodata=" << result.report.noDataSamples << " unavailable=" << result.report.unavailableSamples
                  << " cache=" << result.report.cacheHits << " elapsedMs=" << timer.elapsed() << '\n';
        if (args[1] != "--live-area") for (float h : result.heights) std::cout << h << '\n';
        std::cerr << result.error.toStdString() << '\n' << result.report.issues.join('\n').toStdString() << '\n';
        return result.success() ? 0 : 1;
    }
    QString error;
    Raster r;
    const auto native = fixture("native-sample.tif");
    check(readGeoTiff(native,r,error),"decode live numeric GeoTIFF fixture");
    check(r.width == 32 && r.height == 32 && r.epsg == 2180,"TIFF dimensions and CRS");
    check(r.transform[1] == 1 && r.transform[5] == -1,"Geoportal source spacing is exactly 1 m");
    check(near(r.sample({566600.5,243131.5}).height,203.14999389648438),"TIFF top-left pixel center");
    check(near(r.sample({566601,243131.5}).height,(203.14999389648438+203.1199951171875)/2),"bilinear interpolation");
    check(r.sample({566599,243131}).status == SampleStatus::Outside,"outside raster never clamps silently");
    check(!readGeoTiff(fixture("evrf-sample.tif"),r,error) && error.contains("RGB"),"reject service RGB TIFF as elevation");
    check(!readGeoTiff(QByteArray("<ExceptionReport/>"),r,error),"reject service XML as TIFF");
    check(!readGeoTiff(native.left(400),r,error),"reject truncated TIFF");
    auto bad = native; mutateTag(bad,273,0xfffffff0);
    check(!readGeoTiff(bad,r,error),"reject strip offset outside input");
    bad = native; mutateTag(bad,259,5);
    check(!readGeoTiff(bad,r,error),"reject unsupported compression");
    bad = native; mutateTag(bad,256,65535);
    check(!readGeoTiff(bad,r,error),"reject inconsistent strip dimensions");
    check(readGeoTiff(fixture("signed16-big-endian.tif"),r,error),"big-endian signed16 TIFF with two strips");
    check(r.sample({100.5,199.5}).height == -2 && r.sample({101.5,199.5}).height == 0
          && r.sample({100.5,198.5}).height == 10,"signed strips preserve row order and zero height");
    check(r.sample({101.5,198.5}).status == SampleStatus::NoData,"TIFF NoData tag");
    check(readGeoTiff(fixture("signed16-pixel-is-point.tif"),r,error)
          && r.sample({100,200}).height == -2,"PixelIsPoint registration has no half-pixel displacement");
    check(readAsciiGrid(fixture("evrf-sample.asc"),2180,r,error),"decode live multipart EVRF ASCII grid");
    check(r.width == 32 && r.height == 32 && r.transform[1] == 1,"ASCII grid is 1 m");
    check(near(r.sample({566600.5,243131.5}).height,203.38999938964844),"ASCII numeric heights preserve decimals");
    check(!readAsciiGrid("ncols 9999999999 nrows 2 cellsize 1 xllcorner 0 yllcorner 0",2180,r,error),"reject excessive ASCII allocation");
    check(!readAsciiGrid("ncols 2 nrows 2 cellsize 1 xllcorner 0 yllcorner 0 1 2",2180,r,error),"reject truncated ASCII");
    check(readAsciiGrid("ncols 2 nrows 2 xllcenter 0 yllcenter 0 cellsize 1 NODATA_value -9999\n0 -2 4 -9999",4326,r,error),"ASCII center origin and NoData");
    check(r.sample({0,1}).valid() && r.sample({0,1}).height == 0,"zero is valid in generic raster");
    check(r.sample({0,1},true).status == SampleStatus::NoData,"dataset-specific zero policy");
    check(r.sample({1,1}).height == -2,"negative elevation preserved");
    check(r.sample({.5,.5}).status == SampleStatus::NoData,"interpolation does not mix a void");
    check(r.sample({0,0}).valid(),"zero-weight void neighbor does not invalidate a post");
    Raster rotated;
    rotated.width = rotated.height = 2; rotated.values = {1,2,3,4};
    rotated.transform = {{100,0,-2,200,2,0}};
    check(near(rotated.sample({99,201}).height,1),"rotated affine grid");
    XY p;
    check(project({52,19},2180,p) && near(p.x,500000,1e-5)
          && near(p.y,459309.20940316166,.001),"CS92 reference from geographic WCS subset");
    check(!readGeoTiff(fixture("projection-west.tif"),r,error),"reject service response with user-defined CRS");
    check(!readGeoTiff(fixture("projection-east.tif"),r,error),"reject unidentifiable CRS without assuming it is CS92");
    const auto references = QJsonDocument::fromJson(fixture("projection-reference.json")).object().value("points").toArray();
    check(references.size() == 130,"independent projection reference grid");
    double maxProjectionError = 0;
    for (const auto entry : references) {
        const auto point = entry.toObject();
        const bool valid = project({point["latitude"].toDouble(),point["longitude"].toDouble()},2180,p);
        const double delta = std::hypot(p.x-point["x"].toDouble(),p.y-point["y"].toDouble());
        maxProjectionError = std::max(maxProjectionError,delta);
        check(valid && delta < .001,"CS92 forward projection agrees with independent PROJ reference within 1 mm");
    }
    std::cout << "Maximum projection difference: " << maxProjectionError << " m\n";
    check(project({52,19},4326,p) && p.x == 19 && p.y == 52,"explicit lon/lat raster order");
    check(!project({52,19},9999,p) && !project({0,0},2180,p),"reject unsupported projection and domain");
    check(readHgt(hgt(3,-5),-1,-2,r,error),"read big-endian HGT");
    check(sampleLegacyHgt(r,{-.5,-1.5}).valid() && sampleLegacyHgt(r,{-.5,-1.5}).height == -5,"negative HGT positions and heights");
    check(!readHgt(QByteArray(19,'x'),0,0,r,error),"reject malformed HGT dimensions");
    check(readHgt(hgt(3,-32768),0,0,r,error) && sampleLegacyHgt(r,{.5,.5}).status == SampleStatus::NoData,"HGT void detection");
    const auto catalog = datasets(error);
    check(!catalog.isEmpty() && error.isEmpty(),"embedded dataset catalogue");
    QJsonArray entries;
    for (const auto &entry : catalog) entries.append(entry.definition);
    const auto parseEntries = [&](const QJsonArray &items) {
        return parseDatasets(QJsonDocument(QJsonObject{{"version",1},{"datasets",items}}).toJson(),error);
    };
    auto invalid = entries.first().toObject(); invalid["id"] = "fixture.invalid-auth";
    invalid["authentication"] = QJsonObject{{"type","unsupported"},{"secret","fixture.key"}};
    auto mixed = entries; mixed.insert(1,invalid);
    auto validEntries = parseEntries(mixed);
    check(validEntries.size() == catalog.size() && error.contains("fixture.invalid-auth")
        && validEntries.last().id == catalog.last().id,"invalid authentication rejects only its object and parsing continues");
    invalid = entries.first().toObject(); invalid["id"] = "fixture.invalid-crs"; invalid["crs"] = 9999;
    mixed = entries; mixed.prepend(invalid); mixed.append(entries.first()); mixed.append(false);
    validEntries = parseEntries(mixed);
    check(validEntries.size() == catalog.size() && error.contains("fixture.invalid-crs")
        && error.contains(catalog.first().id) && error.contains("entry "),
        "unsupported CRS, duplicate ID and non-object are individually rejected");
    invalid["origin"] = QJsonArray{};
    check(parseEntries({invalid}).isEmpty() && error.contains("grid definition"),"invalid grid is diagnosed");
    check(parseDatasets("{",error).isEmpty() && error.contains("JSON"),"malformed JSON has a file-level diagnostic");
    check(parseDatasets("{\"version\":1}",error).isEmpty() && !error.isEmpty(),"missing datasets array is a file-level error");
    QMap<QString,Dataset> byId;
    for (const auto &entry : catalog) byId.insert(entry.id,entry);
    bool requiredPresent = true;
    for (const QString &id : {QString("pl.gugik.nmt1.kron86"),QString("pl.gugik.nmt1.evrf2007"),
                             QString("cz.cuzk.dmr4g"),QString("cz.cuzk.dmr5g")})
        requiredPresent &= byId.contains(id);
    check(requiredPresent && byId.size() == catalog.size(),"required datasets and unique IDs survive catalogue expansion");
    if (!requiredPresent) return 1;
    const auto finland = byId.value("fi.nls.dem2");
    const auto netherlands = byId.value("nl.pdok.ahn.dtm05");
    const auto england = byId.value("gb.ea.lidar.dtm1");
    const auto estonia = byId.value("ee.maru.dtm1");
    const auto denmark = byId.value("dk.datafordeler.dhm.terraen");
    check(denmark.apiKeyParameter == "apikey" && denmark.apiKeySecret == "geo.elevation.dk.datafordeler.apiKey"
        && denmark.resolution == 1 && QUrlQuery(coverageUrl(denmark,{0,0})).queryItemValue("FORMAT") == "GTiff"
        && !QUrlQuery(coverageUrl(denmark,{0,0})).hasQueryItem("apikey"),
        "Denmark uses a 1 m grid and secret reference; public request URL contains no key");
    const QUrlQuery gbQuery(coverageUrl(england,{-8,-3279}));
    check(gbQuery.allQueryItemValues("SUBSET").value(0).startsWith("X(")
        && gbQuery.allQueryItemValues("SUBSET").value(1).startsWith("Y(")
        && gbQuery.queryItemValue("SCALESIZE") == "i(1026),j(1026)",
        "England uses projected subset axes separately from scaling axes");
    check(QUrlQuery(coverageUrl(estonia,{2689,-8086})).queryItemValue("FORMAT") == "image/tiff"
        && QUrlQuery(coverageUrl(byId.value("de.bw.dgm1"),{0,0})).queryItemValue("FORMAT") == "GeoTIFF",
        "Estonia format override preserves existing WCS 1 TIFF alias");
    const auto eeFixture = fixture("../2026-09-19/ee-gb/ee-land-32.tif");
    check(readGeoTiff(eeFixture,r,error) && r.epsg == 3857 && r.transform[1] == 2
        && near(r.values.value(528),2.690624952316284) && r.values.value(0) == 0,
        "Estonia live Float32 fixture preserves fractional and zero heights");
    bad = eeFixture; mutateTag(bad,257,31);
    check(readGeoTiff(bad,r,error) && r.values.size() == 32*31,
        "final TIFF strip may include padding rows beyond image height");
    check(!readGeoTiff(bad.chopped(4),r,error),"truncated padded strip remains rejected");
    mutateTag(bad,279,4097);
    check(!readGeoTiff(bad,r,error),"arbitrary extra strip bytes remain rejected");
    check(readGeoTiff(fixture("../2026-09-19/ee-gb/gb-catalog-32.tif"),r,error)
        && r.epsg == 3857 && r.hasNoData && near(r.values.value(59),33.3849983215332),
        "England live big-endian tiled Float32 fixture and NoData decode");
    check(r.sample({r.transform[0]+r.transform[1]*.5,r.transform[3]+r.transform[5]*.5}).status == SampleStatus::NoData,
        "England extreme negative sentinel is NoData, never a terrain height");
    Dataset probeGrid = england;
    probeGrid.blockPixels = 30; probeGrid.originX = -16384; probeGrid.originY = 6715392;
    const Raster expanded = r;
    check(validateRasterGrid(probeGrid,{0,0},r,error),"expanded grid covers all requested sample centres");
    probeGrid.allowExpandedGrid = false;
    check(!validateRasterGrid(probeGrid,{0,0},r,error),"strict datasets reject server-expanded grids");
    probeGrid.allowExpandedGrid = true;
    r.transform[0] += 4;
    check(!validateRasterGrid(probeGrid,{0,0},r,error),"expanded grid cannot miss a requested edge");
    r = expanded; r.transform[1] *= 2;
    check(!validateRasterGrid(probeGrid,{0,0},r,error),"expanded grid cannot exceed its extent budget");
    r = expanded; r.epsg = 2180;
    check(!validateRasterGrid(probeGrid,{0,0},r,error),"expanded grid still requires matching CRS");
    r = expanded; --r.width;
    check(!validateRasterGrid(probeGrid,{0,0},r,error),"expanded grid still requires exact dimensions");
    r = expanded; r.transform[2] = .01;
    check(!validateRasterGrid(probeGrid,{0,0},r,error),"expanded grid rejects rotation");
    r = expanded; r.transform[0] = std::numeric_limits<double>::quiet_NaN();
    check(!validateRasterGrid(probeGrid,{0,0},r,error),"expanded grid rejects non-finite transforms");
    check(finland.epsg == 3067 && supportedCrs(3067) && project({60,27},3067,p)
        && near(p.x,500000),"Finland retains its supported native TM35FIN grid");
    check(finland.apiKeySecret == "geo.elevation.fi.nls.apiKey"
        && !coverageUrl(finland,{0,0}).toString().contains("api-key"),"Finland catalogue stores a secret reference, not a credential URL");
    const QUrlQuery nlQuery(coverageUrl(netherlands,{1226,-11337}));
    check(netherlands.epsg == 25831 && nlQuery.queryItemValue("SUBSETTINGCRS").endsWith("/25831")
        && nlQuery.queryItemValue("OUTPUTCRS").endsWith("/25831")
        && nlQuery.queryItemValue("GEOTIFF:COMPRESSION") == "None",
        "Netherlands uses explicit UTM subsetting/output and uncompressed TIFF");
    const auto &d = byId["pl.gugik.nmt1.kron86"];
    const auto &polishAscii = byId["pl.gugik.nmt1.evrf2007"];
    check(d.resolution == 1 && polishAscii.resolution == 1 && byId["cz.cuzk.dmr4g"].resolution == 5,"Polish 1 m and Czech 5 m datasets");
    check(d.blockPixels == 1024 && d.concurrentRequests == 4
        && polishAscii.blockPixels == 512 && polishAscii.concurrentRequests == 1,
        "TIFF uses four concurrent 1024 m blocks; ASCII keeps verified serial 512 m blocks");
    const auto url = coverageUrl(d,{2,3});
    const QUrlQuery query(url);
    check(query.allQueryItemValues("SUBSET").size() == 2 && query.queryItemValue("SCALESIZE") == "x(1026),y(1026)","WCS repeated subsets and fixed native-resolution dimensions");
    check(query.queryItemValue("COVERAGEID") == "DTM_PL-KRON86-NH_TIFF","numeric TIFF coverage selection");
    check(cacheRelativePath(d,{0,0}) != cacheRelativePath(polishAscii,{0,0}),"dataset-separated cache identity");
    Dataset revised = d; revised.definition["resolution"] = 5;
    check(cacheRelativePath(d,{0,0}) != cacheRelativePath(revised,{0,0}),"configuration changes invalidate cache identity");
    revised = d; revised.definition["noDataPolicy"] = "fill";
    check(cacheRelativePath(d,{0,0}) == cacheRelativePath(revised,{0,0}),"NoData policy preserves the original raw-data cache identity");
    check(d.noDataPolicy == "fallback" && netherlands.noDataPolicy == "fill",
        "NoData policy defaults to fallback and Netherlands opts into fill");
    check(blockFor(d,{d.originX+1024,d.originY-100}).column == 1,"consistent adjacent block boundary");
    check(nearDataset(d,{{52,19}}) && !nearDataset(finland,{{52,19}})
        && nearDataset(finland,{}),"location filter keeps nearby sources and leaves an unknown location unfiltered");
    Dataset local; local.epsg = 3857;
    XY centre; project({60,24},3857,centre);
    local.minX=centre.x+19000; local.maxX=centre.x+19500;
    local.minY=centre.y-100; local.maxY=centre.y+100;
    check(nearDataset(local,{{60,24}}),"10 km ground buffer accounts for Mercator scale at 60 degrees");
    local.minX=centre.x+21000; local.maxX=centre.x+22000;
    check(!nearDataset(local,{{60,24}}),"sources beyond the buffer are hidden");
    local.epsg=4326; local.minX=19; local.maxX=20; local.minY=51; local.maxY=52;
    check(nearDataset(local,{{50.9,18.9},{52.1,20.1}},0),"filter includes a dataset intersecting the tile footprint");
    Dataset oldGrid = d;
    oldGrid.definition["blockPixels"] = 512; oldGrid.definition.remove("concurrentRequests");
    check(cacheRelativePath(oldGrid,{0,0}) != cacheRelativePath(d,{0,0}),"old 512 m TIFF cache cannot be reused as 1024 m data");
    check(hgtFileName(-1,-2) == "S01W002.hgt","HGT hemisphere filename");
    QTemporaryDir temp;
    check(write(temp.path()+"/N52E019.hgt",hgt(3,10)),"create local HGT fixture");
    check(findHgtFile(temp.path(),52,19) == temp.path()+"/N52E019.hgt","legacy root HGT lookup");
    check(write(temp.path()+"/hgt/N52E019.hgt",hgt(3,20)),"create organized HGT fixture");
    check(findHgtFile(temp.path(),52,19) == temp.path()+"/hgt/N52E019.hgt","HGT subdirectory wins conflicts");
    check(write(temp.path()+"/world_hgt/N52E019.hgt",hgt(3,20)),"create user-managed world HGT fixture");
    check(findHgtFile(temp.path(),52,19) == temp.path()+"/world_hgt/N52E019.hgt",
        "world_hgt takes precedence over both legacy paths");
    std::atomic_bool cancel{false};
    check(write(temp.path()+"/hgt/N60E027.hgt",hgt(3,15)),"prepare Finland fallback fixture");
    auto withoutKey = generate(temp.path(),finland.id,{{60.1,27.1}},2.0,0,cancel);
    check(withoutKey.success() && withoutKey.report.downloads == 0 && withoutKey.report.hgtSamples == 1
        && withoutKey.report.issues.join('\n').contains(finland.apiKeySecret),
        "missing API key skips requests and visibly reports the reference with HGT fallback");
    const QString invalidKey = "invalid:fixture-key";
    withoutKey = generate(temp.path(),finland.id,{{60.1,27.1}},2.0,0,cancel,{},{{finland.apiKeySecret,invalidKey}});
    check(withoutKey.success() && withoutKey.report.downloads == 0
        && !withoutKey.report.issues.join('\n').contains(invalidKey),"invalid Basic username is rejected without exposing its value");
    auto generated = generate(temp.path(),"",{{52.5,19.5}},1.0,2,cancel);
    check(generated.success() && generated.heights[0] == 22 && generated.report.hgtSamples == 1,"HGT generation and offset");
    generated = generate(temp.path(),"",{{52.5,19.5},{53.5,19.5}},1.0,0,cancel);
    check(!generated.success() && generated.heights.isEmpty(),"partial missing tile never returns commit-ready heights");
    cancel = true;
    generated = generate(temp.path(),d.id,{{52,19}},1.0,0,cancel);
    check(generated.cancelled && generated.heights.isEmpty(),"cancel before network work");
    cancel = false;
    write(temp.path()+"/hgt/N40E019.hgt",hgt(3,12));
    generated = generate(temp.path(),d.id,{{40.5,19.5}},1.0,0,cancel);
    check(generated.success() && generated.report.fallbackSamples == 1 && generated.report.outsideSamples == 1,"outside Poland uses reported HGT fallback without HTTP");
    check(!generate("","",{{52,19}},1.0,0,cancel).success(),"empty geoPath cannot write into working directory");
    // Complete prepared block + metadata, so this exercises the production disk
    // cache path without any network service or fake projection implementation.
    const auto &ascii = polishAscii;
    project({52,19},2180,p);
    const auto block = blockFor(ascii,p);
    const double left = ascii.originX+block.column*512-1;
    const double bottom = ascii.originY-block.row*512+1-514;
    QByteArray cachedGrid = QString("ncols 514\nnrows 514\nxllcorner %1\nyllcorner %2\ncellsize 1\nNODATA_value -9999\n")
        .arg(left,0,'f',9).arg(bottom,0,'f',9).toLatin1();
    const QByteArray gridHeader = cachedGrid;
    for (int i = 0; i < 514*514; ++i) cachedGrid += "120 ";
    const QString cachePath = QDir(temp.path()).filePath(cacheRelativePath(ascii,block));
    const auto storeGrid = [&](const QByteArray &grid) {
        QJsonObject metadata;
        metadata["sha256"] = QString::fromLatin1(QCryptographicHash::hash(grid,QCryptographicHash::Sha256).toHex());
        return write(cachePath,grid) && write(cachePath+".json",QJsonDocument(metadata).toJson());
    };
    check(storeGrid(cachedGrid),"prepare offline cache fixture");
    generated = generate(temp.path(),ascii.id,{{52,19},{52.00005,19.00005}},1.0,3,cancel);
    check(generated.success() && generated.report.cacheHits == 1 && generated.report.downloads == 0
          && generated.report.primarySamples == 2 && generated.heights[0] == 123,"offline cached native grid generation");
    cachedGrid = gridHeader;
    for (int i = 0; i < 514*514; ++i) cachedGrid += "-9999 ";
    check(storeGrid(cachedGrid),"prepare explicit NoData cache fixture");
    generated = generate(temp.path(),ascii.id,{{52,19}},1.0,0,cancel);
    check(generated.success() && generated.report.noDataSamples == 1 && generated.report.fallbackSamples == 1
          && generated.heights[0] == 20,"cached NoData falls back to organized HGT with provenance");
    runCzechWcsTests(check);
    runArcGisImageServerTests(check);
    runDownloadTests(check);
    runNoDataFillTests(check);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
