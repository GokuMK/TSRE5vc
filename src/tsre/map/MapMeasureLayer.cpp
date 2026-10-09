/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "MapMeasureLayer.h"
#include "MapPalette.h"
#include "MapView.h"
#include "OsmMapLayer.h"
#include "TrackMapLayer.h"
#include <QOpenGLFunctions>
#include <cmath>
#include <vector>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderItem.h>

MapMeasureLayer::MapMeasureLayer() : line(std::make_unique<OglObj>()), halo(std::make_unique<OglObj>()) {}

MapMeasureLayer::~MapMeasureLayer() = default;

void MapMeasureLayer::set(const MapGroundPoint &a, const MapGroundPoint &b) {
    from = a;
    to = b;
    active = true;
    ++changes;
}

void MapMeasureLayer::clear() {
    active = false;
    ++changes;
}

bool MapMeasureLayer::shown() const {
    return active && gameLength() > 0.0;
}

double MapMeasureLayer::gameLength() const {
    const double dx = double(to.tileX - from.tileX) * 2048.0 + double(to.x) - double(from.x);
    const double dz = double(to.tileZ - from.tileZ) * 2048.0 + double(to.z) - double(from.z);
    return std::sqrt(dx * dx + dz * dz);
}

double MapMeasureLayer::geoLength(GeoWorldCoordinateConverter *converter) const {
    if (converter == nullptr)
        return -1.0;
    double lat1, lon1, lat2, lon2;
    OsmMapLayer::toLatLon(converter, from.tileX, from.tileZ, from.x, from.z, lat1, lon1);
    OsmMapLayer::toLatLon(converter, to.tileX, to.tileZ, to.x, to.z, lat2, lon2);
    return geodesicMetres(lat1, lon1, lat2, lon2);
}

QString MapMeasureLayer::text(double gameMetres, double geoMetres) {
    const qint64 game = qint64(std::llround(gameMetres));
    // Both only when they differ by the precision shown: rounding alone would make
    // the label switch between one and two values while dragging.
    if (geoMetres < 0.0 || !std::isfinite(geoMetres) || std::abs(geoMetres - gameMetres) < 1.0)
        //% "%1 m"
        return qtTrId("route.editor.map.measure.length").arg(game);
    //% "%1 m (geo %2 m)"
    return qtTrId("route.editor.map.measure.length.geo").arg(game).arg(qint64(std::llround(geoMetres)));
}

MapLabel MapMeasureLayer::label(GeoWorldCoordinateConverter *converter) const {
    MapLabel label;
    label.tileX = to.tileX;
    label.tileZ = to.tileZ;
    label.x = to.x;
    label.z = to.z;
    label.text = text(gameLength(), geoLength(converter));
    label.priority = LabelPriority;
    label.major = true;
    label.kind = MapLabelKind::Measure;
    return label;
}

void MapMeasureLayer::pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette,
                                      float pixelRatio) {
    if (!shown())
        return;
    // Ends relative to the view's tile.
    const float a[2] = {float(from.tileX - view.tileX) * 2048.0f + from.x,
                        float(from.tileZ - view.tileZ) * 2048.0f + from.z};
    const float b[2] = {float(to.tileX - view.tileX) * 2048.0f + to.x,
                        float(to.tileZ - view.tileZ) * 2048.0f + to.z};
    const float mpp = view.metresPerPixel * pixelRatio;
    auto build = [&](OglObj &object, float height, float width, float marker, const QColor &colour) {
        const float segment[6] = {a[0], height, a[1], b[0], height, b[1]};
        std::vector<float> triangles;
        TrackMapLayer::appendRibbons(triangles, segment, 1, width * mpp);
        // The start; the end has the label's dot.
        TrackMapLayer::appendOctagon(triangles, a[0], height, a[1], marker * mpp);
        object.setMaterial(float(colour.redF()), float(colour.greenF()), float(colour.blueF()));
        object.init(triangles.data(), int(triangles.size()), RenderItem::V, GL_TRIANGLES);
        object.pushRenderItem(queue);
    };
    build(*halo, HaloHeight, LinePixels + 2.0f * HaloPixels, MapLabelLayer::DotPixels + 2.0f * HaloPixels,
          palette.labelHalo);
    build(*line, LineHeight, LinePixels, MapLabelLayer::DotPixels, palette.pointer);
}

double MapMeasureLayer::geodesicMetres(double lat1, double lon1, double lat2, double lon2) {
    constexpr double Pi = 3.14159265358979323846;
    constexpr double A = 6378137.0, F = 1.0 / 298.257223563, B = A * (1.0 - F);
    const double radians = Pi / 180.0;
    const double l = (lon2 - lon1) * radians;
    const double u1 = std::atan((1.0 - F) * std::tan(lat1 * radians));
    const double u2 = std::atan((1.0 - F) * std::tan(lat2 * radians));
    const double sinU1 = std::sin(u1), cosU1 = std::cos(u1), sinU2 = std::sin(u2), cosU2 = std::cos(u2);
    double lambda = l;
    for (int i = 0; i < 200; ++i) {
        const double sinLambda = std::sin(lambda), cosLambda = std::cos(lambda);
        const double sinSigma = std::sqrt((cosU2 * sinLambda) * (cosU2 * sinLambda)
                + (cosU1 * sinU2 - sinU1 * cosU2 * cosLambda) * (cosU1 * sinU2 - sinU1 * cosU2 * cosLambda));
        if (sinSigma == 0.0)
            return 0.0;  // the same point
        const double cosSigma = sinU1 * sinU2 + cosU1 * cosU2 * cosLambda;
        const double sigma = std::atan2(sinSigma, cosSigma);
        const double sinAlpha = cosU1 * cosU2 * sinLambda / sinSigma;
        const double cos2Alpha = 1.0 - sinAlpha * sinAlpha;
        const double cos2SigmaM = cos2Alpha != 0.0 ? cosSigma - 2.0 * sinU1 * sinU2 / cos2Alpha : 0.0;
        const double c = F / 16.0 * cos2Alpha * (4.0 + F * (4.0 - 3.0 * cos2Alpha));
        const double previous = lambda;
        lambda = l + (1.0 - c) * F * sinAlpha
                * (sigma + c * sinSigma * (cos2SigmaM + c * cosSigma * (-1.0 + 2.0 * cos2SigmaM * cos2SigmaM)));
        if (std::abs(lambda - previous) < 1e-12) {
            const double uSq = cos2Alpha * (A * A - B * B) / (B * B);
            const double k1 = 1.0 + uSq / 16384.0 * (4096.0 + uSq * (-768.0 + uSq * (320.0 - 175.0 * uSq)));
            const double k2 = uSq / 1024.0 * (256.0 + uSq * (-128.0 + uSq * (74.0 - 47.0 * uSq)));
            const double deltaSigma = k2 * sinSigma * (cos2SigmaM + k2 / 4.0
                    * (cosSigma * (-1.0 + 2.0 * cos2SigmaM * cos2SigmaM)
                       - k2 / 6.0 * cos2SigmaM * (-3.0 + 4.0 * sinSigma * sinSigma)
                         * (-3.0 + 4.0 * cos2SigmaM * cos2SigmaM)));
            return B * k1 * (sigma - deltaSigma);
        }
    }
    // Nearly antipodal points, where the series does not converge: the sphere.
    const double dLat = (lat2 - lat1) * radians, dLon = (lon2 - lon1) * radians;
    const double h = std::sin(dLat / 2) * std::sin(dLat / 2)
            + std::cos(lat1 * radians) * std::cos(lat2 * radians) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2.0 * 6371008.8 * std::asin(std::min(1.0, std::sqrt(h)));
}
