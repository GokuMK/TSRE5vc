/* TSRE5 - Copyright (C) 2016 Piotr Gadecki. GPL-3.0-or-later. */
#pragma once

#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QUrl>
#include <QVector>
#include <atomic>
#include <functional>

namespace Imagery {

struct GeographicPoint {
    double latitude = 0;
    double longitude = 0;
};

struct Dataset {
    QString id, name, provider, layer, style, format;
    QString tileMatrixSet, tileMatrixTemplate, directory, revision;
    QString urlTemplate;
    QString downloadUrlTemplate, apiKeySecret, apiKeyParameter;
    QString wmsVersion, bboxAxisOrder;
    QString attribution, license, information;
    QUrl endpoint, attributionUrl, informationUrl;
    QMap<QString,QString> dimensions;
    QVector<int> requestSizes;
    int tilePixels = 256, minZoom = 0, maxZoom = 0;
    int crs = 0, maxRequestPixels = 0, requestBlockPixels = 0;
    int defaultRequestSize = 0;
    double nativeResolution = 0, fileTileSize = 0;
    double minLongitude = -180, minLatitude = -85.05112878;
    double maxLongitude = 180, maxLatitude = 85.05112878;
    int cacheMaxAgeDays = 0;
    bool detailedTerrainApproved = false;
    bool distantTerrainApproved = false;
    bool persistentCache = true;
    bool defaultDetailedSource = false;
    bool defaultDistantSource = false;
    bool userDefined = false;
    QJsonObject definition;
};

QVector<Dataset> datasets(QString &error);
QVector<Dataset> builtInDatasets(QString &error);
QString userDatasetCataloguePath();
QVector<Dataset> parseDatasets(const QByteArray &json, QString &error);
QVector<Dataset> mergeDatasets(const QByteArray &builtInJson,
                               const QByteArray &userJson, QString &error);
QString defaultDetailedSourceId(const QVector<Dataset> &catalogue);
QString defaultDistantSourceId(const QVector<Dataset> &catalogue);
bool nearDataset(const Dataset &dataset, const QVector<GeographicPoint> &area,
                 double bufferMetres = 10000);

struct TileAddress {
    int zoom = 0, column = 0, row = 0;
};

QUrl tileUrl(const Dataset &dataset, TileAddress tile);
QUrl staticMapUrl(const Dataset &dataset, GeographicPoint centre, int zoom);
QUrl wmsUrl(const Dataset &dataset, double minX, double minY,
            double maxX, double maxY, int width, int height);
QUrl arcGisMapUrl(const Dataset &dataset, double minX, double minY,
                  double maxX, double maxY, int width, int height);
QUrl arcGisImageUrl(const Dataset &dataset, double minX, double minY,
                    double maxX, double maxY, int width, int height);
int chooseZoom(const Dataset &dataset, double latitude,
               double targetMetresPerPixel);
QPointF webMercatorPixel(GeographicPoint point, int zoom, int tilePixels = 256);
QString cacheRelativePath(const Dataset &dataset, TileAddress tile);

using Progress = std::function<void(int done, int total, const QString &message)>;

struct Request {
    QString root;
    QString datasetId;
    int width = 0, height = 0;
    int controlColumns = 0, controlRows = 0;
    QVector<GeographicPoint> controlPoints;
    double terrainSizeMetres = 0;
    int sourcePixels = 0;
    QMap<QString,QString> secrets;
};

struct Report {
    int zoom = -1, tiles = 0, cacheHits = 0, downloads = 0;
    qint64 downloadedBytes = 0;
    double targetMetresPerPixel = 0;
    double sourceMetresPerPixel = 0;
    QStringList issues;
};

struct Result {
    QImage image;
    Report report;
    QString error;
    bool cancelled = false;
    bool success() const { return error.isEmpty() && !cancelled && !image.isNull(); }
};

// Worker-thread operation. Coordinate conversion into controlPoints must be
// completed on the UI thread before calling this function.
Result generate(const Request &request, std::atomic_bool &cancel,
                const Progress &progress = {});

}
