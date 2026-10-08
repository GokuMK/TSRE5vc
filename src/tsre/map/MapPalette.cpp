/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "MapPalette.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>

MapPalette MapPalette::light() {
    return MapPalette();
}

MapPalette MapPalette::dark() {
    MapPalette palette;
    palette.name = "dark";
    palette.background = QColor(26, 28, 33);
    palette.track = QColor(220, 222, 228);
    palette.road = QColor(196, 156, 98);
    palette.junction = QColor(255, 96, 96);
    palette.end = QColor(110, 156, 255);
    palette.pointer = QColor(255, 160, 40);
    palette.itemBorder = QColor(235, 236, 240);
    palette.path = QColor(40, 110, 60);
    palette.pathNode = QColor(60, 200, 100);
    palette.wagon = QColor(110, 150, 235);
    palette.engine = QColor(60, 90, 200);
    palette.terrainBorder = QColor(70, 73, 82);
    palette.quadBorder = QColor(50, 53, 61);
    palette.label = QColor(236, 237, 242);
    palette.labelHalo = QColor(20, 22, 26);
    palette.marker = QColor(255, 96, 192);
    return palette;
}

bool MapPalette::fromJson(const QByteArray &json, MapPalette &palette, QString *error) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isObject()) {
        if (error != nullptr)
            *error = parseError.errorString();
        return false;
    }
    const QJsonObject object = document.object();
    auto read = [&object](const char *key, QColor &colour) {
        const QColor value(object.value(key).toString());
        if (value.isValid())
            colour = value;
    };
    read("background", palette.background);
    read("track", palette.track);
    read("road", palette.road);
    read("junction", palette.junction);
    read("end", palette.end);
    read("pointer", palette.pointer);
    read("signal", palette.signal);
    read("speedPost", palette.speedPost);
    read("platform", palette.platform);
    read("siding", palette.siding);
    read("carSpawner", palette.carSpawner);
    read("levelCrossing", palette.levelCrossing);
    read("hazard", palette.hazard);
    read("pickup", palette.pickup);
    read("soundRegion", palette.soundRegion);
    read("itemBorder", palette.itemBorder);
    read("path", palette.path);
    read("pathNode", palette.pathNode);
    read("wagon", palette.wagon);
    read("engine", palette.engine);
    read("speedZone", palette.speedZone);
    read("failedSignal", palette.failedSignal);
    read("event", palette.event);
    read("selection", palette.selection);
    read("label", palette.label);
    read("labelHalo", palette.labelHalo);
    read("marker", palette.marker);
    read("terrainBorder", palette.terrainBorder);
    read("quadBorder", palette.quadBorder);
    read("missingTile", palette.missingTile);
    if (object.value("osmAreaAlpha").isDouble())
        palette.osmAreaAlpha = float(std::clamp(object.value("osmAreaAlpha").toDouble(), 0.0, 1.0));
    for (const char *key : {"terrainFade", "overlayFade"})
        if (object.value(key).isDouble())
            palette.overlayFade = float(std::clamp(object.value(key).toDouble(), 0.0, 1.0));
    return true;
}

QString MapPalette::directory() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
            .filePath("map-palettes");
}

MapPalette MapPalette::named(const QString &name) {
    if (name.compare("dark", Qt::CaseInsensitive) == 0)
        return dark();
    if (name.isEmpty() || name.compare("light", Qt::CaseInsensitive) == 0)
        return light();
    QFile file(QDir(directory()).filePath(name + ".json"));
    MapPalette palette;
    if (file.open(QIODevice::ReadOnly) && fromJson(file.readAll(), palette))
        palette.name = name;
    return palette;
}

QStringList MapPalette::available() {
    QStringList names = {"light", "dark"};
    const QStringList files = QDir(directory()).entryList({"*.json"}, QDir::Files, QDir::Name);
    for (const QString &file : files)
        names.append(file.chopped(5));
    return names;
}
