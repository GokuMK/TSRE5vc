#pragma once
#include <QString>
#include <QMap>
#include <memory>

// Route-wide procedural source. Independent of the MSTS TFile::Mat record.
struct TerrainMaterialDefinition {
    quint32 uid = 0;
    QString displayName;
    QString texture; // Relative to the route's TERRTEX directory.
};

class TerrainMaterialLibrary {
public:
    static constexpr const char *FileName = "terrainmaterials.dat";
    explicit TerrainMaterialLibrary(QString routeDirectory);
    static std::shared_ptr<TerrainMaterialLibrary> current();
    bool reload();
    void poll(); // Throttled, UI thread only; never called by generation workers.
    bool save(QString &error);
    quint32 addImage(const QString &source, QString &error);
    bool rename(quint32 uid, const QString &name, QString &error);
    const QMap<quint32,TerrainMaterialDefinition> &materials() const { return definitions; }
    const TerrainMaterialDefinition *find(quint32 uid) const;
    QString textureDirectory() const;
    QString path() const;
    QString error() const { return loadError; }
    quint64 revision() const { return generation; }
    static bool validTextureName(const QString &name);
private:
    QString directory, loadError;
    QMap<quint32,TerrainMaterialDefinition> definitions;
    quint64 nextUid = 1, generation = 0;
    qint64 stamp = -1, fileSize = -1, nextPoll = 0;
    bool loaded = false;
};
