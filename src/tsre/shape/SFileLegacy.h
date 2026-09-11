/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SFILELEGACY_H
#define	SFILELEGACY_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QString>
#include <QPointer>
#include <QSharedPointer>
#include <vector>
#include <QVector>
#include <tsre/shape/ComplexShape.h>

class FileBuffer;
class ShapeTextureInfo;
class ShapeHierarchyInfo;
class ContentHierarchyInfo;
class RenderItem;

class SFileLegacy : public ComplexShape {
public:

    struct SObjHeader {
        QVector<int> geometryNodeMap;
    };

    struct fshader {
        QString name;
        float alpha = 0;
    };

    struct czes {
        int iloscv = 0;
        int prim_state_idx = 0;
        //QOpenGLBuffer VBO;
        //QOpenGLVertexArrayObject VAO;
        int offset = 0;
        int* idx = nullptr;
        bool enabled = true;
    };

    struct fvertex {
        unsigned short arg1;
        unsigned int point;
        unsigned int normal;
        unsigned short arg2;
        unsigned short arg3;
        unsigned short material;
        unsigned int uvpoint = 0;
    };

    struct sub {
        std::vector<fvertex> vertices;
        int iloscc = 0;
        SObjHeader header;
        czes* czesci = nullptr;
        QOpenGLBuffer VBO;
        QOpenGLVertexArrayObject VAO;
    };

    struct dist {
        int levelSelection = 0;
        int ilosch = 0;
        int* hierarchia = nullptr;
        int iloscs = 0;
        sub* subobiekty = nullptr;
    };

    struct matrt {
        QString name;
        float param[16];
        float fixed[16];
        bool isFixed = false;
        long long int hash = 0;
    };

    struct primst {
        int vtx_state, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8;
    };

    struct text {
        int image, arg1, arg2, arg3;
    };

    struct vtxs {
        int arg1, matrix, arg2, arg3, arg4;
    };

    struct imgs {
        QString name;
        int tex = 0;
        int texAddr = -1;
    };

    struct fpoint {
        float x, y, z;
    };

    struct punlist {
        fpoint* points = nullptr;
        fpoint* uv_points = nullptr;
        fpoint* normals = nullptr;
        int ipoints = 0;
        int iuv_points = 0;
        int inormals = 0;
    };

    // zmienne
    //struct VBO {
    //    int w[];
    //};

    struct EsdBoundingBox {
        float shape[6];
        float rotation[3];
        float translation[3];

        EsdBoundingBox(){
            rotation[0] = 0;
            rotation[1] = 0;
            rotation[2] = 0;
            translation[0] = 0;
            translation[1] = 0;
            translation[2] = 0;
        }
    };

    struct AnimFrameId {
        unsigned int id1 = 0;
        unsigned int id2 = 0;
        float offset = 0;
    };

    struct AnimNode {
        struct TcbKey {
            int frame = 0;
            float quat[4];
            float param[5];
        };
        struct SlerpRot {
            int frame = 0;
            float quat[4];
        };
        struct LinearKey {
            int frame = 0;
            float pos[3];
        };
        QVector<TcbKey> tcbKey;
        QVector<AnimFrameId> tcbId;
        QVector<SlerpRot> slerpRot;
        QVector<AnimFrameId> slerpbId;
        QVector<LinearKey> linearKey;
        QVector<AnimFrameId> linearId;
    };

    struct Animation {
        int frames = 0;
        float fps = 0;
        QVector<AnimNode> node;
        void loadC(FileBuffer* data, int length);
        void loadX(FileBuffer* data);
    };

    QVector<Animation> animations;
    bool animated = false;

    QString pathid;
    QString sciezka;
    QString nazwa;
    QString texPath;
    QString sdName;
    int isinit = 0;
    int loaded = 0;
    bool loadedSd = false;
    int texloaded = 0;
    int ref = 0;
    int esdDetailLevel = -1;
    int esdAlternativeTexture = -1;
    QVector<EsdBoundingBox> esdBoundingBox;

    //VBO vbo[];
    punlist tpoints{};
    // zmienne
    int iloscd = 0;
    int iloscm = 0;
    matrt* macierz = nullptr;
    int ilosci = 0;
    imgs* image = nullptr;
    int ilosct = 0;
    text* texture = nullptr;
    int iloscv = 0;
    vtxs* vtxstate = nullptr;
    int iloscps = 0;
    primst* primstate = nullptr;
    dist* distancelevel = nullptr;
    int currentDistanceLevel = 0;

    int ishaders = 0;
    fshader* shader = nullptr;
    float size = 0;
    float bound[6]{};

    const QString& getPathId() const override { return pathid; }
    const QString& getTexPath() const override { return texPath; }
    int getEsdDetailLevel() const override { return esdDetailLevel; }
    bool isLoaded() const override { return loaded == 1; }
    float getSize() const override { return size; }
    const float* getBound() const override { return bound; }

    SFileLegacy();
    SFileLegacy(QString pathid, QString name, QString texp );
    SFileLegacy(const SFileLegacy&) = delete;
    SFileLegacy& operator=(const SFileLegacy&) = delete;
    virtual ~SFileLegacy();
    void load() override;
    bool loadData();
    bool initGL();
    bool isGLReady() const;
    void enablePart(unsigned int uid, unsigned int stateId = 0) override;
    void disablePart(unsigned int uid, unsigned int stateId = 0) override;
    void updateSim(float deltaTime, unsigned int stateId = 0) override;
    void render() override;
    void render(quint32 selectionId, unsigned int stateId) override;
    void pushRenderItem() override;
    void pushRenderItem(quint32 selectionId, unsigned int stateId) override;
    void invalidateRenderState(bool invalidateMatrixCache = true) override;
    void getSize();
    bool getBoxPoints(QVector<float> &points) override;
    void getFloorBorderLinePoints(float *&punkty) override;
    bool isSnapable() const override;
    void addSnapablePoints(QVector<float> &out) override;
    void reload() override;
    unsigned int newState() override;
    void setAnimated(unsigned int stateId, bool animated) override;
    void setEnabledSubObjs(unsigned int stateId, unsigned int enabledSubObjs) override;
    void setCurrentDistanceLevel(unsigned int stateId, int level) override;
    void enableSubObjByName(unsigned int stateId, const QString& name, bool val) override;
    void enableSubObjByNameQueue(unsigned int stateId, const QString& name, bool val) override;
    void fillShapeTextureInfo(QHash<int, ShapeTextureInfo*> &list, unsigned int stateId = 0) override;
    void fillShapeHierarchyInfo(ShapeHierarchyInfo* info, unsigned int stateId = 0) override;
    void fillContentHierarchyInfo(QVector<ContentHierarchyInfo*> &list, int parent) override;
private:
    bool glReady = false;
    QPointer<QOpenGLContext> glContext;
    void clearRenderItems(unsigned int stateId);
    QHash<unsigned int, QVector<QSharedPointer<RenderItem>>> renderItemOwners;
    void clearData();
    static void odczytajshadersc(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajpunktyc(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajuvpunktyc(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajnormalnec(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajmatricesc(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajimagesc(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajtexturesc(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajvtx_statesc(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajprim_statesc(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajloddc(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajshaders(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajpunkty(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajuvpunkty(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajnormalne(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajmatrices(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajimages(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajtextures(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajvtx_states(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajprim_states(FileBuffer* bufor, SFileLegacy* pliks);
    static void odczytajlodd(FileBuffer* bufor, SFileLegacy* pliks);
    struct State {
        bool animated = false;
        int enabledSubObjs = 0xFFFFFFFF;
        float frameCount = 0;
        unsigned long long int lastTime = 0;
        QMap<QString, bool> enableSubObjQueue;
        int distanceLevel = 0;
    };
    QVector<State> state;

    void loadSd();
    float* getPmatrix(int currentDlevel, float* pmatrix, int matrix);
    float* getPmatrixAnimated(int currentDlevel, float* pmatrix, int matrix, float frame);
    void buildFrameIds();
    unsigned long long getTextureStateHash() const;
    bool snapable = false;
    //float *mvMatrix = NULL;
    bool requiresUpdate = false;
    QHash<unsigned int, QVector<RenderItem *>> renderItems;
    QHash<unsigned int, unsigned long long> renderItemsTextureHash;
};

#endif	/* SFILELEGACY_H */
