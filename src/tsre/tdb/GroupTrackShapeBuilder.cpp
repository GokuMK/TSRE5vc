/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tdb/GroupTrackShapeBuilder.h>
#include <tsre/tdb/TrackShape.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/world/objects/GroupObj.h>
#include <tsre/world/objects/TrackObj.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/math3d/Vector3f.h>
#include <tsre/procedural/ComplexLine.h>
#include <QDebug>
#include <QHash>
#include <QSet>
#include <QString>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
static const float kGroupTrackSiteTolerance = 0.05f;
static const float kGroupTrackConnectionTolerance = 1.25f;
static const float kGroupTrackDirectionDotThreshold = -0.9f;

struct GroupTrackSite {
    float position[3] = {0, 0, 0};
    QSet<int> objectIndices;
    QVector<int> edgeIds;
    bool external = false;
};

struct GroupTrackEndpoint {
    int id = -1;
    int edgeId = -1;
    bool atStart = false;
    int objectIndex = -1;
    float position[3] = {0, 0, 0};
    float direction[3] = {0, 0, 1};
};

struct GroupTrackLocalSite {
    int root = -1;
    int objectIndex = -1;
    QVector<int> endpointIds;
    float position[3] = {0, 0, 0};
};

struct GroupTrackEdge {
    int id = -1;
    int objectIndex = -1;
    int startSite = -1;
    int endSite = -1;
    float startPosition[3] = {0, 0, 0};
    float endPosition[3] = {0, 0, 0};
    float startDirection[3] = {0, 0, 1};
    float reverseStartDirection[3] = {0, 0, 1};
    float length = 0;
    QVector<unsigned int> forwardSections;
    QVector<unsigned int> reverseSections;
};

struct GroupTrackRoute {
    int startSite = -1;
    int endSite = -1;
    float startPosition[3] = {0, 0, 0};
    float startDirection[3] = {0, 0, 1};
    float length = 0;
    QVector<int> edgeIds;
    QVector<bool> reversed;
    QVector<unsigned int> sections;
};

struct GroupTrackSiteMatch {
    int left = -1;
    int right = -1;
    float cost = 0;
    int leftEndpoint = -1;
    int rightEndpoint = -1;
};

struct GroupTrackMatchingResult {
    int matches = 0;
    float cost = 0;
    QVector<GroupTrackSiteMatch> pairs;
};

static float absoluteX(int tileX, float localX) {
    return tileX * 2048.0f + localX;
}

static float absoluteZFromWorld(int tileZ, float localZ) {
    return tileZ * 2048.0f + localZ;
}

static float trackShapeRotDegFromDirection(const float* direction) {
    if (fabs(direction[0]) < 0.0001f && fabs(direction[2]) < 0.0001f)
        return 0.0f;
    return atan2(-direction[0], direction[2]) * 180.0 / M_PI;
}

static void directionFromDrawAngle(float* out, float angle) {
    out[0] = -sin(angle);
    out[1] = 0.0f;
    out[2] = cos(angle);
}

static float squaredDistance3(const float* left, const float* right) {
    float dx = left[0] - right[0];
    float dy = left[1] - right[1];
    float dz = left[2] - right[2];
    return dx * dx + dy * dy + dz * dz;
}

static void extractTrackTdbAngles(const float* q, float* qe) {
    float vect[3] = {0, 0, 10};
    Vec3::transformQuat(vect, vect, (float*)q);

    float sinv = 2 * (q[0] * q[2] - q[1] * q[3]);
    if (sinv > 1.0f)
        sinv = 1.0f;
    if (sinv < -1.0f)
        sinv = -1.0f;

    float pitch = asin(sinv);
    if (vect[2] < 0)
        pitch = M_PI - pitch;
    if (vect[2] == 0 && vect[0] < 0)
        pitch = M_PI / 2;
    if (vect[2] == 0 && vect[0] > 0)
        pitch = -M_PI / 2;

    sinv = vect[1] / 10.0f;
    if (sinv > 1.0f)
        sinv = 1.0f;
    if (sinv < -1.0f)
        sinv = -1.0f;

    qe[0] = asin(sinv);
    qe[1] = pitch;
    qe[2] = 0.0f;
}

static void transformTrackLocalPointToRelativeWorld(float* out, const float* localPoint, const float* qe, const float* baseRelative) {
    Vector3f point(localPoint[0], localPoint[1], localPoint[2]);
    point.rotateX(-qe[0], 0);
    point.rotateY(-qe[1], 0);
    out[0] = baseRelative[0] + point.x;
    out[1] = baseRelative[1] - point.y;
    out[2] = baseRelative[2] - point.z;
}

static void inverseTrackRelativeWorldPointToLocal(float* out, const float* relativeWorldPoint, const float* qe) {
    Vector3f point(relativeWorldPoint[0], -relativeWorldPoint[1], -relativeWorldPoint[2]);
    point.rotateY(qe[1], 0);
    point.rotateX(qe[0], 0);
    out[0] = point.x;
    out[1] = point.y;
    out[2] = point.z;
}

static void transformTrackLocalDirectionToRelativeWorld(float* out, const float* localDirection, const float* qe) {
    float origin[3] = {0, 0, 0};
    float endpoint[3] = {localDirection[0], localDirection[1], localDirection[2]};
    float worldOrigin[3] = {0, 0, 0};
    float worldEndpoint[3] = {0, 0, 0};
    float base[3] = {0, 0, 0};
    transformTrackLocalPointToRelativeWorld(worldOrigin, origin, qe, base);
    transformTrackLocalPointToRelativeWorld(worldEndpoint, endpoint, qe, base);
    Vec3::sub(out, worldEndpoint, worldOrigin);
    if (Vec3::length(out) < 0.0001f)
        Vec3::set(out, 0, 0, 1);
    else
        Vec3::normalize(out, out);
}

static void inverseTrackRelativeWorldDirectionToLocal(float* out, const float* relativeWorldDirection, const float* qe) {
    float origin[3] = {0, 0, 0};
    float endpoint[3] = {relativeWorldDirection[0], relativeWorldDirection[1], relativeWorldDirection[2]};
    float localOrigin[3] = {0, 0, 0};
    float localEndpoint[3] = {0, 0, 0};
    inverseTrackRelativeWorldPointToLocal(localOrigin, origin, qe);
    inverseTrackRelativeWorldPointToLocal(localEndpoint, endpoint, qe);
    Vec3::sub(out, localEndpoint, localOrigin);
    if (Vec3::length(out) < 0.0001f)
        Vec3::set(out, 0, 0, 1);
    else
        Vec3::normalize(out, out);
}

static int findReverseSectionId(TSectionDAT* tsection, QHash<int, int>& cache, int sectionId) {
    if (cache.contains(sectionId))
        return cache[sectionId];

    TSection* section = tsection->sekcja[sectionId];
    if (section == NULL) {
        cache[sectionId] = sectionId;
        return sectionId;
    }

    if (section->type == 0) {
        cache[sectionId] = sectionId;
        return sectionId;
    }

    if (section->type == 1) {
        for (auto it = tsection->sekcja.begin(); it != tsection->sekcja.end(); ++it) {
            TSection* candidate = it->second;
            if (candidate == NULL || candidate->type != 1)
                continue;
            if (fabs(candidate->radius - section->radius) > 0.001f)
                continue;
            if (fabs(candidate->angle + section->angle) > 0.0001f)
                continue;
            cache[sectionId] = candidate->id;
            return candidate->id;
        }
    }

    cache[sectionId] = sectionId;
    return sectionId;
}

static void buildTrackPathSections(TSectionDAT* tsection, TrackShape::SectionIdx* path, QVector<TSection>& sections) {
    sections.clear();
    for (int i = 0; i < path->n; i++) {
        TSection* section = tsection->sekcja[(int)path->sect[i]];
        if (section != NULL)
            sections.push_back(*section);
    }
}

static bool buildTrackPathEdge(
        TSectionDAT* tsection,
        TrackObj* track,
        int objectIndex,
        int pathIndex,
        QHash<int, int>& reverseSections,
        const float* originAbsolute,
        GroupTrackEdge& edge) {

    TrackShape* shape = tsection->shape[track->sectionIdx];
    if (shape == NULL || pathIndex < 0 || pathIndex >= shape->numpaths)
        return false;

    TrackShape::SectionIdx* path = &shape->path[pathIndex];
    QVector<TSection> sections;
    buildTrackPathSections(tsection, path, sections);
    if (sections.size() != path->n)
        return false;

    ComplexLine line;
    line.init(sections);
    float startPoint[6] = {0, 0, 0, 0, 0, 0};
    float endPoint[6] = {0, 0, 0, 0, 0, 0};
    line.getDrawPosition(startPoint, 0.0f);
    line.getDrawPosition(endPoint, line.getLength());

    float pathRotRad = path->rotDeg * M_PI / 180.0f;
    float startShapeLocal[3] = {path->pos[0], -path->pos[1], path->pos[2]};
    float endShapeLocal[3] = {endPoint[0], -endPoint[1], endPoint[2]};
    Vector3f endShapeVec(endShapeLocal[0], endShapeLocal[1], endShapeLocal[2]);
    endShapeVec.rotateY(pathRotRad, 0);
    endShapeLocal[0] = startShapeLocal[0] + endShapeVec.x;
    endShapeLocal[1] = startShapeLocal[1] + endShapeVec.y;
    endShapeLocal[2] = startShapeLocal[2] + endShapeVec.z;

    float startShapeDirection[3];
    float endShapeDirection[3];
    directionFromDrawAngle(startShapeDirection, pathRotRad + startPoint[4]);
    directionFromDrawAngle(endShapeDirection, pathRotRad + endPoint[4]);

    float qe[3];
    extractTrackTdbAngles((float*)track->qDirection, qe);
    float trackRelative[3] = {
        absoluteX(track->x, track->position[0]) - originAbsolute[0],
        track->position[1] - originAbsolute[1],
        absoluteZFromWorld(track->y, track->position[2]) - originAbsolute[2]
    };

    float startRelative[3];
    float endRelative[3];
    float startWorldDirection[3];
    float endWorldDirection[3];
    transformTrackLocalPointToRelativeWorld(startRelative, startShapeLocal, qe, trackRelative);
    transformTrackLocalPointToRelativeWorld(endRelative, endShapeLocal, qe, trackRelative);
    transformTrackLocalDirectionToRelativeWorld(startWorldDirection, startShapeDirection, qe);
    transformTrackLocalDirectionToRelativeWorld(endWorldDirection, endShapeDirection, qe);

    Vec3::copy(edge.startPosition, (float*)startRelative);
    Vec3::copy(edge.endPosition, (float*)endRelative);

    Vec3::copy(edge.startDirection, (float*)startWorldDirection);
    Vec3::copy(edge.reverseStartDirection, (float*)endWorldDirection);
    Vec3::scale(edge.reverseStartDirection, edge.reverseStartDirection, -1.0f);

    if (Vec3::length(edge.startDirection) < 0.0001f)
        Vec3::set(edge.startDirection, 0, 0, -1);
    else
        Vec3::normalize(edge.startDirection, edge.startDirection);
    if (Vec3::length(edge.reverseStartDirection) < 0.0001f)
        Vec3::set(edge.reverseStartDirection, 0, 0, 1);
    else
        Vec3::normalize(edge.reverseStartDirection, edge.reverseStartDirection);

    edge.objectIndex = objectIndex;
    edge.length = line.getLength();
    for (int i = 0; i < path->n; i++)
        edge.forwardSections.push_back(path->sect[i]);
    for (int i = path->n - 1; i >= 0; i--)
        edge.reverseSections.push_back(findReverseSectionId(tsection, reverseSections, path->sect[i]));
    return true;
}

static int findGroupTrackSite(QVector<GroupTrackSite>& sites, const float* position, int objectIndex) {
    float bestDistSq = std::numeric_limits<float>::max();
    int bestSite = -1;
    for (int i = 0; i < sites.size(); i++) {
        float dx = sites[i].position[0] - position[0];
        float dy = sites[i].position[1] - position[1];
        float dz = sites[i].position[2] - position[2];
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            bestSite = i;
        }
        if (distSq <= kGroupTrackSiteTolerance * kGroupTrackSiteTolerance) {
            sites[i].objectIndices.insert(objectIndex);
            return i;
        }
    }

    if (bestSite >= 0 && bestDistSq <= 1.0f) {
        qDebug() << "GroupTrackShapeBuilder: near site miss dist" << sqrt(bestDistSq)
                 << "newPos" << position[0] << position[1] << position[2]
                 << "nearestSite" << bestSite
                 << "nearestPos" << sites[bestSite].position[0]
                 << sites[bestSite].position[1]
                 << sites[bestSite].position[2];
    }

    sites.push_back(GroupTrackSite());
    Vec3::copy(sites.back().position, (float*)position);
    sites.back().objectIndices.insert(objectIndex);
    return sites.size() - 1;
}

static int findGroupTrackRoot(QVector<int>& parents, int id) {
    if (parents[id] == id)
        return id;
    parents[id] = findGroupTrackRoot(parents, parents[id]);
    return parents[id];
}

static void unionGroupTrackRoots(QVector<int>& parents, int left, int right) {
    int rootLeft = findGroupTrackRoot(parents, left);
    int rootRight = findGroupTrackRoot(parents, right);
    if (rootLeft != rootRight)
        parents[rootRight] = rootLeft;
}

static void buildGroupTrackEndpoints(const QVector<GroupTrackEdge>& edges, QVector<GroupTrackEndpoint>& endpoints) {
    endpoints.clear();
    endpoints.reserve(edges.size() * 2);
    for (int i = 0; i < edges.size(); i++) {
        GroupTrackEndpoint start;
        start.id = endpoints.size();
        start.edgeId = edges[i].id;
        start.atStart = true;
        start.objectIndex = edges[i].objectIndex;
        Vec3::copy(start.position, (float*)edges[i].startPosition);
        Vec3::copy(start.direction, (float*)edges[i].startDirection);
        endpoints.push_back(start);

        GroupTrackEndpoint end;
        end.id = endpoints.size();
        end.edgeId = edges[i].id;
        end.atStart = false;
        end.objectIndex = edges[i].objectIndex;
        Vec3::copy(end.position, (float*)edges[i].endPosition);
        Vec3::copy(end.direction, (float*)edges[i].reverseStartDirection);
        endpoints.push_back(end);
    }
}

static void mergeCoincidentEndpoints(const QVector<GroupTrackEndpoint>& endpoints, QVector<int>& parents) {
    for (int i = 0; i < endpoints.size(); i++) {
        for (int j = i + 1; j < endpoints.size(); j++) {
            if (endpoints[i].objectIndex != endpoints[j].objectIndex)
                continue;
            if (squaredDistance3((float*)endpoints[i].position, (float*)endpoints[j].position) <= kGroupTrackSiteTolerance * kGroupTrackSiteTolerance)
                unionGroupTrackRoots(parents, i, j);
        }
    }
}

static void buildLocalSites(
        const QVector<GroupTrackEndpoint>& endpoints,
        QVector<int>& parents,
        QVector<GroupTrackLocalSite>& localSites,
        QHash<int, QVector<int>>& localSitesByObject) {

    localSites.clear();
    localSitesByObject.clear();
    QHash<int, int> localSiteIndexByRoot;

    for (int i = 0; i < endpoints.size(); i++) {
        int root = findGroupTrackRoot(parents, i);
        int localSiteIndex = localSiteIndexByRoot.value(root, -1);
        if (localSiteIndex < 0) {
            localSiteIndex = localSites.size();
            localSiteIndexByRoot[root] = localSiteIndex;
            localSites.push_back(GroupTrackLocalSite());
            localSites.back().root = root;
            localSites.back().objectIndex = endpoints[i].objectIndex;
            localSitesByObject[endpoints[i].objectIndex].push_back(localSiteIndex);
        }
        localSites[localSiteIndex].endpointIds.push_back(i);
    }

    for (int i = 0; i < localSites.size(); i++) {
        Vec3::set(localSites[i].position, 0, 0, 0);
        for (int j = 0; j < localSites[i].endpointIds.size(); j++)
            Vec3::add(localSites[i].position, localSites[i].position, (float*)endpoints[localSites[i].endpointIds[j]].position);
        Vec3::scale(localSites[i].position, localSites[i].position, 1.0f / localSites[i].endpointIds.size());
    }
}

static bool buildGroupTrackSiteMatch(
        const GroupTrackLocalSite& left,
        const GroupTrackLocalSite& right,
        const QVector<GroupTrackEndpoint>& endpoints,
        GroupTrackSiteMatch& match) {

    float bestCost = std::numeric_limits<float>::max();
    bool found = false;
    for (int i = 0; i < left.endpointIds.size(); i++) {
        const GroupTrackEndpoint& leftEndpoint = endpoints[left.endpointIds[i]];
        for (int j = 0; j < right.endpointIds.size(); j++) {
            const GroupTrackEndpoint& rightEndpoint = endpoints[right.endpointIds[j]];
            float distSq = squaredDistance3((float*)leftEndpoint.position, (float*)rightEndpoint.position);
            if (distSq > kGroupTrackConnectionTolerance * kGroupTrackConnectionTolerance)
                continue;
            float dot = Vec3::dot((float*)leftEndpoint.direction, (float*)rightEndpoint.direction);
            if (dot > kGroupTrackDirectionDotThreshold)
                continue;
            float cost = sqrt(distSq) + (dot + 1.0f);
            if (cost < bestCost) {
                bestCost = cost;
                match.left = -1;
                match.right = -1;
                match.cost = cost;
                match.leftEndpoint = leftEndpoint.id;
                match.rightEndpoint = rightEndpoint.id;
                found = true;
            }
        }
    }
    return found;
}

static GroupTrackMatchingResult solveGroupTrackSiteMatchingRecursive(
        int index,
        const QVector<QVector<GroupTrackSiteMatch>>& candidateMatrix,
        quint64 usedMask) {

    GroupTrackMatchingResult best;
    if (index >= candidateMatrix.size())
        return best;

    best = solveGroupTrackSiteMatchingRecursive(index + 1, candidateMatrix, usedMask);

    for (int j = 0; j < candidateMatrix[index].size(); j++) {
        quint64 bit = (quint64)1 << j;
        if ((usedMask & bit) != 0)
            continue;
        if (candidateMatrix[index][j].leftEndpoint < 0)
            continue;

        GroupTrackMatchingResult attempt = solveGroupTrackSiteMatchingRecursive(index + 1, candidateMatrix, usedMask | bit);
        attempt.matches += 1;
        attempt.cost += candidateMatrix[index][j].cost;
        attempt.pairs.push_back(candidateMatrix[index][j]);
        if (attempt.matches > best.matches || (attempt.matches == best.matches && attempt.cost < best.cost))
            best = attempt;
    }
    return best;
}

static void matchEndpointsBetweenObjects(
        const QVector<GroupTrackEndpoint>& endpoints,
        QVector<int>& parents,
        QVector<GroupTrackLocalSite>& localSites,
        QHash<int, QVector<int>>& localSitesByObject) {

    QList<int> objectIds = localSitesByObject.keys();
    std::sort(objectIds.begin(), objectIds.end());
    for (int objectIdxA = 0; objectIdxA < objectIds.size(); objectIdxA++) {
        for (int objectIdxB = objectIdxA + 1; objectIdxB < objectIds.size(); objectIdxB++) {
            const QVector<int>& siteIdsA = localSitesByObject[objectIds[objectIdxA]];
            const QVector<int>& siteIdsB = localSitesByObject[objectIds[objectIdxB]];
            if (siteIdsA.size() == 0 || siteIdsB.size() == 0 || siteIdsB.size() > 63)
                continue;

            QVector<QVector<GroupTrackSiteMatch>> candidateMatrix;
            candidateMatrix.resize(siteIdsA.size());
            bool hasAnyCandidate = false;
            for (int i = 0; i < siteIdsA.size(); i++) {
                candidateMatrix[i].resize(siteIdsB.size());
                for (int j = 0; j < siteIdsB.size(); j++) {
                    GroupTrackSiteMatch match;
                    if (buildGroupTrackSiteMatch(localSites[siteIdsA[i]], localSites[siteIdsB[j]], endpoints, match)) {
                        match.left = siteIdsA[i];
                        match.right = siteIdsB[j];
                        candidateMatrix[i][j] = match;
                        hasAnyCandidate = true;
                    }
                }
            }
            if (!hasAnyCandidate)
                continue;

            GroupTrackMatchingResult result = solveGroupTrackSiteMatchingRecursive(0, candidateMatrix, 0);
            for (int i = 0; i < result.pairs.size(); i++) {
                qDebug() << "GroupTrackShapeBuilder: object match"
                         << objectIds[objectIdxA] << result.pairs[i].left
                         << "<->"
                         << objectIds[objectIdxB] << result.pairs[i].right
                         << "cost" << result.pairs[i].cost;
                unionGroupTrackRoots(parents, result.pairs[i].leftEndpoint, result.pairs[i].rightEndpoint);
            }
        }
    }
}

static QString routeSequenceKey(const QVector<int>& edgeIds, const QVector<bool>& reversed) {
    QString key;
    for (int i = 0; i < edgeIds.size(); i++) {
        key += QString::number(edgeIds[i]);
        key += reversed[i] ? "r" : "f";
        key += ",";
    }
    return key;
}

static QString canonicalRouteKey(const QVector<int>& edgeIds, const QVector<bool>& reversed) {
    QString forwardKey = routeSequenceKey(edgeIds, reversed);
    QVector<int> reverseEdgeIds;
    QVector<bool> reverseFlags;
    for (int i = edgeIds.size() - 1; i >= 0; i--) {
        reverseEdgeIds.push_back(edgeIds[i]);
        reverseFlags.push_back(!reversed[i]);
    }
    QString backwardKey = routeSequenceKey(reverseEdgeIds, reverseFlags);
    return forwardKey < backwardKey ? forwardKey : backwardKey;
}

static void collectGroupTrackRoutesRecursive(
        int currentSite,
        int startSite,
        const QVector<GroupTrackSite>& sites,
        const QVector<GroupTrackEdge>& edges,
        QSet<int>& usedEdges,
        GroupTrackRoute route,
        QSet<QString>& routeKeys,
        QVector<GroupTrackRoute>& routes) {

    if (currentSite != startSite && sites[currentSite].external) {
        QString key = canonicalRouteKey(route.edgeIds, route.reversed);
        if (!routeKeys.contains(key)) {
            routeKeys.insert(key);
            route.endSite = currentSite;
            routes.push_back(route);
        }
        return;
    }

    for (int i = 0; i < sites[currentSite].edgeIds.size(); i++) {
        int edgeId = sites[currentSite].edgeIds[i];
        if (usedEdges.contains(edgeId))
            continue;

        const GroupTrackEdge& edge = edges[edgeId];
        bool reversed = false;
        int nextSite = -1;
        if (edge.startSite == currentSite) {
            nextSite = edge.endSite;
        } else if (edge.endSite == currentSite) {
            reversed = true;
            nextSite = edge.startSite;
        } else {
            continue;
        }

        GroupTrackRoute nextRoute = route;
        if (nextRoute.edgeIds.size() == 0) {
            if (reversed) {
                Vec3::copy(nextRoute.startPosition, (float*)edge.endPosition);
                Vec3::copy(nextRoute.startDirection, (float*)edge.reverseStartDirection);
            } else {
                Vec3::copy(nextRoute.startPosition, (float*)edge.startPosition);
                Vec3::copy(nextRoute.startDirection, (float*)edge.startDirection);
            }
            nextRoute.startSite = startSite;
        }

        nextRoute.edgeIds.push_back(edgeId);
        nextRoute.reversed.push_back(reversed);
        nextRoute.length += edge.length;

        const QVector<unsigned int>& sections = reversed ? edge.reverseSections : edge.forwardSections;
        for (int j = 0; j < sections.size(); j++)
            nextRoute.sections.push_back(sections[j]);

        usedEdges.insert(edgeId);
        collectGroupTrackRoutesRecursive(nextSite, startSite, sites, edges, usedEdges, nextRoute, routeKeys, routes);
        usedEdges.remove(edgeId);
    }
}

static void collectGroupTrackRoutes(const QVector<GroupTrackSite>& sites, const QVector<GroupTrackEdge>& edges, QVector<GroupTrackRoute>& routes) {
    routes.clear();
    QSet<QString> routeKeys;
    for (int siteId = 0; siteId < sites.size(); siteId++) {
        if (!sites[siteId].external)
            continue;
        QSet<int> usedEdges;
        GroupTrackRoute route;
        collectGroupTrackRoutesRecursive(siteId, siteId, sites, edges, usedEdges, route, routeKeys, routes);
    }
}

static bool compareGroupTrackRoute(const GroupTrackRoute& left, const GroupTrackRoute& right) {
    QString leftKey = canonicalRouteKey(left.edgeIds, left.reversed);
    QString rightKey = canonicalRouteKey(right.edgeIds, right.reversed);
    if (leftKey != rightKey) return leftKey < rightKey;
    if (left.length != right.length) return left.length < right.length;
    float leftRot = trackShapeRotDegFromDirection((float*)left.startDirection);
    float rightRot = trackShapeRotDegFromDirection((float*)right.startDirection);
    if (leftRot != rightRot) return leftRot < rightRot;
    if (left.startPosition[0] != right.startPosition[0]) return left.startPosition[0] < right.startPosition[0];
    if (left.startPosition[1] != right.startPosition[1]) return left.startPosition[1] < right.startPosition[1];
    return left.startPosition[2] < right.startPosition[2];
}
}

GroupTrackShapeBuilder::Result::~Result() {
    clear();
}

void GroupTrackShapeBuilder::Result::clear() {
    if (shape != NULL) {
        delete[] shape->path;
        delete shape;
        shape = NULL;
    }
    endpoints.clear();
}

bool GroupTrackShapeBuilder::Result::isValid() const {
    return shape != NULL && shape->path != NULL && shape->numpaths > 0;
}

TrackShape* GroupTrackShapeBuilder::Result::releaseShape() {
    TrackShape* released = shape;
    shape = NULL;
    return released;
}

bool GroupTrackShapeBuilder::build(Result& out, GroupObj* group, TrackObj* anchor, TSectionDAT* tsection) {
    out.clear();
    if (group == NULL || anchor == NULL || tsection == NULL)
        return false;

    float anchorAbsolute[3] = {
        absoluteX(anchor->x, anchor->position[0]),
        anchor->position[1],
        absoluteZFromWorld(anchor->y, anchor->position[2])
    };
    float originAbsolute[3] = {
        anchorAbsolute[0],
        anchorAbsolute[1],
        anchorAbsolute[2]
    };
    float anchorQe[3];
    extractTrackTdbAngles((float*)anchor->qDirection, anchorQe);

    QVector<GroupTrackEdge> edges;
    QHash<int, int> reverseSections;
    int trackObjectCount = 0;

    for (int objectIndex = 0; objectIndex < group->objects.size(); objectIndex++) {
        WorldObj* child = group->objects[objectIndex];
        if (child == NULL || child->typeID != WorldObj::trackobj)
            continue;
        if (tsection->isRoadShape(child->sectionIdx))
            continue;

        TrackObj* track = (TrackObj*)child;
        TrackShape* childShape = tsection->shape[track->sectionIdx];
        if (childShape == NULL || childShape->numpaths <= 0)
            return false;

        trackObjectCount++;
        for (int pathIndex = 0; pathIndex < childShape->numpaths; pathIndex++) {
            GroupTrackEdge edge;
            edge.id = edges.size();
            if (!buildTrackPathEdge(tsection, track, objectIndex, pathIndex, reverseSections, originAbsolute, edge))
                return false;
            edges.push_back(edge);
        }
    }

    if (trackObjectCount == 0 || edges.size() == 0)
        return false;

    QVector<GroupTrackEndpoint> endpoints;
    buildGroupTrackEndpoints(edges, endpoints);
    QVector<int> endpointParents;
    endpointParents.resize(endpoints.size());
    for (int i = 0; i < endpointParents.size(); i++)
        endpointParents[i] = i;

    mergeCoincidentEndpoints(endpoints, endpointParents);

    QVector<GroupTrackLocalSite> localSites;
    QHash<int, QVector<int>> localSitesByObject;
    buildLocalSites(endpoints, endpointParents, localSites, localSitesByObject);
    matchEndpointsBetweenObjects(endpoints, endpointParents, localSites, localSitesByObject);
    buildLocalSites(endpoints, endpointParents, localSites, localSitesByObject);

    QVector<GroupTrackSite> sites;
    QHash<int, int> siteByRoot;
    QVector<int> siteEndpointCounts;
    for (int i = 0; i < endpoints.size(); i++) {
        int root = findGroupTrackRoot(endpointParents, i);
        int siteIndex = siteByRoot.value(root, -1);
        if (siteIndex < 0) {
            siteIndex = sites.size();
            siteByRoot[root] = siteIndex;
            sites.push_back(GroupTrackSite());
            siteEndpointCounts.push_back(0);
        }
        Vec3::add(sites[siteIndex].position, sites[siteIndex].position, (float*)endpoints[i].position);
        sites[siteIndex].objectIndices.insert(endpoints[i].objectIndex);
        sites[siteIndex].edgeIds.push_back(endpoints[i].edgeId);
        siteEndpointCounts[siteIndex]++;
    }
    for (int i = 0; i < sites.size(); i++) {
        QSet<int> uniqueEdges = QSet<int>(sites[i].edgeIds.begin(), sites[i].edgeIds.end());
        sites[i].edgeIds = uniqueEdges.values();
        Vec3::scale(sites[i].position, sites[i].position, 1.0f / std::max(1, siteEndpointCounts[i]));
    }
    for (int i = 0; i < edges.size(); i++) {
        edges[i].startSite = siteByRoot[findGroupTrackRoot(endpointParents, i * 2)];
        edges[i].endSite = siteByRoot[findGroupTrackRoot(endpointParents, i * 2 + 1)];
    }
    for (int i = 0; i < sites.size(); i++)
        sites[i].external = sites[i].objectIndices.size() <= 1;

    qDebug() << "GroupTrackShapeBuilder: sites" << sites.size() << "edges" << edges.size();
    for (int i = 0; i < sites.size(); i++) {
        QString edgeList;
        for (int j = 0; j < sites[i].edgeIds.size(); j++) {
            if (!edgeList.isEmpty())
                edgeList += ",";
            edgeList += QString::number(sites[i].edgeIds[j]);
        }
        qDebug() << "GroupTrackShapeBuilder: site" << i
                 << "pos" << sites[i].position[0] << sites[i].position[1] << sites[i].position[2]
                 << "objects" << sites[i].objectIndices.size()
                 << "external" << sites[i].external
                 << "edges" << edgeList;
    }
    for (int i = 0; i < edges.size(); i++) {
        qDebug() << "GroupTrackShapeBuilder: edge" << i
                 << "object" << edges[i].objectIndex
                 << "sites" << edges[i].startSite << edges[i].endSite
                 << "start" << edges[i].startPosition[0] << edges[i].startPosition[1] << edges[i].startPosition[2]
                 << "end" << edges[i].endPosition[0] << edges[i].endPosition[1] << edges[i].endPosition[2]
                 << "sections" << edges[i].forwardSections.size()
                 << "length" << edges[i].length;
    }

    QVector<GroupTrackRoute> routes;
    collectGroupTrackRoutes(sites, edges, routes);
    if (routes.size() == 0)
        return false;

    std::sort(routes.begin(), routes.end(), compareGroupTrackRoute);
    qDebug() << "GroupTrackShapeBuilder: routes" << routes.size();
    for (int i = 0; i < routes.size(); i++) {
        qDebug() << "GroupTrackShapeBuilder: route" << i
                 << "sites" << routes[i].startSite << routes[i].endSite
                 << "start" << routes[i].startPosition[0] << routes[i].startPosition[1] << routes[i].startPosition[2]
                 << "rotDeg" << trackShapeRotDegFromDirection((float*)routes[i].startDirection)
                 << "sections" << routes[i].sections.size()
                 << "length" << routes[i].length;
    }

    out.shape = new TrackShape();
    out.shape->filename = "__group_trackshape__";
    out.shape->numpaths = routes.size();
    out.shape->path = new TrackShape::SectionIdx[out.shape->numpaths];

    for (int i = 0; i < routes.size(); i++) {
        TrackShape::SectionIdx& path = out.shape->path[i];
        if (routes[i].sections.size() <= 0 || routes[i].sections.size() > TrackShape::MaxSectionIdxCount) {
            out.clear();
            return false;
        }

        float startLocal[3] = {
            routes[i].startPosition[0] + originAbsolute[0] - anchorAbsolute[0],
            routes[i].startPosition[1] + originAbsolute[1] - anchorAbsolute[1],
            routes[i].startPosition[2] + originAbsolute[2] - anchorAbsolute[2]
        };
        float endLocal[3] = {
            sites[routes[i].endSite].position[0] + originAbsolute[0] - anchorAbsolute[0],
            sites[routes[i].endSite].position[1] + originAbsolute[1] - anchorAbsolute[1],
            sites[routes[i].endSite].position[2] + originAbsolute[2] - anchorAbsolute[2]
        };
        float startDirectionLocal[3];
        inverseTrackRelativeWorldPointToLocal(startLocal, startLocal, anchorQe);
        inverseTrackRelativeWorldPointToLocal(endLocal, endLocal, anchorQe);
        inverseTrackRelativeWorldDirectionToLocal(startDirectionLocal, routes[i].startDirection, anchorQe);

        path.n = routes[i].sections.size();
        path.pos[0] = startLocal[0];
        path.pos[1] = -startLocal[1];
        path.pos[2] = startLocal[2];
        path.rotDeg = trackShapeRotDegFromDirection((float*)startDirectionLocal);
        for (int j = 0; j < path.n; j++)
            path.sect[j] = routes[i].sections[j];

        out.endpoints.push_back({startLocal[0], startLocal[1], startLocal[2]});
        out.endpoints.push_back({endLocal[0], endLocal[1], endLocal[2]});
    }

    return out.isValid();
}
