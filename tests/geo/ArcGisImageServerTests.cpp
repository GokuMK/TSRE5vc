#include <tsre/geo/ElevationSource.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtEndian>
#include <algorithm>
#include <cmath>

using namespace Elevation;
namespace {
QByteArray fixture(const QString &name) {
    QFile f(QStringLiteral(TSRE_GEO_FIXTURES)+"/../2026-09-17/arcgis/"+name);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const auto bytes = f.readAll();
    return name.endsWith(".qz") ? qUncompress(bytes) : bytes;
}
bool write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
}

void runArcGisImageServerTests(const std::function<void(bool,const char*)> &check) {
    QString error;
    const auto catalog = builtInDatasets(error);
    const auto found = std::find_if(catalog.begin(),catalog.end(),[](const Dataset &d) { return d.id == "cz.cuzk.dmr5g"; });
    check(found != catalog.end(),"ArcGIS dataset is selectable through the common catalogue");
    if (found == catalog.end()) return;
    const auto &d = *found;
    check(d.provider == "arcgis-imageserver" && d.epsg == 25833 && d.resolution == 2
          && d.blockPixels == 1024 && d.zeroIsNoData,"DMR 5G output grid and void policy are configuration");
    const QUrl url = imageServerUrl(d,{223,-2709});
    const QUrlQuery q(url);
    check(url.path().endsWith("/ImageServer/exportImage")
          && q.queryItemValue("bbox") == "456702.000000000,5545982.000000000,458754.000000000,5548034.000000000"
          && q.queryItemValue("size") == "1026,1026", "ArcGIS request includes the shared one-pixel sampling halo");
    check(q.queryItemValue("bboxSR") == "25833" && q.queryItemValue("imageSR") == "25833"
          && q.queryItemValue("adjustAspectRatio") == "false", "ArcGIS server reprojects into the exact configured grid");
    check(q.queryItemValue("f") == "image" && q.queryItemValue("format") == "tiff"
          && q.queryItemValue("pixelType") == "F32" && q.queryItemValue("compression") == "None"
          && q.queryItemValue("bandIds") == "0"
          && QJsonDocument::fromJson(q.queryItemValue("renderingRule",QUrl::FullyDecoded).toUtf8()).object()
              .value("rasterFunction").toString() == "None", "ArcGIS requests raw numeric heights instead of rendered hillshade");
    auto other = d;
    other.endpoint = QUrl("https://example.org/other/ImageServer/");
    other.epsg = 4326; other.resolution = .001; other.blockPixels = 16;
    const QUrl otherUrl = imageServerUrl(other,{0,0});
    check(otherUrl.host() == "example.org" && otherUrl.path() == "/other/ImageServer/exportImage"
          && QUrlQuery(otherUrl).queryItemValue("imageSR") == "4326"
          && QUrlQuery(otherUrl).queryItemValue("size") == "18,18", "ImageServer request builder has no Czech-specific endpoint or CRS");
    other = d; other.definition["provider"] = "wcs-2.0.1";
    check(cacheRelativePath(d,{0,0}) != cacheRelativePath(other,{0,0}),"provider configuration participates in cache identity");

    Raster prague, outside, border;
    check(readGeoTiff(fixture("prague.tif.qz"),prague,error)
          && prague.width == 1026 && prague.height == 1026 && prague.epsg == 25833
          && prague.transform[0] == 456702 && prague.transform[3] == 5548034
          && prague.transform[1] == 2 && prague.transform[5] == -2,
          "existing raster reader decodes the real ArcGIS numeric export without changes");
    check(std::abs(prague.sample({456703,5548033}).height-327.2566833496094) < 1e-5,
          "ArcGIS first pixel agrees with independent float unpacking and pixel-centre registration");
    check(readGeoTiff(fixture("outside.tif.qz"),outside,error)
          && !outside.values.isEmpty()
          && std::all_of(outside.values.begin(),outside.values.end(),[](float v) { return std::isnan(v); }),
          "ArcGIS absent coverage is NoData throughout the decoded raster");
    check(readGeoTiff(fixture("border.tif.qz"),border,error),"decode mixed coverage at the Polish-Czech border");
    const auto controls = QJsonDocument::fromJson(fixture("border-controls.json")).array();
    check(controls.size() == 3,"border fixture includes sparse, zero and valid samples");
    QVector<Point> points;
    for (const auto &entry : controls) {
        const auto c = entry.toObject();
        const auto sample = border.sample({c["x"].toDouble(),c["y"].toDouble()},d.zeroIsNoData);
        const bool valid = c["kind"].toString() == "valid";
        check(valid ? sample.valid() && std::abs(sample.height-c["height"].toDouble()) < 1e-5
                    : sample.status == SampleStatus::NoData,
              "shared sampling respects the configured border void policy");
        points.push_back({c["latitude"].toDouble(),c["longitude"].toDouble()});
    }

    QTemporaryDir temp;
    const auto requests = QJsonDocument::fromJson(fixture("manifest.json")).object()["requests"].toArray();
    for (const auto &entry : requests) {
        const auto m = entry.toObject();
        const auto bytes = fixture(m["name"].toString()+".tif.qz");
        const auto hash = QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        check(hash == m["rawSha256"].toString(),"recorded ArcGIS response checksum");
        const QString path = QDir(temp.path()).filePath(cacheRelativePath(d,{m["column"].toInt(),m["row"].toInt()}));
        check(write(path,bytes) && write(path+".json",QJsonDocument(QJsonObject{{"sha256",hash}}).toJson()),
              "prepare verified ArcGIS cache fixture");
    }
    std::atomic_bool cancel{false};
    auto result = generate(temp.path(),d.id,{{50.08,14.42}},1.0,3,3,cancel);
    const Geo::CrsTransform utm33(25833);
    XY xy; utm33.forward({50.08,14.42},xy);
    check(result.success() && result.report.primarySamples == 1 && result.report.cacheHits == 1
          && result.report.downloads == 0 && std::abs(result.heights[0]-(prague.sample(xy,true).height+3)) < 1e-5,
          "ArcGIS provider shares cache, coordinate conversion, sampling and height offset");
    QByteArray hgt(18,Qt::Uninitialized);
    for (int i=0; i<9; ++i) qToBigEndian<qint16>(777,hgt.data()+i*2);
    check(write(temp.path()+"/world_hgt/N48E013.hgt",hgt) && write(temp.path()+"/world_hgt/N50E015.hgt",hgt),"prepare HGT fallback fixtures");
    result = generate(temp.path(),d.id,{{48.6,13.5}}, 1.0, 0, 0, cancel);
    check(result.success() && result.report.cacheHits == 1 && result.report.downloads == 0
          && result.report.noDataSamples == 1 && result.report.fallbackSamples == 1 && result.heights[0] == 777,
          "ArcGIS missing coverage uses shared HGT fallback and report");
    result = generate(temp.path(),d.id,points, 1.0, 0, 0, cancel);
    check(result.success() && result.report.cacheHits == 1 && result.report.downloads == 0
          && result.report.primarySamples == 1 && result.report.noDataSamples == 2
          && result.report.fallbackSamples == 2 && result.heights[0] == 777 && result.heights[1] == 777
          && std::abs(result.heights[2]-899.9236450195312) < .001,
          "one border block combines ArcGIS heights and HGT without Czech-specific terrain logic");
    cancel = true;
    result = generate(temp.path(),d.id,points, 1.0, 0, 0, cancel);
    check(result.cancelled && result.heights.isEmpty() && result.report.downloads == 0,"ArcGIS provider observes shared cancellation");
}
