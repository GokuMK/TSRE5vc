#include <tsre/geo/ElevationSource.h>
#include <tsre/geo/ElevationDownload.h>

#include <QCache>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <memory>

namespace Elevation {
namespace {
constexpr qint64 MaxDownload = 32 * 1024 * 1024;
constexpr int MaxBlocks = 2048;
QString decimal(double value) { return QString::number(value, 'f', 9); }
bool inside(const Dataset &d, XY p) {
    return p.x >= d.minX && p.y >= d.minY && p.x < d.maxX && p.y < d.maxY;
}
std::array<double,4> bounds(const Dataset &d, Block b) {
    const double left = d.originX + double(b.column)*d.blockPixels*d.resolution - d.resolution;
    const double top = d.originY - double(b.row)*d.blockPixels*d.resolution + d.resolution;
    const double span = (d.blockPixels+2)*d.resolution;
    return {{left, top-span, left+span, top}};
}
bool decode(const Dataset &d, const QByteArray &bytes, Raster &r, QString &error) {
    if (d.provider == "arcgis-imageserver") {
        const auto serviceError = QJsonDocument::fromJson(bytes).object().value("error").toObject();
        if (!serviceError.isEmpty()) {
            error = QStringLiteral("ImageServer error %1: %2").arg(serviceError.value("code").toInt())
                .arg(serviceError.value("message").toString().left(512));
            return false;
        }
        return readGeoTiff(bytes,r,error);
    }
    return d.format == "image/tiff" ? readWcsTiff(bytes,d.epsg,r,error) : readAsciiGrid(bytes,d.epsg,r,error);
}
bool validate(const Dataset &d, Block b, const Raster &r, QString &error) {
    const auto box = bounds(d,b);
    if (r.epsg != d.epsg || r.width != d.blockPixels+2 || r.height != d.blockPixels+2
            || std::abs(r.transform[0]-box[0]) > 1e-5 || std::abs(r.transform[3]-box[3]) > 1e-5
            || std::abs(r.transform[1]-d.resolution) > 1e-8
            || std::abs(r.transform[5]+d.resolution) > 1e-8
            || r.transform[2] != 0 || r.transform[4] != 0) {
        error = QStringLiteral("Service raster does not match the requested elevation grid");
        return false;
    }
    return true;
}
QByteArray readFile(const QString &path, qint64 max = MaxDownload) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > max) return {};
    return file.readAll();
}
bool saveFile(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
class HgtSource final : public Source {
public:
    HgtSource(QString path, Report &r) : root(std::move(path)), report(r) { cache.setMaxCost(128*1024); }
    bool prepare(const QVector<Point> &, std::atomic_bool &, const Progress &, QString &) override { return true; }
    Sample sample(Point p) override {
        if (!std::isfinite(p.latitude) || !std::isfinite(p.longitude)
                || p.latitude < -90 || p.latitude >= 90 || p.longitude < -180 || p.longitude >= 180)
            return {0,SampleStatus::Outside};
        const int lat = int(std::floor(p.latitude)), lon = int(std::floor(p.longitude));
        const QString key = hgtFileName(lat,lon);
        Raster *r = cache.object(key);
        if (!r) {
            if (failed.contains(key)) return {};
            QString error;
            auto loaded = std::make_unique<Raster>();
            const QString path = findHgtFile(root,lat,lon);
            if (path.isEmpty() || !readHgt(readFile(path,64*1024*1024),lat,lon,*loaded,error)) {
                failed.insert(key);
                report.issue(QStringLiteral("%1: %2").arg(key,path.isEmpty() ? QStringLiteral("HGT file missing") : error));
                return {};
            }
            const int cost = int((loaded->values.size()*sizeof(float)+1023)/1024);
            r = loaded.get(); cache.insert(key,loaded.release(),cost);
        }
        return sampleLegacyHgt(*r,p);
    }
private:
    QString root;
    Report &report;
    QCache<QString,Raster> cache;
    QSet<QString> failed;
};

// Acquisition is separate from sampling. Other protocols can provide the same
// validated local block contract without changing terrain generation.
class RasterProvider {
public:
    virtual ~RasterProvider() = default;
    virtual QMap<Block,QString> acquire(const QVector<Block> &blocks,
        std::atomic_bool &cancel, const Progress &progress) = 0;
};
class CachedRasterProvider : public RasterProvider {
public:
    CachedRasterProvider(QString path, Dataset data, Report &r, QString key)
        : root(std::move(path)), dataset(std::move(data)), report(r), apiKey(std::move(key)) {}
    QMap<Block,QString> acquire(const QVector<Block> &blocks, std::atomic_bool &cancel,
                               const Progress &progress) override {
        QMap<Block,QString> prepared;
        QVector<Block> missing;
        int done = 0, failures = 0;
        bool stopNetwork = false;
        const auto notify = [&](int count, const QString &activity) {
            if (progress) progress(count,blocks.size(),
                QStringLiteral("%1 elevation blocks: %2/%3").arg(activity).arg(count).arg(blocks.size()));
        };
        notify(0,QStringLiteral("Checking"));
        // Resolve every cache hit even if later HTTP requests fail.
        for (const Block b : blocks) {
            if (cancel) return {};
            const QString path = cached(b);
            if (path.isEmpty()) missing.push_back(b);
            else { prepared.insert(b,path); notify(++done,QStringLiteral("Prepared")); }
        }
        QByteArray authorization;
        if (!missing.isEmpty() && !dataset.apiKeySecret.isEmpty()) {
            if (apiKey.isEmpty() || apiKey.contains(':') || apiKey.contains('\r') || apiKey.contains('\n')) {
                report.issue(QStringLiteral("Missing or invalid elevation API key: %1 in profile-local secrets.json; using available cache and HGT")
                    .arg(dataset.apiKeySecret));
                notify(blocks.size(),QStringLiteral("Prepared"));
                return prepared;
            }
            authorization = "Basic " + (apiKey.toUtf8()+':').toBase64();
        }
        for (qsizetype first=0; first<missing.size(); first+=dataset.concurrentRequests) {
            if (cancel) return {};
            if (stopNetwork) {
                report.issue(QStringLiteral("Downloads stopped after three consecutive failures; using available cache and HGT"));
                notify(blocks.size(),QStringLiteral("Prepared"));
                break;
            }
            QVector<QUrl> urls;
            const int count = int(std::min(qsizetype(dataset.concurrentRequests),missing.size()-first));
            for (int i=0; i<count; ++i) urls.push_back(requestUrl(missing[first+i]));
            const auto responses = downloadWave(urls,cancel,[&](int finished) {
                notify(done+finished,QStringLiteral("Completed"));
            },{},authorization);
            if (cancel) return {};
            // Decode after all replies finish, keeping CPU/cache writes out of
            // the network event loop. One wave is at most four bounded bodies.
            for (int i=0; i<count; ++i) {
                if (cancel) return {};
                const Block b = missing[first+i];
                QString error = responses[i].error;
                QString path;
                if (error.isEmpty()) path = store(b,responses[i].bytes,error);
                if (path.isEmpty()) {
                    ++failures;
                    if (failures>=3) stopNetwork = true;
                    report.issue(QStringLiteral("Block %1,%2: %3").arg(b.column).arg(b.row).arg(error));
                } else { failures = 0; prepared.insert(b,path); }
                ++done;
            }
            notify(done,QStringLiteral("Prepared"));
        }
        return prepared;
    }
private:
    QString cached(Block b) {
        const QString path = QDir(root).filePath(cacheRelativePath(dataset,b));
        Raster raster;
        QString error;
        QByteArray bytes = readFile(path);
        bool valid = !bytes.isEmpty() && decode(dataset,bytes,raster,error) && validate(dataset,b,raster,error);
        if (valid) {
            const QJsonObject metadata = QJsonDocument::fromJson(readFile(path+".json",64*1024)).object();
            valid = metadata.value("sha256").toString().toLatin1() == QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
        }
        if (valid) { ++report.cacheHits; return path; }
        return {};
    }
    QString store(Block b, const QByteArray &bytes, QString &error) {
        Raster raster;
        if (!decode(dataset,bytes,raster,error) || !validate(dataset,b,raster,error)) return {};
        const QString path = QDir(root).filePath(cacheRelativePath(dataset,b));
        ++report.downloads;
        QJsonObject metadata;
        metadata["dataset"] = dataset.id;
        metadata["url"] = requestUrl(b).toString();
        metadata["retrievedUtc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        metadata["verticalDatum"] = dataset.verticalDatum;
        metadata["sha256"] = QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !saveFile(path,bytes)
                || !saveFile(path+".json",QJsonDocument(metadata).toJson())) {
            error = QStringLiteral("Cannot write elevation cache: %1").arg(path); return {};
        }
        return path;
    }
    QString root;
protected:
    virtual QUrl requestUrl(Block b) const = 0;
    Dataset dataset;
private:
    Report &report;
    QString apiKey;
};
class WcsProvider final : public CachedRasterProvider {
public:
    using CachedRasterProvider::CachedRasterProvider;
private:
    QUrl requestUrl(Block b) const override { return coverageUrl(dataset,b); }
};
class ArcGisImageServerProvider final : public CachedRasterProvider {
public:
    using CachedRasterProvider::CachedRasterProvider;
private:
    QUrl requestUrl(Block b) const override { return imageServerUrl(dataset,b); }
};

class RasterSource final : public Source {
public:
    RasterSource(Dataset data, std::unique_ptr<RasterProvider> p,
                 double spacing, Report &r)
        : dataset(std::move(data)), provider(std::move(p)),
          targetSpacing(spacing), report(r) {
        cache.setMaxCost(64*1024);
    }

    bool prepare(const QVector<Point> &points, std::atomic_bool &cancel,
                 const Progress &progress, QString &error) override {
        QMap<Block,bool> blocks;

        for (const Point p : points) {
            if (cancel) return false;

            XY xy;
            if (!project(p,dataset.epsg,xy) || !inside(dataset,xy))
                continue;

            blocks.insert(blockFor(dataset,xy),true);

            const int taps = filterTaps(p);
            if (taps > 1) {
                const double span = filterSpan(p);
                const double radius = span * (double(taps)-1) / (2.0*taps);

                // Filter taps may cross cache-block edges, so prepare footprint corners too.
                for (double dx : {-radius,radius})
                    for (double dy : {-radius,radius}) {
                        const XY q{xy.x+dx,xy.y+dy};
                        if (inside(dataset,q))
                            blocks.insert(blockFor(dataset,q),true);
                    }
            }

            if (blocks.size() > MaxBlocks) {
                error = QStringLiteral(
                    "Requested area exceeds 2048 elevation blocks; generate a smaller area");
                return false;
            }
        }

        prepared = provider->acquire(blocks.keys(),cancel,progress);
        if (!cancel && dataset.noDataPolicy == "fill" && !prepared.isEmpty()) {
            if (progress) progress(0,0,QStringLiteral("Filling elevation NoData from neighbouring heights"));
            if (!prepareFilled(cancel,error)) return false;
        }
        return !cancel;
    }

    Sample sample(Point p) override {
        XY xy;
        if (!project(p,dataset.epsg,xy) || !inside(dataset,xy))
            return {0,SampleStatus::Outside};

        const int taps = filterTaps(p);
        if (taps == 1)
            return sampleProjected(xy);

        const double span = filterSpan(p);
        double height = 0;

        // Box-filter finer source rasters over one terrain-grid footprint.
        for (int y = 0; y < taps; ++y) {
            const double dy = ((y+.5)/taps-.5)*span;

            for (int x = 0; x < taps; ++x) {
                const double dx = ((x+.5)/taps-.5)*span;
                const Sample value = sampleProjected({xy.x+dx,xy.y+dy});

                // Do not average across NoData, unavailable blocks or coverage edges.
                if (!value.valid())
                    return value;

                height += value.height;
            }
        }

        return {
            float(height / double(taps*taps)),
            SampleStatus::Valid
        };
    }

private:
    bool prepareFilled(std::atomic_bool &cancel, QString &error) {
        int minCol = prepared.firstKey().column, maxCol = minCol;
        int minRow = prepared.firstKey().row, maxRow = minRow;
        for (auto it=prepared.cbegin(); it!=prepared.cend(); ++it) {
            minCol = std::min(minCol,it.key().column); maxCol = std::max(maxCol,it.key().column);
            minRow = std::min(minRow,it.key().row); maxRow = std::max(maxRow,it.key().row);
        }
        const qint64 width = (qint64(maxCol)-minCol+1)*dataset.blockPixels+2;
        const qint64 height = (qint64(maxRow)-minRow+1)*dataset.blockPixels+2;
        if (width > 32*1024*1024 || height > 32*1024*1024 || width*height > 32*1024*1024) {
            error = QStringLiteral("NoData fill area exceeds 32 million source pixels; generate a smaller area");
            return false;
        }
        filledRaster.width = int(width); filledRaster.height = int(height); filledRaster.epsg = dataset.epsg;
        const auto box = bounds(dataset,{minCol,minRow});
        filledRaster.transform = {{box[0],dataset.resolution,0,box[3],0,-dataset.resolution}};
        filledRaster.values.fill(std::numeric_limits<float>::quiet_NaN(),width*height);
        QBitArray available(width*height,false);
        for (auto it=prepared.cbegin(); it!=prepared.cend(); ++it) {
            if (cancel) return false;
            Raster block;
            if (!decode(dataset,readFile(it.value()),block,error) || !validate(dataset,it.key(),block,error))
                return false;
            const int dx = (it.key().column-minCol)*dataset.blockPixels;
            const int dy = (it.key().row-minRow)*dataset.blockPixels;
            for (int y=0; y<block.height; ++y) for (int x=0; x<block.width; ++x) {
                const int dst = (dy+y)*filledRaster.width+dx+x;
                const float h = block.values[y*block.width+x];
                available.setBit(dst);
                if (std::isfinite(h) && !(block.hasNoData && h == block.noData)
                        && !(dataset.zeroIsNoData && h == 0)) filledRaster.values[dst] = h;
            }
        }
        report.filledPixels = fillNoData(filledRaster,false,cancel,available);
        return !cancel;
    }

    double filterSpan(Point p) const {
        // Geographic raster units cannot be compared directly with terrain metres.
        if (dataset.epsg == 4326)
            return 0;

        if (dataset.epsg == 3857) {
            // Web Mercator map metres are stretched by sec(latitude).
            constexpr double pi = 3.14159265358979323846;
            return targetSpacing /
                std::cos(p.latitude*pi/180.0);
        }

        return targetSpacing;
    }

    int filterTaps(Point p) const {
        const double span = filterSpan(p);

        // Equal-resolution and coarser sources retain the existing sharp bilinear path.
        if (!std::isfinite(span)
                || span <= dataset.resolution*1.05)
            return 1;

        // Bound CPU work while retaining adequate area sampling for fine rasters.
        return std::clamp(
            int(std::ceil(span/dataset.resolution)),
            2,32);
    }

    Sample sampleProjected(XY xy) {
        if (!inside(dataset,xy))
            return {0,SampleStatus::Outside};

        const Block b = blockFor(dataset,xy);
        const QString path = prepared.value(b);

        if (path.isEmpty())
            return {0,SampleStatus::Unavailable};
        if (!filledRaster.values.isEmpty()) return filledRaster.sample(xy);

        Raster *r = cache.object(path);
        if (!r) {
            auto loaded = std::make_unique<Raster>();
            QString error;

            if (!decode(dataset,readFile(path),*loaded,error)
                    || !validate(dataset,b,*loaded,error)) {
                report.issue(error);
                prepared.remove(b);
                return {};
            }

            const int cost =
                int((loaded->values.size()*sizeof(float)+1023)/1024);

            r = loaded.get();
            cache.insert(path,loaded.release(),cost);
        }

        return r->sample(xy,dataset.zeroIsNoData);
    }

    Dataset dataset;
    Raster filledRaster;
    std::unique_ptr<RasterProvider> provider;
    double targetSpacing;
    Report &report;
    QMap<Block,QString> prepared;
    QCache<QString,Raster> cache;
};
}

QVector<Dataset> datasets(QString &error) {
    error.clear();
    QFile file(QStringLiteral(":/geo/elevation-datasets.json"));
    if (!file.open(QIODevice::ReadOnly)) { error = QStringLiteral("Elevation catalogue is missing"); return {}; }
    const QJsonObject document = QJsonDocument::fromJson(file.readAll()).object();
    if (document.value("version").toInt() != 1) { error = QStringLiteral("Invalid elevation catalogue"); return {}; }
    QVector<Dataset> result;
    QSet<QString> ids;
    for (const auto entry : document.value("datasets").toArray()) {
        Dataset d;
        d.definition = entry.toObject(); const auto &o = d.definition;
        d.id = o.value("id").toString(); d.name = o.value("name").toString();
        d.provider = o.value("provider").toString();
        d.endpoint = QUrl(o.value("endpoint").toString()); d.coverage = o.value("coverage").toString();
        d.axisX = o.value("axisX").toString(); d.axisY = o.value("axisY").toString();
        const auto authentication = o.value("authentication").toObject();
        if (o.contains("authentication")) {
            d.apiKeySecret = authentication.value("secret").toString();
            static const QRegularExpression reference(QStringLiteral("^[A-Za-z0-9._-]+$"));
            if (authentication.value("type").toString() != "basic-api-key"
                    || !reference.match(d.apiKeySecret).hasMatch()) {
                error = QStringLiteral("Invalid elevation authentication definition: %1").arg(d.id); return {};
            }
        }
        d.format = o.value("format").toString(); d.epsg = o.value("crs").toInt();
        d.verticalDatum = o.value("verticalDatum").toString();
        d.resolution = o.value("resolution").toDouble(); d.blockPixels = o.value("blockPixels").toInt();
        d.concurrentRequests = o.value("concurrentRequests").toInt(1);
        const auto origin = o.value("origin").toArray(), box = o.value("bounds").toArray();
        if (origin.size() != 2 || box.size() != 4) { error = QStringLiteral("Invalid elevation grid definition"); return {}; }
        d.originX = origin.at(0).toDouble(); d.originY = origin.at(1).toDouble();
        d.minX = box.at(0).toDouble(); d.minY = box.at(1).toDouble();
        d.maxX = box.at(2).toDouble(); d.maxY = box.at(3).toDouble();
        d.zeroIsNoData = o.value("zeroIsNoData").toBool();
        d.noDataPolicy = o.value("noDataPolicy").toString(QStringLiteral("fallback"));
        const bool wcs2 = d.provider == "wcs-2.0.1";
        const bool wcs1 = d.provider == "wcs-1.0.0";
        const bool wcs = wcs1 || wcs2;
        const bool arcgis = d.provider == "arcgis-imageserver";
        if (d.id.isEmpty() || ids.contains(d.id) || d.id.contains('/') || d.id.contains('\\') || d.id.contains("..")
                || (!wcs && !arcgis) || d.endpoint.scheme() != "https"
                || (o.contains("noDataPolicy") && !o.value("noDataPolicy").isString())
                || (d.noDataPolicy != "fallback" && d.noDataPolicy != "fill")
                || d.endpoint.host().isEmpty() || d.resolution <= 0
                || (wcs2 && (d.coverage.isEmpty() || d.axisX.isEmpty() || d.axisY.isEmpty() || d.axisX == d.axisY))
                || (wcs1 && d.coverage.isEmpty())
                || (arcgis && d.format != "image/tiff")
                || d.blockPixels < 16 || d.blockPixels > 1024
                || d.concurrentRequests < 1 || d.concurrentRequests > 4
                || d.maxX <= d.minX || d.maxY <= d.minY || !supportedCrs(d.epsg)
                || (d.format != "image/tiff" && d.format != "image/x-aaigrid")) {
            error = QStringLiteral("Invalid elevation dataset definition: %1").arg(d.id); return {};
        }
        ids.insert(d.id); result.push_back(d);
    }
    return result;
}
Block blockFor(const Dataset &d, XY p) {
    const double size = d.blockPixels*d.resolution;
    return {int(std::floor((p.x-d.originX)/size)),int(std::floor((d.originY-p.y)/size))};
}
QUrl coverageUrl(const Dataset &d, Block b) {
    QUrl url = d.endpoint;
    QUrlQuery query(url);
    const auto box = bounds(d,b);

    query.addQueryItem("SERVICE","WCS");
    query.addQueryItem("REQUEST","GetCoverage");

    if (d.provider == "wcs-1.0.0") {
        query.addQueryItem("VERSION","1.0.0");
        query.addQueryItem("COVERAGE",d.coverage);
        query.addQueryItem("CRS",QStringLiteral("EPSG:%1").arg(d.epsg));
        query.addQueryItem("RESPONSE_CRS",QStringLiteral("EPSG:%1").arg(d.epsg));
        query.addQueryItem("BBOX",
            QStringLiteral("%1,%2,%3,%4")
                .arg(decimal(box[0]), decimal(box[1]),
                     decimal(box[2]), decimal(box[3])));
        query.addQueryItem("WIDTH",QString::number(d.blockPixels+2));
        query.addQueryItem("HEIGHT",QString::number(d.blockPixels+2));

        // Known-good value for the LGL WCS 1.0 service.
        query.addQueryItem("FORMAT",
            d.format == "image/tiff" ? "GeoTIFF" : d.format);
    } else {
        query.addQueryItem("VERSION","2.0.1");
        query.addQueryItem("COVERAGEID",d.coverage);
        query.addQueryItem("SUBSET",
            QStringLiteral("%1(%2,%3)")
                .arg(d.axisX,decimal(box[0]),decimal(box[2])));
        query.addQueryItem("SUBSET",
            QStringLiteral("%1(%2,%3)")
                .arg(d.axisY,decimal(box[1]),decimal(box[3])));
        query.addQueryItem("SCALESIZE",
            QStringLiteral("%1(%3),%2(%3)")
                .arg(d.axisX,d.axisY).arg(d.blockPixels+2));
        query.addQueryItem("FORMAT",d.format);
    }

    url.setQuery(query);
    return url;
}
QUrl imageServerUrl(const Dataset &d, Block b) {
    QUrl url = d.endpoint;
    QString path = url.path();
    if (path.endsWith('/')) path.chop(1);
    url.setPath(path+"/exportImage");
    QUrlQuery query(url);
    const auto box = bounds(d,b);
    query.addQueryItem("f","image");
    query.addQueryItem("bbox",QStringLiteral("%1,%2,%3,%4").arg(decimal(box[0]),decimal(box[1]),decimal(box[2]),decimal(box[3])));
    query.addQueryItem("bboxSR",QString::number(d.epsg));
    query.addQueryItem("imageSR",QString::number(d.epsg));
    query.addQueryItem("size",QStringLiteral("%1,%1").arg(d.blockPixels+2));
    query.addQueryItem("adjustAspectRatio","false");
    query.addQueryItem("format","tiff");
    query.addQueryItem("pixelType","F32");
    query.addQueryItem("compression","None");
    query.addQueryItem("bandIds","0");
    query.addQueryItem("renderingRule",QStringLiteral(R"({"rasterFunction":"None"})"));
    query.addQueryItem("interpolation","RSP_BilinearInterpolation");
    url.setQuery(query); return url;
}
QString cacheRelativePath(const Dataset &d, Block b) {
    auto downloadDefinition = d.definition;
    downloadDefinition.remove("noDataPolicy"); // Filling changes sampling, never downloaded bytes.
    const QByteArray signature = QCryptographicHash::hash(QJsonDocument(downloadDefinition).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex();
    return QStringLiteral("cache/%1/v1-%2/%3_%4.%5").arg(d.id,QString::fromLatin1(signature))
        .arg(b.column).arg(b.row).arg(d.format == "image/tiff" ? "tif" : "asc");
}
QString hgtFileName(int lat, int lon) {
    return QStringLiteral("%1%2%3%4.hgt").arg(lat < 0 ? "S" : "N").arg(std::abs(lat),2,10,QChar('0'))
        .arg(lon < 0 ? "W" : "E").arg(std::abs(lon),3,10,QChar('0'));
}
QString findHgtFile(const QString &root, int lat, int lon) {
    if (root.trimmed().isEmpty()) return {};
    const QString name = hgtFileName(lat,lon);
    for (const QString prefix : {QStringLiteral("hgt/"),QString()}) {
        const QString path = QDir(root).filePath(prefix+name);
        if (QFileInfo(path).isFile()) return path;
    }
    return {};
}
void Report::issue(const QString &message) {
    if (!message.isEmpty() && issues.size() < 20 && !issues.contains(message)) issues.push_back(message);
}
Result generate(const QString &root, const QString &id, const QVector<Point> &points,
                double targetSpacing, float yOffset,
                std::atomic_bool &cancel, const Progress &progress,
                const QMap<QString,QString> &secrets) {
    Result result;
    if (root.trimmed().isEmpty()) { result.error = QStringLiteral("Set the geodata directory (geoPath) first"); return result; }
    if (points.isEmpty() || points.size() > 16*1024*1024
            || !std::isfinite(targetSpacing) || targetSpacing <= 0
            || !std::isfinite(yOffset)) {
        result.error = QStringLiteral("Invalid elevation generation request"); return result;
    }
    HgtSource hgt(root,result.report);
    std::unique_ptr<Source> primary;
    bool fillPolicy = false;
    if (!id.isEmpty()) {
        const auto catalog = datasets(result.error);
        if (!result.error.isEmpty()) return result;
        for (const auto &d : catalog) if (d.id == id) {
            fillPolicy = d.noDataPolicy == "fill";
            std::unique_ptr<RasterProvider> provider;
            if (d.provider == "arcgis-imageserver")
                provider = std::make_unique<ArcGisImageServerProvider>(root,d,result.report,secrets.value(d.apiKeySecret));
            else provider = std::make_unique<WcsProvider>(root,d,result.report,secrets.value(d.apiKeySecret));
            primary = std::make_unique<RasterSource>(d,std::move(provider),targetSpacing,result.report);
        }
        if (!primary) { result.error = QStringLiteral("Unknown elevation dataset: %1").arg(id); return result; }
        if (!primary->prepare(points,cancel,progress,result.error)) {
            result.cancelled = cancel.load(); return result;
        }
    }
    if (cancel) { result.cancelled = true; return result; }
    result.heights.reserve(points.size());
    int missing = 0;
    for (qsizetype i = 0; i < points.size(); ++i) {
        if (cancel) { result.cancelled = true; result.heights.clear(); return result; }
        if (progress && i%4096 == 0) progress(int(i),int(points.size()),QStringLiteral("Sampling elevation"));
        Sample value = primary ? primary->sample(points[i]) : hgt.sample(points[i]);
        if (primary) {
            if (value.valid()) ++result.report.primarySamples;
            else {
                if (value.status == SampleStatus::Outside) ++result.report.outsideSamples;
                else if (value.status == SampleStatus::NoData) ++result.report.noDataSamples;
                else ++result.report.unavailableSamples;
                if (!(fillPolicy && value.status == SampleStatus::NoData))
                    value = hgt.sample(points[i]);
                if (value.valid()) { ++result.report.fallbackSamples; ++result.report.hgtSamples; }
            }
        } else if (value.valid()) ++result.report.hgtSamples;
        if (!value.valid()) ++missing;
        result.heights.push_back(value.height+yOffset);
    }
    if (missing) {
        result.error = (fillPolicy
            ? QStringLiteral("%1 samples have no usable elevation after NoData filling and available fallback; no elevation heights were applied")
            : QStringLiteral("%1 samples have no usable elevation, including HGT fallback; no elevation heights were applied")).arg(missing);
        result.heights.clear();
    }
    return result;
}
}
