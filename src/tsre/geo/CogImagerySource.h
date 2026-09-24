/* TSRE5 - Copyright (C) 2016 Piotr Gadecki. GPL-3.0-or-later. */
#pragma once

#include <tsre/geo/ImagerySource.h>

namespace Imagery {

// Loads only the internal COG tiles required by the projected request bounds.
// The returned image exactly covers minX,minY,maxX,maxY in dataset.crs.
bool loadStacCogImage(const QString &root, const Dataset &dataset,
                      const QVector<GeographicPoint> &controlPoints,
                      double minX, double minY, double maxX, double maxY,
                      int width, int height, std::atomic_bool &cancel,
                      const Progress &progress, Report &report,
                      QImage &image, QString &error);

}
