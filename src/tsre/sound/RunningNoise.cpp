/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/sound/RunningNoise.h>

QString RunningNoiseNames::name(RunningNoise value) {
    switch (value) {
    case RunningNoise::Vehicle:
        return QStringLiteral("Vehicle");
    case RunningNoise::Both:
        return QStringLiteral("Both");
    case RunningNoise::Route:
        return QStringLiteral("Route");
    case RunningNoise::Regions:
        return QStringLiteral("Regions");
    case RunningNoise::Unset:
        break;
    }
    return QString();
}

RunningNoise RunningNoiseNames::parse(const QString &text) {
    const QString value = text.trimmed().toLower();
    if (value == QLatin1String("vehicle"))
        return RunningNoise::Vehicle;
    if (value == QLatin1String("both"))
        return RunningNoise::Both;
    if (value == QLatin1String("route"))
        return RunningNoise::Route;
    if (value == QLatin1String("regions"))
        return RunningNoise::Regions;
    return RunningNoise::Unset;
}
