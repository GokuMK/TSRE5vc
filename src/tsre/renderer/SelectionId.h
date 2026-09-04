/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TSRE_RENDERER_SELECTIONID_H
#define TSRE_RENDERER_SELECTIONID_H

#include <QtGlobal>

namespace SelectionIdCodec {

constexpr quint32 SelectorShift = 27;
constexpr quint32 SelectorMask = 0x1fu;
constexpr quint32 PayloadMask = 0x07ffffffu;
constexpr quint32 PartMask = 0x7ffu;
constexpr quint32 MaximumObjectIndex = 0xffffu;
constexpr quint32 MaximumPart = PartMask;
constexpr quint32 MaximumTerrainPatch = 0xfffu;
constexpr quint32 MaximumDatabaseItem = 0xffffffu;

enum class Kind : quint8 {
    None,
    WorldObject,
    Terrain,
    ActivityObject,
    DatabaseItem,
    ActivityService,
    Unknown
};

enum class DatabaseKind : quint8 {
    Track = 0,
    Road = 1
};

struct DecodedSelection {
    bool valid = false;
    Kind kind = Kind::Unknown;
    quint8 selector = 0;
    qint8 tileXOffset = 0;
    qint8 tileZOffset = 0;
    quint32 primaryId = 0;
    quint16 part = 0;
    quint16 patchId = 0;
    quint16 feature = 0;
    DatabaseKind databaseKind = DatabaseKind::Track;
    quint32 databaseItemId = 0;
};

bool tryWorldObject(int tileXOffset, int tileZOffset, int objectIndex,
                    int part, quint32 &id);
bool tryTerrain(int tileXOffset, int tileZOffset, int patchId,
                int feature, quint32 &id);
bool tryActivityObject(int activityId, int part, quint32 &id);
bool tryDatabaseItem(int databaseKind, qint64 itemId, quint32 &id);
bool tryActivityService(int serviceId, int part, quint32 &id);
bool tryWithPart(quint32 baseId, int part, quint32 &id);
bool tryWithTerrainPatch(quint32 baseId, int patchId, int feature,
                         quint32 &id);

quint32 worldObject(int tileXOffset, int tileZOffset, int objectIndex,
                    int part = 0);
quint32 terrain(int tileXOffset, int tileZOffset, int patchId,
                int feature = 0);
quint32 activityObject(int activityId, int part = 0);
quint32 databaseItem(DatabaseKind kind, qint64 itemId);
quint32 activityService(int serviceId, int part = 0);
quint32 withPart(quint32 baseId, int part);
quint32 withTerrainPatch(quint32 baseId, int patchId, int feature = 0);

DecodedSelection decode(quint32 id);
bool isKind(quint32 id, Kind kind);

} // namespace SelectionIdCodec

#endif // TSRE_RENDERER_SELECTIONID_H
