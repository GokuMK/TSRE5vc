#include <tsre/tests/TrackNodeDataTestSuite.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <QDebug>
#include <QTextStream>
#include <memory>

namespace {
std::unique_ptr<FileBuffer> buffer(const QString &text) {
    auto bytes = new unsigned char[text.size() * 2];
    for(int i = 0; i < text.size(); ++i) {
        bytes[2*i] = text[i].unicode() & 255;
        bytes[2*i+1] = text[i].unicode() >> 8;
    }
    return std::make_unique<FileBuffer>(bytes, text.size() * 2);
}
QString saved(TRnode &node) {
    QString result;
    QTextStream stream(&result);
    stream.setRealNumberPrecision(6);
    node.saveToStream(stream, 1);
    return result;
}
void loadNode(TRnode &node, const QString &text) {
    auto data = buffer(text);
    ParserX::NextTokenInside(data.get());
    node.loadUtf16Data(data.get());
}
void loadDb(TDB &db, const QString &text) {
    auto data = buffer(text);
    ParserX::NextTokenInside(data.get());
    db.loadUtf16Data(data.get());
}
}

int TsreTests::runTrackNodeDataSuite() {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name) {
        if(ok) ++passed;
        else { ++failed; qWarning() << "[tests:tdb-fields] FAIL" << name; }
    };
    TSectionDAT definitions(false, false);
    const QString uid = "UiD ( -11 12 16777217 4294967295 -13 14 1.25 -2.5 3.75 -0.125 0.25 -0.5 )";
    for(const QString &byte : {QString("00"), QString("10"), QString("a5"), QString("ff")}) {
        const QString vector = "TrackNode ( 1 TrVectorNode ( TrVectorSections ( 1 "
            "16777217 4294967295 -11 12 16777219 16777221 4294967295 " + byte +
            " -13 14 1.25 -2.5 3.75 -0.125 0.25 -0.5 ) "
            "TrItemRefs ( 1 TrItemRef ( 16777217 ) ) ) TrPins ( 1 1 TrPin ( 16777219 1 ) TrPin ( 2 0 ) ) )";
        TRnode node;
        loadNode(node, vector);
        const auto &s = node.trVectorSection[0];
        check(node.typ == 1 && node.iTrv == 1 && s.sectionIndex == 16777217u
            && s.shapeIndex == 4294967295u && s.worldTileX == -11 && s.worldTileZ == 12
            && s.worldObjectId == 16777219u && s.startEndpointIndex == 16777221u
            && s.endEndpointIndex == 4294967295u && s.opaqueByte == byte.toUInt(nullptr, 16)
            && s.tileX == -13 && s.tileZ == 14 && s.x == 1.25f && s.y == -2.5f
            && s.z == 3.75f && s.ax == -0.125f && s.ay == 0.25f && s.az == -0.5f
            && node.trItemRef[0] == 16777217 && node.pins[0].link == 16777219
            && node.pins[0].direction == 1 && node.pins[1].link == 2, "vector field mapping and exact integers");
        TRnode copy(node);
        check(saved(copy) == saved(node) && copy.trVectorSection != node.trVectorSection
            && copy.trItemRef != node.trItemRef, "deep copy preserves typed fields");
        for(bool road : {false, true}) {
            TDB db(&definitions, road);
            loadDb(db, "TrackDB ( TrackNodes ( 1 " + vector + " ) )");
            check(saved(*db.trackNodes[1]) == saved(node), "both load paths agree");
            QString serialized;
            QTextStream out(&serialized);
            out.setRealNumberPrecision(6);
            db.saveToStream(out);
            TDB reloaded(&definitions, road);
            loadDb(reloaded, serialized);
            TRnode single;
            loadNode(single, saved(node));
            check(saved(*reloaded.trackNodes[1]) == saved(node) && saved(single) == saved(node)
                && serialized.contains(" " + byte + " -13 14"), "both save paths preserve byte and large IDs");
        }
    }
    for(const QString &kind : {QString("TrEndNode ( 3 )"), QString("TrEndNode ( 4294967295 )"),
            QString("TrJunctionNode ( 16777217 4294967295 16777219 )"),
            QString("TrJunctionNode ( 16777217 4294967295 )")}) {
        bool junction = kind.startsWith("TrJunction");
        QString text = "TrackNode ( 1 " + kind + " " + uid + (junction
            ? " TrPins ( 1 2 TrPin ( 2 0 ) TrPin ( 3 1 ) TrPin ( 4 0 ) ) )"
            : " TrPins ( 1 0 TrPin ( 2 0 ) ) )");
        TRnode node;
        loadNode(node, text);
        const auto &u = node.uid;
        check(u.worldTileX == -11 && u.worldTileZ == 12 && u.worldObjectId == 16777217u
            && u.worldEndpointIndex == 4294967295u && u.tileX == -13 && u.tileZ == 14
            && u.x == 1.25f && u.y == -2.5f && u.z == 3.75f && u.ax == -0.125f
            && u.ay == 0.25f && u.az == -0.5f, "UID field mapping");
        check(junction ? node.junction.unknown0 == 16777217u && node.junction.shapeIndex == 4294967295u
            && node.junction.unknown2 == (kind.contains("16777219") ? 16777219u : 0u)
            : node.endNodeValue == (kind.contains("4294967295") ? 4294967295u : 3u),
            "end scalar and optional junction metadata");
        TDB db(&definitions, false);
        loadDb(db, "TrackDB ( TrackNodes ( 1 " + text + " ) )");
        QString serialized;
        QTextStream out(&serialized);
        db.saveToStream(out);
        TDB reloaded(&definitions, false);
        loadDb(reloaded, serialized);
        TRnode single;
        loadNode(single, saved(node));
        TRnode copy(node);
        check(saved(*db.trackNodes[1]) == saved(node) && saved(*reloaded.trackNodes[1]) == saved(node)
            && saved(single) == saved(node) && saved(copy) == saved(node), "UID metadata round trip and copy");
    }
    {
        auto data = buffer(" 1.6777217e+007 -2.147483648e+009 4.294967295e+009 ");
        check(TrackNodeText::readUInt(data.get()) == 16777217u
            && TrackNodeText::readInt(data.get()) == (-2147483647 - 1)
            && TrackNodeText::readUInt(data.get()) == 4294967295u, "older scientific integer literals");
    }
    for(const QString &invalid : {QString("4294967296"), QString("-1"), QString("1.5")}) {
        auto data = buffer(invalid + " ");
        bool rejected = false;
        try { TrackNodeText::readUInt(data.get()); }
        catch(const FileBuffer::ParseError &) { rejected = true; }
        check(rejected, "invalid integer rejected without narrowing");
    }
    qInfo() << "[tests:tdb-fields] passed=" << passed << "failed=" << failed;
    return failed ? 1 : 0;
}
