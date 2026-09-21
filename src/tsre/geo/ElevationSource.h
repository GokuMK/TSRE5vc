#pragma once

#include <tsre/geo/ElevationRaster.h>
#include <QJsonObject>
#include <QMap>
#include <QUrl>
#include <atomic>
#include <functional>

namespace Elevation {
struct Dataset {
    QString id, name, coverage, format, verticalDatum, axisX, axisY;
    QString requestFormat, scaleAxisX, scaleAxisY;
    QString directory, fileGrid, downloadUrlTemplate, downloadCompression;
    QString stacCollection, fileRevision;
    QString coordinateTransform, transformAssetPath, transformAssetEntry;
    QString attribution;
    QString provider = QStringLiteral("wcs-2.0.1");
    QUrl endpoint, stacEndpoint, transformAssetUrl;
    // Caller resolves this profile-secret reference; transport selects Basic or query auth.
    QString apiKeySecret;
    QString apiKeyParameter; // Nonempty for query-api-key; otherwise HTTP Basic.
    int epsg = 2180, blockPixels = 512, concurrentRequests = 1;
    double resolution = 1, originX = 0, originY = 0, fileTileSize = 0;
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool zeroIsNoData = false;
    bool allowExpandedGrid = false;
    bool defaultFileSource = false;
    QString noDataPolicy = QStringLiteral("fallback");
    QJsonObject definition;
};
QVector<Dataset> datasets(QString &error);
// Returns valid entries and reports rejected entries in error. Invalid JSON is fatal.
QVector<Dataset> parseDatasets(const QByteArray &json, QString &error);
QString defaultFileSourceId(const QVector<Dataset> &catalogue);
bool nearDataset(const Dataset &dataset, const QVector<Point> &area, double bufferMetres = 10000);
struct Block {
    int column = 0, row = 0;
    bool operator<(const Block &other) const {
        return row < other.row || (row == other.row && column < other.column);
    }
};
Block blockFor(const Dataset &dataset, XY point);
QUrl coverageUrl(const Dataset &dataset, Block block);
// Validate the returned georeferencing; expanded grids require explicit opt-in.
bool validateRasterGrid(const Dataset &dataset, Block block, const Raster &raster, QString &error);
QUrl imageServerUrl(const Dataset &dataset, Block block);
QString cacheRelativePath(const Dataset &dataset, Block block);
QString hgtFileName(int latitude, int longitude);
QString findHgtFile(const QString &root, const Dataset &dataset,
                    int latitude, int longitude);
QUrl fileDownloadUrl(const Dataset &dataset, int latitude, int longitude);
bool readHgtFile(const QString &path, int latitude, int longitude,
                 Raster &raster, QString &error);
using Progress = std::function<void(int done, int total, const QString &message)>;
struct Report {
    int primarySamples = 0, fallbackSamples = 0;
    int noDataSamples = 0, outsideSamples = 0, unavailableSamples = 0;
    int cacheHits = 0, downloads = 0;
    int filledPixels = 0;
    QString fallbackSourceName;
    QStringList issues;
    void issue(const QString &message);
};
struct Result {
    QVector<float> heights;
    Report report;
    QString error;
    bool cancelled = false;
    bool success() const { return error.isEmpty() && !cancelled && !heights.isEmpty(); }
};
// prepare() may retrieve data. sample() uses local data only.
class Source {
public:
    virtual ~Source() = default;
    virtual bool prepare(const QVector<Point> &points, std::atomic_bool &cancel,
                         const Progress &progress, QString &error) = 0;
    virtual Sample sample(Point point) = 0;
};
// Called on a worker thread. An empty dataset ID is accepted as the legacy
// alias for the catalogue's default file source.
// targetSpacing is the output terrain vertex spacing in metres.
Result generate(const QString &root, const QString &datasetId,
                const QVector<Point> &points, double targetSpacing, float yOffset,
                std::atomic_bool &cancel, const Progress &progress = {},
                const QMap<QString,QString> &secrets = {});
}
