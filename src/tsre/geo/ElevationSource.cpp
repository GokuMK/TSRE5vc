#include <tsre/geo/ElevationSource.h>

#include <QCache>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
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
    return d.format == "image/tiff" ? readGeoTiff(bytes,r,error) : readAsciiGrid(bytes,d.epsg,r,error);
}
bool validate(const Dataset &d, Block b, const Raster &r, QString &error) {
    const auto box = bounds(d,b);
    if (r.epsg != d.epsg || r.width != d.blockPixels+2 || r.height != d.blockPixels+2
            || std::abs(r.transform[0]-box[0]) > 1e-5 || std::abs(r.transform[3]-box[3]) > 1e-5
            || std::abs(r.transform[1]-d.resolution) > 1e-8
            || std::abs(r.transform[5]+d.resolution) > 1e-8
            || r.transform[2] != 0 || r.transform[4] != 0) {
        error = QStringLiteral("Service raster does not match the requested native-resolution grid");
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
QByteArray download(QNetworkAccessManager &manager, const QUrl &url,
                    std::atomic_bool &cancel, QString &error) {
    if (cancel) return {};
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "TSRE5vc terrain elevation");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    std::unique_ptr<QNetworkReply> reply(manager.get(request));
    reply->setReadBufferSize(1024*1024);
    QEventLoop loop;
    QTimer timer, deadline;
    QByteArray bytes;
    bool tooLarge = false, timedOut = false;
    QObject::connect(reply.get(), &QNetworkReply::readyRead, &loop, [&] {
        bytes += reply->readAll();
        if (bytes.size() > MaxDownload) { tooLarge = true; reply->abort(); }
    });
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] { if (cancel) reply->abort(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] { timedOut = true; reply->abort(); });
    timer.start(50); deadline.setSingleShot(true); deadline.start(45000);
    if (!reply->isFinished()) loop.exec();
    bytes += reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (cancel) return {};
    if (tooLarge || bytes.size() > MaxDownload) error = QStringLiteral("Elevation response exceeds 32 MiB");
    else if (timedOut) error = QStringLiteral("Elevation request timed out");
    else if (reply->error() != QNetworkReply::NoError || status != 200)
        error = QStringLiteral("Elevation request failed (HTTP %1): %2").arg(status).arg(reply->errorString());
    if (!error.isEmpty()) return {};
    return bytes;
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
    virtual QString acquire(Block b, std::atomic_bool &cancel, QString &error) = 0;
};
class WcsProvider final : public RasterProvider {
public:
    WcsProvider(QString path, Dataset data, Report &r)
        : root(std::move(path)), dataset(std::move(data)), report(r) {}
    QString acquire(Block b, std::atomic_bool &cancel, QString &error) override {
        const QString path = QDir(root).filePath(cacheRelativePath(dataset,b));
        Raster raster;
        QByteArray bytes = readFile(path);
        bool cached = !bytes.isEmpty() && decode(dataset,bytes,raster,error) && validate(dataset,b,raster,error);
        if (cached) {
            const QJsonObject metadata = QJsonDocument::fromJson(readFile(path+".json",64*1024)).object();
            cached = metadata.value("sha256").toString().toLatin1() == QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
        }
        if (cached) { ++report.cacheHits; return path; }
        error.clear();
        if (failedDownloads >= 3) {
            error = QStringLiteral("Downloads stopped after three consecutive failures; using available cache and HGT");
            return {};
        }
        bytes = download(network,coverageUrl(dataset,b),cancel,error);
        if (cancel) return {};
        if (!error.isEmpty() || !decode(dataset,bytes,raster,error) || !validate(dataset,b,raster,error)) {
            ++failedDownloads;
            return {};
        }
        failedDownloads = 0;
        ++report.downloads;
        QJsonObject metadata;
        metadata["dataset"] = dataset.id;
        metadata["url"] = coverageUrl(dataset,b).toString();
        metadata["retrievedUtc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        metadata["verticalDatum"] = dataset.verticalDatum;
        metadata["sha256"] = QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !saveFile(path,bytes)
                || !saveFile(path+".json",QJsonDocument(metadata).toJson())) {
            error = QStringLiteral("Cannot write elevation cache: %1").arg(path); return {};
        }
        return path;
    }
private:
    QString root;
    Dataset dataset;
    Report &report;
    QNetworkAccessManager network;
    int failedDownloads = 0;
};

class RasterSource final : public Source {
public:
    RasterSource(Dataset data, std::unique_ptr<RasterProvider> p, Report &r)
        : dataset(std::move(data)), provider(std::move(p)), report(r) { cache.setMaxCost(64*1024); }
    bool prepare(const QVector<Point> &points, std::atomic_bool &cancel,
                 const Progress &progress, QString &error) override {
        QMap<Block,bool> blocks;
        for (const Point p : points) {
            if (cancel) return false;
            XY xy;
            if (project(p,dataset.epsg,xy) && inside(dataset,xy)) blocks.insert(blockFor(dataset,xy),true);
            if (blocks.size() > MaxBlocks) {
                error = QStringLiteral("Requested area exceeds 2048 elevation blocks; generate a smaller area"); return false;
            }
        }
        int done = 0;
        for (auto it = blocks.cbegin(); it != blocks.cend(); ++it) {
            if (cancel) return false;
            const Block b = it.key();
            if (progress) progress(done,blocks.size(),QStringLiteral("Preparing 1 m elevation block %1/%2").arg(done+1).arg(blocks.size()));
            QString problem;
            const QString path = provider->acquire(b,cancel,problem);
            if (cancel) return false;
            if (path.isEmpty()) report.issue(QStringLiteral("Block %1,%2: %3").arg(b.column).arg(b.row).arg(problem));
            else prepared.insert(b,path);
            ++done;
        }
        return true;
    }
    Sample sample(Point p) override {
        XY xy;
        if (!project(p,dataset.epsg,xy) || !inside(dataset,xy)) return {0,SampleStatus::Outside};
        const Block b = blockFor(dataset,xy);
        const QString path = prepared.value(b);
        if (path.isEmpty()) return {0,SampleStatus::Unavailable};
        Raster *r = cache.object(path);
        if (!r) {
            auto loaded = std::make_unique<Raster>();
            QString error;
            if (!decode(dataset,readFile(path),*loaded,error) || !validate(dataset,b,*loaded,error)) {
                report.issue(error); prepared.remove(b); return {};
            }
            const int cost = int((loaded->values.size()*sizeof(float)+1023)/1024);
            r = loaded.get(); cache.insert(path,loaded.release(),cost);
        }
        return r->sample(xy,dataset.zeroIsNoData);
    }
private:
    Dataset dataset;
    std::unique_ptr<RasterProvider> provider;
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
        d.endpoint = QUrl(o.value("endpoint").toString()); d.coverage = o.value("coverage").toString();
        d.axisX = o.value("axisX").toString(); d.axisY = o.value("axisY").toString();
        d.format = o.value("format").toString(); d.epsg = o.value("crs").toInt();
        d.verticalDatum = o.value("verticalDatum").toString();
        d.resolution = o.value("resolution").toDouble(); d.blockPixels = o.value("blockPixels").toInt();
        const auto origin = o.value("origin").toArray(), box = o.value("bounds").toArray();
        if (origin.size() != 2 || box.size() != 4) { error = QStringLiteral("Invalid elevation grid definition"); return {}; }
        d.originX = origin.at(0).toDouble(); d.originY = origin.at(1).toDouble();
        d.minX = box.at(0).toDouble(); d.minY = box.at(1).toDouble();
        d.maxX = box.at(2).toDouble(); d.maxY = box.at(3).toDouble();
        d.zeroIsNoData = o.value("zeroIsNoData").toBool();
        if (d.id.isEmpty() || ids.contains(d.id) || d.id.contains('/') || d.id.contains('\\') || d.id.contains("..")
                || o.value("provider").toString() != "wcs-2.0.1" || d.endpoint.scheme() != "https"
                || d.endpoint.host().isEmpty() || d.coverage.isEmpty() || d.resolution <= 0
                || d.axisX.isEmpty() || d.axisY.isEmpty() || d.axisX == d.axisY
                || d.blockPixels < 16 || d.blockPixels > 1024
                || d.maxX <= d.minX || d.maxY <= d.minY || (d.epsg != 2180 && d.epsg != 4326)
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
    query.addQueryItem("SERVICE","WCS"); query.addQueryItem("VERSION","2.0.1");
    query.addQueryItem("REQUEST","GetCoverage"); query.addQueryItem("COVERAGEID",d.coverage);
    query.addQueryItem("SUBSET",QStringLiteral("%1(%2,%3)").arg(d.axisX,decimal(box[0]),decimal(box[2])));
    query.addQueryItem("SUBSET",QStringLiteral("%1(%2,%3)").arg(d.axisY,decimal(box[1]),decimal(box[3])));
    query.addQueryItem("SCALESIZE",QStringLiteral("%1(%3),%2(%3)").arg(d.axisX,d.axisY).arg(d.blockPixels+2));
    query.addQueryItem("FORMAT",d.format);
    url.setQuery(query); return url;
}
QString cacheRelativePath(const Dataset &d, Block b) {
    const QByteArray signature = QCryptographicHash::hash(QJsonDocument(d.definition).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex();
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
                float yOffset, std::atomic_bool &cancel, const Progress &progress) {
    Result result;
    if (root.trimmed().isEmpty()) { result.error = QStringLiteral("Set the geodata directory (geoPath) first"); return result; }
    if (points.isEmpty() || points.size() > 16*1024*1024 || !std::isfinite(yOffset)) {
        result.error = QStringLiteral("Invalid elevation generation request"); return result;
    }
    HgtSource hgt(root,result.report);
    std::unique_ptr<Source> primary;
    if (!id.isEmpty()) {
        const auto catalog = datasets(result.error);
        if (!result.error.isEmpty()) return result;
        for (const auto &d : catalog) if (d.id == id)
            primary = std::make_unique<RasterSource>(d,std::make_unique<WcsProvider>(root,d,result.report),result.report);
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
                value = hgt.sample(points[i]);
                if (value.valid()) { ++result.report.fallbackSamples; ++result.report.hgtSamples; }
            }
        } else if (value.valid()) ++result.report.hgtSamples;
        if (!value.valid()) ++missing;
        result.heights.push_back(value.height+yOffset);
    }
    if (missing) {
        result.error = QStringLiteral("%1 samples have no usable elevation, including HGT fallback; no elevation heights were applied").arg(missing);
        result.heights.clear();
    }
    return result;
}
}
