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
#include <QFileInfo>

namespace NightTextures {

namespace {

NightTextures::Frame current;

bool present(const QString &file) {
    return QFileInfo(ContentPath::textureSource(file)).isFile();
}

}

QString find(const QString &textureFolder, const QString &name) {
    if (name.isEmpty())
        return {};
    for (const QString &folder : {ContentPath::normalize(textureFolder), ContentPath::parentDirectory(textureFolder)}) {
        // A season directory: NIGHT, uppercase by TSRE's file name rules.
        const QString night = ContentPath::join(ContentPath::join(folder, "NIGHT"), name);
        if (present(night))
            return night;
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
