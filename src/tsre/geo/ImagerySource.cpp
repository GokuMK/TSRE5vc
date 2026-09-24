/* TSRE5 - Copyright (C) 2016 Piotr Gadecki. GPL-3.0-or-later. */
#include <tsre/geo/ImagerySource.h>
#include <tsre/geo/CrsTransform.h>

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
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QThread>
#include <QUrlQuery>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Imagery {
namespace {
constexpr double Pi = 3.14159265358979323846;
constexpr double MercatorLimit = 85.0511287798066;
constexpr qint64 MaxTileBytes = 16 * 1024 * 1024;
constexpr int MaxTiles = 1600;
constexpr int MaxMosaicSide = 12288;
constexpr int DownloadAttempts = 3;

struct DownloadResult { QByteArray bytes; QString error; };
struct LocalTile { int column = 0, row = 0, normalizedColumn = 0; };

QString safePathPart(QString value) {
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),QStringLiteral("_"));
    return value;
}

bool validImage(const QByteArray &bytes, const Dataset &dataset, QImage &image,
                QString &error) {
    image = QImage::fromData(bytes);
    if (image.isNull()) {
        error = QStringLiteral("Imagery response is not a JPEG or PNG image");
        return false;
    }
    if (image.width() != dataset.tilePixels || image.height() != dataset.tilePixels) {
        error = QStringLiteral("Imagery tile has size %1 x %2; expected %3 x %3")
            .arg(image.width()).arg(image.height()).arg(dataset.tilePixels);
        image = {};
        return false;
    }
    image = image.convertToFormat(QImage::Format_RGB888);
    return true;
}

QVector<DownloadResult> downloadWave(const QVector<QUrl> &urls,
        std::atomic_bool &cancel, const std::function<void()> &completed) {
    QVector<DownloadResult> results(urls.size());
    if (urls.isEmpty() || cancel) return results;
    if (urls.size() > 4) {
        for (auto &result : results) result.error = QStringLiteral("Invalid imagery download batch");
        return results;
    }
    QNetworkAccessManager network;
    QEventLoop loop;
    struct Pending {
        QNetworkReply *reply = nullptr;
        QByteArray bytes;
        bool tooLarge = false, timedOut = false, done = false;
    };
    std::array<Pending,4> pending;
    int finished = 0;
    QTimer cancellation;
    QObject::connect(&cancellation,&QTimer::timeout,&loop,[&] {
        if (cancel) for (auto &item : pending)
            if (item.reply && !item.reply->isFinished()) item.reply->abort();
    });
    for (int i=0;i<urls.size();++i) {
        QNetworkRequest request(urls[i]);
        request.setRawHeader("User-Agent","TSRE5vc terrain imagery");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(30000);
        auto *reply = pending[i].reply = network.get(request);
        reply->setReadBufferSize(1024*1024);
        auto *deadline = new QTimer(reply);
        deadline->setSingleShot(true);
        QObject::connect(deadline,&QTimer::timeout,&loop,[&,i,reply] {
            pending[i].timedOut = true;
            reply->abort();
        });
        QObject::connect(reply,&QNetworkReply::readyRead,&loop,[&,i,reply] {
            auto &item = pending[i];
            item.bytes += reply->readAll();
            if (item.bytes.size() > MaxTileBytes) {
                item.tooLarge = true;
                reply->abort();
            }
        });
        const auto finish = [&,i,reply,deadline] {
            auto &item = pending[i];
            if (item.done) return;
            item.done = true;
            deadline->stop();
            item.bytes += reply->readAll();
            auto &result = results[i];
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (cancel) result.error = QStringLiteral("Imagery download cancelled");
            else if (item.tooLarge || item.bytes.size() > MaxTileBytes)
                result.error = QStringLiteral("Imagery response exceeds %1 bytes").arg(MaxTileBytes);
            else if (item.timedOut) result.error = QStringLiteral("Imagery request timed out");
            else if (reply->error() != QNetworkReply::NoError || status != 200)
                result.error = QStringLiteral("Imagery request failed (HTTP %1): %2")
                    .arg(status).arg(reply->errorString());
            else result.bytes = std::move(item.bytes);
            ++finished;
            if (completed) completed();
            if (finished == urls.size()) loop.quit();
        };
        QObject::connect(reply,&QNetworkReply::finished,&loop,finish);
        deadline->start(45000);
        if (reply->isFinished()) QTimer::singleShot(0,&loop,finish);
    }
    cancellation.start(50);
    if (finished < urls.size()) loop.exec();
    return results;
}

QVector<DownloadResult> downloadWaveWithRetries(const QVector<QUrl> &urls,
        std::atomic_bool &cancel, const std::function<void()> &completed) {
    QVector<DownloadResult> results(urls.size());
    QVector<int> pending;
    pending.reserve(urls.size());
    for (int i=0;i<urls.size();++i) pending.push_back(i);
    for (int attempt=0;attempt<DownloadAttempts && !pending.isEmpty() && !cancel;++attempt) {
        if (attempt>0) QThread::msleep(unsigned(250*(1 << (attempt-1))));
        QVector<QUrl> retryUrls;
        retryUrls.reserve(pending.size());
        for (const int index:pending) retryUrls.push_back(urls[index]);
        const auto retryResults=downloadWave(retryUrls,cancel,{});
        QVector<int> failed;
        for (int i=0;i<pending.size();++i) {
            const int original=pending[i];
            results[original]=retryResults[i];
            if (!retryResults[i].error.isEmpty()) failed.push_back(original);
        }
        pending=std::move(failed);
    }
    if (!cancel && !pending.isEmpty()) for (const int index:pending)
        results[index].error += QStringLiteral(" (after %1 attempts)").arg(DownloadAttempts);
    if (completed) for (int i=0;i<urls.size();++i) completed();
    return results;
}

double groundResolution(double latitude, int zoom) {
    return 156543.03392804097 * std::cos(latitude*Pi/180.0) / std::ldexp(1.0,zoom);
}

bool isFresh(const QFileInfo &file, const Dataset &dataset) {
    if (!file.isFile() || file.size() <= 0 || file.size() > MaxTileBytes) return false;
    if (dataset.cacheMaxAgeDays <= 0) return true;
    return file.lastModified().daysTo(QDateTime::currentDateTime()) <= dataset.cacheMaxAgeDays;
}

bool decodeImage(const QByteArray &bytes, int width, int height,
                 QImage &image, QString &error) {
    image=QImage::fromData(bytes);
    if (image.isNull()) {
        error=QStringLiteral("Imagery response is not a JPEG or PNG image");
        return false;
    }
    if (image.width()!=width || image.height()!=height) {
        error=QStringLiteral("Imagery image has size %1 x %2; expected %3 x %4")
            .arg(image.width()).arg(image.height()).arg(width).arg(height);
        image={};
        return false;
    }
    image=image.convertToFormat(QImage::Format_RGB888);
    return true;
}

bool compose(const QImage &source, const QVector<QPointF> &sourcePoints,
             const Request &request, std::atomic_bool &cancel,
             const Progress &progress, QImage &output) {
    output=QImage(request.width,request.height,QImage::Format_RGB888);
    if (output.isNull()) return false;
    const int columns=request.controlColumns,rows=request.controlRows;
    for (int y=0;y<request.height;++y) {
        if (cancel) { output={}; return false; }
        const double gridY=(y+.5)*(rows-1)/request.height;
        const int cellY=std::min(int(gridY),rows-2);
        const double fy=gridY-cellY;
        uchar *destination=output.scanLine(y);
        for (int x=0;x<request.width;++x) {
            const double gridX=(x+.5)*(columns-1)/request.width;
            const int cellX=std::min(int(gridX),columns-2);
            const double fx=gridX-cellX;
            const QPointF &p00=sourcePoints[cellY*columns+cellX];
            const QPointF &p10=sourcePoints[cellY*columns+cellX+1];
            const QPointF &p01=sourcePoints[(cellY+1)*columns+cellX];
            const QPointF &p11=sourcePoints[(cellY+1)*columns+cellX+1];
            const double gx=(p00.x()*(1-fx)+p10.x()*fx)*(1-fy)
                           +(p01.x()*(1-fx)+p11.x()*fx)*fy;
            const double gy=(p00.y()*(1-fx)+p10.y()*fx)*(1-fy)
                           +(p01.y()*(1-fx)+p11.y()*fx)*fy;
            const double sampleX=std::clamp(gx-.5,0.0,double(source.width()-1));
            const double sampleY=std::clamp(gy-.5,0.0,double(source.height()-1));
            const int x0=int(sampleX),y0=int(sampleY);
            const int x1=std::min(x0+1,source.width()-1),y1=std::min(y0+1,source.height()-1);
            const double dx=sampleX-x0,dy=sampleY-y0;
            const uchar *line0=source.constScanLine(y0),*line1=source.constScanLine(y1);
            for (int channel=0;channel<3;++channel) {
                const double top=line0[x0*3+channel]*(1-dx)+line0[x1*3+channel]*dx;
                const double bottom=line1[x0*3+channel]*(1-dx)+line1[x1*3+channel]*dx;
                destination[x*3+channel]=uchar(std::clamp(
                    int(std::lround(top*(1-dy)+bottom*dy)),0,255));
            }
        }
        if (progress && (y%64==0 || y+1==request.height))
            progress(y+1,request.height,QStringLiteral("Composing terrain imagery"));
    }
    return true;
}

QString imageCacheRelativePath(const Dataset &dataset, const QUrl &url) {
    const QByteArray digest=QCryptographicHash::hash(url.toEncoded(QUrl::FullyEncoded),
                                                     QCryptographicHash::Sha256).toHex();
    const QString extension=dataset.format=="image/png"?QStringLiteral("png"):QStringLiteral("jpg");
    return QStringLiteral("cache/imagery/%1/%2/image_%3/%4.%5")
        .arg(dataset.directory,safePathPart(dataset.revision)).arg(dataset.crs)
        .arg(QString::fromLatin1(digest.left(24)),extension);
}

QVector<Dataset> loadCatalogue(const QByteArray &builtInJson,
                               const QByteArray *userJson, QString &error) {
    QString builtInError;
    QVector<Dataset> result = parseDatasets(builtInJson,builtInError);
    if (result.isEmpty()) { error = builtInError; return {}; }
    if (!userJson) { error = builtInError; return result; }
    QString userError;
    auto user = parseDatasets(*userJson,userError);
    if (user.isEmpty() && !userError.isEmpty()) {
        error = builtInError;
        if (!error.isEmpty()) error += '\n';
        error += QStringLiteral("User imagery catalogue: %1").arg(userError);
        return result;
    }
    QMap<QString,int> positions;
    for (int i=0;i<result.size();++i) positions.insert(result[i].id,i);
    for (auto dataset : user) {
        dataset.userDefined = true;
        if (positions.contains(dataset.id)) result[positions.value(dataset.id)] = dataset;
        else { positions.insert(dataset.id,result.size()); result.push_back(dataset); }
    }
    error = builtInError;
    if (!userError.isEmpty()) {
        if (!error.isEmpty()) error += '\n';
        error += QStringLiteral("User imagery catalogue: %1").arg(userError);
    }
    return result;
}
}

QVector<Dataset> parseDatasets(const QByteArray &json, QString &error) {
    error.clear();
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(json,&parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
        error = QStringLiteral("Invalid imagery catalogue JSON at byte %1: %2")
            .arg(parseError.offset).arg(parseError.errorString());
        return {};
    }
    const QJsonObject root = parsed.object();
    if (root.value("version").toInt() != 1 || !root.value("datasets").isArray()) {
        error = QStringLiteral("Invalid imagery catalogue");
        return {};
    }
    const QString detailedDefault = root.value("defaultDetailedSource").toString();
    const QString distantDefault = root.value("defaultDistantSource").toString();
    static const QRegularExpression idPattern(QStringLiteral("^[A-Za-z0-9._-]+$"));
    static const QRegularExpression directoryPattern(QStringLiteral("^[A-Za-z0-9_-]+(?:/[A-Za-z0-9_-]+)*$"));
    static const QRegularExpression matrixPattern(QStringLiteral("^[A-Za-z0-9:._-]*\\{zoom\\}[A-Za-z0-9:._-]*$"));
    static const QRegularExpression dimensionPattern(QStringLiteral("^[A-Za-z0-9_.:-]+$"));
    QVector<Dataset> result;
    QSet<QString> ids;
    QStringList rejected;
    int index = 0;
    for (const auto value : root.value("datasets").toArray()) {
        ++index;
        if (!value.isObject()) {
            rejected << QStringLiteral("Skipped imagery dataset entry %1: object expected").arg(index);
            continue;
        }
        Dataset dataset;
        dataset.definition = value.toObject();
        const auto &object = dataset.definition;
        dataset.id = object.value("id").toString();
        dataset.name = object.value("name").toString();
        dataset.provider = object.value("provider").toString();
        dataset.endpoint = QUrl(object.value("endpoint").toString());
        dataset.layer = object.value("layer").toString();
        dataset.style = object.value("style").toString();
        dataset.format = object.value("format").toString();
        dataset.tileMatrixSet = object.value("tileMatrixSet").toString();
        dataset.tileMatrixTemplate = object.value("tileMatrixTemplate").toString();
        dataset.wmsVersion = object.value("version").toString(QStringLiteral("1.3.0"));
        dataset.bboxAxisOrder = object.value("bboxAxisOrder").toString(QStringLiteral("xy"));
        dataset.directory = object.value("directory").toString();
        dataset.revision = object.value("revision").toString(QStringLiteral("current"));
        dataset.tilePixels = object.value("tilePixels").toInt();
        dataset.minZoom = object.value("minZoom").toInt();
        dataset.maxZoom = object.value("maxZoom").toInt();
        dataset.crs = object.value("crs").toInt();
        dataset.maxRequestPixels = object.value("maxRequestPixels").toInt();
        dataset.defaultRequestSize = object.value("defaultRequestSize").toInt();
        dataset.nativeResolution = object.value("nativeResolution").toDouble();
        dataset.cacheMaxAgeDays = object.value("cacheMaxAgeDays").toInt();
        dataset.detailedTerrainApproved = object.value("detailedTerrainApproved").toBool();
        dataset.distantTerrainApproved = object.value("distantTerrainApproved").toBool();
        dataset.defaultDetailedSource = dataset.id == detailedDefault;
        dataset.defaultDistantSource = dataset.id == distantDefault;
        dataset.attribution = object.value("attribution").toString();
        dataset.license = object.value("license").toString();
        dataset.information = object.value("information").toString();
        dataset.attributionUrl = QUrl(object.value("attributionUrl").toString());
        dataset.informationUrl = QUrl(object.value("informationUrl").toString());
        const auto bounds = object.value("boundsWgs84").toArray();
        if (bounds.size() == 4) {
            dataset.minLongitude = bounds[0].toDouble();
            dataset.minLatitude = bounds[1].toDouble();
            dataset.maxLongitude = bounds[2].toDouble();
            dataset.maxLatitude = bounds[3].toDouble();
        }
        bool dimensionsValid = object.value("dimensions").isUndefined()
            || object.value("dimensions").isObject();
        const auto dimensions = object.value("dimensions").toObject();
        for (auto it=dimensions.begin();it!=dimensions.end();++it) {
            if (!dimensionPattern.match(it.key()).hasMatch() || !it.value().isString()
                    || it.value().toString().isEmpty() || it.value().toString().size()>256) {
                dimensionsValid = false;
                break;
            }
            dataset.dimensions.insert(it.key(),it.value().toString());
        }
        bool requestSizesValid=object.value("requestSizes").isUndefined()
            || object.value("requestSizes").isArray();
        QSet<int> requestSizeSet;
        for (const auto sizeValue:object.value("requestSizes").toArray()) {
            const int size=sizeValue.toInt();
            if (!sizeValue.isDouble() || size<256 || size>8192
                    || double(size)!=sizeValue.toDouble() || requestSizeSet.contains(size)) {
                requestSizesValid=false;
                break;
            }
            requestSizeSet.insert(size);
            dataset.requestSizes.push_back(size);
        }
        const QString label = dataset.id.isEmpty()
            ? QStringLiteral("entry %1").arg(index) : dataset.id;
        const bool powerOfTwo = dataset.tilePixels >= 64 && dataset.tilePixels <= 1024
            && (dataset.tilePixels & (dataset.tilePixels-1)) == 0;
        const bool boundsValid = bounds.size() == 4
            && dataset.minLongitude >= -180 && dataset.maxLongitude <= 180
            && dataset.minLatitude >= -MercatorLimit && dataset.maxLatitude <= MercatorLimit
            && dataset.minLongitude < dataset.maxLongitude
            && dataset.minLatitude < dataset.maxLatitude;
        const bool urlsValid = dataset.endpoint.scheme() == "https" && !dataset.endpoint.host().isEmpty()
            && (dataset.attributionUrl.isEmpty() || dataset.attributionUrl.scheme() == "https")
            && (dataset.informationUrl.isEmpty() || dataset.informationUrl.scheme() == "https");
        const bool wmts=dataset.provider=="wmts-kvp-webmercator";
        const bool wms=dataset.provider=="wms-kvp";
        const bool arcGis=dataset.provider=="arcgis-mapserver-export";
        const bool providerValid=(wmts
                    && !dataset.style.isEmpty()
                    && !dataset.tileMatrixSet.isEmpty()
                    && matrixPattern.match(dataset.tileMatrixTemplate).hasMatch()
                    && powerOfTwo && dataset.minZoom>=0
                    && dataset.maxZoom>=dataset.minZoom && dataset.maxZoom<=24)
                || (wms && Geo::CrsTransform::supports(dataset.crs)
                    && dataset.wmsVersion=="1.3.0"
                    && (dataset.bboxAxisOrder=="xy" || dataset.bboxAxisOrder=="yx")
                    && dataset.maxRequestPixels>=256 && dataset.maxRequestPixels<=8192)
                || (arcGis && Geo::CrsTransform::supports(dataset.crs)
                    && dataset.maxRequestPixels>=256 && dataset.maxRequestPixels<=8192);
        if (!dataset.requestSizes.isEmpty()) {
            requestSizesValid=requestSizesValid
                && (wms || arcGis)
                && requestSizeSet.contains(dataset.defaultRequestSize);
            for (const int size:dataset.requestSizes)
                requestSizesValid=requestSizesValid && size<=dataset.maxRequestPixels;
        } else requestSizesValid=requestSizesValid && dataset.defaultRequestSize==0;
        if (!idPattern.match(dataset.id).hasMatch() || ids.contains(dataset.id)
                || dataset.name.trimmed().isEmpty()
                || !providerValid
                || !urlsValid || dataset.layer.isEmpty()
                || (dataset.format != "image/jpeg" && dataset.format != "image/png")
                || !directoryPattern.match(dataset.directory).hasMatch()
                || !idPattern.match(dataset.revision).hasMatch()
                || dataset.nativeResolution <= 0
                || dataset.cacheMaxAgeDays < 0 || !boundsValid || !dimensionsValid
                || !requestSizesValid) {
            rejected << QStringLiteral("Skipped imagery dataset %1: invalid or unsupported definition").arg(label);
            continue;
        }
        ids.insert(dataset.id);
        result.push_back(dataset);
    }
    if (result.isEmpty()) {
        error = rejected.isEmpty() ? QStringLiteral("Imagery catalogue contains no datasets")
                                   : rejected.join('\n');
        return {};
    }
    error = rejected.join('\n');
    return result;
}

QVector<Dataset> builtInDatasets(QString &error) {
    QFile file(QStringLiteral(":/geo/imagery-datasets.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Imagery catalogue is missing");
        return {};
    }
    return parseDatasets(file.readAll(),error);
}

QString userDatasetCataloguePath() {
    return QStringLiteral("assets/geo/imagery-datasets.json");
}

QVector<Dataset> datasets(QString &error) {
    QFile builtIn(QStringLiteral(":/geo/imagery-datasets.json"));
    if (!builtIn.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Imagery catalogue is missing");
        return {};
    }
    const QByteArray builtInJson = builtIn.readAll();
    const QString path = userDatasetCataloguePath();
    const QFileInfo info(path);
    if (!info.exists()) return loadCatalogue(builtInJson,nullptr,error);
    if (!info.isFile() || info.size() <= 0 || info.size() > 8*1024*1024) {
        auto result = loadCatalogue(builtInJson,nullptr,error);
        if (!error.isEmpty()) error += '\n';
        error += QStringLiteral("User imagery catalogue %1 has an invalid file size or type")
            .arg(QDir::toNativeSeparators(path));
        return result;
    }
    QFile user(path);
    if (!user.open(QIODevice::ReadOnly)) {
        auto result = loadCatalogue(builtInJson,nullptr,error);
        if (!error.isEmpty()) error += '\n';
        error += QStringLiteral("Cannot read user imagery catalogue: %1")
            .arg(QDir::toNativeSeparators(path));
        return result;
    }
    const QByteArray userJson = user.readAll();
    return loadCatalogue(builtInJson,&userJson,error);
}

QVector<Dataset> mergeDatasets(const QByteArray &builtInJson,
                               const QByteArray &userJson, QString &error) {
    return loadCatalogue(builtInJson,&userJson,error);
}

QString defaultDetailedSourceId(const QVector<Dataset> &catalogue) {
    for (const auto &dataset : catalogue)
        if (dataset.defaultDetailedSource && dataset.detailedTerrainApproved) return dataset.id;
    for (const auto &dataset : catalogue)
        if (dataset.detailedTerrainApproved) return dataset.id;
    return {};
}

QString defaultDistantSourceId(const QVector<Dataset> &catalogue) {
    for (const auto &dataset : catalogue)
        if (dataset.defaultDistantSource && dataset.distantTerrainApproved) return dataset.id;
    for (const auto &dataset : catalogue)
        if (dataset.distantTerrainApproved) return dataset.id;
    return {};
}

bool nearDataset(const Dataset &dataset, const QVector<GeographicPoint> &area,
                 double bufferMetres) {
    if (area.isEmpty()) return true;
    const double latitudeBuffer = std::max(0.0,bufferMetres)/111320.0;
    for (const auto point : area) {
        const double cosine = std::max(.1,std::cos(point.latitude*Pi/180.0));
        const double longitudeBuffer = latitudeBuffer/cosine;
        if (point.longitude >= dataset.minLongitude-longitudeBuffer
                && point.longitude <= dataset.maxLongitude+longitudeBuffer
                && point.latitude >= dataset.minLatitude-latitudeBuffer
                && point.latitude <= dataset.maxLatitude+latitudeBuffer)
            return true;
    }
    return false;
}

QUrl tileUrl(const Dataset &dataset, TileAddress tile) {
    QUrl url = dataset.endpoint;
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("SERVICE"),QStringLiteral("WMTS"));
    query.addQueryItem(QStringLiteral("REQUEST"),QStringLiteral("GetTile"));
    query.addQueryItem(QStringLiteral("VERSION"),QStringLiteral("1.0.0"));
    query.addQueryItem(QStringLiteral("LAYER"),dataset.layer);
    query.addQueryItem(QStringLiteral("STYLE"),dataset.style);
    query.addQueryItem(QStringLiteral("FORMAT"),dataset.format);
    query.addQueryItem(QStringLiteral("TILEMATRIXSET"),dataset.tileMatrixSet);
    QString matrix = dataset.tileMatrixTemplate;
    matrix.replace(QStringLiteral("{zoom}"),QString::number(tile.zoom));
    query.addQueryItem(QStringLiteral("TILEMATRIX"),matrix);
    query.addQueryItem(QStringLiteral("TILEROW"),QString::number(tile.row));
    query.addQueryItem(QStringLiteral("TILECOL"),QString::number(tile.column));
    for (auto it=dataset.dimensions.cbegin();it!=dataset.dimensions.cend();++it)
        query.addQueryItem(it.key(),it.value());
    url.setQuery(query);
    return url;
}

QUrl wmsUrl(const Dataset &dataset, double minX, double minY,
            double maxX, double maxY, int width, int height) {
    QUrl url=dataset.endpoint;
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("SERVICE"),QStringLiteral("WMS"));
    query.addQueryItem(QStringLiteral("REQUEST"),QStringLiteral("GetMap"));
    query.addQueryItem(QStringLiteral("VERSION"),dataset.wmsVersion);
    query.addQueryItem(QStringLiteral("LAYERS"),dataset.layer);
    query.addQueryItem(QStringLiteral("STYLES"),dataset.style);
    query.addQueryItem(QStringLiteral("FORMAT"),dataset.format);
    query.addQueryItem(QStringLiteral("TRANSPARENT"),QStringLiteral("FALSE"));
    query.addQueryItem(QStringLiteral("CRS"),QStringLiteral("EPSG:%1").arg(dataset.crs));
    const auto number=[](double value){return QString::number(value,'f',3);};
    const QString bbox=dataset.bboxAxisOrder=="yx"
        ? QStringLiteral("%1,%2,%3,%4").arg(number(minY),number(minX),number(maxY),number(maxX))
        : QStringLiteral("%1,%2,%3,%4").arg(number(minX),number(minY),number(maxX),number(maxY));
    query.addQueryItem(QStringLiteral("BBOX"),bbox);
    query.addQueryItem(QStringLiteral("WIDTH"),QString::number(width));
    query.addQueryItem(QStringLiteral("HEIGHT"),QString::number(height));
    query.addQueryItem(QStringLiteral("EXCEPTIONS"),QStringLiteral("XML"));
    for (auto it=dataset.dimensions.cbegin();it!=dataset.dimensions.cend();++it)
        query.addQueryItem(it.key(),it.value());
    url.setQuery(query);
    return url;
}

QUrl arcGisMapUrl(const Dataset &dataset, double minX, double minY,
                  double maxX, double maxY, int width, int height) {
    QUrl url=dataset.endpoint;
    QUrlQuery query(url);
    const auto number=[](double value){return QString::number(value,'f',3);};
    query.addQueryItem(QStringLiteral("bbox"),QStringLiteral("%1,%2,%3,%4")
        .arg(number(minX),number(minY),number(maxX),number(maxY)));
    query.addQueryItem(QStringLiteral("bboxSR"),QString::number(dataset.crs));
    query.addQueryItem(QStringLiteral("imageSR"),QString::number(dataset.crs));
    query.addQueryItem(QStringLiteral("size"),QStringLiteral("%1,%2").arg(width).arg(height));
    query.addQueryItem(QStringLiteral("dpi"),QStringLiteral("96"));
    query.addQueryItem(QStringLiteral("format"),dataset.format=="image/png"
                       ?QStringLiteral("png"):QStringLiteral("jpg"));
    query.addQueryItem(QStringLiteral("transparent"),QStringLiteral("false"));
    if (!dataset.layer.isEmpty())
        query.addQueryItem(QStringLiteral("layers"),QStringLiteral("show:%1").arg(dataset.layer));
    query.addQueryItem(QStringLiteral("f"),QStringLiteral("image"));
    for (auto it=dataset.dimensions.cbegin();it!=dataset.dimensions.cend();++it)
        query.addQueryItem(it.key(),it.value());
    url.setQuery(query);
    return url;
}

int chooseZoom(const Dataset &dataset, double latitude,
               double targetMetresPerPixel) {
    const double target = std::max(targetMetresPerPixel,dataset.nativeResolution);
    int selected = dataset.minZoom;
    double best = std::numeric_limits<double>::infinity();
    for (int zoom=dataset.minZoom;zoom<=dataset.maxZoom;++zoom) {
        const double resolution = groundResolution(latitude,zoom);
        const double difference = std::abs(std::log(resolution/target));
        if (difference < best) { best = difference; selected = zoom; }
    }
    return selected;
}

QPointF webMercatorPixel(GeographicPoint point, int zoom, int tilePixels) {
    const double latitude = std::clamp(point.latitude,-MercatorLimit,MercatorLimit);
    const double world = std::ldexp(double(tilePixels),zoom);
    const double sine = std::sin(latitude*Pi/180.0);
    return { (point.longitude+180.0)/360.0*world,
             (.5-std::log((1+sine)/(1-sine))/(4*Pi))*world };
}

QString cacheRelativePath(const Dataset &dataset, TileAddress tile) {
    const QString extension = dataset.format == "image/png" ? QStringLiteral("png")
                                                              : QStringLiteral("jpg");
    return QStringLiteral("cache/imagery/%1/%2/%3/%4/%5/%6.%7")
        .arg(dataset.directory,safePathPart(dataset.revision),safePathPart(dataset.tileMatrixSet))
        .arg(tile.zoom).arg(tile.column).arg(tile.row).arg(extension);
}

Result generate(const Request &request, std::atomic_bool &cancel,
                const Progress &progress) {
    Result result;
    if (request.root.trimmed().isEmpty()) {
        result.error = QStringLiteral("Geodata directory is not configured");
        return result;
    }
    if (request.width < 64 || request.height < 64 || request.width > 16384
            || request.height > 16384 || request.controlColumns < 2
            || request.controlRows < 2
            || request.controlPoints.size() != request.controlColumns*request.controlRows
            || request.terrainSizeMetres <= 0
            || qint64(request.width)*request.height*3 > 384ll*1024*1024) {
        result.error = QStringLiteral("Invalid imagery output grid");
        return result;
    }
    QString catalogueError;
    const auto catalogue = datasets(catalogueError);
    const Dataset *dataset = nullptr;
    for (const auto &entry : catalogue) if (entry.id == request.datasetId) {
        dataset = &entry;
        break;
    }
    if (!dataset) {
        result.error = QStringLiteral("Unknown imagery source: %1").arg(request.datasetId);
        return result;
    }
    if (cancel) { result.cancelled = true; return result; }
    double centreLatitude = 0;
    for (const auto point : request.controlPoints) {
        if (!std::isfinite(point.latitude) || !std::isfinite(point.longitude)
                || std::abs(point.latitude)>90 || std::abs(point.longitude)>180) {
            result.error = QStringLiteral("Invalid geographic imagery control grid");
            return result;
        }
        centreLatitude += point.latitude;
    }
    centreLatitude /= request.controlPoints.size();
    result.report.targetMetresPerPixel = request.terrainSizeMetres
        / std::max(request.width,request.height);
    if (dataset->provider=="wms-kvp" || dataset->provider=="arcgis-mapserver-export") {
        Geo::CrsTransform transform(dataset->crs);
        QVector<QPointF> projected;
        projected.reserve(request.controlPoints.size());
        for (const auto point:request.controlPoints) {
            Geo::ProjectedPoint converted;
            if (!transform.forward({point.latitude,point.longitude},converted)) {
                result.error=QStringLiteral("Cannot transform terrain coordinates to EPSG:%1")
                    .arg(dataset->crs);
                return result;
            }
            projected.push_back({converted.x,converted.y});
        }
        double minX=projected[0].x(),maxX=minX,minY=projected[0].y(),maxY=minY;
        for (const auto point:projected) {
            minX=std::min(minX,point.x());maxX=std::max(maxX,point.x());
            minY=std::min(minY,point.y());maxY=std::max(maxY,point.y());
        }
        const double extentWidth=maxX-minX,extentHeight=maxY-minY;
        if (!(extentWidth>0) || !(extentHeight>0)) {
            result.error=QStringLiteral("Invalid projected imagery bounds");
            return result;
        }
        int requestLimit=dataset->maxRequestPixels;
        double requestedSpacing=result.report.targetMetresPerPixel;
        if (request.sourcePixels>0) {
            if (!dataset->requestSizes.contains(request.sourcePixels)) {
                result.error=QStringLiteral("Unsupported imagery request size %1 for %2")
                    .arg(request.sourcePixels).arg(dataset->name);
                return result;
            }
            requestLimit=std::min(requestLimit,request.sourcePixels);
            requestedSpacing=request.terrainSizeMetres/request.sourcePixels;
        } else if (!dataset->requestSizes.isEmpty()) {
            requestLimit=std::min(requestLimit,dataset->defaultRequestSize);
            requestedSpacing=request.terrainSizeMetres/dataset->defaultRequestSize;
        }
        const double desired=std::max(requestedSpacing,dataset->nativeResolution);
        const double desiredWidth=extentWidth/desired;
        const double desiredHeight=extentHeight/desired;
        const double scale=std::min(1.0,requestLimit/std::max(desiredWidth,desiredHeight));
        const int sourceWidth=std::clamp(int(std::ceil(desiredWidth*scale)),64,requestLimit);
        const int sourceHeight=std::clamp(int(std::ceil(desiredHeight*scale)),64,requestLimit);
        if (qint64(sourceWidth)*sourceHeight*3
                +qint64(request.width)*request.height*3 > 384ll*1024*1024) {
            result.error=QStringLiteral("Imagery request requires too much decoded memory");
            return result;
        }
        result.report.sourceMetresPerPixel=std::max(extentWidth/sourceWidth,
                                                     extentHeight/sourceHeight);
        result.report.tiles=1;
        const QUrl url=dataset->provider=="wms-kvp"
            ?wmsUrl(*dataset,minX,minY,maxX,maxY,sourceWidth,sourceHeight)
            :arcGisMapUrl(*dataset,minX,minY,maxX,maxY,sourceWidth,sourceHeight);
        const QString path=QDir(request.root).filePath(imageCacheRelativePath(*dataset,url));
        QImage source;
        QString imageError;
        const QFileInfo cached(path);
        if (isFresh(cached,*dataset)) {
            QFile file(path);
            QByteArray bytes;
            if (file.open(QIODevice::ReadOnly)) bytes=file.readAll();
            if (decodeImage(bytes,sourceWidth,sourceHeight,source,imageError))
                ++result.report.cacheHits;
            else QFile::remove(path);
        }
        if (source.isNull()) {
            const auto downloads=downloadWaveWithRetries({url},cancel,[&] {
                if (progress) progress(1,1,QStringLiteral("Downloading terrain imagery"));
            });
            if (cancel) {result.cancelled=true;return result;}
            if (downloads.isEmpty() || !downloads[0].error.isEmpty()) {
                result.error=downloads.isEmpty()?QStringLiteral("Imagery request failed")
                    : downloads[0].error;
                return result;
            }
            if (!decodeImage(downloads[0].bytes,sourceWidth,sourceHeight,source,imageError)) {
                result.error=imageError;
                return result;
            }
            QDir().mkpath(QFileInfo(path).absolutePath());
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly)
                    || file.write(downloads[0].bytes)!=downloads[0].bytes.size()
                    || !file.commit())
                result.report.issues << QStringLiteral("Cannot write imagery cache image %1")
                    .arg(QDir::toNativeSeparators(path));
            ++result.report.downloads;
            result.report.downloadedBytes+=downloads[0].bytes.size();
        }
        QVector<QPointF> sourcePoints;
        sourcePoints.reserve(projected.size());
        for (const auto point:projected)
            sourcePoints.push_back({(point.x()-minX)/extentWidth*sourceWidth,
                                    (maxY-point.y())/extentHeight*sourceHeight});
        if (!compose(source,sourcePoints,request,cancel,progress,result.image)) {
            if (cancel) result.cancelled=true;
            else result.error=QStringLiteral("Cannot allocate imagery output");
            return result;
        }
        if (!catalogueError.isEmpty()) result.report.issues << catalogueError;
        return result;
    }
    result.report.zoom = chooseZoom(*dataset,centreLatitude,
                                    result.report.targetMetresPerPixel);
    result.report.sourceMetresPerPixel = groundResolution(centreLatitude,result.report.zoom);
    const int matrixTiles = 1 << result.report.zoom;
    const double worldPixels = double(dataset->tilePixels)*matrixTiles;
    QVector<QPointF> projected;
    projected.reserve(request.controlPoints.size());
    double referenceX = 0;
    for (int i=0;i<request.controlPoints.size();++i) {
        QPointF pixel = webMercatorPixel(request.controlPoints[i],result.report.zoom,
                                         dataset->tilePixels);
        if (i==0) referenceX = pixel.x();
        while (pixel.x()-referenceX > worldPixels*.5) pixel.rx() -= worldPixels;
        while (pixel.x()-referenceX < -worldPixels*.5) pixel.rx() += worldPixels;
        projected.push_back(pixel);
    }
    double minX=projected[0].x(),maxX=minX,minY=projected[0].y(),maxY=minY;
    for (const auto point : projected) {
        minX=std::min(minX,point.x()); maxX=std::max(maxX,point.x());
        minY=std::min(minY,point.y()); maxY=std::max(maxY,point.y());
    }
    int minColumn=int(std::floor((minX-1)/dataset->tilePixels));
    int maxColumn=int(std::floor((maxX+1)/dataset->tilePixels));
    int minRow=std::max(0,int(std::floor((minY-1)/dataset->tilePixels)));
    int maxRow=std::min(matrixTiles-1,int(std::floor((maxY+1)/dataset->tilePixels)));
    const int tileColumns=maxColumn-minColumn+1,tileRows=maxRow-minRow+1;
    const qint64 tileCount=qint64(tileColumns)*tileRows;
    const qint64 mosaicWidth=qint64(tileColumns)*dataset->tilePixels;
    const qint64 mosaicHeight=qint64(tileRows)*dataset->tilePixels;
    const qint64 decodedBytes=mosaicWidth*mosaicHeight*3+qint64(request.width)*request.height*3;
    if (tileColumns<=0 || tileRows<=0 || tileCount>MaxTiles
            || mosaicWidth>MaxMosaicSide || mosaicHeight>MaxMosaicSide
            || decodedBytes>384ll*1024*1024) {
        result.error = QStringLiteral("Imagery request is too large (%1 tiles, %2 x %3 source pixels)")
            .arg(tileCount).arg(mosaicWidth).arg(mosaicHeight);
        return result;
    }
    result.report.tiles=int(tileCount);
    QImage mosaic(int(mosaicWidth),int(mosaicHeight),QImage::Format_RGB888);
    if (mosaic.isNull()) { result.error=QStringLiteral("Cannot allocate imagery mosaic"); return result; }
    mosaic.fill(Qt::black);
    QPainter painter(&mosaic);
    QVector<LocalTile> missing;
    const QDir root(request.root);
    for (int row=minRow;row<=maxRow;++row) for (int column=minColumn;column<=maxColumn;++column) {
        const int normalized=(column%matrixTiles+matrixTiles)%matrixTiles;
        const TileAddress address{result.report.zoom,normalized,row};
        const QString path=root.filePath(cacheRelativePath(*dataset,address));
        QFileInfo file(path);
        QImage image;
        QString imageError;
        if (isFresh(file,*dataset)) {
            QFile cached(path);
            QByteArray bytes;
            if (cached.open(QIODevice::ReadOnly)) bytes=cached.readAll();
            if (validImage(bytes,*dataset,image,imageError)) {
                painter.drawImage((column-minColumn)*dataset->tilePixels,
                                  (row-minRow)*dataset->tilePixels,image);
                ++result.report.cacheHits;
                continue;
            }
            QFile::remove(path);
        }
        missing.push_back({column,row,normalized});
    }
    int completed=0;
    bool requiredTileMissing=false;
    for (int offset=0;offset<missing.size() && !cancel;offset+=4) {
        const int count=std::min(4,int(missing.size())-offset);
        QVector<QUrl> urls;
        for (int i=0;i<count;++i) {
            const auto tile=missing[offset+i];
            urls.push_back(tileUrl(*dataset,{result.report.zoom,tile.normalizedColumn,tile.row}));
        }
        const auto downloads=downloadWaveWithRetries(urls,cancel,[&] {
            ++completed;
            if (progress) progress(completed,missing.size(),QStringLiteral("Downloading imagery tiles"));
        });
        for (int i=0;i<count;++i) {
            const auto tile=missing[offset+i];
            if (!downloads[i].error.isEmpty()) {
                requiredTileMissing=true;
                result.report.issues << QStringLiteral("Tile %1/%2/%3: %4")
                    .arg(result.report.zoom).arg(tile.normalizedColumn).arg(tile.row)
                    .arg(downloads[i].error);
                continue;
            }
            QImage image;
            QString imageError;
            if (!validImage(downloads[i].bytes,*dataset,image,imageError)) {
                requiredTileMissing=true;
                result.report.issues << QStringLiteral("Tile %1/%2/%3: %4")
                    .arg(result.report.zoom).arg(tile.normalizedColumn).arg(tile.row).arg(imageError);
                continue;
            }
            const TileAddress address{result.report.zoom,tile.normalizedColumn,tile.row};
            const QString path=root.filePath(cacheRelativePath(*dataset,address));
            QDir().mkpath(QFileInfo(path).absolutePath());
            QSaveFile output(path);
            if (!output.open(QIODevice::WriteOnly)
                    || output.write(downloads[i].bytes)!=downloads[i].bytes.size()
                    || !output.commit()) {
                result.report.issues << QStringLiteral("Cannot write imagery cache tile %1")
                    .arg(QDir::toNativeSeparators(path));
            }
            painter.drawImage((tile.column-minColumn)*dataset->tilePixels,
                              (tile.row-minRow)*dataset->tilePixels,image);
            ++result.report.downloads;
            result.report.downloadedBytes += downloads[i].bytes.size();
        }
    }
    painter.end();
    if (cancel) { result.cancelled=true; return result; }
    if (requiredTileMissing) {
        result.error = QStringLiteral("Required imagery tiles are unavailable");
        return result;
    }
    QVector<QPointF> sourcePoints;
    sourcePoints.reserve(projected.size());
    for (const auto point:projected)
        sourcePoints.push_back({point.x()-minColumn*dataset->tilePixels,
                                point.y()-minRow*dataset->tilePixels});
    if (!compose(mosaic,sourcePoints,request,cancel,progress,result.image)) {
        if (cancel) result.cancelled=true;
        else result.error=QStringLiteral("Cannot allocate imagery output");
        return result;
    }
    if (!catalogueError.isEmpty()) result.report.issues << catalogueError;
    return result;
}

}
