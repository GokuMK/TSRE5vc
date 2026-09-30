/* This file is part of TSRE5. Licensed under GPL-3.0-or-later; see LICENSE.md. */
#ifndef TELEPOLEOBJ_H
#define TELEPOLEOBJ_H

#include <tsre/world/objects/WorldObj.h>
#include <array>
#include <optional>
#include <QVector>

class ComplexShape;
class OglObj;

class TelepoleObj : public WorldObj {
public:
    using Vector = std::array<float, 3>;

    TelepoleObj();
    TelepoleObj(const TelepoleObj &other);
    ~TelepoleObj() override;
    WorldObj* clone() override;
    void load(int x, int y) override;
    using WorldObj::set;
    void set(TS::TokenId token, FileBuffer* data) override;
    void set(QString token, FileBuffer* data) override;
    void set(QString token, QString value) override;
    void set(QString token, long long value) override;
    void save(QTextStream* out) override;
    bool allowNew() override { return true; }
    void pushRenderItems(float lod, float posx, float posz, float*, float*,
            float, quint32 selectionId) override;
    void render(GLUU*, float lod, float posx, float posz, float*, float*,
            float, quint32 selectionId, int renderMode) override;
    void updateSim(float deltaTime) override;
    void deleteVBO() override;
    bool select(int value) override;
    void setPosition(int tileX, int tileZ, float *position) override;
    void translate(float x, float y, float z) override;
    bool hasLinePoints() override;
    void getLinePoints(float *&points) override;

    bool setEndPosition(int tileX, int tileZ, const float *position);
    float spanLength() const;
    unsigned int populationValue() const;
    int configIndex() const;
    void setConfigIndex(int index);
    float separation() const;

private:
    std::optional<unsigned int> population, startType, endType, config, quality;
    // Native world fields stay in MSTS source coordinates. Rendering converts
    // them to TSRE's tile-local OpenGL coordinate convention on demand.
    std::optional<Vector> startPosition, endPosition, direction;
    std::optional<float> startDirection, endDirection, maxVisDistance;
    bool hasUid = false, hasPosition = false, hasVdbId = false;
    bool coordinatesConverted = false;
    bool newObjectRequested = false;
    int selectionValue = 0;

    QVector<Vector> polePoints;
    QVector<Vector> wirePathPoints;
    QVector<std::array<float, 16>> poleTransforms;
    QVector<OglObj*> wireObjects;
    std::array<float, 16> wireTransform{};
    ComplexShape *poleShape = nullptr;
    unsigned int poleShapeState = 0;
    QString loadedPoleShape;
    OglObj *endpointMarker = nullptr;
    OglObj *selectedEndpointMarker = nullptr;
    OglObj *guideLine = nullptr;
    bool generatedGeometryValid = false;
    bool wireGeometryInitialized = false;

    void readField(TS::TokenId token, FileBuffer* data, bool binary);
    void initializeNewObject();
    void recalculatePopulation();
    void updateMidpoint();
    void invalidateGeneratedGeometry(bool resetShape = false);
    void ensureDerivedPoints();
    void ensurePoleShape();
    void ensurePoleTransforms();
    void ensureWireGeometry();
    void ensureInteractiveGeometry();
    Vector internalPoint(const Vector &source) const;
    Vector sourcePoint(int tileX, int tileZ, const float *internal) const;
};

#endif
