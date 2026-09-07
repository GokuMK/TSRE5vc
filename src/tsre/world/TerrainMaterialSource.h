#pragma once
#include <QString>
#include <memory>
class TFile;

// Value snapshot of a picked shader pair: independent of the palette tile's lifetime.
struct TerrainMaterialSource {
    struct Shader {
        QString name;
        QString textures[2];
        int textureArgs[2][2]{};
        int uvArgs[2][4]{};
        int textureCount = 0;
        int uvCount = 0;
    } normal, auxiliary;
    static std::shared_ptr<const TerrainMaterialSource> capture(const TFile &file, int id);
    QString key() const;
    // Append only; never renumber existing bitmap IDs or static patch references.
    int appendTo(TFile &file, QString &error) const;
};
