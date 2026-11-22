/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/ShapeAxes.h>
#include <QString>
#include <QStringView>
#include <QtGlobal>
#include <algorithm>

namespace ShapeAxes {

Axes fromName(const QString &name) {
    const QString value = name.trimmed();
    if (value.isEmpty() || value.compare(QLatin1String("MSTS"), Qt::CaseInsensitive) == 0)
        return Axes::Msts;
    if (value.compare(QLatin1String("glTF"), Qt::CaseInsensitive) == 0)
        return Axes::Gltf;
    return Axes::Unknown;
}

Axes ofModel(const QString &modelPath) {
    // Its extension alone (no file system; a display's square asks a frame).
    const qsizetype dot = modelPath.lastIndexOf(QLatin1Char('.'));
    const qsizetype folder = std::max(modelPath.lastIndexOf(QLatin1Char('/')), modelPath.lastIndexOf(QLatin1Char('\\')));
    if (dot <= folder)
        return Axes::Gltf;
    return QStringView(modelPath).mid(dot + 1).compare(QLatin1String("s"), Qt::CaseInsensitive) == 0 ? Axes::Msts
                                                                                                   : Axes::Gltf;
}

bool mirrors(Axes written, Axes model) {
    const auto of = [](Axes a) { return a == Axes::Gltf ? Axes::Gltf : Axes::Msts; };
    return of(written) != of(model);
}

QString unknown(const QString &name) {
    //% "its .sd's ESD_TSREAxes ( %1 ) is neither MSTS nor glTF; its positions are read as MSTS"
    return qtTrId("shapeaxes.unknown").arg(name.trimmed());
}

}
