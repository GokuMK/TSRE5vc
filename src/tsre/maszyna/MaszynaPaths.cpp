/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/maszyna/MaszynaPaths.h>
#include <tsre/Game.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

const char *const TextureExtensions[] = {".dds", ".tga", ".ktx", ".png", ".bmp", ".jpg", ".tex"};

bool holdsVehicle(const QString &folder) {
    return !folder.isEmpty() && !QDir(folder).entryList(QStringList() << "*.mmd", QDir::Files).isEmpty();
}

}

namespace MaszynaPaths {

Name clean(const QString &name) {
    Name out;
    QString text = name.toLower();
    text.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const qsizetype traits = text.lastIndexOf(QLatin1Char(':'));
    if (traits >= 0) {
        const QString letters = text.mid(traits + 1);
        out.clampS = letters.contains(QLatin1Char('s'));
        out.clampT = letters.contains(QLatin1Char('t'));
        text.truncate(traits);
    }
    while (text.startsWith(QLatin1Char('/')))
        text.remove(0, 1);
    if (text.endsWith(QLatin1String(".mat")))
        text.chop(4);
    else
        for (const char *extension : TextureExtensions)
            if (text.endsWith(QLatin1String(extension))) {
                text.chop(4);
                break;
            }
    out.text = text;
    return out;
}

QStringList places(const QString &name, const QString &vehicleFolder) {
    QStringList out;
    if (!vehicleFolder.isEmpty())
        out << QDir::cleanPath(vehicleFolder + QLatin1Char('/') + name);
    const QString root = QDir::cleanPath(Game::root);
    if (root.isEmpty())
        return out;
    if (name.startsWith(QLatin1String("dynamic/")))
        out << root + QLatin1String("/TRAINS/TRAINSET/") + name.mid(8);
    else if (name.startsWith(QLatin1String("textures/")))
        out << root + QLatin1String("/TEXTURES/MASZYNA/") + name.mid(9);
    else
        out << root + QLatin1String("/TEXTURES/MASZYNA/") + name;
    return out;
}

QString expectedPlace(const QString &name, const QString &vehicleFolder) {
    const QStringList tried = places(name, vehicleFolder);
    const QString first = name.section(QLatin1Char('/'), 0, 0);
    const bool own = !name.contains(QLatin1Char('/')) || QDir(vehicleFolder).exists(first);
    return own || tried.size() < 2 ? tried.first() : tried.last();
}

QString findMaterial(const QStringList &places) {
    for (const QString &place : places) {
        const QString file = place + QLatin1String(".mat");
        if (QFile::exists(file))
            return file;
    }
    return QString();
}

QString findTexture(const QStringList &places) {
    for (const QString &place : places)
        for (const char *extension : TextureExtensions) {
            const QString file = place + QLatin1String(extension);
            if (QFile::exists(file))
                return file;
        }
    return QString();
}

QString vehicleFolder(const QString &modelPath, const QString &texPath) {
    if (holdsVehicle(texPath))
        return QDir::cleanPath(texPath);
    QDir folder = QFileInfo(modelPath).dir();
    const QString own = folder.path();
    for (int up = 0; up <= 2; ++up) {
        if (holdsVehicle(folder.path()))
            return folder.path();
        if (!folder.cdUp())
            break;
    }
    return own;
}

}
