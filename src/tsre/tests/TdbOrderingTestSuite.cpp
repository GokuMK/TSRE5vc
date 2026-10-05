#include <tsre/tests/TdbOrderingTestSuite.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <QDebug>
#include <QTextStream>
#include <algorithm>
#include <cstring>
#include <vector>

namespace {
class TestDatabase : public TDB {
public:
    using TDB::TDB;
    using TDB::sortItemRefs;
};
}

int TsreTests::runTdbOrderingSuite() {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name) {
        if (ok) ++passed;
        else { ++failed; qWarning() << "[tests:tdb-ordering] FAIL" << name; }
    };
    TSectionDAT sections(false, false);
    TestDatabase database(&sections, false);
    auto *node = new TRnode();
    node->typ = 1;
    database.trackNodes[1] = node;
    database.iTRnodes = 1;

    // Enough equal-position items to expose the old std::sort permutation.
    std::vector<int> input{41}, expected{40};
    for (int id = 39; id >= 0; --id) {
        database.trackItems[id] = TRitem::newPickupItem(id, 10.0f);
        input.push_back(id);
        expected.push_back(id);
    }
    database.trackItems[40] = TRitem::newPickupItem(40, 0.0f);
    database.trackItems[41] = TRitem::newPickupItem(41, 20.0f);
    input.push_back(40);
    expected.push_back(41);
    node->iTri = input.size();
    node->trItemRef = new int[input.size()];
    std::copy(input.begin(), input.end(), node->trItemRef);
    database.sortItemRefs();
    check(std::equal(expected.begin(), expected.end(), node->trItemRef),
          "ascending path position with stable ties");
    database.sortItemRefs();
    check(std::equal(expected.begin(), expected.end(), node->trItemRef),
          "repeated sorting preserves every reference position");

    auto *pickup = database.trackItems[0];
    pickup->setPickupContent(1000000.0f);
    pickup->pickupTrItemData2 = 0x280;
    QString serialized;
    QTextStream stream(&serialized);
    stream.setRealNumberPrecision(6);
    pickup->save(&stream);
    check(serialized.contains("PickupTrItemData ( 1e+06 00000280 )"),
          "pickup content uses real-number format and flags use hexadecimal");
    const QString payload = serialized.mid(serialized.indexOf("PickupTrItemData")
                                           + QString("PickupTrItemData").size());
    const int bytes = payload.size() * sizeof(char16_t);
    auto *memory = new unsigned char[bytes];
    std::memcpy(memory, payload.utf16(), bytes);
    FileBuffer buffer(memory, bytes);
    TRitem reloaded(0);
    reloaded.init("pickupitem");
    reloaded.set("pickuptritemdata", &buffer);
    check(reloaded.pickupTrItemData1 == 1000000.0f && reloaded.pickupTrItemData2 == 0x280,
          "six-digit scientific pickup content and flags reload unchanged");
    qInfo() << "[tests:tdb-ordering] passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
