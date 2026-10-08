#pragma once
#include <QStringList>

// Shared terrain/transfer texture policy, and the season directory of MSTS
// shapes' alternative textures.
namespace TerrainSeason {
// Translate only TRK Clear names for consumers using legacy seasonal flags.
QString legacySeason(const QString &season);
// Directory of a shape's seasonal textures under its texture path ("/SPRING",
// "/SNOW" ...; empty for the main one) for its .sd ESD_Alternative_Texture
// flags. Clear seasons are their season (SpringClear: Spring, SummerClear:
// the main directory); Winter and the snow seasons use SNOW for shapes with
// the Snow or SnowTrack flag.
QString shapeTextureDirectory(int alternativeFlags, const QString &season);
QString canonical(const QString &season);
QStringList variants();
QString directory(const QString &root, const QString &variant);
QStringList available(const QString &root);
QString resolve(const QString &root, const QString &variant, const QString &file,
                bool warn = true, bool ignoreSeasons = false);
}
