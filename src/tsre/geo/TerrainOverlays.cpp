/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "TerrainOverlays.h"
#include <QDir>
#include <QFile>
#include <QObject>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <settings/SettingsAccess.h>
#include <tsre/Game.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/geo/MapDataOSM.h>
#include <tsre/texture/TexLib.h>

namespace TerrainOverlays {

namespace {

std::unordered_map<int, QImage> &images() {
    static std::unordered_map<int, QImage> store;
    return store;
}

float overlayOpacity = 1.0f;

// One OSM drawing at a time: the web path answers later.
struct OsmJob {
    std::unique_ptr<MapDataOSM> data;
    std::unique_ptr<QObject> context;  // the running job's connections
};

OsmJob &osm() {
    static OsmJob job;
    return job;
}

}

int key(int x, int z) {
    return x * 10000 + z;
}

const QImage *image(int k) {
    const auto found = images().find(k);
    return found != images().end() && !found->second.isNull() ? &found->second : nullptr;
}

bool has(int x, int z) {
    return image(key(x, z)) != nullptr;
}

void set(int x, int z, const QImage &picture) {
    const int k = key(x, z);
    images()[k] = picture.format() == QImage::Format_RGB888 ? picture
                                                            : picture.convertToFormat(QImage::Format_RGB888);
    TexLib::reloadTexIfPresent(QString::number(k) + QStringLiteral(".:maptex"));
}

QString diskPath(int x, int z) {
    return ContentPath::normalize(Game::root + "/ROUTES/" + Game::route + "/TERRAIN_MAPS/")
            + QString::number(key(x, z)) + QStringLiteral(".png");
}

bool loadFromDisk(int x, int z) {
    const QString path = diskPath(x, z);
    if (!QFile::exists(path))
        return false;
    const QImage picture(path);
    if (picture.isNull())
        return false;
    set(x, z, picture);
    return true;
}

bool saveToDisk(int x, int z, QString &error) {
    const QImage *picture = image(key(x, z));
    if (picture == nullptr) {
        //% "This tile has no overlay to save."
        error = qtTrId("route.editor.overlay.error.none");
        return false;
    }
    const QString path = diskPath(x, z);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !picture->save(path, "PNG")) {
        //% "Cannot write %1"
        error = qtTrId("route.editor.overlay.error.write").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

bool osmBusy() {
    return osm().context != nullptr;
}

bool createFromOsm(int x, int z, int tileSize, std::function<void(bool, const QString &)> done, QString &error) {
    OsmJob &job = osm();
    if (job.context != nullptr) {
        //% "OSM data for another tile is still being downloaded."
        error = qtTrId("route.editor.overlay.error.osm.busy");
        return false;
    }
    if (Game::GeoCoordConverter == nullptr) {
        //% "This route has no geographic reference, so OSM data cannot be placed on it."
        error = qtTrId("route.editor.overlay.error.osm.reference");
        return false;
    }
    // The tile's latitude and longitude box, from its corners (z counts the other way).
    double minLat = 999, minLon = 999, maxLat = -999, maxLon = -999;
    for (int corner = 0; corner < 4; ++corner) {
        PreciseTileCoordinate tile;
        tile.setTWxyzU(x, -z, (corner & 1) ? tileSize : 0, 0, (corner & 2) ? tileSize : 0);
        LatitudeLongitudeCoordinate latLon;
        IghCoordinate igh;
        Game::GeoCoordConverter->ConvertToInternal(&tile, &igh);
        Game::GeoCoordConverter->ConvertToLatLon(&igh, &latLon);
        minLat = std::min(minLat, latLon.Latitude);
        maxLat = std::max(maxLat, latLon.Latitude);
        minLon = std::min(minLon, latLon.Longitude);
        maxLon = std::max(maxLon, latLon.Longitude);
    }
    if (job.data == nullptr)
        job.data = std::make_unique<MapDataOSM>();
    MapDataOSM &data = *job.data;
    data.tileX = x;
    data.tileZ = -z;
    data.level = tileSize / 2048.0;
    data.tileSize = tileSize;
    data.minlon = minLon;
    data.minlat = minLat;
    data.maxlon = maxLon;
    data.maxlat = maxLat;
    job.context = std::make_unique<QObject>();
    auto finish = [x, z, done](bool ok, const QString &message) {
        OsmJob &running = osm();
        // Deleted later: its connection is still being called.
        running.context.release()->deleteLater();
        if (ok) {
            const int resolution = Settings::integer("core.maps.imageResolution");
            QImage picture(resolution, resolution, QImage::Format_RGB32);
            if (!running.data->draw(&picture)) {
                //% "No OSM data was found for this tile."
                done(false, qtTrId("route.editor.overlay.error.osm.empty"));
                return;
            }
            set(x, z, picture);
        }
        done(ok, message);
    };
    QObject::connect(&data, &MapDataOSM::loaded, job.context.get(), [finish] { finish(true, QString()); });
    QObject::connect(&data, &MapDataOSM::failed, job.context.get(),
                     [finish](const QString &message) { finish(false, message); });
    data.load();
    return true;
}

float opacity() {
    return overlayOpacity;
}

void setOpacity(float value) {
    overlayOpacity = std::clamp(value, 0.0f, 1.0f);
}

}
