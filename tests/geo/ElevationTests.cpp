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
int main(int argc, char **argv) {
    QCoreApplication app(argc,argv);
    const auto args = app.arguments();
    if (args.size() >= 4 && args[1] == "--live") {
        // Explicit opt-in only; normal ctest is fully offline.
        std::atomic_bool cancel{false};
        const auto result = generate(args[2],args[3],{{52.0,19.0},{52.00005,19.00005}},0,cancel);
        std::cout << "success=" << result.success() << " primary=" << result.report.primarySamples
                  << " fallback=" << result.report.fallbackSamples << " downloads=" << result.report.downloads
                  << " cache=" << result.report.cacheHits << '\n';
        for (float h : result.heights) std::cout << h << '\n';
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
    check(catalog.size() == 2 && error.isEmpty(),"embedded dataset catalogue");
    check(catalog[0].resolution == 1 && catalog[1].resolution == 1,"only 1 m datasets offered");
    const auto &d = catalog[0];
    const auto url = coverageUrl(d,{2,3});
    const QUrlQuery query(url);
    check(query.allQueryItemValues("SUBSET").size() == 2 && query.queryItemValue("SCALESIZE") == "x(514),y(514)","WCS repeated subsets and fixed native-resolution dimensions");
    check(query.queryItemValue("COVERAGEID") == "DTM_PL-KRON86-NH_TIFF","numeric TIFF coverage selection");
    check(cacheRelativePath(catalog[0],{0,0}) != cacheRelativePath(catalog[1],{0,0}),"dataset-separated cache identity");
    Dataset revised = d; revised.definition["resolution"] = 5;
    check(cacheRelativePath(d,{0,0}) != cacheRelativePath(revised,{0,0}),"configuration changes invalidate cache identity");
    check(blockFor(d,{d.originX+512,d.originY-100}).column == 1,"consistent adjacent block boundary");
    check(hgtFileName(-1,-2) == "S01W002.hgt","HGT hemisphere filename");
    QTemporaryDir temp;
    check(write(temp.path()+"/N52E019.hgt",hgt(3,10)),"create local HGT fixture");
    check(findHgtFile(temp.path(),52,19) == temp.path()+"/N52E019.hgt","legacy root HGT lookup");
    check(write(temp.path()+"/hgt/N52E019.hgt",hgt(3,20)),"create organized HGT fixture");
    check(findHgtFile(temp.path(),52,19) == temp.path()+"/hgt/N52E019.hgt","HGT subdirectory wins conflicts");
    std::atomic_bool cancel{false};
    auto generated = generate(temp.path(),"",{{52.5,19.5}},2,cancel);
    check(generated.success() && generated.heights[0] == 22 && generated.report.hgtSamples == 1,"HGT generation and offset");
    generated = generate(temp.path(),"",{{52.5,19.5},{53.5,19.5}},0,cancel);
    check(!generated.success() && generated.heights.isEmpty(),"partial missing tile never returns commit-ready heights");
    cancel = true;
    generated = generate(temp.path(),d.id,{{52,19}},0,cancel);
    check(generated.cancelled && generated.heights.isEmpty(),"cancel before network work");
    cancel = false;
    write(temp.path()+"/hgt/N40E019.hgt",hgt(3,12));
    generated = generate(temp.path(),d.id,{{40.5,19.5}},0,cancel);
    check(generated.success() && generated.report.fallbackSamples == 1 && generated.report.outsideSamples == 1,"outside Poland uses reported HGT fallback without HTTP");
    check(!generate("","",{{52,19}},0,cancel).success(),"empty geoPath cannot write into working directory");
    // Complete prepared block + metadata, so this exercises the production disk
    // cache path without any network service or fake projection implementation.
    const auto &ascii = catalog[1];
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
    generated = generate(temp.path(),ascii.id,{{52,19},{52.00005,19.00005}},3,cancel);
    check(generated.success() && generated.report.cacheHits == 1 && generated.report.downloads == 0
          && generated.report.primarySamples == 2 && generated.heights[0] == 123,"offline cached native grid generation");
    cachedGrid = gridHeader;
    for (int i = 0; i < 514*514; ++i) cachedGrid += "-9999 ";
    check(storeGrid(cachedGrid),"prepare explicit NoData cache fixture");
    generated = generate(temp.path(),ascii.id,{{52,19}},0,cancel);
    check(generated.success() && generated.report.noDataSamples == 1 && generated.report.fallbackSamples == 1
          && generated.heights[0] == 20,"cached NoData falls back to organized HGT with provenance");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
