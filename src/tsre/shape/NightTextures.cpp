/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/NightTextures.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>

namespace NightTextures {

namespace {

NightTextures::Frame current;

// A folder's Night folder in its own spelling, empty for none.
QString nightFolder(const QString &folder) {
    static QMutex lock;
    static QHash<QString, QString> found;
    const QString key = ContentPath::key(folder);
    QMutexLocker locked(&lock);
    const auto it = found.constFind(key);
    if (it != found.constEnd())
        return it.value();
    QString night;
    const QDir dir(ContentPath::normalize(folder));
    for (const QString &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (entry.compare("night", Qt::CaseInsensitive) == 0) {
            night = dir.filePath(entry);
            break;
        }
    found.insert(key, night);
    return night;
}

bool present(const QString &file) {
    return QFileInfo(ContentPath::textureSource(file)).isFile();
}

}

QString find(const QString &textureFolder, const QString &name) {
    if (name.isEmpty())
        return {};
    for (const QString &folder : {ContentPath::normalize(textureFolder), ContentPath::parentDirectory(textureFolder)}) {
        const QString night = nightFolder(folder);
        if (!night.isEmpty() && present(ContentPath::join(night, name)))
            return ContentPath::join(night, name);
    }
    return {};
}

void setFrame(const Frame &frame) {
    current = frame;
}

const Frame &frame() {
    return current;
}

bool wantedForSun(double elevationDegrees, bool wanted) {
    return wanted ? elevationDegrees < ReleaseElevation : elevationDegrees < LoadElevation;
}

}
