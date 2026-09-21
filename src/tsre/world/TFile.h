#pragma once
#include "TerrainFileData.h"
#include <QVector>

// One authoritative native value model. Hot accessors never allocate or search.
class TFile : public TerrainFile::Data {
public:
    using Mat=TerrainFile::Shader;
    using Patch=TerrainFile::Patch;
    using PatchUv=TerrainFile::PatchUv;
    enum class PatchField : int {
        CenterX,AverageY,CenterZ,FactorY,RangeY,RadiusM,ShaderIndex,
        TextureX,TextureY,TextureW,TextureB,TextureC,TextureH
    };
    static constexpr int PatchFieldCount=13;
    bool loaded=false,used=false;
    TFile()=default;
    TFile(const TFile&)=default;
    TFile &operator=(const TFile&)=default;
    TFile(TFile&&) noexcept=default;
    TFile &operator=(TFile&&) noexcept=default;
    virtual ~TFile()=default;
    std::vector<Patch> &patches() {return patchSets.back().patches;}
    const std::vector<Patch> &patches() const {return patchSets.back().patches;}
    int patchCount() const {return activeSet()?int(activeSet()->patchesPerSide.value_or(0)):0;}
    int materialCount() const {return int(shaders.size()/(paired?2:1));}
    bool hasMaterial(int id) const {return id>=0&&id<materialCount();}
    Mat &material(int id) {return shaders[id];}
    const Mat &material(int id) const {return shaders[id];}
    // Flat tables have one material, never an invented auxiliary copy.
    Mat &auxiliary(int id) {return shaders[paired?id+materialCount():id];}
    const Mat &auxiliary(int id) const {return shaders[paired?id+materialCount():id];}
    QString textureName(int id,int slot=0) const;
    TerrainFile::Water waterValues() const {return water.value_or(TerrainFile::Water{});}
    void setTextureName(int id,const QString &name,int slot=0);
    float detailScale(int id) const {return hasMaterial(id)&&material(id).uvCalcs.size()>1?material(id).uvCalcs[1].scale:32.0f;}
    void setDetailScale(int id,float scale) {
        if(hasMaterial(id)&&material(id).textures.size()>1) {
            if(material(id).uvCalcs.size()<2)material(id).uvCalcs.resize(2);
            material(id).uvCalcs[1].scale=scale;
        }
    }
    int appendMaterial(const Mat &primary,const Mat *auxiliary,QString &error);
    bool moveMaterialToFront(int id,QString &error);
    bool canRemapMaterials(QString &error) const;
    int newMat();
    int cloneMat(int id);
    int getMatByTexture(QString name);
    void removeMat(int id);
    void setBufferNames(QString name);
    void initNew(QString name,int samples,int spacing,int patches);
    float patchValue(int patchId,PatchField field) const;
    void setPatchValue(int patchId,PatchField field,float value);
    struct LayoutInfo {int samples=0;float spacing=0;int patches=0;};
    static bool readLayoutInfo(const QString &path,LayoutInfo &info);
    bool readT(QString path);
    bool load(FileBuffer *input);
    bool save(QString path);
    void save(QDataStream &stream);
    bool preflight(QString &error) const;
    bool validateRuntime(QString &error) const;
    bool loadPatchFlags(const QString &directory,QString &error);
    bool patchFlagsWritable() const;

    QString sampleMaterialBuffer,bakedMaterialInfo;
    struct BakeRecord {
        quint64 revision=0;quint32 resolution=0;
        QString settings,sources,validation;
    };
    quint64 materialContentRevision=0;
    QMap<QString,BakeRecord> seasonalBakes;
    bool bakedMaterialsValid=true;
    QByteArray bakeMetadata() const;
    bool readBakeMetadata(const QByteArray &bytes);
    void selectBakeVariant(const QString &variant);
    bool materialUidMapPresent=false,materialUidMapValid=true;
    QMap<int,quint32> materialUids;

private:
    struct PatchFlagsResource {
        QString path;QByteArray original;
        std::vector<quint32> inlineFlags;
        bool valid=false;
    };
    std::vector<PatchFlagsResource> patchFlagsResources;
    struct BakeBlock {QString variant;QByteArray label,opaque;};
    QByteArray bakeLabel;
    std::vector<BakeBlock> bakeBlocks;
    QMap<TS::TokenId,QByteArray> extensionBaseline;
    bool adopt(TerrainFile::Data parsed,QString &error);
    void readExtensions();
    QByteArray extensionPayload(TS::TokenId token) const;
    bool prepare(TerrainFile::Data &data,QString &error) const;
};

