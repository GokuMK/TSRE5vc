#include <tsre/geo/ElevationSource.h>
#include <tsre/geo/CogElevationSource.h>
#include <tsre/geo/ElevationDownload.h>
#include <mzip/miniz/miniz.h>

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
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <memory>

namespace Elevation {
namespace {
constexpr qint64 MaxDownload = 32 * 1024 * 1024;
constexpr qint64 MaxHgtBytes = 64 * 1024 * 1024;
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
}
bool validateRasterGrid(const Dataset &d, Block b, const Raster &r, QString &error) {
    const auto box = bounds(d,b);
    const auto &t = r.transform;
    bool grid = std::abs(t[0]-box[0]) <= 1e-5 && std::abs(t[3]-box[3]) <= 1e-5
        && std::abs(t[1]-d.resolution) <= 1e-8 && std::abs(t[5]+d.resolution) <= 1e-8;
    if (d.allowExpandedGrid && t[1] > 0 && t[5] < 0) {
        // Reprojection may expand the envelope. Require all requested pixel
        // centres (including halos) to remain sampleable, with at most 10%
        // excess extent on any edge. Never relabel the returned transform.
        const double margin = (box[2]-box[0])*.1;
        const double right = t[0]+r.width*t[1], bottom = t[3]+r.height*t[5];
        grid = t[0] >= box[0]-margin && right <= box[2]+margin
            && bottom >= box[1]-margin && t[3] <= box[3]+margin
            && t[0]+t[1]*.5 <= box[0]+d.resolution*.5+1e-5
            && right-t[1]*.5 >= box[2]-d.resolution*.5-1e-5
            && bottom-t[5]*.5 <= box[1]+d.resolution*.5+1e-5
            && t[3]+t[5]*.5 >= box[3]-d.resolution*.5-1e-5;
    }
    if (!grid || r.epsg != d.epsg || r.width != d.blockPixels+2 || r.height != d.blockPixels+2
            || !std::all_of(t.begin(),t.end(),[](double v) { return std::isfinite(v); })
            || t[2] != 0 || t[4] != 0) {
        error = QStringLiteral("Service raster does not match the requested elevation grid");
        return false;
    }
    return true;
}
namespace {
QByteArray readFile(const QString &path, qint64 max = MaxDownload) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > max) return {};
    return file.readAll();
}
bool gunzip(const QByteArray &input, QByteArray &output, QString &error) {
    output.clear();
    if (input.size() < 18 || quint8(input[0]) != 0x1f || quint8(input[1]) != 0x8b
            || quint8(input[2]) != 8 || (quint8(input[3]) & 0xe0)) {
        error = QStringLiteral("Invalid gzip header"); return false;
    }
    const quint8 flags = quint8(input[3]);
    qsizetype offset = 10, trailer = input.size()-8;
    if (flags & 0x04) {
        if (offset+2 > trailer) { error = QStringLiteral("Truncated gzip extra field"); return false; }
        const quint16 size = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(input.constData()+offset));
        offset += 2+size;
    }
    const auto skipString = [&] {
        while (offset < trailer && input[offset] != '\0') ++offset;
        return offset++ < trailer;
    };
    if ((flags & 0x08) && !skipString()) { error = QStringLiteral("Truncated gzip file name"); return false; }
    if ((flags & 0x10) && !skipString()) { error = QStringLiteral("Truncated gzip comment"); return false; }
    if (flags & 0x02) offset += 2;
    if (offset >= trailer) { error = QStringLiteral("Truncated gzip stream"); return false; }
    const auto *tail = reinterpret_cast<const uchar*>(input.constData()+trailer);
    const quint32 expectedCrc = qFromLittleEndian<quint32>(tail);
    const quint32 size = qFromLittleEndian<quint32>(tail+4);
    if (size == 0 || size > MaxHgtBytes) { error = QStringLiteral("Invalid gzip output size"); return false; }
    output.resize(size);
    const size_t written = tinfl_decompress_mem_to_mem(output.data(),size,
        input.constData()+offset,size_t(trailer-offset),0);
    if (written != size) { output.clear(); error = QStringLiteral("Cannot decompress gzip elevation file"); return false; }
    const quint32 actualCrc = quint32(mz_crc32(MZ_CRC32_INIT,
        reinterpret_cast<const unsigned char*>(output.constData()),output.size()));
    if (actualCrc != expectedCrc) { output.clear(); error = QStringLiteral("Gzip elevation checksum mismatch"); return false; }
    return true;
}
bool decodeHgtFileBytes(const QByteArray &stored, bool compressed, int lat, int lon,
                        Raster &raster, QString &error) {
    QByteArray raw;
    if (compressed) {
        if (!gunzip(stored,raw,error)) return false;
    } else raw = stored;
    return readHgt(raw,lat,lon,raster,error);
}
bool saveFile(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
class FileHgtSource final : public Source {
public:
    FileHgtSource(QString path, Dataset data, Report &r)
        : root(std::move(path)), dataset(std::move(data)), report(r) { cache.setMaxCost(128*1024); }
    bool prepare(const QVector<Point> &points, std::atomic_bool &cancel,
                 const Progress &progress, QString &error) override {
        struct Cell { int latitude, longitude; };
        QMap<QString,Cell> missing;
        for (const Point p : points) {
            if (cancel) return false;
            if (!std::isfinite(p.latitude) || !std::isfinite(p.longitude)
                    || p.latitude < dataset.minY || p.latitude >= dataset.maxY
                    || p.longitude < dataset.minX || p.longitude >= dataset.maxX) continue;
            const int lat = int(std::floor(p.latitude)), lon = int(std::floor(p.longitude));
            if (findHgtFile(root,dataset,lat,lon).isEmpty())
                missing.insert(hgtFileName(lat,lon),{lat,lon});
        }
        if (missing.size() > MaxBlocks) {
            error = QStringLiteral("Requested area exceeds 2048 elevation files; generate a smaller area");
            return false;
        }
        if (missing.isEmpty() || dataset.downloadUrlTemplate.isEmpty()) return true;
        const QVector<Cell> cells = missing.values();
        int done = 0, failures = 0;
        for (qsizetype first=0; first<cells.size(); first+=dataset.concurrentRequests) {
            if (cancel) return false;
            if (failures >= 3) {
                report.issue(QStringLiteral("File downloads stopped after three consecutive failures; using available local elevation files"));
                break;
            }
            const int count = int(std::min(qsizetype(dataset.concurrentRequests),cells.size()-first));
            QVector<QUrl> urls;
            for (int i=0; i<count; ++i)
                urls.push_back(fileDownloadUrl(dataset,cells[first+i].latitude,cells[first+i].longitude));
            const auto responses = downloadWave(urls,cancel,[&](int finished) {
                if (progress) progress(done+finished,cells.size(),QStringLiteral("Downloading elevation files"));
            });
            if (cancel) return false;
            for (int i=0; i<count; ++i) {
                const Cell cell = cells[first+i];
                QString error = responses[i].error;
                if (error.isEmpty()) {
                    Raster raster;
                    const bool compressed = dataset.downloadCompression == "gzip";
                    if (decodeHgtFileBytes(responses[i].bytes,compressed,cell.latitude,cell.longitude,raster,error)
                            && findHgtFile(root,dataset,cell.latitude,cell.longitude).isEmpty()) {
                        const QString suffix = compressed ? QStringLiteral(".gz") : QString();
                        const QString path = QDir(root).filePath(dataset.directory+'/'+hgtFileName(cell.latitude,cell.longitude)+suffix);
                        QJsonObject metadata{{"dataset",dataset.id},{"url",urls[i].toString()},
                            {"retrievedUtc",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                            {"verticalDatum",dataset.verticalDatum},{"attribution",dataset.attribution},
                            {"attributionUrl",dataset.definition.value("attributionUrl")},
                            {"sha256",QString::fromLatin1(QCryptographicHash::hash(responses[i].bytes,QCryptographicHash::Sha256).toHex())}};
                        if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !saveFile(path,responses[i].bytes)
                                || !saveFile(path+".json",QJsonDocument(metadata).toJson())) {
                            QFile::remove(path);
                            error = QStringLiteral("Cannot write elevation file: %1").arg(path);
                        } else ++report.downloads;
                    }
                }
                if (!error.isEmpty()) {
                    ++failures;
                    report.issue(QStringLiteral("%1: %2").arg(hgtFileName(cell.latitude,cell.longitude),error));
                } else failures = 0;
                ++done;
            }
        }
        return !cancel;
    }
    Sample sample(Point p) override {
        if (!std::isfinite(p.latitude) || !std::isfinite(p.longitude)
                || p.latitude < dataset.minY || p.latitude >= dataset.maxY
                || p.longitude < dataset.minX || p.longitude >= dataset.maxX)
            return {0,SampleStatus::Outside};
        const int lat = int(std::floor(p.latitude)), lon = int(std::floor(p.longitude));
        const QString key = hgtFileName(lat,lon);
        Raster *r = cache.object(key);
        if (!r) {
            if (failed.contains(key)) return {};
            QString error;
            auto loaded = std::make_unique<Raster>();
            const QString path = findHgtFile(root,dataset,lat,lon);
            if (path.isEmpty() || !readHgtFile(path,lat,lon,*loaded,error)) {
                failed.insert(key);
                report.issue(QStringLiteral("%1: %2").arg(key,path.isEmpty()
                    ? QStringLiteral("Elevation file missing") : error));
                return {};
            }
            const int cost = int((loaded->values.size()*sizeof(float)+1023)/1024);
            r = loaded.get(); cache.insert(key,loaded.release(),cost);
        }
        return sampleLegacyHgt(*r,p);
    }
private:
    QString root;
    Dataset dataset;
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
            if (apiKey.isEmpty() || (dataset.apiKeyParameter.isEmpty() && apiKey.contains(':'))
                    || apiKey.contains('\r') || apiKey.contains('\n')) {
                report.issue(QStringLiteral("Missing or invalid elevation API key: %1 in profile-local secrets.json; using available cache and configured fallback")
                    .arg(dataset.apiKeySecret));
                notify(blocks.size(),QStringLiteral("Prepared"));
                return prepared;
            }
            if (dataset.apiKeyParameter.isEmpty()) authorization = "Basic " + (apiKey.toUtf8()+':').toBase64();
        }
        for (qsizetype first=0; first<missing.size(); first+=dataset.concurrentRequests) {
            if (cancel) return {};
            if (stopNetwork) {
                report.issue(QStringLiteral("Downloads stopped after three consecutive failures; using available cache and configured fallback"));
                notify(blocks.size(),QStringLiteral("Prepared"));
                break;
            }
            QVector<QUrl> urls;
            const int count = int(std::min(qsizetype(dataset.concurrentRequests),missing.size()-first));
            for (int i=0; i<count; ++i) urls.push_back(requestUrl(missing[first+i]));
            const auto responses = downloadWave(urls,cancel,[&](int finished) {
                notify(done+finished,QStringLiteral("Completed"));
            },{},authorization,{dataset.apiKeyParameter,apiKey});
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
        bool valid = !bytes.isEmpty() && decode(dataset,bytes,raster,error) && validateRasterGrid(dataset,b,raster,error);
        if (valid) {
            const QJsonObject metadata = QJsonDocument::fromJson(readFile(path+".json",64*1024)).object();
            valid = metadata.value("sha256").toString().toLatin1() == QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
        }
        if (valid) { ++report.cacheHits; return path; }
        return {};
    }
    QString store(Block b, const QByteArray &bytes, QString &error) {
        Raster raster;
        if (!decode(dataset,bytes,raster,error) || !validateRasterGrid(dataset,b,raster,error)) return {};
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
        : projection(data.epsg), dataset(std::move(data)), provider(std::move(p)),
          targetSpacing(spacing), report(r) {
        cache.setMaxCost(64*1024);
    }

    bool prepare(const QVector<Point> &points, std::atomic_bool &cancel,
                 const Progress &progress, QString &error) override {
        QMap<Block,bool> blocks;

        for (const Point p : points) {
            if (cancel) return false;

            XY xy;
            if (!projection.forward(p,xy) || !inside(dataset,xy))
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
        if (!projection.forward(p,xy) || !inside(dataset,xy))
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
            if (!decode(dataset,readFile(it.value()),block,error) || !validateRasterGrid(dataset,it.key(),block,error))
                return false;
            const int dx = (it.key().column-minCol)*dataset.blockPixels;
            const int dy = (it.key().row-minRow)*dataset.blockPixels;
            for (int y=0; y<block.height; ++y) for (int x=0; x<block.width; ++x) {
                const int dst = (dy+y)*filledRaster.width+dx+x;
                float h = block.values[y*block.width+x];
                if (dataset.allowExpandedGrid) {
                    const auto sample = block.sample({box[0]+(dx+x+.5)*dataset.resolution,
                        box[3]-(dy+y+.5)*dataset.resolution},dataset.zeroIsNoData);
                    h = sample.valid() ? sample.height : std::numeric_limits<float>::quiet_NaN();
                }
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
                    || !validateRasterGrid(dataset,b,*loaded,error)) {
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

    Geo::CrsTransform projection;
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
    return parseDatasets(file.readAll(),error);
}
QVector<Dataset> parseDatasets(const QByteArray &json, QString &error) {
    error.clear();
    QJsonParseError parseError;
    const QJsonObject document = QJsonDocument::fromJson(json,&parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        error = QStringLiteral("Invalid elevation catalogue JSON at byte %1: %2")
            .arg(parseError.offset).arg(parseError.errorString()); return {};
    }
    if (document.value("version").toInt() != 1 || !document.value("datasets").isArray()) {
        error = QStringLiteral("Invalid elevation catalogue"); return {};
    }
    QVector<Dataset> result;
    QSet<QString> ids;
    QStringList rejected;
    int index = 0;
    for (const auto entry : document.value("datasets").toArray()) {
        ++index;
        Dataset d;
        d.definition = entry.toObject(); const auto &o = d.definition;
        d.id = o.value("id").toString(); d.name = o.value("name").toString();
        d.provider = o.value("provider").toString();
        d.endpoint = QUrl(o.value("endpoint").toString()); d.coverage = o.value("coverage").toString();
        d.axisX = o.value("axisX").toString(); d.axisY = o.value("axisY").toString();
        d.scaleAxisX = o.value("scaleAxisX").toString(d.axisX);
        d.scaleAxisY = o.value("scaleAxisY").toString(d.axisY);
        d.requestFormat = o.value("requestFormat").toString();
        d.allowExpandedGrid = o.value("allowExpandedGrid").toBool();
        d.directory = o.value("directory").toString();
        d.fileGrid = o.value("fileGrid").toString();
        d.attribution = o.value("attribution").toString();
        const auto download = o.value("download").toObject();
        d.downloadUrlTemplate = download.value("urlTemplate").toString();
        d.downloadCompression = download.value("compression").toString();
        d.fileTileSize = download.value("tileSize").toDouble();
        d.fileRevision = download.value("revision").toString();
        d.stacEndpoint = QUrl(download.value("endpoint").toString());
        d.stacCollection = download.value("collection").toString();
        const QString label = d.id.isEmpty() ? QStringLiteral("entry %1").arg(index) : d.id;
        const auto authentication = o.value("authentication").toObject();
        if (o.contains("authentication")) {
            d.apiKeySecret = authentication.value("secret").toString();
            static const QRegularExpression reference(QStringLiteral("^[A-Za-z0-9._-]+$"));
            const QString type = authentication.value("type").toString();
            if (type == "query-api-key") d.apiKeyParameter = authentication.value("parameter").toString();
            if ((type != "basic-api-key" && type != "query-api-key")
                    || (type == "query-api-key" && !reference.match(d.apiKeyParameter).hasMatch())
                    || !reference.match(d.apiKeySecret).hasMatch()) {
                rejected << QStringLiteral("Skipped elevation dataset %1: invalid or unsupported authentication").arg(label);
                continue;
            }
        }
        d.format = o.value("format").toString(); d.epsg = o.value("crs").toInt();
        d.verticalDatum = o.value("verticalDatum").toString();
        d.resolution = o.value("resolution").toDouble(); d.blockPixels = o.value("blockPixels").toInt();
        d.concurrentRequests = o.value("concurrentRequests").toInt(1);
        const auto origin = o.value("origin").toArray(), box = o.value("bounds").toArray();
        const bool file = d.provider == "file";
        if ((!file && origin.size() != 2) || box.size() != 4) {
            rejected << QStringLiteral("Skipped elevation dataset %1: invalid grid definition").arg(label); continue;
        }
        if (!file) { d.originX = origin.at(0).toDouble(); d.originY = origin.at(1).toDouble(); }
        d.minX = box.at(0).toDouble(); d.minY = box.at(1).toDouble();
        d.maxX = box.at(2).toDouble(); d.maxY = box.at(3).toDouble();
        d.zeroIsNoData = o.value("zeroIsNoData").toBool();
        d.noDataPolicy = o.value("noDataPolicy").toString(QStringLiteral("fallback"));
        const bool wcs2 = d.provider == "wcs-2.0.1";
        const bool wcs1 = d.provider == "wcs-1.0.0";
        const bool wcs = wcs1 || wcs2;
        const bool arcgis = d.provider == "arcgis-imageserver";
        static const QRegularExpression relativeDirectory(
            QStringLiteral("^[A-Za-z0-9_-]+(?:/[A-Za-z0-9_-]+)*$"));
        static const QRegularExpression safeName(QStringLiteral("^[A-Za-z0-9._-]+$"));
        QUrl fileProbe;
        if (!d.downloadUrlTemplate.isEmpty()) {
            QString probe = d.downloadUrlTemplate;
            probe.replace("{latitudeBand}","N00").replace("{tile}","N00E000")
                .replace("{northing}","0").replace("{easting}","0");
            fileProbe = QUrl(probe);
        }
        const bool validHgt = file && d.format == "hgt" && d.fileGrid == "degree"
            && d.epsg == 4326 && relativeDirectory.match(d.directory).hasMatch()
            && !o.contains("authentication")
            && (!o.contains("download") || (o.value("download").isObject()
                && !d.downloadUrlTemplate.isEmpty()
                && d.downloadUrlTemplate.contains("{latitudeBand}")
                && d.downloadUrlTemplate.contains("{tile}")
                && d.downloadCompression == "gzip"
                && fileProbe.scheme() == "https" && !fileProbe.host().isEmpty()));
        const bool validProjectedTiff = file && d.format == "geotiff" && d.fileGrid == "projected"
            && d.resolution > 0 && d.fileTileSize > 0
            && relativeDirectory.match(d.directory).hasMatch() && !o.contains("authentication")
            && safeName.match(d.fileRevision).hasMatch()
            && o.value("download").isObject() && d.downloadUrlTemplate.contains("{northing}")
            && d.downloadUrlTemplate.contains("{easting}")
            && fileProbe.scheme() == "https" && !fileProbe.host().isEmpty();
        const bool validStacTiff = file && d.format == "geotiff" && d.fileGrid == "stac"
            && d.resolution > 0 && relativeDirectory.match(d.directory).hasMatch()
            && !o.contains("authentication") && o.value("download").isObject()
            && d.stacEndpoint.scheme() == "https" && !d.stacEndpoint.host().isEmpty()
            && !d.stacCollection.isEmpty();
        const bool validFile = validHgt || validProjectedTiff || validStacTiff;
        if (d.id.isEmpty() || ids.contains(d.id) || d.id.contains('/') || d.id.contains('\\') || d.id.contains("..")
                || (!wcs && !arcgis && !validFile)
                || (o.contains("allowExpandedGrid") && !o.value("allowExpandedGrid").isBool())
                || (o.contains("requestFormat") && (!o.value("requestFormat").isString() || d.requestFormat.isEmpty()))
                || (o.contains("scaleAxisX") && !o.value("scaleAxisX").isString())
                || (o.contains("scaleAxisY") && !o.value("scaleAxisY").isString())
                || (o.contains("noDataPolicy") && !o.value("noDataPolicy").isString())
                || (d.noDataPolicy != "fallback" && d.noDataPolicy != "fill")
                || (!file && (d.endpoint.scheme() != "https" || d.endpoint.host().isEmpty() || d.resolution <= 0))
                || (wcs2 && (d.coverage.isEmpty() || d.axisX.isEmpty() || d.axisY.isEmpty() || d.axisX == d.axisY))
                || (wcs2 && (d.scaleAxisX.isEmpty() || d.scaleAxisY.isEmpty() || d.scaleAxisX == d.scaleAxisY))
                || (wcs1 && d.coverage.isEmpty())
                || (arcgis && d.format != "image/tiff")
                || (!file && (d.blockPixels < 16 || d.blockPixels > 1024))
                || d.concurrentRequests < 1 || d.concurrentRequests > 4
                || d.maxX <= d.minX || d.maxY <= d.minY || !Geo::CrsTransform::supports(d.epsg)
                || (!file && d.format != "image/tiff" && d.format != "image/x-aaigrid")) {
            rejected << QStringLiteral("Skipped elevation dataset %1: invalid or unsupported definition").arg(label); continue;
        }
        ids.insert(d.id); result.push_back(d);
    }
    const QString configuredDefault = document.value("defaultFileSource").toString();
    bool markedDefault = false;
    if (!configuredDefault.isEmpty()) {
        for (auto &dataset : result) if (dataset.id == configuredDefault && dataset.provider == "file") {
            dataset.defaultFileSource = markedDefault = true; break;
        }
        if (!markedDefault)
            rejected << QStringLiteral("Default elevation file source is missing or invalid: %1").arg(configuredDefault);
    } else if (document.contains("defaultFileSource")) {
        rejected << QStringLiteral("Default elevation file source must be a dataset ID");
    }
    if (!markedDefault) for (auto &dataset : result) if (dataset.provider == "file") {
        dataset.defaultFileSource = true; break;
    }
    error = rejected.join('\n');
    return result;
}
QString defaultFileSourceId(const QVector<Dataset> &catalogue) {
    for (const auto &dataset : catalogue) if (dataset.defaultFileSource) return dataset.id;
    for (const auto &dataset : catalogue) if (dataset.provider == "file") return dataset.id;
    return {};
}
bool nearDataset(const Dataset &d, const QVector<Point> &area, double bufferMetres) {
    if (area.isEmpty()) return true; // No usable route location: do not hide sources.
    const Geo::CrsTransform projection(d.epsg);
    double left = std::numeric_limits<double>::infinity(), right = -left;
    double bottom = left, top = right, maxLatitude = 0;
    for (const auto p : area) {
        XY xy;
        if (!projection.forward(p,xy)) continue;
        left = std::min(left,xy.x); right = std::max(right,xy.x);
        bottom = std::min(bottom,xy.y); top = std::max(top,xy.y);
        maxLatitude = std::max(maxLatitude,std::abs(p.latitude));
    }
    if (!std::isfinite(left)) return false;
    double dx = std::max(0.0,bufferMetres), dy = dx;
    const double cosine = std::max(.01,std::cos(maxLatitude*3.14159265358979323846/180));
    if (d.epsg == 3857) dx = dy = dx/cosine;
    else if (d.epsg == 4326) { dy /= 111320; dx = dy/cosine; }
    return right+dx >= d.minX && left-dx <= d.maxX && top+dy >= d.minY && bottom-dy <= d.maxY;
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

        // Retain the established TIFF alias unless a service specifies its own.
        query.addQueryItem("FORMAT",
            !d.requestFormat.isEmpty() ? d.requestFormat : d.format == "image/tiff" ? "GeoTIFF" : d.format);
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
                .arg(d.scaleAxisX.isEmpty() ? d.axisX : d.scaleAxisX,
                     d.scaleAxisY.isEmpty() ? d.axisY : d.scaleAxisY).arg(d.blockPixels+2));
        query.addQueryItem("FORMAT",d.requestFormat.isEmpty() ? d.format : d.requestFormat);
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
QString findHgtFile(const QString &root, const Dataset &dataset, int lat, int lon) {
    if (root.trimmed().isEmpty() || dataset.provider != "file" || dataset.format != "hgt"
            || dataset.directory.isEmpty()) return {};
    const QString name = hgtFileName(lat,lon);
    const QString directory = QDir(root).filePath(dataset.directory);
    for (const QString suffix : {QString(),QStringLiteral(".gz")}) {
        const QString path = QDir(directory).filePath(name+suffix);
        if (QFileInfo(path).isFile()) return path;
    }
    return {};
}
QUrl fileDownloadUrl(const Dataset &dataset, int lat, int lon) {
    QString tile = hgtFileName(lat,lon);
    tile.chop(4);
    QString url = dataset.downloadUrlTemplate;
    url.replace("{latitudeBand}",tile.left(3));
    url.replace("{tile}",tile);
    return QUrl(url);
}
bool readHgtFile(const QString &path, int lat, int lon, Raster &raster, QString &error) {
    const QByteArray bytes = readFile(path,MaxHgtBytes);
    if (bytes.isEmpty()) { error = QStringLiteral("Cannot read elevation file"); return false; }
    return decodeHgtFileBytes(bytes,path.endsWith(".gz",Qt::CaseInsensitive),lat,lon,raster,error);
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
    QString catalogueError;
    const auto catalog = datasets(catalogueError);
    result.report.issue(catalogueError);
    if (catalog.isEmpty()) { result.error = catalogueError.isEmpty()
        ? QStringLiteral("Elevation catalogue contains no usable datasets") : catalogueError; return result; }
    const QString fallbackId = defaultFileSourceId(catalog);
    const QString selectedId = id.isEmpty() ? fallbackId : id;
    const Dataset *selected = nullptr, *fallbackDataset = nullptr;
    for (const auto &dataset : catalog) {
        if (dataset.id == selectedId) selected = &dataset;
        if (dataset.id == fallbackId) fallbackDataset = &dataset;
    }
    if (!selected) { result.error = QStringLiteral("Unknown elevation dataset: %1").arg(selectedId); return result; }
    const auto createSource = [&](const Dataset &dataset) -> std::unique_ptr<Source> {
        if (dataset.provider == "file" && dataset.format == "geotiff")
            return createCogElevationSource(root,dataset,result.report);
        if (dataset.provider == "file")
            return std::make_unique<FileHgtSource>(root,dataset,result.report);
        std::unique_ptr<RasterProvider> provider;
        if (dataset.provider == "arcgis-imageserver")
            provider = std::make_unique<ArcGisImageServerProvider>(root,dataset,result.report,secrets.value(dataset.apiKeySecret));
        else provider = std::make_unique<WcsProvider>(root,dataset,result.report,secrets.value(dataset.apiKeySecret));
        return std::make_unique<RasterSource>(dataset,std::move(provider),targetSpacing,result.report);
    };
    std::unique_ptr<Source> primary;
    primary = createSource(*selected);
    if (!primary->prepare(points,cancel,progress,result.error)) {
        result.cancelled = cancel.load(); return result;
    }
    if (cancel) { result.cancelled = true; return result; }
    const bool fillPolicy = selected->noDataPolicy == "fill";
    std::unique_ptr<Source> fallback;
    if (fallbackDataset && fallbackDataset->id != selected->id) {
        fallback = createSource(*fallbackDataset);
        result.report.fallbackSourceName = fallbackDataset->name;
    }
    QVector<Sample> samples;
    samples.reserve(points.size());
    QVector<Point> fallbackPoints;
    QVector<qsizetype> fallbackIndices;
    for (qsizetype i = 0; i < points.size(); ++i) {
        if (cancel) { result.cancelled = true; return result; }
        if (progress && i%4096 == 0) progress(int(i),int(points.size()),QStringLiteral("Sampling elevation"));
        const Sample value = primary->sample(points[i]);
        samples.push_back(value);
        if (value.valid()) ++result.report.primarySamples;
        else {
            if (value.status == SampleStatus::Outside) ++result.report.outsideSamples;
            else if (value.status == SampleStatus::NoData) ++result.report.noDataSamples;
            else ++result.report.unavailableSamples;
            if (fallback && !(fillPolicy && value.status == SampleStatus::NoData)) {
                fallbackPoints.push_back(points[i]); fallbackIndices.push_back(i);
            }
        }
    }
    if (fallback && !fallbackPoints.isEmpty()) {
        if (!fallback->prepare(fallbackPoints,cancel,progress,result.error)) {
            result.cancelled = cancel.load(); return result;
        }
        for (qsizetype i=0; i<fallbackPoints.size(); ++i) {
            if (cancel) { result.cancelled = true; return result; }
            const Sample value = fallback->sample(fallbackPoints[i]);
            if (value.valid()) { samples[fallbackIndices[i]] = value; ++result.report.fallbackSamples; }
        }
    }
    result.heights.reserve(samples.size());
    int missing = 0;
    for (const Sample value : samples) {
        if (!value.valid()) ++missing;
        result.heights.push_back(value.height+yOffset);
    }
    if (missing) {
        result.error = (fillPolicy
            ? QStringLiteral("%1 samples have no usable elevation after NoData filling and available fallback; no elevation heights were applied")
            : fallback ? QStringLiteral("%1 samples have no usable elevation, including the configured fallback; no elevation heights were applied")
                       : QStringLiteral("%1 samples have no usable elevation from the selected source; no elevation heights were applied")).arg(missing);
        result.heights.clear();
    }
    return result;
}
}
