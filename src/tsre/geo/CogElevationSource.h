#pragma once

#include <memory>
#include <QMap>
#include <QJsonObject>
#include <QString>

namespace Elevation {
class Source;
struct Dataset;
struct Report;

// Pure STAC metadata helpers. Explicit asset keys use the COG header as the
// authoritative grid; legacy catalogues retain their resolution/CRS selection.
QJsonObject selectStacElevationAsset(const Dataset &dataset, const QJsonObject &feature);
QString stacElevationRevision(const QJsonObject &feature);

std::unique_ptr<Source> createCogElevationSource(const QString &root,
    const Dataset &dataset, Report &report,
    const QMap<QString,QString> &secrets = {});
}
