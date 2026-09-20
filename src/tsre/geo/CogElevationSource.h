#pragma once

#include <memory>
#include <QString>

namespace Elevation {
class Source;
struct Dataset;
struct Report;

std::unique_ptr<Source> createCogElevationSource(const QString &root,
    const Dataset &dataset, Report &report);
}
