/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "SelectionId.h"

#include <QDebug>

namespace SelectionIdCodec {
namespace {

constexpr quint32 WorldObjectSelectorMinimum = 1;
constexpr quint32 WorldObjectSelectorMaximum = 9;
constexpr quint32 TerrainSelector = 10;
constexpr quint32 ActivityObjectSelector = 11;
constexpr quint32 DatabaseItemSelector = 12;
constexpr quint32 ActivityServiceSelector = 13;

bool validTileOffset(int value) {
    return value >= -1 && value <= 1;
}

bool validPrimaryId(int value) {
    return value >= 0
            && static_cast<quint32>(value) <= MaximumObjectIndex;
}

bool validPart(int value) {
    return value >= 0 && static_cast<quint32>(value) <= MaximumPart;
}

quint32 checkedResult(bool valid, quint32 id, const char *function) {
    if (valid)
        return id;
    qWarning() << function << "rejected an out-of-range selection field";
    Q_ASSERT_X(valid, function, "out-of-range selection field");
    return 0;
}

} // namespace

bool tryWorldObject(int tileXOffset, int tileZOffset, int objectIndex,
                    int part, quint32 &id) {
    id = 0;
    if (!validTileOffset(tileXOffset) || !validTileOffset(tileZOffset)
            || !validPrimaryId(objectIndex) || !validPart(part))
        return false;
    const quint32 selector = static_cast<quint32>(tileXOffset + 1) * 3u
            + static_cast<quint32>(tileZOffset + 1) + 1u;
    id = (selector << SelectorShift)
            | (static_cast<quint32>(objectIndex) << 11)
            | static_cast<quint32>(part);
    return true;
}

bool tryTerrain(int tileXOffset, int tileZOffset, int patchId,
                int feature, quint32 &id) {
    id = 0;
    if (!validTileOffset(tileXOffset) || !validTileOffset(tileZOffset)
            || patchId < 0
            || static_cast<quint32>(patchId) > MaximumTerrainPatch
            || !validPart(feature))
        return false;
    id = (TerrainSelector << SelectorShift)
            | (static_cast<quint32>(tileXOffset + 1) << 25)
            | (static_cast<quint32>(tileZOffset + 1) << 23)
            | (static_cast<quint32>(patchId) << 11)
            | static_cast<quint32>(feature);
    return true;
}

bool tryActivityObject(int activityId, int part, quint32 &id) {
    id = 0;
    if (!validPrimaryId(activityId) || !validPart(part))
        return false;
    id = (ActivityObjectSelector << SelectorShift)
            | (static_cast<quint32>(activityId) << 11)
            | static_cast<quint32>(part);
    return true;
}

bool tryDatabaseItem(int databaseKind, qint64 itemId, quint32 &id) {
    id = 0;
    if (databaseKind < static_cast<int>(DatabaseKind::Track)
            || databaseKind > static_cast<int>(DatabaseKind::Road)
            || itemId < 0
            || static_cast<quint64>(itemId) > MaximumDatabaseItem)
        return false;
    id = (DatabaseItemSelector << SelectorShift)
            | (static_cast<quint32>(databaseKind) << 24)
            | static_cast<quint32>(itemId);
    return true;
}

bool tryActivityService(int serviceId, int part, quint32 &id) {
    id = 0;
    if (!validPrimaryId(serviceId) || !validPart(part))
        return false;
    id = (ActivityServiceSelector << SelectorShift)
            | (static_cast<quint32>(serviceId) << 11)
            | static_cast<quint32>(part);
    return true;
}

DecodedSelection decode(quint32 id) {
    DecodedSelection decoded;
    decoded.selector = static_cast<quint8>((id >> SelectorShift)
                                           & SelectorMask);
    if (id == 0) {
        decoded.valid = true;
        decoded.kind = Kind::None;
        return decoded;
    }

    if (decoded.selector >= WorldObjectSelectorMinimum
            && decoded.selector <= WorldObjectSelectorMaximum) {
        decoded.valid = true;
        decoded.kind = Kind::WorldObject;
        const int tileIndex = decoded.selector - 1;
        decoded.tileXOffset = static_cast<qint8>(tileIndex / 3 - 1);
        decoded.tileZOffset = static_cast<qint8>(tileIndex % 3 - 1);
        decoded.primaryId = (id >> 11) & MaximumObjectIndex;
        decoded.part = static_cast<quint16>(id & PartMask);
        return decoded;
    }

    if (decoded.selector == TerrainSelector) {
        const quint32 xCode = (id >> 25) & 0x3u;
        const quint32 zCode = (id >> 23) & 0x3u;
        if (xCode == 3 || zCode == 3)
            return decoded;
        decoded.valid = true;
        decoded.kind = Kind::Terrain;
        decoded.tileXOffset = static_cast<qint8>(xCode) - 1;
        decoded.tileZOffset = static_cast<qint8>(zCode) - 1;
        decoded.patchId = static_cast<quint16>((id >> 11)
                                               & MaximumTerrainPatch);
        decoded.feature = static_cast<quint16>(id & PartMask);
        return decoded;
    }

    if (decoded.selector == ActivityObjectSelector
            || decoded.selector == ActivityServiceSelector) {
        decoded.valid = true;
        decoded.kind = decoded.selector == ActivityObjectSelector
                ? Kind::ActivityObject : Kind::ActivityService;
        decoded.primaryId = (id >> 11) & MaximumObjectIndex;
        decoded.part = static_cast<quint16>(id & PartMask);
        return decoded;
    }

    if (decoded.selector == DatabaseItemSelector) {
        const quint32 kind = (id >> 24) & 0x7u;
        if (kind > static_cast<quint32>(DatabaseKind::Road))
            return decoded;
        decoded.valid = true;
        decoded.kind = Kind::DatabaseItem;
        decoded.databaseKind = static_cast<DatabaseKind>(kind);
        decoded.databaseItemId = id & MaximumDatabaseItem;
        return decoded;
    }

    return decoded;
}

bool tryWithPart(quint32 baseId, int part, quint32 &id) {
    id = 0;
    if (baseId == 0)
        return true;
    if (!validPart(part))
        return false;
    const DecodedSelection decoded = decode(baseId);
    if (!decoded.valid
            || (decoded.kind != Kind::WorldObject
                && decoded.kind != Kind::ActivityObject
                && decoded.kind != Kind::ActivityService))
        return false;
    id = (baseId & ~PartMask) | static_cast<quint32>(part);
    return true;
}

bool tryWithTerrainPatch(quint32 baseId, int patchId, int feature,
                         quint32 &id) {
    id = 0;
    if (baseId == 0)
        return true;
    const DecodedSelection decoded = decode(baseId);
    if (!decoded.valid || decoded.kind != Kind::Terrain)
        return false;
    return tryTerrain(decoded.tileXOffset, decoded.tileZOffset,
                      patchId, feature, id);
}

quint32 worldObject(int tileXOffset, int tileZOffset, int objectIndex,
                    int part) {
    quint32 id = 0;
    const bool valid = tryWorldObject(tileXOffset, tileZOffset, objectIndex,
                                      part, id);
    return checkedResult(valid, id, "SelectionIdCodec::worldObject");
}

quint32 terrain(int tileXOffset, int tileZOffset, int patchId, int feature) {
    quint32 id = 0;
    const bool valid = tryTerrain(tileXOffset, tileZOffset, patchId,
                                  feature, id);
    return checkedResult(valid, id, "SelectionIdCodec::terrain");
}

quint32 activityObject(int activityId, int part) {
    quint32 id = 0;
    const bool valid = tryActivityObject(activityId, part, id);
    return checkedResult(valid, id,
                         "SelectionIdCodec::activityObject");
}

quint32 databaseItem(DatabaseKind kind, qint64 itemId) {
    quint32 id = 0;
    const bool valid = tryDatabaseItem(static_cast<int>(kind), itemId, id);
    return checkedResult(valid, id, "SelectionIdCodec::databaseItem");
}

quint32 activityService(int serviceId, int part) {
    quint32 id = 0;
    const bool valid = tryActivityService(serviceId, part, id);
    return checkedResult(valid, id,
                         "SelectionIdCodec::activityService");
}

quint32 withPart(quint32 baseId, int part) {
    quint32 id = 0;
    const bool valid = tryWithPart(baseId, part, id);
    return checkedResult(valid, id,
                         "SelectionIdCodec::withPart");
}

quint32 withTerrainPatch(quint32 baseId, int patchId, int feature) {
    quint32 id = 0;
    const bool valid = tryWithTerrainPatch(baseId, patchId, feature, id);
    return checkedResult(valid, id,
                         "SelectionIdCodec::withTerrainPatch");
}

bool isKind(quint32 id, Kind kind) {
    const DecodedSelection decoded = decode(id);
    return decoded.valid && decoded.kind == kind;
}

} // namespace SelectionIdCodec
