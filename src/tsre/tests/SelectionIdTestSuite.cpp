#include <tsre/tests/SelectionIdTestSuite.h>

#include <QDebug>

#include <tsre/renderer/SelectionId.h>

int TsreTests::runSelectionIdSuite(bool verbose) {
    using namespace SelectionIdCodec;

    int passed = 0;
    int failed = 0;
    auto check = [&](bool condition, const char *name) {
        if (condition) {
            ++passed;
            if (verbose)
                qInfo() << "[tests:selection-id] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:selection-id] FAIL" << name;
        }
    };

    bool worldRoundTrips = true;
    const int worldParts[] = {0, 15, 16, 255, 256, 2047};
    for (int x = -1; x <= 1; ++x) {
        for (int z = -1; z <= 1; ++z) {
            for (int objectIndex : {0, 65535}) {
                for (int part : worldParts) {
                    quint32 id = 0;
                    const bool packed = tryWorldObject(
                                x, z, objectIndex, part, id);
                    const DecodedSelection decoded = decode(id);
                    worldRoundTrips = worldRoundTrips && packed
                            && decoded.valid
                            && decoded.kind == Kind::WorldObject
                            && decoded.tileXOffset == x
                            && decoded.tileZOffset == z
                            && decoded.primaryId
                               == static_cast<quint32>(objectIndex)
                            && decoded.part == part;
                }
            }
        }
    }
    check(worldRoundTrips,
          "world-all-tile-slots-index-and-part-boundaries-round-trip");

    quint32 rejectedId = 99;
    check(!tryWorldObject(-2, 0, 0, 0, rejectedId) && rejectedId == 0
          && !tryWorldObject(0, 2, 0, 0, rejectedId)
          && !tryWorldObject(0, 0, -1, 0, rejectedId)
          && !tryWorldObject(0, 0, 65536, 0, rejectedId)
          && !tryWorldObject(0, 0, 0, -1, rejectedId)
          && !tryWorldObject(0, 0, 0, 2048, rejectedId),
          "world-overflow-is-rejected-without-truncation");

    const quint32 firstDenseObject = worldObject(0, 0, 0, 0);
    const quint32 secondDenseObject = worldObject(0, 0, 1, 0);
    check(firstDenseObject != secondDenseObject
          && decode(firstDenseObject).primaryId == 0
          && decode(secondDenseObject).primaryId == 1,
          "world-selection-uses-dense-index-not-persistent-uid");

    bool terrainRoundTrips = true;
    const int terrainPatches[] = {0, 255, 256, 1023, 1024, 4095};
    for (int x = -1; x <= 1; ++x) {
        for (int z = -1; z <= 1; ++z) {
            for (int patchId : terrainPatches) {
                for (int feature : {0, 2047}) {
                    quint32 id = 0;
                    const bool packed = tryTerrain(
                                x, z, patchId, feature, id);
                    const DecodedSelection decoded = decode(id);
                    terrainRoundTrips = terrainRoundTrips && packed
                            && decoded.valid
                            && decoded.kind == Kind::Terrain
                            && decoded.tileXOffset == x
                            && decoded.tileZOffset == z
                            && decoded.patchId == patchId
                            && decoded.feature == feature;
                }
            }
        }
    }
    check(terrainRoundTrips,
          "terrain-location-patch-and-feature-boundaries-round-trip");
    check(!tryTerrain(-2, 0, 0, 0, rejectedId)
          && !tryTerrain(0, 2, 0, 0, rejectedId)
          && !tryTerrain(0, 0, -1, 0, rejectedId)
          && !tryTerrain(0, 0, 4096, 0, rejectedId)
          && !tryTerrain(0, 0, 0, -1, rejectedId)
          && !tryTerrain(0, 0, 0, 2048, rejectedId),
          "terrain-overflow-is-rejected-without-truncation");

    bool activityRoundTrips = true;
    for (int primary : {0, 65535}) {
        for (int part : {0, 2047}) {
            quint32 objectId = 0;
            quint32 serviceId = 0;
            activityRoundTrips = activityRoundTrips
                    && tryActivityObject(primary, part, objectId)
                    && tryActivityService(primary, part, serviceId);
            const DecodedSelection object = decode(objectId);
            const DecodedSelection service = decode(serviceId);
            activityRoundTrips = activityRoundTrips
                    && object.valid && object.kind == Kind::ActivityObject
                    && object.primaryId == static_cast<quint32>(primary)
                    && object.part == part
                    && service.valid && service.kind == Kind::ActivityService
                    && service.primaryId == static_cast<quint32>(primary)
                    && service.part == part;
        }
    }
    check(activityRoundTrips,
          "activity-object-and-service-boundaries-round-trip");
    check(!tryActivityObject(-1, 0, rejectedId)
          && !tryActivityObject(65536, 0, rejectedId)
          && !tryActivityObject(0, 2048, rejectedId)
          && !tryActivityService(-1, 0, rejectedId)
          && !tryActivityService(65536, 0, rejectedId)
          && !tryActivityService(0, 2048, rejectedId),
          "activity-overflow-is-rejected-without-truncation");

    bool databaseRoundTrips = true;
    const qint64 databaseItems[] = {0, 65535, 65536, 16777215};
    for (int kind : {0, 1}) {
        for (qint64 itemId : databaseItems) {
            quint32 id = 0;
            const bool packed = tryDatabaseItem(kind, itemId, id);
            const DecodedSelection decoded = decode(id);
            databaseRoundTrips = databaseRoundTrips && packed
                    && decoded.valid
                    && decoded.kind == Kind::DatabaseItem
                    && static_cast<int>(decoded.databaseKind) == kind
                    && decoded.databaseItemId
                       == static_cast<quint32>(itemId);
        }
    }
    check(databaseRoundTrips,
          "database-kind-and-item-boundaries-round-trip");
    check(!tryDatabaseItem(-1, 0, rejectedId)
          && !tryDatabaseItem(2, 0, rejectedId)
          && !tryDatabaseItem(7, 0, rejectedId)
          && !tryDatabaseItem(0, -1, rejectedId)
          && !tryDatabaseItem(0, 16777216, rejectedId),
          "database-reserved-kinds-and-overflow-are-rejected");

    quint32 replacedPart = 0;
    const quint32 worldBase = worldObject(1, -1, 1234, 15);
    const quint32 activityBase = activityObject(4321, 255);
    check(tryWithPart(worldBase, 2047, replacedPart)
          && decode(replacedPart).primaryId == 1234
          && decode(replacedPart).part == 2047
          && tryWithPart(activityBase, 256, replacedPart)
          && decode(replacedPart).primaryId == 4321
          && decode(replacedPart).part == 256
          && tryWithPart(0, 123, replacedPart) && replacedPart == 0,
          "part-replacement-clears-the-old-field-and-preserves-zero");
    check(!tryWithPart(worldBase, 2048, replacedPart)
          && !tryWithPart(databaseItem(DatabaseKind::Track, 1),
                          1, replacedPart),
          "part-replacement-rejects-overflow-and-incompatible-kinds");

    quint32 replacedPatch = 0;
    const quint32 terrainBase = terrain(-1, 1, 3, 7);
    check(tryWithTerrainPatch(terrainBase, 4095, 2047, replacedPatch)
          && decode(replacedPatch).tileXOffset == -1
          && decode(replacedPatch).tileZOffset == 1
          && decode(replacedPatch).patchId == 4095
          && decode(replacedPatch).feature == 2047,
          "terrain-patch-replacement-preserves-location");

    check(decode(0).valid && decode(0).kind == Kind::None
          && !decode(1).valid
          && !decode(14u << SelectorShift).valid
          && !decode(31u << SelectorShift).valid,
          "zero-and-reserved-selectors-decode-safely");
    check(!decode((10u << SelectorShift) | (3u << 25)).valid
          && !decode((10u << SelectorShift) | (3u << 23)).valid,
          "terrain-reserved-location-codes-are-invalid");
    check(decode(worldObject(-1, -1, 0, 0)).selector == 1
          && decode(worldObject(1, 1, 0, 0)).selector == 9
          && decode(terrain(0, 0, 0, 0)).selector == 10
          && decode(activityObject(0, 0)).selector == 11
          && decode(databaseItem(DatabaseKind::Track, 0)).selector == 12
          && decode(activityService(0, 0)).selector == 13,
          "defined-selector-values-remain-stable");
    check(worldObject(1, -1, 0xabcd, 0x456)
              == ((7u << 27) | (0xabcdu << 11) | 0x456u)
          && terrain(1, -1, 0xabc, 0x456)
              == ((10u << 27) | (2u << 25)
                  | (0xabcu << 11) | 0x456u)
          && activityObject(0xabcd, 0x456)
              == ((11u << 27) | (0xabcdu << 11) | 0x456u)
          && databaseItem(DatabaseKind::Road, 0xabcdef)
              == ((12u << 27) | (1u << 24) | 0xabcdefu)
          && activityService(0xabcd, 0x456)
              == ((13u << 27) | (0xabcdu << 11) | 0x456u),
          "encoded-bit-fields-match-the-documented-layout");

    qInfo() << "[tests:selection-id] cases=" << (passed + failed)
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
