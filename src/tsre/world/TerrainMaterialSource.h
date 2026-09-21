#pragma once
#include <QString>
#include <memory>
#include <QVector>
#include "TerrainFileData.h"
class TFile;

// Value snapshot of a picked shader pair: independent of the palette tile's lifetime.
struct TerrainMaterialSource {
    using Shader=TerrainFile::Shader;
    Shader normal,auxiliary;
    bool paired=true;
    static std::shared_ptr<const TerrainMaterialSource> capture(const TFile &file, int id);
    QString key() const;
    // Append only; never renumber existing bitmap IDs or static patch references.
    int appendTo(TFile &file, QString &error) const;
    static void restorePalette(TFile &file,
            const QVector<std::shared_ptr<const TerrainMaterialSource>> &palette);
};
