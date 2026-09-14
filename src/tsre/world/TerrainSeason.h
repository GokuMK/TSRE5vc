#pragma once
#include <QStringList>

// Shared terrain/transfer texture policy. Shape alternative flags remain separate.
namespace TerrainSeason {
// Translate only TRK Clear names for consumers using legacy seasonal flags.
QString legacySeason(const QString &season);
QString canonical(const QString &season);
QStringList variants();
QString directory(const QString &root, const QString &variant);
QStringList available(const QString &root);
QString resolve(const QString &root, const QString &variant, const QString &file,
                bool warn = true, bool ignoreSeasons = false);
}
