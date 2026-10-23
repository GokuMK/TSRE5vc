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
#include <array>
#include <deque>
#include <vector>
#include <QVector>
#include <tsre/renderer/MeshHandle.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/ShapeAnimation.h>

class RenderQueue;

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
        // Sphere around the part's vertices in its own space; radius -1 until
        // the part is uploaded.
        float boundCenter[3] = {0.0f, 0.0f, 0.0f};
        float boundRadius = -1.0f;
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
        // Renderer-owned VNTA vertices of all parts, uploaded by initGL().
        MeshHandle mesh;
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
        // Night texture (task 35): its file (looked up once: 0 not yet, 1
        // found, -1 none) and its TexLib texture while held.
        QString nightFile;
        signed char night = 0;
        int nightTex = -1;
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

    // Its keyframe animation: one channel per matrix (ShapeAnimation).
    struct Animation {
        int frames = 0;
        float fps = 0;
        QVector<ShapeAnimation::Channel> node;
        void loadC(FileBuffer* data, int length);
        void loadX(FileBuffer* data);
    };

    QVector<Animation> animations;

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
    bool hasLoadFailed() const override { return loaded == 2; }
    float getSize() const override { return size; }
    const float* getBound() const override { return bound; }

    SFileLegacy();
    SFileLegacy(QString pathid, QString name, QString texp );
    SFileLegacy(const SFileLegacy&) = delete;
    SFileLegacy& operator=(const SFileLegacy&) = delete;
    virtual ~SFileLegacy();
    void load() override;
    ComplexShape *detachedCopy() const override;
    void loadDetached() override;
    void adopt(ComplexShape &copy) override;
    bool loadData();
    bool initGL();
    bool isGLReady() const;
    void enablePart(unsigned int uid, unsigned int stateId = 0) override;
    void disablePart(unsigned int uid, unsigned int stateId = 0) override;
    void updateSim(float deltaTime, unsigned int stateId = 0) override;
    void pushRenderItem(RenderQueue &queue) override;
    void pushRenderItem(RenderQueue &queue, quint32 selectionId, unsigned int stateId) override;
    void invalidateRenderState(bool invalidateMatrixCache = true) override;
    void getSize();
    bool getBoxPoints(QVector<float> &points) override;
    void getFloorBorderLinePoints(float *&punkty) override;
    bool isSnapable() const override;
    void addSnapablePoints(QVector<float> &out) override;
    bool matrixByName(const QString &name, float *out) const override;
    int partCount() const override;
    QString partName(int part) const override;
    int partParent(int part) const override;
    bool partRest(int part, float *out) const override;
    float partLastFrame(int part) const override;
    float animationFrames() const override;
    float animationRate() const override;
    void setPartFrame(unsigned int stateId, int part, float frame) override;
    void setPartTransform(unsigned int stateId, int part, const float *local) override;
    void clearPose(unsigned int stateId) override;
    bool partMatrix(unsigned int stateId, int part, float *out) const override;
    void releaseState(unsigned int stateId) override;
    void reload() override;
    unsigned int newState() override;
    void setAnimated(unsigned int stateId, bool animated) override;
    void setEnabledSubObjs(unsigned int stateId, unsigned int enabledSubObjs) override;
    void setCurrentDistanceLevel(unsigned int stateId, int level) override;
    int distanceLevelCount() const override;
    float distanceLevelDistance(int level) const override;
    void enableSubObjByName(unsigned int stateId, const QString& name, bool val) override;
    void enableSubObjByNameQueue(unsigned int stateId, const QString& name, bool val) override;
    void fillShapeTextureInfo(QHash<int, ShapeTextureInfo*> &list, unsigned int stateId = 0) override;
    void fillShapeHierarchyInfo(ShapeHierarchyInfo* info, unsigned int stateId = 0) override;
    void fillContentHierarchyInfo(QVector<ContentHierarchyInfo*> &list, int parent) override;
private:
    bool glReady = false;
    // texPath before loadSd() adds the season's directory.
    QString textureRoot;
    // Retires a set of cached packets through the renderer.
    void clearRenderItems(quint64 key);
    // Static packets depend only on the distance level and the enabled
    // sub-objects, so states that agree on both share them.
    quint64 packetKey(unsigned int stateId) const;
    // Animated parts are refilled every frame into packets and matrices
    // reused per state.
    struct AnimatedPackets {
        QVector<RenderItem*> items;
        // A deque keeps matrix addresses stable as the pool grows.
        std::deque<std::array<float, 16>> matrices;
        quint64 frame = 0;
        bool used = false;
    };
    QHash<unsigned int, AnimatedPackets> animatedPackets;
    void clearAnimatedPackets();
    unsigned char gatherSurface(int primState) const;
    void setGatherTexture(RenderItem *item, int primState, bool texEnabled);
    // Night textures (task 35): held while the frame wants them, released by
    // day; the packets are rebuilt when that changes.
    bool nightHeld = false;
    void releaseNightTextures();
    void clearData();
    void releaseMeshes();
public:
    // Section readers; public so the token tests can drive them directly.
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
private:
    struct State {
        bool animated = false;
        // Its parts posed (setPartFrame, setPartTransform).
        ShapeAnimation::Pose pose;
        // Released (releaseState): newState() takes it again.
        bool released = false;
        int enabledSubObjs = 0xFFFFFFFF;
        float frameCount = 0;
        unsigned long long int lastTime = 0;
        QMap<QString, bool> enableSubObjQueue;
        int distanceLevel = 0;
    };
    QVector<State> state;

    void loadSd();
    float* getPmatrix(int currentDlevel, float* pmatrix, int matrix);
    // Every matrix of the level as the state poses it, with its parents,
    // into `out` (one 4x4 per matrix).
    void posedMatrices(unsigned int stateId, int currentDlevel, std::vector<std::array<float, 16>> &out) const;
    // Whether the state's parts move: drawn with matrices of its own.
    bool movesParts(unsigned int stateId) const;
    std::vector<std::array<float, 16>> posedScratch;
    bool snapable = false;
    //float *mvMatrix = NULL;
    bool requiresUpdate = false;
    // Static packets by packetKey(); repeated objects submit the same packets,
    // which lets the renderer draw them instanced.
    QHash<quint64, QVector<RenderItem *>> renderItems;
};

#endif	/* SFILELEGACY_H */
