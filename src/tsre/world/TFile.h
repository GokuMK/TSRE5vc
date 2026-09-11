/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/fileFunctions/FileBuffer.h>
#include <QString>
#include <QByteArray>
#include <QVector>
#include <QMap>
#include <unordered_map>

#ifndef TFILE_H
#define	TFILE_H

class TFile {
public:
    enum class PatchField : int {
        CenterX = 0,
        AverageY = 1,
        CenterZ = 2,
        FactorY = 3,
        RangeY = 4,
        RadiusM = 5,
        ShaderIndex = 6,
        TextureX = 7,
        TextureY = 8,
        TextureW = 9,
        TextureB = 10,
        TextureC = 11,
        TextureH = 12
    };
    static constexpr int PatchFieldCount = 13;

    struct Mat {
        QString* tex[2];
        QString* name = NULL;
        int atex[2][2];
        int itex[2][4];
        int count153 = 0;
        int count155 = 0;
        
        Mat(){
            tex[0] = NULL;
            tex[1] = NULL;
        }
    };
    bool loaded = false;
    bool used = false;
    float* tdata = NULL;
    float* errorBias = NULL;
    //mat* materials;
    std::unordered_map<int, Mat> materials;
    std::unordered_map<int, Mat> amaterials;
    int* flags = nullptr;
    float WSW = 0;
    float WSE = 0;
    float WNE = 0;
    float WNW = 0;
    float* errthresholdScale = NULL;
    float* alwaysselectMaxdist = NULL;
    int* nsamples = NULL;
    float* sampleRotation = NULL;
    float* sampleSize = NULL;
    float floor = 0;
    float scale = 1;
    
    bool waterLevel = false;
    int materialsCount = 0;
    
    QString* sampleFbuffer = NULL;
    QString* sampleYbuffer = NULL;
    QString* sampleEbuffer = NULL;
    QString* sampleNbuffer = NULL;
    // TSRE experimental extension; nonempty reference enables procedural materials.
    QString sampleMaterialBuffer;
    // Internal cache identity/legacy-reader adapter. New files serialize the
    // structured seasonal records below, not this runtime string.
    QString bakedMaterialInfo;
    struct BakeRecord {
        quint64 revision = 0;
        quint32 resolution = 0;
        QString settings, sources, validation;
    };
    quint64 materialContentRevision = 0;
    QMap<QString,BakeRecord> seasonalBakes;
    bool bakedMaterialsValid = true;
    QByteArray bakeMetadata() const;
    bool readBakeMetadata(const QByteArray &bytes);
    // Runtime cache identity; only the structured metadata is written in new files.
    void selectBakeVariant(const QString &variant);
    // Absence retains the legacy local-shader source; invalid presence must not.
    bool materialUidMapPresent = false;
    bool materialUidMapValid = true;
    QMap<int,quint32> materialUids;
    struct OpaqueSampleBuffer {
        bool present = false;
        QString label;
        QByteArray payload;
    };
    OpaqueSampleBuffer sampleASbuffer;
    OpaqueSampleBuffer sampleUSbuffer;
    QVector<TS::TokenId> opaqueSampleBufferOrder;
    
    int patchsetDistance = 0;
    int patchsetNpatches = 0;
    
    TFile();
    TFile(const TFile& orig);
    virtual ~TFile();
    bool readT(QString fSfile);
    bool load(FileBuffer* data);
    bool save(QString name);
    void save(QDataStream &write);
    int newMat();
    int cloneMat(int id);
    int getMatByTexture(QString tname);
    void removeMat(int id);
    void setBufferNames(QString name);
    void initNew(QString name, int samples, int sampleS, int patches);
    float patchValue(int patchId, PatchField field) const;
    void setPatchValue(int patchId, PatchField field, float value);
    
private:
    void get139(FileBuffer* data, int length);
    void get151(FileBuffer* data);
    void get153(FileBuffer* data, TFile::Mat* m);
    void get156(FileBuffer* data, TFile::Mat* m);
    void get157(FileBuffer* data);
    void get163(FileBuffer* data, int n);
    void get251(FileBuffer* data);
    void getOpaqueSampleBuffer(FileBuffer *data, int blockEnd,
                               OpaqueSampleBuffer &buffer);
    static int opaqueSampleBufferBlockLength(const OpaqueSampleBuffer &buffer);
    static void saveOpaqueSampleBuffer(QDataStream &write, TS::TokenId token,
                                       const OpaqueSampleBuffer &buffer);
    
    void print();
    int cloneAMat(int id);
};

#endif	/* TFILE_H */

