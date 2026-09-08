#include "TerrainMaterialSource.h"
#include "TFile.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QSet>
#include <algorithm>

namespace {
bool copyShader(const TFile::Mat &from, TerrainMaterialSource::Shader &to) {
    if (!from.name || from.count153 < 1 || from.count153 > 2 || from.count155 < 0 || from.count155 > 2) return false;
    to.name=*from.name; to.textureCount=from.count153; to.uvCount=from.count155;
    for (int i=0;i<to.textureCount;++i) {
        if (!from.tex[i] || from.tex[i]->isEmpty()) return false;
        to.textures[i]=*from.tex[i];
        for (int j=0;j<2;++j) to.textureArgs[i][j]=from.atex[i][j];
    }
    for (int i=0;i<to.uvCount;++i) for (int j=0;j<4;++j) to.uvArgs[i][j]=from.itex[i][j];
    return true;
}
TFile::Mat ownedShader(const TerrainMaterialSource::Shader &from) {
    TFile::Mat to;
    to.name=new QString(from.name); to.count153=from.textureCount; to.count155=from.uvCount;
    for (int i=0;i<from.textureCount;++i) {
        to.tex[i]=new QString(from.textures[i]);
        for (int j=0;j<2;++j) to.atex[i][j]=from.textureArgs[i][j];
    }
    for (int i=0;i<from.uvCount;++i) for (int j=0;j<4;++j) to.itex[i][j]=from.uvArgs[i][j];
    return to;
}
}
std::shared_ptr<const TerrainMaterialSource> TerrainMaterialSource::capture(const TFile &file, int id) {
    auto normal=file.materials.find(id), auxiliary=file.amaterials.find(id);
    if (id<0 || id>=file.materialsCount || normal==file.materials.end() || auxiliary==file.amaterials.end()) return {};
    auto source=std::make_shared<TerrainMaterialSource>();
    if (!copyShader(normal->second,source->normal) || !copyShader(auxiliary->second,source->auxiliary)) return {};
    return source;
}
QString TerrainMaterialSource::key() const {
    QByteArray bytes;
    QDataStream out(&bytes,QIODevice::WriteOnly);
    for (const auto *shader : {&normal,&auxiliary}) {
        out << shader->name << shader->textureCount << shader->uvCount;
        for (int i=0;i<shader->textureCount;++i) {
            out << shader->textures[i].toLower().replace('\\','/');
            for (int a : shader->textureArgs[i]) out << a;
        }
        for (int i=0;i<shader->uvCount;++i) for (int a : shader->uvArgs[i]) out << a;
    }
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
int TerrainMaterialSource::appendTo(TFile &file, QString &error) const {
    const int id=file.materialsCount;
    if (id<0 || id>=256) { error="Procedural shader palette is full (256 entries). No existing shader was replaced."; return -1; }
    if (file.materials.count(id) || file.amaterials.count(id)) { error="Cannot append to an inconsistent terrain shader table"; return -1; }
    file.materials.emplace(id,ownedShader(normal));
    file.amaterials.emplace(id,ownedShader(auxiliary));
    ++file.materialsCount;
    return id;
}
void TerrainMaterialSource::restorePalette(TFile &file,
        const QVector<std::shared_ptr<const TerrainMaterialSource>> &palette) {
    // Legacy shader records can share QString pointers after palette moves.
    QSet<QString*> strings;
    for (auto *table : {&file.materials, &file.amaterials}) {
        for (const auto &entry : *table) {
            strings.insert(entry.second.name);
            for (int i=0; i<std::min(2,entry.second.count153); ++i)
                strings.insert(entry.second.tex[i]);
        }
        table->clear();
    }
    for (auto *text : strings) delete text;
    file.materialsCount = palette.size();
    for (int i=0; i<palette.size(); ++i) {
        file.materials.emplace(i,ownedShader(palette[i]->normal));
        file.amaterials.emplace(i,ownedShader(palette[i]->auxiliary));
    }
}
