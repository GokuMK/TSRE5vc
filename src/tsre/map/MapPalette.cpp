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
