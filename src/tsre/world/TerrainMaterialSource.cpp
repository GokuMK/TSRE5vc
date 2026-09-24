#include "TerrainMaterialSource.h"
#include "TFile.h"
#include <QCryptographicHash>

std::shared_ptr<const TerrainMaterialSource> TerrainMaterialSource::capture(const TFile &file,int id) {
    if(!file.hasMaterial(id)||file.material(id).textures.empty())return {};
    auto source=std::make_shared<TerrainMaterialSource>();
    source->normal=file.material(id);source->paired=file.paired;
    if(file.paired)source->auxiliary=file.auxiliary(id);
    return source;
}
QString TerrainMaterialSource::key() const {
    TerrainFile::Data data;data.shaders.push_back(normal);
    if(paired)data.shaders.push_back(auxiliary);
    for(auto &shader:data.shaders)for(auto &slot:shader.textures)
        slot.filename=slot.filename.toLower().replace('\\','/');
    QString error;const auto bytes=data.encode(error);
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
int TerrainMaterialSource::appendTo(TFile &file,QString &error) const {
    if(file.materialCount()>=256){error="Procedural shader palette is full (256 entries)";return -1;}
    if(file.paired!=paired){error="Cannot import a flat shader as a paired material or vice versa";return -1;}
    return file.appendMaterial(normal,paired?&auxiliary:nullptr,error);
}
void TerrainMaterialSource::restorePalette(TFile &file,
    const QVector<std::shared_ptr<const TerrainMaterialSource>> &palette) {
    std::vector<TerrainFile::Shader> shaders;shaders.reserve(palette.size()*(file.paired?2:1));
    for(const auto &source:palette)shaders.push_back(source->normal);
    if(file.paired)for(const auto &source:palette)shaders.push_back(source->auxiliary);
    file.shaders=std::move(shaders);
}
