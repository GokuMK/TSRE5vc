#pragma once

#include <QString>
#include <memory>

namespace Elevation {
struct Dataset;
struct Report;
class Source;

std::unique_ptr<Source> createWfsElevationSource(const QString &root,
                                                 const Dataset &dataset,
                                                 double targetSpacing,
                                                 Report &report);
}
