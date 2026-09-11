/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/TFile.h>
#include <QDebug>
#include <QFile>
#include <QSaveFile>
#include <tsre/fileFunctions/TS.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <QDataStream>
#include <tsre/world/TerrainGridLayout.h>
#include <memory>
#include <cmath>

namespace {
void readLayoutBlocks(FileBuffer &data, TFile::LayoutInfo &info, int depth) {
    if (depth > 6)
        throw FileBuffer::ParseError("Terrain layout nesting limit");
    while (data.off < data.readEnd()) {
        const auto block = data.readBlock();
        FileBuffer::ScopedLimit scope(data, block.end);
        switch (block.id) {
        case TS::terrain:
        case TS::terrain_samples:
        case TS::terrain_patches:
        case TS::terrain_patchset:
            data.skipLabel();
            readLayoutBlocks(data, info, depth + 1);
            break;
        case TS::terrain_patchsets:
            data.skipLabel();
            data.getInt(); // Child count; dimensions follow in bounded blocks.
            readLayoutBlocks(data, info, depth + 1);
            break;
        case TS::terrain_nsamples:
            data.skipLabel(); info.samples = data.getInt(); break;
        case TS::terrain_sample_size:
            data.skipLabel(); info.spacing = data.getFloat(); break;
        case TS::terrain_patchset_npatches:
            data.skipLabel(); info.patches = data.getInt(); break;
        default: break;
        }
        data.off = block.end;
    }
}
}

bool TFile::readLayoutInfo(const QString &path, LayoutInfo &info) {
    info = LayoutInfo{};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    try {
        std::unique_ptr<FileBuffer> data(ReadFile::read(&file));
        if (!data) return false;
        data->require(32);
        data->off = 32;
        readLayoutBlocks(*data, info, 0);
        return info.samples > 0 && info.patches > 0
                && std::isfinite(info.spacing) && info.spacing > 0;
    } catch (const FileBuffer::ParseError &) {
        return false;
    }
}

TFile::TFile() {
    loaded = false;
    used = false;
}

TFile::TFile(const TFile& orig) {
}

TFile::~TFile() {
}

float TFile::patchValue(int patchId, PatchField field) const {
    return tdata[patchId * PatchFieldCount + static_cast<int>(field)];
}

void TFile::setPatchValue(int patchId, PatchField field, float value) {
    tdata[patchId * PatchFieldCount + static_cast<int>(field)] = value;
}

void TFile::initNew(QString name, int samples, int sampleS, int patches){
    sampleMaterialBuffer.clear();
    bakedMaterialInfo.clear(); seasonalBakes.clear(); materialContentRevision=0; bakedMaterialsValid=true;
    materialUidMapPresent=false; materialUidMapValid=true; materialUids.clear();
    TerrainGridLayout layout;
    QString layoutError;
    if (!TerrainGridLayout::tryCreate(samples, static_cast<float>(sampleS),
                                      patches, 0.0f, layout, layoutError)) {
        qWarning() << "Cannot initialize terrain descriptor" << name
                   << layoutError;
        loaded = false;
        return;
    }
    sampleEbuffer = new QString(name + "_e.raw");
    sampleNbuffer = new QString(name + "_n.raw");
    sampleYbuffer = new QString(name + "_y.raw");
    
    waterLevel = true;
    WSW = 0;
    WSE = 0;
    WNE = 0;
    WNW = 0;
    
    errthresholdScale = new float();
    *errthresholdScale = 1;
    alwaysselectMaxdist = new float();
    *alwaysselectMaxdist = 0;
    nsamples = new int(samples);
    sampleRotation = new float();
    *sampleRotation = 0;
    sampleSize = new float(sampleS);
    floor = -63.0;
    scale = 0.00195312;

    patchsetDistance = 0;
    patchsetNpatches = patches;
    flags = new int[patches*patches];
    tdata = new float[patches * patches * PatchFieldCount];
    errorBias = new float[patches*patches];
    const float patchSize = layout.patchWorldSize;
    // Open Rails names this descriptor field FactorY. Its underlying formula
    // is not yet documented, but MSRE confirms that the legacy 99.48125458
    // value for a 128 m patch scales linearly with physical patch size.
    const float patchFactorY = 99.48125458f * patchSize / 128.0f;
    const float textureScale = layout.defaultPatchTextureScale();
    float patchPosZ = -0.5*patchSize;
    float patchPosX = 0.5*patchSize;
    for(int j = 0; j < patches; j++, patchPosZ -= patchSize){
        patchPosX = 0.5*patchSize;
        for(int i = 0; i < patches; i++, patchPosX += patchSize){
            flags[(j*patches+i)] = 0;
            const int patchId = j * patches + i;
            setPatchValue(patchId, PatchField::CenterX, patchPosX);
            setPatchValue(patchId, PatchField::AverageY, 1.0f);
            setPatchValue(patchId, PatchField::CenterZ, patchPosZ);
            setPatchValue(patchId, PatchField::FactorY, patchFactorY);
            setPatchValue(patchId, PatchField::RangeY, 0.0f);
            setPatchValue(patchId, PatchField::RadiusM, 0.5f * patchSize);
            setPatchValue(patchId, PatchField::ShaderIndex, 0.0f);
            setPatchValue(patchId, PatchField::TextureX, 0.0001f);
            setPatchValue(patchId, PatchField::TextureY, 0.0001f);
            setPatchValue(patchId, PatchField::TextureW, textureScale);
            setPatchValue(patchId, PatchField::TextureB, 0.0f);
            setPatchValue(patchId, PatchField::TextureC, 0.0f);
            setPatchValue(patchId, PatchField::TextureH, textureScale);
            errorBias[patchId] = 1;
        }
    }
        
    newMat();
    loaded = true;
}

void TFile::setBufferNames(QString name){
    sampleEbuffer = new QString(name + "_e.raw");
    sampleNbuffer = new QString(name + "_n.raw");
    sampleYbuffer = new QString(name + "_y.raw");
}

bool TFile::readT(QString fSfile) {
        fSfile.replace("//","/");
        qDebug() << "T File "<< fSfile;
        QFile file(fSfile);
        if (!file.open(QIODevice::ReadOnly)){
            //qDebug() << "fail t file "<< fSfile;
            return false;
        }
        FileBuffer* data = ReadFile::read(&file);
        //qDebug() << "Date:" << data->length;
        const bool ok = load(data);
        delete data;
        return ok;
}
bool TFile::load(FileBuffer* data) {
    loaded = false;
    sampleMaterialBuffer.clear();
    bakedMaterialInfo.clear(); seasonalBakes.clear(); materialContentRevision=0; bakedMaterialsValid=true;
    materialUidMapPresent = false;
    materialUidMapValid = true;
    materialUids.clear();
    try {
        FileBuffer::ScopedLimit input(*data, data->readEnd());
        data->require(32);
        data->off += 32;
        data->findToken(TS::terrain);
        const int end = data->readBlockEnd();
        FileBuffer::ScopedLimit root(*data, end);
        data->skipLabel();
        while (data->off < end) {
            const auto block = data->readBlock();
            FileBuffer::ScopedLimit child(*data, block.end);
            switch (block.id) {
            case TS::terrain_errthreshold_scale:
                data->skipLabel();
                errthresholdScale = new float(data->getFloat());
                break;
            case TS::terrain_alwaysselect_maxdist:
                data->skipLabel();
                alwaysselectMaxdist = new float(data->getFloat());
                break;
            case TS::terrain_samples: get139(data, block.end); break;
            case TS::terrain_water_height_offset: get251(data); break;
            case TS::terrain_shaders: get151(data); break;
            case TS::terrain_patches: get157(data); break;
            default: qDebug() << "TFile - unknown token:" << TS::describe(block.id); break;
            }
            data->off = block.end;
        }
        loaded = true;
        return true;
    } catch (const FileBuffer::ParseError& error) {
        qWarning() << "Invalid terrain SIMIS data at" << data->off << ":" << error.what();
        return false;
    }
}

void TFile::get139(FileBuffer* data, int length) {
        int slen;
        bool bakeContainerSeen=false;
        data->skipLabel();
        while (data->off < length) {
            const auto block = data->readBlock();
            FileBuffer::ScopedLimit child(*data, block.end);
            const auto pozycja = block.id;
            const int akto = block.body, offset = block.end - block.body;

            switch (pozycja) {
                case TS::terrain_nsamples:
                    data->skipLabel();
                    nsamples = new int;
                    *nsamples = data->getInt();
                    break;
                case TS::terrain_sample_rotation:
                    data->skipLabel();
                    sampleRotation = new float;
                    *sampleRotation = data->getFloat();
                    break;    
                case TS::terrain_sample_floor:
                    data->skipLabel();
                    floor = data->getFloat();
                    break;
                case TS::terrain_sample_scale:
                    data->skipLabel();
                    scale = data->getFloat();
                    break;
                case TS::terrain_sample_size:
                    data->skipLabel();
                    sampleSize = new float;
                    *sampleSize = data->getFloat();
                    break;
                case TS::terrain_sample_fbuffer:
                    data->skipLabel();
                    slen = data->getShort()*2;
                    sampleFbuffer = data->getString(data->off, data->off + slen);
                    break;
                case TS::terrain_sample_ybuffer:
                    data->skipLabel();
                    slen = data->getShort()*2;
                    sampleYbuffer = data->getString(data->off, data->off + slen);
                    break;
                case TS::terrain_sample_ebuffer:
                    data->skipLabel();
                    slen = data->getShort()*2;
                    sampleEbuffer = data->getString(data->off, data->off + slen);
                    break;
                case TS::terrain_sample_nbuffer:
                    data->skipLabel();
                    slen = data->getShort()*2;
                    sampleNbuffer = data->getString(data->off, data->off + slen);
                    break;
                case TS::TSRETerrainMaterialMap: {
                    const bool duplicate=materialUidMapPresent;
                    materialUidMapPresent=true; materialUidMapValid=false;
                    const int end=akto+offset;
                    if (duplicate || offset<5 || end>data->length) return;
                    const int labelBytes=data->get()*2;
                    if (labelBytes+4>end-data->off) return;
                    data->off+=labelBytes;
                    const quint32 count=quint32(data->getInt());
                    if (count>256 || quint64(count)*8!=quint64(end-data->off)) return;
                    for (quint32 i=0;i<count;++i) {
                        const quint32 id=quint32(data->getInt()), uid=quint32(data->getInt());
                        if (id>255 || !uid || materialUids.contains(int(id))) return;
                        materialUids.insert(int(id),uid);
                    }
                    materialUidMapValid=true;
                    break;
                }
                case TS::TSRETerrainMaterialBuffer:
                case TS::TSRETerrainBakedMaterial: {
                    QString &value = pozycja == TS::TSRETerrainMaterialBuffer
                            ? sampleMaterialBuffer : bakedMaterialInfo;
                    // Keep malformed presence active/refused, never silently disable it.
                    value = ":invalid procedural material reference:";
                    const int end = akto + offset;
                    if (offset < 3 || end > data->length) return;
                    const int labelBytes = data->get() * 2;
                    if (labelBytes + 2 > end - data->off) return;
                    data->off += labelBytes;
                    const int bytes = quint16(data->getShort()) * 2;
                    if (bytes == 0 || bytes > end - data->off) return;
                    QString *reference = data->getString(data->off, data->off + bytes);
                    value = *reference;
                    delete reference;
                    break;
                }
                case TS::TSRETerrainBakedMaterials: {
                    const int end=akto+offset;
                    if (bakeContainerSeen || end>data->length || offset<13 || offset>1024*1024
                            || !readBakeMetadata(QByteArray(reinterpret_cast<const char*>(data->data+data->off),end-data->off))) {
                        bakedMaterialsValid=false; bakedMaterialInfo=":invalid bake metadata:";return;
                    }
                    bakeContainerSeen=true;
                    break;
                }
                case TS::terrain_sample_asbuffer:
                    getOpaqueSampleBuffer(data, akto + offset, sampleASbuffer);
                    if (sampleASbuffer.present && !opaqueSampleBufferOrder.contains(TS::terrain_sample_asbuffer))
                        opaqueSampleBufferOrder.push_back(TS::terrain_sample_asbuffer);
                    break;
                case TS::terrain_sample_usbuffer:
                    getOpaqueSampleBuffer(data, akto + offset, sampleUSbuffer);
                    if (sampleUSbuffer.present && !opaqueSampleBufferOrder.contains(TS::terrain_sample_usbuffer))
                        opaqueSampleBufferOrder.push_back(TS::terrain_sample_usbuffer);
                    break;
                default:
                    qDebug() << "TFile - unknown token: "<< TS::describe(pozycja);
                    break;
            }
            data->off = akto + offset;
            if(data->off >= length) break;
        }
    }

void TFile::getOpaqueSampleBuffer(FileBuffer *data, int blockEnd,
                                  OpaqueSampleBuffer &buffer) {
    buffer = OpaqueSampleBuffer{};
    if (data == NULL || data->off >= data->length || blockEnd <= data->off
            || blockEnd > data->length) {
        qWarning() << "Invalid opaque terrain sample buffer bounds"
                   << (data == NULL ? 0 : data->off) << blockEnd
                   << (data == NULL ? 0 : data->length);
        return;
    }
    const int labelCharacters = data->get();
    const int labelBytes = labelCharacters * 2;
    if (labelBytes > blockEnd - data->off) {
        qWarning() << "Invalid opaque terrain sample buffer label length"
                   << labelCharacters;
        return;
    }
    if (labelBytes > 0) {
        QString *label = data->getString(data->off, data->off + labelBytes);
        buffer.label = *label;
        delete label;
        data->off += labelBytes;
    }
    buffer.payload = QByteArray(
            reinterpret_cast<const char*>(data->data + data->off),
            blockEnd - data->off);
    buffer.present = true;
}

int TFile::opaqueSampleBufferBlockLength(const OpaqueSampleBuffer &buffer) {
    return 1 + buffer.label.length() * 2 + buffer.payload.size();
}

void TFile::saveOpaqueSampleBuffer(QDataStream &write, TS::TokenId token,
                                   const OpaqueSampleBuffer &buffer) {
    if (!buffer.present)
        return;
    write << static_cast<quint32>(token);
    write << static_cast<qint32>(opaqueSampleBufferBlockLength(buffer));
    write << static_cast<quint8>(buffer.label.length());
    for (const QChar character : buffer.label)
        write << character.unicode();
    if (!buffer.payload.isEmpty())
        write.writeRawData(buffer.payload.constData(), buffer.payload.size());
}


namespace {
FileBuffer::Block nextTerrainBlock(FileBuffer* data, TS::TokenId id) {
    data->findToken(id);
    data->off -= 4;
    return data->readBlock();
}

int terrainBlockCount(FileBuffer* data) {
    const int count = data->getInt();
    if (count < 0 || count > (data->readEnd() - data->off) / 9)
        throw FileBuffer::ParseError("Invalid terrain child count");
    return count;
}
}

void TFile::get151(FileBuffer* data) {
    data->skipLabel();
    const int count = terrainBlockCount(data);
    // Keep TSRE's existing detailed/auxiliary split; shader policy is unchanged.
    materialsCount = count / 2;
    for (int j = 0; j < count; ++j) {
        const auto shader = nextTerrainBlock(data, TS::terrain_shader);
        FileBuffer::ScopedLimit shaderScope(*data, shader.end);
        Mat* mat = j < materialsCount ? &materials[j] : &amaterials[j - materialsCount];
        data->skipLabel();
        mat->name = new QString(data->readString());
        while (data->off < shader.end) {
            const auto child = data->readBlock();
            FileBuffer::ScopedLimit childScope(*data, child.end);
            switch (child.id) {
            case TS::terrain_texslots: get153(data, mat); break;
            case TS::terrain_uvcalcs: get156(data, mat); break;
            default: break;
            }
            data->off = child.end;
        }
        data->off = shader.end;
    }
}

void TFile::get153(FileBuffer* data, TFile::Mat* mat) {
    data->skipLabel();
    const int count = terrainBlockCount(data);
    if (count > 2)
        throw FileBuffer::ParseError("TSRE supports at most two terrain texture slots");
    mat->count153 = count;
    for (int j = 0; j < count; ++j) {
        const auto slot = nextTerrainBlock(data, TS::terrain_texslot);
        FileBuffer::ScopedLimit scope(*data, slot.end);
        data->skipLabel();
        mat->tex[j] = new QString(data->readString());
        mat->atex[j][0] = data->getInt();
        mat->atex[j][1] = data->getInt();
        data->off = slot.end;
    }
}

void TFile::get156(FileBuffer* data, TFile::Mat* mat) {
    data->skipLabel();
    const int count = terrainBlockCount(data);
    if (count > 2)
        throw FileBuffer::ParseError("TSRE supports at most two terrain UV calculations");
    mat->count155 = count;
    for (int j = 0; j < count; ++j) {
        const auto uv = nextTerrainBlock(data, TS::terrain_uvcalc);
        FileBuffer::ScopedLimit scope(*data, uv.end);
        data->skipLabel();
        for (int k = 0; k < 4; ++k)
            mat->itex[j][k] = data->getInt();
        data->off = uv.end;
    }
}

void TFile::get157(FileBuffer* data) {
    data->skipLabel();
    const auto sets = nextTerrainBlock(data, TS::terrain_patchsets);
    FileBuffer::ScopedLimit setsScope(*data, sets.end);
    data->skipLabel();
    const int count = terrainBlockCount(data);
    for (int j = 0; j < count; ++j) {
        const auto set = nextTerrainBlock(data, TS::terrain_patchset);
        FileBuffer::ScopedLimit setScope(*data, set.end);
        data->skipLabel();
        while (data->off < set.end) {
            const auto child = data->readBlock();
            FileBuffer::ScopedLimit childScope(*data, child.end);
            switch (child.id) {
            case TS::terrain_patchset_distance:
                data->skipLabel();
                patchsetDistance = data->getInt();
                break;
            case TS::terrain_patchset_npatches:
                data->skipLabel();
                patchsetNpatches = data->getInt();
                break;
            case TS::terrain_patchset_patches:
                get163(data, patchsetNpatches);
                break;
            default: break;
            }
            data->off = child.end;
        }
        // Preserve TSRE's last-set-wins behavior.
        data->off = set.end;
    }
}

void TFile::get163(FileBuffer* data, int n) {
        data->skipLabel();
        if (n <= 0 || n > TerrainGridLayout::MaximumPatchesPerSide) {
            throw FileBuffer::ParseError("Unsupported terrain patch grid");
        }
        //int ilosc = data.getInt();
        //qDebug() << "i to " << n;
        if (n * n > (data->readEnd() - data->off) / 69)
            throw FileBuffer::ParseError("Truncated terrain patch collection");
        delete[] tdata;
        delete[] errorBias;
        delete[] flags;
        tdata = new float[n * n * PatchFieldCount];
        errorBias = new float[n*n];
        flags = new int[n*n];
        for (int j = 0; j < n*n; j++) {
            const auto patch = nextTerrainBlock(data, TS::terrain_patchset_patch);
            FileBuffer::ScopedLimit scope(*data, patch.end);
            data->skipLabel();
            
            flags[j] = data->getInt();//&0xFFFF;
            //data->off += 4*6;
            setPatchValue(j, PatchField::CenterX, data->getFloat());
            setPatchValue(j, PatchField::AverageY, data->getFloat());
            setPatchValue(j, PatchField::CenterZ, data->getFloat());
            setPatchValue(j, PatchField::FactorY, data->getFloat());
            setPatchValue(j, PatchField::RangeY, data->getFloat());
            setPatchValue(j, PatchField::RadiusM, data->getFloat());
            setPatchValue(j, PatchField::ShaderIndex,
                          static_cast<float>(data->getInt()));
            if (patchValue(j, PatchField::ShaderIndex) >= materialsCount)
                setPatchValue(j, PatchField::ShaderIndex,
                              patchValue(j, PatchField::ShaderIndex)
                              - materialsCount);
            setPatchValue(j, PatchField::TextureX, data->getFloat());
            setPatchValue(j, PatchField::TextureY, data->getFloat());
            setPatchValue(j, PatchField::TextureW, data->getFloat());
            setPatchValue(j, PatchField::TextureB, data->getFloat());
            setPatchValue(j, PatchField::TextureC, data->getFloat());
            setPatchValue(j, PatchField::TextureH, data->getFloat());
            errorBias[j] = data->getFloat();
            //qDebug() << tdata[j*7+1] << tdata[j*7+2] << tdata[j*7+3] << tdata[j*7+4] << tdata[j*7+5] << tdata[j*7+6];
            
            data->off = patch.end;
        }
    }

void TFile::get251(FileBuffer* data) {
        data->skipLabel();
        const int payloadBytes = data->readEnd() - data->off;
        if (payloadBytes != 4 && payloadBytes != 16)
            throw FileBuffer::ParseError("Invalid terrain_water_height_offset payload size");

        // MSTS's legacy one-float form applies the same height to every corner.
        const float sw = data->getFloat();
        const float se = payloadBytes == 4 ? sw : data->getFloat();
        const float ne = payloadBytes == 4 ? sw : data->getFloat();
        const float nw = payloadBytes == 4 ? sw : data->getFloat();
        WSW = sw;
        WSE = se;
        WNE = ne;
        WNW = nw;
        waterLevel = true;
    }

int TFile::cloneMat(int id){
    QString* name = new QString();
    *name += *materials[id].name;
    materials[materialsCount].name = name;
    
    materials[materialsCount].count153 = materials[id].count153;
    for(int i = 0; i < materials[id].count153; i++){
        name = new QString();
        *name += *materials[id].tex[i];
        materials[materialsCount].tex[i] = name;
        materials[materialsCount].atex[i][0] = materials[id].atex[i][0];
        materials[materialsCount].atex[i][1] = materials[id].atex[i][1];
    }
    
    materials[materialsCount].count155 = materials[id].count155;
    for(int i = 0; i < materials[id].count155; i++){
        materials[materialsCount].itex[i][0] = materials[id].itex[i][0];
        materials[materialsCount].itex[i][1] = materials[id].itex[i][1];
        materials[materialsCount].itex[i][2] = materials[id].itex[i][2];
        materials[materialsCount].itex[i][3] = materials[id].itex[i][3];
    }
        
    cloneAMat(id);
    return this->materialsCount++;
}



int TFile::cloneAMat(int id){
    QString* name = new QString();
    *name += *amaterials[id].name;
    amaterials[materialsCount].name = name;
    
    amaterials[materialsCount].count153 = amaterials[id].count153;
    for(int i = 0; i < amaterials[id].count153; i++){
        name = new QString();
        *name += *amaterials[id].tex[i];
        amaterials[materialsCount].tex[i] = name;
        amaterials[materialsCount].atex[i][0] = amaterials[id].atex[i][0];
        amaterials[materialsCount].atex[i][1] = amaterials[id].atex[i][1];
    }
    
    amaterials[materialsCount].count155 = amaterials[id].count155;
    for(int i = 0; i < materials[id].count155; i++){
        amaterials[materialsCount].itex[i][0] = amaterials[id].itex[i][0];
        amaterials[materialsCount].itex[i][1] = amaterials[id].itex[i][1];
        amaterials[materialsCount].itex[i][2] = amaterials[id].itex[i][2];
        amaterials[materialsCount].itex[i][3] = amaterials[id].itex[i][3];
    }
        
    return this->materialsCount;
}

int TFile::newMat(){
    QString* name = new QString();
    *name += "DetailTerrain";
    materials[materialsCount].name = name;
    name = new QString();
    *name += "AlphaTerrain";
    amaterials[materialsCount].name = name;
    
    materials[materialsCount].count153 = 2;
    name = new QString();
    *name += "terrain.ace";
    materials[materialsCount].tex[0] = name;
    materials[materialsCount].atex[0][0] = 1;
    materials[materialsCount].atex[0][1] = 0;
    name = new QString();
    *name += "microtex.ace";
    materials[materialsCount].tex[1] = name;
    materials[materialsCount].atex[1][0] = 1;
    materials[materialsCount].atex[1][1] = 1;

    materials[materialsCount].count155 = 2;
    materials[materialsCount].itex[0][0] = 1;
    materials[materialsCount].itex[0][1] = 0;
    materials[materialsCount].itex[0][2] = 0;
    materials[materialsCount].itex[0][3] = 0;
    materials[materialsCount].itex[1][0] = 2;
    materials[materialsCount].itex[1][1] = 0;
    materials[materialsCount].itex[1][2] = 1;
    materials[materialsCount].itex[1][3] = 1107296256;
    
    amaterials[materialsCount].count153 = 1;
    name = new QString();
    *name += "terrain.ace";
    amaterials[materialsCount].tex[0] = name;
    amaterials[materialsCount].atex[0][0] = 1;
    amaterials[materialsCount].atex[0][1] = 0;
    
    amaterials[materialsCount].count155 = 1;
    amaterials[materialsCount].itex[0][0] = 1;
    amaterials[materialsCount].itex[0][1] = 0;
    amaterials[materialsCount].itex[0][2] = 0;
    amaterials[materialsCount].itex[0][3] = 0;
        
    return this->materialsCount++;
}

void TFile::removeMat(int id){
    if (!sampleMaterialBuffer.isEmpty()) {
        qWarning() << "Procedural terrain: shader removal/renumbering is disabled";
        return;
    }
    if(id <= 0) 
        return;
    if(id > materialsCount)
        return;
    
    const float defaultTextureScale = 0.998f
            / static_cast<float>(*nsamples / patchsetNpatches);
    for(int j = 0; j < patchsetNpatches*patchsetNpatches; j++){
        if(tdata[j*13+0+6] == id){
            tdata[j*13+0+6] = 0;
            tdata[j*13 + 1 + 6] = 0.001;
            tdata[j*13 + 2 + 6] = 0.001;
            tdata[j*13 + 3 + 6] = defaultTextureScale;
            tdata[j*13 + 4 + 6] = 0.0;
            tdata[j*13 + 5 + 6] = 0.0;
            tdata[j*13 + 6 + 6] = defaultTextureScale;
        }
    }
    for(int j = 0; j < patchsetNpatches*patchsetNpatches; j++){
        if(tdata[j*13+0+6] == materialsCount-1)
            tdata[j*13+0+6] = id;
    }
    
    qDebug() << *materials[id].name;
    materials[id] = materials[materialsCount-1];
    amaterials[id] = amaterials[materialsCount-1];
    qDebug() << *materials[id].name;
    materialsCount--;
}

int TFile::getMatByTexture(QString tname){
    for(int j = 0; j < materialsCount; j++)
        for(int i = 0; i < materials[j].count153; i++){
            if(tname.toLower() == materials[j].tex[i]->toLower())
                return j;
        }
    return -1;
}

bool TFile::save(QString name){
    name.replace("//", "/");
    QSaveFile file(name);
    qDebug() << "zapis .t "<<name;
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream write(&file);
    write.setByteOrder(QDataStream::LittleEndian);
    write.setFloatingPointPrecision(QDataStream::SinglePrecision);
    save(write);
    const bool ok = write.status() == QDataStream::Ok;
    write.setDevice(nullptr);
    return ok && file.commit();
}

void TFile::save(QDataStream &write){
    if (materialUidMapPresent && (!materialUidMapValid || materialUids.size()>256)) {
        write.setStatus(QDataStream::WriteFailed); return;
    }
    for (auto it=materialUids.cbegin();materialUidMapPresent && it!=materialUids.cend();++it)
        if (it.key()<0 || it.key()>255 || !it.value()) { write.setStatus(QDataStream::WriteFailed); return; }
    //calculate size
    
    int t137 = 0;
    if(errthresholdScale != NULL)
        t137 = 13;
   
    int t251 = 0;
    if(waterLevel)
        t251 = 25;
    
    int t138 = 0;
    if(alwaysselectMaxdist != NULL)
        t138 = 13;

    // 139
    int t139 = 1;
    if (materialUidMapPresent) t139 += 13 + materialUids.size()*8;
    if (!sampleMaterialBuffer.isEmpty())
        t139 += sampleMaterialBuffer.length()*2 + 11;
    const QByteArray bakedMetadata=bakeMetadata();
    if (!bakedMetadata.isEmpty()) t139 += bakedMetadata.size()+8;
    // 140
    if(nsamples != NULL)
        t139+=13;
    // 141 
    if(sampleRotation != NULL)
        t139+=13;
    // 142
    t139+=13;
    // 143 
    t139+=13;
    // 144 
    if(sampleSize != NULL)
        t139+=13;
    // TS::terrain_sample_asbuffer
    if(sampleASbuffer.present)
        t139 += opaqueSampleBufferBlockLength(sampleASbuffer) + 8;
    if(sampleUSbuffer.present)
        t139 += opaqueSampleBufferBlockLength(sampleUSbuffer) + 8;
    // 145
    if(sampleFbuffer != NULL)
        t139+=sampleFbuffer->length()*2+3+8;
    // 146
    if(sampleYbuffer != NULL)
        t139+=sampleYbuffer->length()*2+3+8;
    // 147
    if(sampleEbuffer != NULL)
        t139+=sampleEbuffer->length()*2+3+8;
    // 148
    if(sampleNbuffer != NULL)
        t139+=sampleNbuffer->length()*2+3+8;
    
    // 151
    int t151 = 0;
    t151+=5;
    QVector<int> t152(materialsCount * 2);
    QVector<int> t153(materialsCount * 2);
    Mat tmat;
    for(int j = 0; j < materialsCount*2; j++){
        t152[j] = 1;
        if(j < materialsCount)
            tmat = materials[j];
        else
            tmat = amaterials[j-materialsCount];
        t152[j] += tmat.name->length()*2+2;
        t152[j] += 13;
        t153[j] = 5;
        for(int i = 0; i < tmat.count153; i++){
            t152[j] += 11;
            t152[j] += tmat.tex[i]->length()*2;
            t152[j] += 8;
            t153[j] += 11;
            t153[j] += tmat.tex[i]->length()*2;
            t153[j] += 8;
        }
        t152[j] += 13;
        t152[j] += 25*tmat.count155;
        t151 += t152[j] + 8;
    }

    // 157
    int t157 = 0;
    t157 = 69*patchsetNpatches*patchsetNpatches+9+24+3+8+4+1+9;
    
    int t136 = 1 + t137 + t251 + t138 + t139 + 8 + t151 + 8 + t157 + 8;
    
    //write
    const char header[] = {
        0x53,0x49,0x4D,0x49,0x53,0x41,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
        0x4A,0x49,0x4E,0x58,0x30,0x74,0x36,0x62,0x5F,0x5F,0x5F,0x5F,0x5F,0x5F,0x0D,0x0A
    };
    write.writeRawData(header, 32);
    write << quint32(TS::terrain);
    write << (qint32)t136;
    write << (qint8)0;
    
    if(errthresholdScale != NULL){
        write << quint32(TS::terrain_errthreshold_scale);
        write << (qint32)5;
        write << (qint8)0;
        write << *errthresholdScale;
    }
    
    if(waterLevel){
        write << quint32(TS::terrain_water_height_offset);
        write << (qint32)17;
        write << (qint8)0;
        write << WSW;
        write << WSE;
        write << WNE;
        write << WNW;
    }
    
    if(alwaysselectMaxdist != NULL){
        write << quint32(TS::terrain_alwaysselect_maxdist);
        write << (qint32)5;
        write << (qint8)0;
        write << *alwaysselectMaxdist;
    }
    
    write << quint32(TS::terrain_samples);
    write << (qint32)t139;
    write << (qint8)0;
    // 140
    if(nsamples != NULL){
        write << quint32(TS::terrain_nsamples);
        write << (qint32)5;
        write << (qint8)0;
        write << *nsamples;
    }
    // 141 
    if(sampleRotation != NULL){
        write << quint32(TS::terrain_sample_rotation);
        write << (qint32)5;
        write << (qint8)0;
        write << *sampleRotation;
    }
    // 142
    write << quint32(TS::terrain_sample_floor);
    write << (qint32)5;
    write << (qint8)0;
    write << floor;
    // 143 
    write << quint32(TS::terrain_sample_scale);
    write << (qint32)5;
    write << (qint8)0;
    write << scale;
    // 144 
    if(sampleSize != NULL){
        write << quint32(TS::terrain_sample_size);
        write << (qint32)5;
        write << (qint8)0;
        write << *sampleSize;
    }
    // TS::terrain_sample_asbuffer
    QVector<TS::TokenId> opaqueOrder = opaqueSampleBufferOrder;
    if (sampleASbuffer.present && !opaqueOrder.contains(TS::terrain_sample_asbuffer))
        opaqueOrder.push_back(TS::terrain_sample_asbuffer);
    if (sampleUSbuffer.present && !opaqueOrder.contains(TS::terrain_sample_usbuffer))
        opaqueOrder.push_back(TS::terrain_sample_usbuffer);
    for (const TS::TokenId token : opaqueOrder) {
        if (token == TS::terrain_sample_asbuffer)
            saveOpaqueSampleBuffer(write, token, sampleASbuffer);
        else if (token == TS::terrain_sample_usbuffer)
            saveOpaqueSampleBuffer(write, token, sampleUSbuffer);
    }
    // 145
    if(sampleFbuffer != NULL){
        write << quint32(TS::terrain_sample_fbuffer);
        write << (qint32)sampleFbuffer->length()*2+3;
        write << (qint8)0;
        write << (qint16)sampleFbuffer->length();
        for(int i = 0; i < sampleFbuffer->length(); i++){
            write << sampleFbuffer->at(i).unicode();
        }
    }
    // 146
    if(sampleYbuffer != NULL){
        write << quint32(TS::terrain_sample_ybuffer);
        write << (qint32)sampleYbuffer->length()*2+3;
        write << (qint8)0;
        write << (qint16)sampleYbuffer->length();
        for(int i = 0; i < sampleYbuffer->length(); i++){
            write << sampleYbuffer->at(i).unicode();
        }
    }
    // 147
    if(sampleEbuffer != NULL){
        write << quint32(TS::terrain_sample_ebuffer);
        write << (qint32)sampleEbuffer->length()*2+3;
        write << (qint8)0;
        write << (qint16)sampleEbuffer->length();
        for(int i = 0; i < sampleEbuffer->length(); i++){
            write << sampleEbuffer->at(i).unicode();
        }
    }
    // 148
    if(sampleNbuffer != NULL){
        write << quint32(TS::terrain_sample_nbuffer);
        write << (qint32)sampleNbuffer->length()*2+3;
        write << (qint8)0;
        write << (qint16)sampleNbuffer->length();
        for(int i = 0; i < sampleNbuffer->length(); i++){
            write << sampleNbuffer->at(i).unicode();
        }
    }

    // 151 
    if (!sampleMaterialBuffer.isEmpty()) {
        write << quint32(TS::TSRETerrainMaterialBuffer)
              << qint32(sampleMaterialBuffer.length()*2+3) << qint8(0)
              << quint16(sampleMaterialBuffer.length());
        for (QChar c : sampleMaterialBuffer) write << c.unicode();
    }
    if (!bakedMetadata.isEmpty()) {
        write << quint32(TS::TSRETerrainBakedMaterials) << qint32(bakedMetadata.size());
        write.writeRawData(bakedMetadata.constData(),bakedMetadata.size());
    }
    if (materialUidMapPresent) {
        write << quint32(TS::TSRETerrainMaterialMap)
              << qint32(5+materialUids.size()*8) << qint8(0) << quint32(materialUids.size());
        for (auto it=materialUids.cbegin();it!=materialUids.cend();++it)
            write << quint32(it.key()) << it.value();
    }
    write << quint32(TS::terrain_shaders);
    write << (qint32)t151;
    write << (qint8)0;
    write << (qint32)materialsCount*2;
    
    for(int j = 0; j < materialsCount*2; j++){
        if(j < materialsCount)
            tmat = materials[j];
        else
            tmat = amaterials[j-materialsCount];
        write << quint32(TS::terrain_shader);
        write << (qint32)t152[j];
        write << (qint8)0;
        write << (qint16)tmat.name->length();
        for(int i = 0; i < tmat.name->length(); i++){
            write << tmat.name->at(i).unicode();
        }
        write << quint32(TS::terrain_texslots);
        write << (qint32)t153[j];
        write << (qint8)0;
        write << (qint32)tmat.count153;
        for(int i = 0; i < tmat.count153; i++){
            write << quint32(TS::terrain_texslot);
            write << (qint32)(tmat.tex[i]->length()*2+3+8);
            write << (qint8)0;
            write << (qint16)tmat.tex[i]->length();
            for(int ii = 0; ii < tmat.tex[i]->length(); ii++){
                write << tmat.tex[i]->at(ii).unicode();
            }
            write << (qint32)tmat.atex[i][0];
            write << (qint32)tmat.atex[i][1];
        }
        write << quint32(TS::terrain_uvcalcs);
        write << (qint32)(25*tmat.count155+5);
        write << (qint8)0;
        write << (qint32)tmat.count155;
        for(int i = 0; i < tmat.count155; i++){
            write << quint32(TS::terrain_uvcalc);
            write << (qint32)17;
            write << (qint8)0;
            write << (qint32)tmat.itex[i][0];
            write << (qint32)tmat.itex[i][1];
            write << (qint32)tmat.itex[i][2];
            write << (qint32)tmat.itex[i][3];
        }
    }
    
    // 157
    write << quint32(TS::terrain_patches);
    write << (qint32)t157;
    write << (qint8)0;
    
    write << quint32(TS::terrain_patchsets);
    write << (qint32)69*patchsetNpatches*patchsetNpatches+9+24+3+8+4+1;
    write << (qint8)0;
    write << (qint32)1;
    
    write << quint32(TS::terrain_patchset);
    write << (qint32)69*patchsetNpatches*patchsetNpatches+9+24+3;
    write << (qint8)0;

    write << quint32(TS::terrain_patchset_distance);
    write << (qint32)5;
    write << (qint8)0;
    write << (qint32)patchsetDistance;
    
    write << quint32(TS::terrain_patchset_npatches);
    write << (qint32)5;
    write << (qint8)0;
    write << (qint32)patchsetNpatches;
    
    write << quint32(TS::terrain_patchset_patches);
    write << (qint32)69*patchsetNpatches*patchsetNpatches+1;
    write << (qint8)0;

    for(int j = 0; j < patchsetNpatches*patchsetNpatches; j++){
        write << quint32(TS::terrain_patchset_patch);
        write << (qint32)61;
        write << (qint8)0;
        write << (qint32)flags[j];
        write << patchValue(j, PatchField::CenterX);
        write << patchValue(j, PatchField::AverageY);
        write << patchValue(j, PatchField::CenterZ);
        write << patchValue(j, PatchField::FactorY);
        write << patchValue(j, PatchField::RangeY);
        write << patchValue(j, PatchField::RadiusM);
        write << static_cast<qint32>(patchValue(j, PatchField::ShaderIndex));
        write << patchValue(j, PatchField::TextureX);
        write << patchValue(j, PatchField::TextureY);
        write << patchValue(j, PatchField::TextureW);
        write << patchValue(j, PatchField::TextureB);
        write << patchValue(j, PatchField::TextureC);
        write << patchValue(j, PatchField::TextureH);
        write << (float)errorBias[j];
    }
}

void TFile::print(){
    qDebug() << "Materials count " << (qint32)materialsCount;
    
    Mat tmat;
    for(int j = 0; j < materialsCount*2; j++){
        if(j < materialsCount)
            tmat = materials[j];
        else
            tmat = amaterials[j-materialsCount];
        qDebug() << "Material " << j <<" "<< *tmat.name;

        qDebug() << (qint32)tmat.count153;
        for(int i = 0; i < tmat.count153; i++){

            qDebug() << "- "<< i << " " << *tmat.tex[i]
            << " " << (qint32)tmat.atex[i][0]
            << " " << (qint32)tmat.atex[i][1];
        }

        qDebug() << (qint32)tmat.count155;
        for(int i = 0; i < tmat.count155; i++){
            qDebug() << "+ "<< i << " " << (qint32)tmat.itex[i][0]
            << " " << (qint32)tmat.itex[i][1]
            << " " << (qint32)tmat.itex[i][2]
            << " " << (qint32)tmat.itex[i][3];
        }
    }
}
