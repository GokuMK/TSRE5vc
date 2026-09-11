#pragma once
#include <QStringList>

// Procedural-only policy; static terrain/shape alternatives remain unchanged.
namespace TerrainSeason {
QString canonical(const QString &season);
QStringList variants();
QString directory(const QString &root, const QString &variant);
QStringList available(const QString &root);
QString resolve(const QString &root, const QString &variant, const QString &file,
                bool warn = true, bool ignoreSeasons = false);
}
