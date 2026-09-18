// Offline regressions from small official WCS 2.0.1 responses.
#include <tsre/geo/ElevationSource.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDir>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QtEndian>
#include <algorithm>
#include <cmath>

using namespace Elevation;
namespace {
QByteArray fixture(const char *name) {
    QFile f(QStringLiteral(TSRE_GEO_FIXTURES)+"/czech/"+QString::fromLatin1(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}
void mutateTag(QByteArray &b, quint16 tag, quint32 value) {
    const qsizetype base = b.indexOf("II*");
    const auto ifd = base+qFromLittleEndian<quint32>(b.constData()+base+4);
    for (int i = 0; i < qFromLittleEndian<quint16>(b.constData()+ifd); ++i) {
        const auto pos = ifd+2+i*12;
        if (qFromLittleEndian<quint16>(b.constData()+pos) == tag)
            qToLittleEndian<quint32>(value,b.data()+pos+8);
    }
}
}
void runCzechWcsTests(const std::function<void(bool,const char*)> &check) {
    QString error;
    Raster r, adjacent;
    const auto bytes = fixture("sample-native.bin");
    const auto start = bytes.indexOf("II*"), end = bytes.lastIndexOf("\n--wcs--");
    check(start >= 0 && end > start,"Czech live MIME fixture is available");
    if (start < 0 || end <= start) return;
    check(!readGeoTiff(bytes.mid(start,end-start),r,error),"bare TIFF without CRS is rejected");
    check(readWcsTiff(bytes,3045,r,error),"decode WCS GML plus tiled float TIFF");
    check(r.width == 32 && r.height == 32 && r.epsg == 3045
          && r.transform[1] == 5 && r.transform[5] == -5,"Czech grid is 5 m with internal E,N order");
    check(std::abs(r.sample({458004.232277163,5546156.442936342}).height-187.9635009765625) < 1e-5,
          "Czech top-left pixel agrees with independently unpacked float");
    check(std::abs(r.values.value(1)-187.96890258789062) < 1e-5,"numeric heights preserve fractional metres");
    check(readWcsTiff(fixture("adjacent.bin"),3045,adjacent,error)
          && std::abs(adjacent.transform[0]-(r.transform[0]+r.width*5)) < 1e-7,
          "adjacent native grids align without half-pixel shift");
    check(!readWcsTiff(bytes,2180,adjacent,error),"metadata CRS cannot be relabelled by dataset config");
    auto bad = bytes; bad.replace("EPSG/0/3045","EPSG/0/2180");
    check(!readWcsTiff(bad,3045,adjacent,error),"conflicting GML CRS rejected");
    bad = bytes.left(start)+fixture("../native-sample.tif")+bytes.mid(end);
    check(!readWcsTiff(bad,3045,adjacent,error),"GML cannot override an explicit conflicting TIFF CRS");
    bad = bytes; bad.replace("cid:MD_LAS.tif","cid:other.tif");
    check(!readWcsTiff(bad,3045,adjacent,error),"GML must reference the actual TIFF part");
    check(!readWcsTiff(bytes.left(bytes.size()-12),3045,adjacent,error),"truncated MIME rejected");
    // Change MIME line endings without touching binary TIFF bytes.
    auto crlf = bytes.left(start).replace("\n","\r\n")+bytes.mid(start,end-start)+QByteArray("\r\n--wcs--\r\n");
    check(readWcsTiff(crlf,3045,adjacent,error),"CRLF multipart decoding preserves TIFF bytes");
    bad = bytes; bad[end-1] = '\r';
    check(readWcsTiff(bad,3045,adjacent,error),"LF multipart never trims a trailing binary CR byte");
    bad = bytes; mutateTag(bad,324,0xfffffff0);
    check(!readWcsTiff(bad,3045,adjacent,error),"out-of-bounds TIFF tile rejected");
    bad = bytes; mutateTag(bad,325,4);
    check(!readWcsTiff(bad,3045,adjacent,error),"inconsistent TIFF tile size rejected");
    bad = bytes; mutateTag(bad,322,0);
    check(!readWcsTiff(bad,3045,adjacent,error),"zero tile width rejected");
    check(readWcsTiff(fixture("outside.bin"),3045,r,error)
          && std::all_of(r.values.begin(),r.values.end(),[](float v) { return std::isnan(v); }),
          "sparse missing coverage becomes NoData, never fabricated zero elevation");
    check(readWcsTiff(fixture("border.bin"),3045,r,error) && r.width == 256 && r.height == 256,
          "multi-tile border coverage decoded");
    check(r.sample({r.transform[0]+2.5,r.transform[3]-642.5}).status == SampleStatus::NoData,
          "absent tile in partly populated raster is NoData");
    const auto zero = std::find(r.values.begin(),r.values.end(),0.f);
    check(zero != r.values.end(),"border fixture contains unmarked zero voids");
    if (zero != r.values.end()) {
        const auto i = zero-r.values.begin();
        const XY xy{r.transform[0]+(i%r.width+.5)*5,r.transform[3]-(i/r.width+.5)*5};
        check(r.sample(xy).valid() && r.sample(xy,true).status == SampleStatus::NoData,
              "zero void policy remains dataset configuration");
    }
    const auto controls = QJsonDocument::fromJson(fixture("projection-reference.json")).array();
    check(controls.size() == 40,"independent UTM 33N projection reference grid");
    double maxError = 0;
    for (const auto &entry : controls) {
        const auto c = entry.toObject(); XY xy, alias;
        const Point p{c["latitude"].toDouble(),c["longitude"].toDouble()};
        const bool valid = project(p,3045,xy) && project(p,25833,alias);
        maxError = std::max(maxError,std::hypot(xy.x-c["easting"].toDouble(),xy.y-c["northing"].toDouble()));
        check(valid && maxError < .001 && xy.x == alias.x && xy.y == alias.y,
              "UTM 33N agrees with independent PROJ within 1 mm and normalizes N-E axis order");
    }
    const auto catalog = datasets(error);
    const auto found = std::find_if(catalog.begin(),catalog.end(),[](const Dataset &d) { return d.id == "cz.cuzk.dmr4g"; });
    check(found != catalog.end(),"Czech source is supplied by the common catalogue");
    if (found == catalog.end()) return;
    const auto &d = *found;
    check(d.epsg == 3045 && d.resolution == 5 && d.blockPixels == 256 && d.zeroIsNoData,
          "Czech native grid and zero void policy are configuration");
    QTemporaryDir temp;
    const auto write = [&](const QString &path, const QByteArray &data) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
    };
    for (const Block b : {Block{130,86},Block{76,213},Block{76,214}}) {
        const QString name = QStringLiteral("%1_%2.bin").arg(b.column).arg(b.row);
        const auto data = fixture(name.toLatin1().constData());
        const QString path = QDir(temp.path()).filePath(cacheRelativePath(d,b));
        const QJsonObject metadata{{"sha256",QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex())}};
        check(write(path,data) && write(path+".json",QJsonDocument(metadata).toJson()),"prepare real Czech cached response");
    }
    std::atomic_bool cancel{false};
    auto result = generate(temp.path(),d.id,{{50.08,14.42},{50.08005,14.42005}},1.0,0,cancel);
    check(result.success() && result.report.cacheHits == 1 && result.report.downloads == 0
          && result.report.primarySamples == 2 && std::abs(result.heights[0]-197.444) < .001,
          "unchanged production source uses Czech cache and samples native raster");
    QByteArray hgt(18,Qt::Uninitialized);
    for (int i = 0; i < 9; ++i) qToBigEndian<qint16>(777,hgt.data()+i*2);
    check(write(temp.path()+"/hgt/N48E013.hgt",hgt),"prepare synthetic HGT fallback");
    result = generate(temp.path(),d.id,{{48.6,13.5},{48.60005,13.50005}},1.0,0,cancel);
    check(result.success() && result.report.cacheHits == 2 && result.report.downloads == 0
          && result.report.noDataSamples == 2 && result.report.fallbackSamples == 2
          && result.heights[0] == 777 && result.heights[1] == 777,
          "Czech sparse coverage uses shared HGT fallback with explicit provenance");
}
