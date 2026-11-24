/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TRACKOBJ_H
#define	TRACKOBJ_H

#include <tsre/world/objects/WorldObj.h>
#include <QString>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/world/Ref.h>
#include <QSharedPointer>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <QHash>
#include <tsre/procedural/TrackProfileFilters.h>

class RenderQueue;

class OglObj;
class TrackShape;
struct ComplexTrackMeshes;
struct OrtsProfileRoll;
struct ProceduralPathTransform;

class TrackObj : public WorldObj  {
public:
    TrackObj();
    TrackObj(const TrackObj& o);
    WorldObj* clone();
    virtual ~TrackObj();
    bool allowNew();
    void setTemplate(QString val);
    void reload();
    void reloadProceduralProfile() override;
    void loadingFixes();
    void load(int x, int y);
    void loadInit();
    void set(TS::TokenId sh, FileBuffer* val);
    void set(QString sh, QString val);
    void set(QString sh, FileBuffer* data);
    void set(QString sh, long long int val);
    void rotate(float x, float y, float z);
    Ref::RefItem* getRefInfo();
    void save(QTextStream* out);
    QString getShapePath();
    int getDefaultDetailLevel();
    bool isSimilar(WorldObj* obj);
    int updateTrackSectionInfo(QHash<unsigned int, unsigned int> shapes, QHash<unsigned int, unsigned int> sect);
    void pushRenderItems(RenderQueue &queue, float lod, float posx, float posz, float* playerW, float* target, float fov, quint32 selectionId);
    bool castsShadows() override;
    QVector<std::array<float, 5>> jNodePosn;
    void fillJNodePosn();
    void setElevation(float prom);
    float getElevation();
    int getCollisionType();
    bool isRoad() const { return roadShape; }
    // Tracks task 17: where the object's procedural profile came from, as
    // the last drawing found it (the properties panel shows it).
    enum class ProfileSource {
        // Not drawn yet.
        Unknown,
        Static,
        // DISABLED on the object.
        ObjectDisabled,
        // A trackprofiles.dat filter's Profile ( DISABLED ).
        FilterDisabled,
        // Its own ShapeTemplate.
        ObjectTemplate,
        // A trackprofiles.dat filter.
        Filter,
        // Open Rails' guess.
        Guess,
        // The default profile (DefaultTrack, RdProfile, the built-in one).
        Default
    };
    ProfileSource profileSource() const { return proceduralSource; }
    // The profile's ID; for a filter its line, patterns and profile.
    QString profileSourceDetail() const { return proceduralSourceDetail; }
    // Tracks task 17 step 9: the path its junction is set to (a path of its
    // shape; -1 its MainRoute), moved to over SwitchMoveSeconds as Open
    // Rails' SwitchTrackShape moves it (animate false: at once). A static
    // junction shape poses its keyed parts (blades, levers, indicators)
    // between key 0 (MainRoute) and key 1; a procedural one its switches'
    // blades, indicators and hand levers. The simulation sets it later.
    static constexpr float SwitchMoveSeconds = 0.5f;
    void setSwitchRoute(int path, bool animate = true);
    int switchRouteSet() const { return switchRoute; }
private:
    float elevation;
    //unsigned int collideFunction;
    bool getSimpleBorder(float* border);
    bool getBoxPoints(QVector<float> &points);
    void clearProceduralShape();
    bool useProceduralShape();
    // Tracks task 17: the shape's complex groups from the family's COMPLEX
    // member (shared meshes, drawn with the object's matrix), its other
    // paths as plain track; false without a member or a complex group.
    bool useComplexShape(const OrtsTrackProfile &profile, OrtsTrackProfile::ObjectType type,
            const TrackProfileFilters::Filter *filter, const TrackShape &shape,
            const QVector<OrtsProfileRoll> &rolls,
            const QVector<QSharedPointer<const OrtsTrackProfile>> *pathProfiles,
            const QString &routePath, const ProceduralPathTransform &transform);
    QSharedPointer<ComplexTrackMeshes> complexShared;
    bool proceduralShapeInit = false;
    QVector<OglObj*> procShape;
    bool procShapeOwned = false;
    bool proceduralShapeUsesBakedPath = false;
    bool roadShape = false;
    bool proceduralFallback = false;
    ProfileSource proceduralSource = ProfileSource::Unknown;
    QString proceduralSourceDetail;
    // Step 9: the path set, the static shape's key and each procedural
    // switch's (0 its main path set, 1 its other), the clock they last moved
    // by, the static shape's own state once posed, and the blades blended
    // while a procedural switch moves.
    int switchRoute = -1;
    float shapeKey = 0;
    // The key the static shape was last posed at (none: -1; key 0 needs
    // no state of its own).
    float posedKey = 0;
    QVector<float> switchKeys;
    qint64 switchClock = -1;
    bool switchAtOnce = false;
    unsigned int poseState = 0;
    bool hasPoseState = false;
    QHash<int, QVector<OglObj*>> switchBlend;
    QHash<int, float> switchBlendKey;
    float shapeTarget() const;
    float switchTarget(int switchIndex) const;
    // The keys moved by the time since they last moved; the static shape
    // posed.
    void stepSwitches();
    void clearSwitchBlend();
};

#endif	/* TRACKOBJ_H */

