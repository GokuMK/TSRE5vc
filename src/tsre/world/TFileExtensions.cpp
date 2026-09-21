#include "TFile.h"
#include <QDataStream>
#include <QIODevice>
#include <QtEndian>
#include <QSet>
#include <cstring>
#include <algorithm>

namespace {
constexpr TS::TokenId tokens[]={TS::TSRETerrainMaterialBuffer,TS::TSRETerrainMaterialMap,
    TS::TSRETerrainBakedMaterial,TS::TSRETerrainBakedMaterials};
bool known(TS::TokenId token){return std::find(std::begin(tokens),std::end(tokens),token)!=std::end(tokens);}
QByteArray textPayload(const QString &value) {
    if(value.isEmpty())return {};
    QByteArray bytes;QDataStream out(&bytes,QIODevice::WriteOnly);out.setByteOrder(QDataStream::LittleEndian);
    out<<quint8(0)<<quint16(value.size());for(auto c:value)out<<c.unicode();return bytes;
}
QByteArray framed(TS::TokenId token,const QByteArray &payload) {
    QByteArray bytes;QDataStream out(&bytes,QIODevice::WriteOnly);out.setByteOrder(QDataStream::LittleEndian);
    out<<quint32(token)<<quint32(payload.size());out.writeRawData(payload.constData(),payload.size());return bytes;
}
}
QByteArray TFile::extensionPayload(TS::TokenId token) const {
    if(token==TS::TSRETerrainMaterialBuffer)return textPayload(sampleMaterialBuffer);
    const bool legacy=extensionBaseline.contains(TS::TSRETerrainBakedMaterial)
        &&!extensionBaseline.contains(TS::TSRETerrainBakedMaterials)&&seasonalBakes.isEmpty();
    if(token==TS::TSRETerrainBakedMaterial)return legacy?textPayload(bakedMaterialInfo):QByteArray();
    if(token==TS::TSRETerrainBakedMaterials)return legacy?QByteArray():bakeMetadata();
    QByteArray bytes;
    if(token==TS::TSRETerrainMaterialMap&&materialUidMapPresent) {
        QDataStream out(&bytes,QIODevice::WriteOnly);out.setByteOrder(QDataStream::LittleEndian);
        out<<quint8(0)<<quint32(materialUids.size());
        for(auto it=materialUids.cbegin();it!=materialUids.cend();++it)out<<quint32(it.key())<<it.value();
    }
    return bytes;
}
void TFile::readExtensions() {
    const auto &meta=extras.children.value(quint64(TS::terrain_samples)<<32);
    QSet<TS::TokenId> seen;
    for(const auto &raw:meta.unknown) {
        if(raw.size()<9)continue;
        const auto token=qFromLittleEndian<quint32>(raw.constData());if(!known(token))continue;
        if(seen.contains(token)) {
            ambiguous=true;diagnostics<<"Duplicate procedural terrain extension";continue;
        }
        seen.insert(token);
        if(token==TS::TSRETerrainBakedMaterials){readBakeMetadata(raw.mid(8));continue;}
        auto memory=new unsigned char[raw.size()];std::memcpy(memory,raw.constData(),raw.size());
        FileBuffer in(memory,raw.size());
        if(token==TS::TSRETerrainMaterialMap){materialUidMapPresent=true;materialUidMapValid=false;}
        QString *text=token==TS::TSRETerrainMaterialBuffer?&sampleMaterialBuffer:&bakedMaterialInfo;
        try {
            const auto block=in.readBlock();FileBuffer::ScopedLimit limit(in,block.end);in.off=block.payload;
            if(token==TS::TSRETerrainMaterialMap) {
                const auto count=in.getUint();
                if(count>256||quint64(count)*8!=quint64(in.readEnd()-in.off))continue;
                bool valid=true;
                for(quint32 i=0;i<count;++i){const auto id=in.getUint(),uid=in.getUint();
                    if(id>255||!uid||materialUids.contains(int(id))){valid=false;break;}
                    materialUids.insert(int(id),uid);
                }
                materialUidMapValid=valid;
            } else {
                *text=":invalid procedural material reference:";
                const auto value=in.readString();if(!value.isEmpty())*text=value;
            }
        } catch(const FileBuffer::ParseError &) {
            if(token!=TS::TSRETerrainMaterialMap)*text=":invalid procedural material reference:";
        }
    }
    // Presence must be known before constructing the legacy/plural baseline.
    for(auto token:seen)extensionBaseline.insert(token,{});
    for(auto token:seen)extensionBaseline[token]=extensionPayload(token);
}
bool TFile::prepare(TerrainFile::Data &out,QString &error) const {
    if(sampleMaterialBuffer.size()>65535 || bakedMaterialInfo.size()>65535) {
        error="Terrain extension string exceeds binary format limit";return false;
    }
    for(auto it=seasonalBakes.cbegin();it!=seasonalBakes.cend();++it)
        if(it.key().size()>4096 || it->settings.size()>4096 || it->sources.size()>4096 || it->validation.size()>4096) {
            error="Terrain bake metadata string exceeds supported limit";return false;
        }
    if(ambiguous){error="Ambiguous terrain metadata cannot be rewritten";return false;}
    if(!bakedMaterialsValid||(materialUidMapPresent&&!materialUidMapValid)
        ||sampleMaterialBuffer.startsWith(":invalid")||bakedMaterialInfo.startsWith(":invalid")) {
        error="Invalid procedural terrain metadata must be repaired before saving";return false;
    }
    if(materialUidMapPresent)for(auto it=materialUids.cbegin();it!=materialUids.cend();++it)
        if(it.key()<0||it.key()>255||!it.value()){error="Invalid material UiD mapping";return false;}
    for(size_t i=0;i<patchSets.size();++i)if(patchSets[i].flagsBuffer) {
        if(i>=patchFlagsResources.size()||patchFlagsResources[i].inlineFlags.size()!=patchSets[i].patches.size()) {
            error="Patch flag sidecar layout was changed without regeneration";return false;
        }
        const auto &resource=patchFlagsResources[i];
        if(!resource.valid)for(size_t p=0;p<resource.inlineFlags.size();++p)
            if(resource.inlineFlags[p]!=patchSets[i].patches[p].flags) {
                error="Cannot change flags with a missing patch flag sidecar";return false;
            }
    }
    for(const auto &set:patchSets)for(const auto &p:set.patches)
        if(paired&&p.shaderIndex>=quint32(materialCount())&&p.shaderIndex<shaders.size()) {
            error="Direct auxiliary terrain shader assignment is not supported";return false;
        }
    out=static_cast<const TerrainFile::Data&>(*this);
    for(size_t i=0;i<patchFlagsResources.size();++i) {
        const auto &resource=patchFlagsResources[i];if(!resource.valid)continue;
        for(size_t p=0;p<out.patchSets[i].patches.size();++p) {
            auto &flags=out.patchSets[i].patches[p].flags;
            const quint32 changed=flags^quint8(resource.original[int(p)]);
            flags=(resource.inlineFlags[p]&~changed)|(flags&changed);
        }
    }
    const quint64 sampleKey=quint64(TS::terrain_samples)<<32;
    bool any=false;for(auto token:tokens)any|=!extensionPayload(token).isEmpty();
    if(!any&&!out.extras.children.contains(sampleKey))return true;
    auto &meta=out.extras.children[sampleKey];
    QSet<TS::TokenId> found;
    for(auto it=meta.order.begin();it!=meta.order.end();) {
        if(it->index>=0||!known(it->token)){++it;continue;}
        const auto token=it->token;found.insert(token);
        const auto payload=extensionPayload(token);
        if(payload.isEmpty()){it=meta.order.erase(it);continue;}
        if(payload!=extensionBaseline.value(token)) {
            auto &raw=meta.unknown[-it->index-1];QByteArray replacement=payload;
            if(token!=TS::TSRETerrainBakedMaterials&&raw.size()>=9) {
                const int labelSize=1+quint8(raw[8])*2;
                replacement=raw.mid(8,labelSize)+payload.mid(1);
                if((token==TS::TSRETerrainMaterialBuffer||token==TS::TSRETerrainBakedMaterial)
                    &&raw.size()>=8+labelSize+2) {
                    const int end=8+labelSize+2+2*qFromLittleEndian<quint16>(raw.constData()+8+labelSize);
                    replacement+=raw.mid(end);
                }
            }
            raw=framed(token,replacement);
        }
        ++it;
    }
    for(auto token:tokens)if(!found.contains(token)) {
        const auto payload=extensionPayload(token);if(payload.isEmpty())continue;
        meta.order.push_back({token,-int(meta.unknown.size())-1});meta.unknown.push_back(framed(token,payload));
    }
    return true;
}
