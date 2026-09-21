/* TSRE5 — Copyright (C) 2016 Piotr Gadecki. GPL-3.0-or-later. */
#include "TFile.h"
#include "TerrainGridLayout.h"
#include <tsre/fileFunctions/ContentPath.h>
#include <QDataStream>
#include <QDebug>
#include <QSaveFile>
#include <QFile>
#include <QtEndian>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <functional>

namespace {
bool opaque(const TerrainFile::Extras &e) {
    if(!e.unknown.empty()||!e.tail.isEmpty())return true;
    for(const auto &child:e.children)if(opaque(child))return true;
    return false;
}
}
QString TFile::textureName(int id,int slot) const {
    return hasMaterial(id)&&slot>=0&&size_t(slot)<material(id).textures.size()
        ?material(id).textures[slot].filename:QString();
}
void TFile::setTextureName(int id,const QString &name,int slot) {
    if(!hasMaterial(id)||slot<0)return;
    for(auto *shader:{&material(id),&auxiliary(id)}) {
        if(shader->textures.size()<=size_t(slot))shader->textures.resize(slot+1);
        shader->textures[slot].filename=name;
    }
}
bool TFile::canRemapMaterials(QString &error) const {
    // Known TSRE extensions are excluded here; unknown extensions may contain
    // native shader indices. Do not guess how to renumber them.
    auto root=extras;
    auto samplesMeta=root.children.find(quint64(TS::terrain_samples)<<32);
    if(samplesMeta!=root.children.end()) {
        auto &unknown=samplesMeta->unknown;
        unknown.erase(std::remove_if(unknown.begin(),unknown.end(),[](const QByteArray &b) {
            if(b.size()<4)return false;
            const auto token=qFromLittleEndian<quint32>(b.constData());
            return token==TS::TSRETerrainMaterialBuffer||token==TS::TSRETerrainMaterialMap
                ||token==TS::TSRETerrainBakedMaterial||token==TS::TSRETerrainBakedMaterials;
        }),unknown.end());
    }
    bool unsafe=opaque(root);
    for(const auto &s:shaders)unsafe|=opaque(s.extras);
    for(const auto &s:patchSets)unsafe|=opaque(s.extras);
    if(unsafe||ambiguous){error="Unknown terrain data prevents safe shader renumbering";return false;}
    return true;
}
int TFile::appendMaterial(const Mat &primary,const Mat *other,QString &error) {
    if(paired&&!other){error="A paired terrain material requires its auxiliary shader";return -1;}
    if(paired&&!canRemapMaterials(error))return -1;
    const int id=materialCount();
    // Copies precede insertion: callers may pass records from this same vector.
    Mat a=primary,b=other?*other:Mat{};
    if(paired){shaders.insert(shaders.begin()+id,std::move(a));shaders.push_back(std::move(b));}
    else shaders.push_back(std::move(a));
    return id;
}
int TFile::newMat() {
    Mat normal,aux;
    normal.name=paired?"DetailTerrain":"TexDiff";
    normal.textures={{"terrain.ace",1,0}};normal.uvCalcs={{1,0,0,0}};
    if(paired){normal.textures.push_back({"microtex.ace",1,1});normal.uvCalcs.push_back({2,0,1,32});}
    aux.name="AlphaTerrain";aux.textures={{"terrain.ace",1,0}};aux.uvCalcs={{1,0,0,0}};
    QString error;const int id=appendMaterial(normal,&aux,error);
    if(id<0)qWarning()<<error;return id;
}
int TFile::cloneMat(int id) {
    if(!hasMaterial(id))return -1;
    QString error;const int result=appendMaterial(material(id),paired?&auxiliary(id):nullptr,error);
    if(result<0)qWarning()<<error;return result;
}
int TFile::getMatByTexture(QString name) {
    for(int i=0;i<materialCount();++i)for(const auto &slot:material(i).textures)
        if(slot.filename.compare(name,Qt::CaseInsensitive)==0)return i;
    return -1;
}
bool TFile::moveMaterialToFront(int id,QString &error) {
    if(!hasMaterial(id)||!canRemapMaterials(error))return false;
    const int count=materialCount();
    std::rotate(shaders.begin(),shaders.begin()+id,shaders.begin()+id+1);
    if(paired)std::rotate(shaders.begin()+count,shaders.begin()+count+id,shaders.begin()+count+id+1);
    for(auto &set:patchSets)for(auto &patch:set.patches) {
        if(patch.shaderIndex==quint32(id))patch.shaderIndex=0;
        else if(patch.shaderIndex<quint32(id))++patch.shaderIndex;
    }
    return true;
}
void TFile::removeMat(int id) {
    QString error;
    if(id<=0||!hasMaterial(id)||!sampleMaterialBuffer.isEmpty()||!canRemapMaterials(error))return;
    const int count=materialCount();
    for(auto &set:patchSets)for(auto &patch:set.patches) {
        if(patch.shaderIndex==quint32(id)) {
            patch.shaderIndex=0;
            const float span=float(samples.count.value_or(0))/set.patchesPerSide.value_or(1);
            if(span>0)patch.uv={0.001f,0.001f,0.998f/span,0,0,0.998f/span};
        } else if(patch.shaderIndex>quint32(id)&&patch.shaderIndex<quint32(count))--patch.shaderIndex;
    }
    if(paired)shaders.erase(shaders.begin()+count+id);
    shaders.erase(shaders.begin()+id);
}
void TFile::setBufferNames(QString name) {samples.e=name+"_e.raw";samples.n=name+"_n.raw";samples.y=name+"_y.raw";}
void TFile::initNew(QString name,int n,int spacing,int p) {
    TerrainGridLayout layout;QString error;
    if(!TerrainGridLayout::tryCreate(n,float(spacing),p,0,layout,error)){qWarning()<<error;loaded=false;return;}
    TFile next;next.paired=true;next.errorThresholdScale=1;next.alwaysSelectMaxDistance=0;
    next.samples.count=n;next.samples.rotation=0;next.samples.spacing=float(spacing);
    next.samples.floor=-63.0f;next.samples.scale=0.00195312f;next.water=TerrainFile::Water{};
    next.setBufferNames(name);next.newMat();next.patchSets.emplace_back();
    auto &set=next.patchSets.back();set.distance=0;set.patchesPerSide=p;set.patches.resize(p*p);
    for(int z=0;z<p;++z)for(int x=0;x<p;++x) {
        auto &patch=set.patches[z*p+x];const float size=layout.patchWorldSize;
        patch.centerX=(x+0.5f)*size;patch.centerZ=-(z+0.5f)*size;patch.averageY=1;
        patch.radiusM=size/2;patch.sphereRadius=99.48125458f*size/128;
        patch.uv={0.0001f,0.0001f,layout.defaultPatchTextureScale(),0,0,layout.defaultPatchTextureScale()};
    }
    next.loaded=true;*this=std::move(next);
}
bool TFile::adopt(TerrainFile::Data parsed,QString &error) {
    TFile next;static_cast<TerrainFile::Data&>(next)=std::move(parsed);
    next.repairAuxiliaryReferences();next.readExtensions();next.loaded=true;
    next.patchFlagsResources.resize(next.patchSets.size());
    for(size_t i=0;i<next.patchSets.size();++i)if(next.patchSets[i].flagsBuffer)
        for(const auto &patch:next.patchSets[i].patches)next.patchFlagsResources[i].inlineFlags.push_back(patch.flags);
    *this=std::move(next);error.clear();return true;
}
bool TFile::readT(QString path) {
    TerrainFile::Data parsed;QString error;
    if(!parsed.readFile(path,error)){qWarning()<<path<<error;return false;}
    return adopt(std::move(parsed),error);
}
bool TFile::load(FileBuffer *input) {
    TerrainFile::Data parsed;QString error;
    if(!input||!parsed.read(*input,error)){qWarning()<<"Invalid terrain descriptor"<<error;return false;}
    return adopt(std::move(parsed),error);
}
bool TFile::validateRuntime(QString &error) const {
    if(!samples.count||!samples.spacing||!samples.floor||!samples.scale||!activeSet()) {
        error="Terrain descriptor lacks required sample/patch metadata";return false;
    }
    if(!std::isfinite(*samples.floor)||!std::isfinite(*samples.scale)||*samples.scale<=0) {
        error="Invalid terrain height scale/floor";return false;
    }
    return true;
}
bool TFile::preflight(QString &error) const {
    TerrainFile::Data prepared;if(!prepare(prepared,error))return false;
    for(const auto &resource:patchFlagsResources)if(resource.valid) {
        QFile file(resource.path);
        if(!file.open(QIODevice::ReadOnly)||file.readAll()!=resource.original) {
            error="Patch flag sidecar changed externally: "+resource.path;return false;
        }
    }
    return !prepared.encode(error).isEmpty();
}
void TFile::save(QDataStream &stream) {
    TerrainFile::Data prepared;QString error;
    const auto bytes=prepare(prepared,error)?prepared.encode(error):QByteArray();
    if(bytes.isEmpty()||stream.writeRawData(bytes.constData(),bytes.size())!=bytes.size()) {
        qWarning()<<"Cannot serialize terrain"<<error;stream.setStatus(QDataStream::WriteFailed);
    }
}
bool TFile::readLayoutInfo(const QString &path,LayoutInfo &info) {
    QString error;QByteArray bytes;info={};
    if(!TerrainFile::readDescriptorBytes(path,bytes,error))return false;
    auto memory=new unsigned char[bytes.size()];std::memcpy(memory,bytes.constData(),bytes.size());
    FileBuffer in(memory,bytes.size());LayoutInfo found;
    try {
        in.require(32);
        if(!bytes.startsWith("SIMISA")||bytes.mid(16,6)!="JINX0t"||bytes[23]!='b')return false;
        in.off=32;const auto root=in.readBlock();if(root.id!=TS::terrain)return false;
        std::function<void(const FileBuffer::Block&,int)> visit;
        visit=[&](const FileBuffer::Block &parent,int depth) {
            if(depth>5)throw FileBuffer::ParseError("Terrain layout nesting limit");
            FileBuffer::ScopedLimit limit(in,parent.end);in.off=parent.payload;
            if(parent.id==TS::terrain_patchsets)in.getUint();
            if(parent.id==TS::terrain_patchset)found.patches=0; // Last set is active.
            while(in.off<in.readEnd()) {
                const auto b=in.readBlock();in.off=b.payload;
                {FileBuffer::ScopedLimit child(in,b.end);
                    if(parent.id==TS::terrain_samples&&b.id==TS::terrain_nsamples)found.samples=int(in.getUint());
                    else if(parent.id==TS::terrain_samples&&b.id==TS::terrain_sample_size)found.spacing=in.getFloat();
                    else if(parent.id==TS::terrain_patchset&&b.id==TS::terrain_patchset_npatches)found.patches=int(in.getUint());
                    else if((parent.id==TS::terrain&&(b.id==TS::terrain_samples||b.id==TS::terrain_patches))
                        ||(parent.id==TS::terrain_patches&&b.id==TS::terrain_patchsets)
                        ||(parent.id==TS::terrain_patchsets&&b.id==TS::terrain_patchset))visit(b,depth+1);
                }
                in.off=b.end;
            }
        };
        visit(root,0);
        if(found.samples<=0||found.patches<=0||!std::isfinite(found.spacing)||found.spacing<=0)return false;
        info=found;return true;
    }catch(const FileBuffer::ParseError &){return false;}
}
