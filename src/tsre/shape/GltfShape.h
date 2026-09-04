/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef GLTFSHAPE_H
#define GLTFSHAPE_H

#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <tsre/shape/ComplexShape.h>

class ContentHierarchyInfo;
class RenderItem;
class ShapeHierarchyInfo;
class ShapeTextureInfo;

class GltfShape : public ComplexShape {
public:
    GltfShape(QString pathid, QString name, QString texPath);
    ~GltfShape() override;

    const QString& getPathId() const override { return pathid; }
    const QString& getTexPath() const override { return texPath; }

    bool isLoaded() const override { return loaded == 1; }
    float getSize() const override { return size; }
    const float* getBound() const override { return bound; }
    bool getBoxPoints(QVector<float> &points) override;

    void load() override;
    void reload() override;

    unsigned int newState() override;
    void setAnimated(unsigned int stateId, bool animated) override;
    void updateSim(float deltaTime, unsigned int stateId = 0) override;

    void render() override;
    void render(quint32 selectionId, unsigned int stateId) override;
    void pushRenderItem() override;
    void pushRenderItem(quint32 selectionId, unsigned int stateId) override;

    void invalidateRenderState(bool invalidateMatrixCache = true) override;
    void enablePart(unsigned int uid, unsigned int stateId = 0) override;
    void disablePart(unsigned int uid, unsigned int stateId = 0) override;

    void fillShapeTextureInfo(QHash<int, ShapeTextureInfo*> &list, unsigned int stateId = 0) override;
    void fillShapeHierarchyInfo(ShapeHierarchyInfo* info, unsigned int stateId = 0) override;
    void fillContentHierarchyInfo(QVector<ContentHierarchyInfo*> &list, int parent) override;

private:
    struct State {
        bool animated = false;
    };

    struct MaterialRuntime {
        int texId = -1;
        int texAddr = -1;
        bool hasTexture = false;
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float alphaAttr = 1.0f;
        bool doubleSided = false;
        QString debugName;
    };

    struct MeshPrimitiveGpu {
        QOpenGLBuffer VBO;
        QOpenGLVertexArrayObject VAO;
        int vertCount = 0;
        MaterialRuntime material;
    };

    struct MeshGpu {
        QVector<MeshPrimitiveGpu*> primitives;
        float min[3] = {0.0f, 0.0f, 0.0f};
        float max[3] = {0.0f, 0.0f, 0.0f};
        bool hasBounds = false;
    };

    struct DrawUnit {
        int nodeIndex = -1;
        int meshIndex = -1;
        int primitiveIndex = -1;
        bool enabled = true;
    };

    QString pathid;
    QString name;
    QString texPath;

    int isinit = 1;
    int loaded = 0; // 0=not loaded, 1=loaded, 2=loading/failed

    float size = 0.0f;
    float bound[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    QVector<State> state;

    QVector<MeshGpu*> meshes;
    QVector<DrawUnit> drawUnits;
    QVector<float*> nodeWorldMatrices; // owned (float[16])
    QVector<QString> nodeNames;
    QVector<int> nodeParents;

    bool requiresUpdate = false;
    QHash<unsigned int, QVector<RenderItem *>> renderItems;
    QHash<unsigned int, unsigned long long> renderItemsTextureHash;

    void cleanupGpu();
    void cleanupNodeMatrices();
    void cleanupRenderItems();

    bool parseAndBuild();
    void syncTextureAddresses();
    unsigned long long getTextureStateHash() const;
};

#endif /* GLTFSHAPE_H */

