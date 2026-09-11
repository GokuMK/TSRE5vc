#include <tsre/tests/TokenIdTestSuite.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/fileFunctions/NetworkToken.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/fileFunctions/SimisReader.h>
#include <tsre/world/TFile.h>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>

namespace {
using namespace TokenTest;

struct Golden { TS::TokenId actual, expected; const char* name; };
const Golden native[] = {
#include "../../../tests/tokens/NativeTokenGolden.inc"
};

QByteArray saveTerrain(TFile& terrain) {
    return fields([&](QDataStream& s) { terrain.save(s); });
}

// Fixture surgery uses raw LE lengths, independently of the production reader.
QByteArray extraSamples(const QByteArray& original, const QByteArray& extra) {
    QByteArray result = original;
    int off = 41;
    while (off + 9 <= result.size()) {
        const quint32 id = qFromLittleEndian<quint32>(result.constData() + off);
        const quint32 size = qFromLittleEndian<quint32>(result.constData() + off + 4);
        if (id == TS::terrain_samples) {
            result.insert(off + 9, extra);
            qToLittleEndian<quint32>(size + extra.size(), result.data() + off + 4);
            const quint32 rootSize = qFromLittleEndian<quint32>(result.constData() + 36);
            qToLittleEndian<quint32>(rootSize + extra.size(), result.data() + 36);
            return result;
        }
        off += 8 + size;
    }
    throw FileBuffer::ParseError("Test fixture has no terrain_samples");
}
}

int TsreTests::runTokenIdSuite(bool verbose) {
    using namespace TokenTest;
    Suite test{"[tests:tokens]", verbose};
    for (const Golden& entry : native)
        test.check(entry.actual == entry.expected, QString::fromLatin1(entry.name));

    const Golden extensions[] = {
        {TS::Soundsource, 0x00040043u, "soundsource"},
        {TS::Soundregion, 0x00040044u, "soundregion"},
        {TS::terrain_sample_usbuffer, 282u, "terrain_sample_usbuffer"},
        {TS::ORTSListName, 0x00050800u, "ORTSListName"},
        {TS::Flipped, 0x0005080Eu, "Flipped"},
        {TS::Ruler, 0x00060800u, "Ruler"},
        {TS::ShapeTemplate, 0x00060801u, "ShapeTemplate"},
        {TS::TSRETerrainMaterialBuffer, 0x00061000u, "TSRETerrainMaterialBuffer"},
        {TS::TSRETerrainBakedMaterial, 0x00061001u, "TSRETerrainBakedMaterial"},
        {TS::TSRETerrainMaterialMap, 0x00061002u, "TSRETerrainMaterialMap"},
        {TS::TSRETerrainBakedMaterials, 0x00061003u, "TSRETerrainBakedMaterials"}
    };
    for (const Golden& entry : extensions)
        test.check(entry.actual == entry.expected && QString::fromLatin1(TS::name(entry.actual)) == entry.name,
                   QString::fromLatin1(entry.name));
    QSet<QString> names;
    for (const auto& entry : TS::IdName) names.insert(QString::fromLatin1(entry.second).toLower());
    test.check(TS::IdName.size() == 1467 && names.size() == 1467, "unique canonical IDs and case-insensitive names");
    const auto registrySize = TS::IdName.size();
    test.check(QString::fromLatin1(TS::name(0xFFFF0800u)) == "<unknown>"
               && TS::describe(0xFFFF0800u).contains("ffff0800")
               && TS::IdName.size() == registrySize, "unknown diagnostics do not mutate registry");

    const TS::TokenId ids[] = {0x00000003u, 0x00040003u, 0x00050003u, 0x00060003u,
                              0x0006FFFFu, 0x80000800u, 0xFFFF0800u, 0xFFFFFFFFu};
    for (auto id : ids) {
        auto data = buffer(QByteArray(1, 'x') + block(id, uints({123}), "label"));
        data->off = 1; // deliberately unaligned
        const auto header = data->readBlock();
        FileBuffer::ScopedLimit scope(*data, header.end);
        data->skipLabel();
        test.check(header.id == id && data->getUint() == 123
                   && TS::pack(TS::nameSpace(id), TS::localId(id)) == id,
                   QStringLiteral("full-width identity %1").arg(id, 8, 16, QLatin1Char('0')));
    }
    test.check(block(TS::Static).left(4).toHex() == "03000400", "native Static golden wire bytes");
    test.check(block(TS::TSRETerrainMaterialBuffer).left(4).toHex() == "00100600"
               && block(TS::TSRETerrainBakedMaterial).left(4).toHex() == "01100600"
               && block(TS::TSRETerrainMaterialMap).left(4).toHex() == "02100600", "terrain extension golden wire bytes");
    {
        auto data = buffer(block(0x80000800u, "opaque", "unknown") + block(TS::Static, uints({45}), "known"));
        Simis::Block known(data.get(), TS::Static);
        test.check(known.label() == "known" && data->getUint() == 45, "findToken skips unknown labeled sibling");
    }
    const QByteArray valid = block(TS::Static, uints({42}), "label");
    bool truncated = true;
    for (int size = 0; size < valid.size(); ++size) {
        auto data = buffer(valid.left(size));
        truncated &= rejects([&] { data->readBlock(); });
    }
    test.check(truncated, "every header/label/body truncation rejected");
    for (quint32 size : {0u, 0x7FFFFFFFu, 0xFFFFFFFFu}) {
        auto data = buffer(uints({TS::Static, size}) + QByteArray(8, '\0'));
        test.check(rejects([&] { data->readBlock(); }), "invalid/overflowing body length");
    }
    {
        auto data = buffer(uints({TS::Static, 1}) + QByteArray(1, char(255)));
        test.check(rejects([&] { data->readBlock(); }), "label larger than body");
    }
    {
        auto data = buffer(block(TS::terrain, block(TS::terrain_samples)) + QByteArray(20, '\0'));
        const auto parent = data->readBlock();
        FileBuffer::ScopedLimit scope(*data, parent.end);
        data->skipLabel();
        qToLittleEndian<quint32>(20, data->data + data->off + 4);
        test.check(rejects([&] { data->readBlock(); }), "child outside parent but inside input rejected");
    }
    {
        auto data = buffer(block(TS::UiD) + block(TS::Position, floats({1, 2, 3})));
        Simis::Block uid(data.get(), TS::UiD);
        test.check(rejects([&] { data->getUint(); }), "positional field cannot read next sibling");
    }

    TFile terrain;
    terrain.initNew("token-test", 128, 16, 16);
    const QByteArray ordinary = saveTerrain(terrain);
    terrain.sampleMaterialBuffer = "token-test.pmap";
    terrain.bakedMaterialInfo = "v1:pending";
    terrain.materialUidMapPresent = true;
    terrain.materialUids.insert(0, 17);
    terrain.materialUids.insert(255, 0xF1234567u);
    terrain.sampleASbuffer = {true, "AS label", QByteArray::fromHex("001122ff")};
    terrain.sampleUSbuffer = {true, "US label", QByteArray::fromHex("abcdef")};
    terrain.opaqueSampleBufferOrder = {TS::terrain_sample_usbuffer, TS::terrain_sample_asbuffer};
    const QByteArray full = saveTerrain(terrain);
    {
        auto data = buffer(full);
        TFile decoded;
        test.check(decoded.load(data.get()) && saveTerrain(decoded) == full
                   && decoded.materialUids == terrain.materialUids
                   && decoded.opaqueSampleBufferOrder == terrain.opaqueSampleBufferOrder,
                   "all three new terrain blocks and AS/US round-trip byte-for-byte");
    }
    {
        auto data = buffer(ordinary);
        TFile decoded;
        test.check(decoded.load(data.get()) && saveTerrain(decoded) == ordinary,
                   "ordinary Core-only terrain remains byte-identical");
    }
    {
        const QByteArray unknown = block(0xFFFF1000u, string("not a material map"), "extension")
                + block(TS::pack(6, TS::localId(TS::terrain_nsamples)), uints({777}));
        auto data = buffer(extraSamples(full, unknown));
        TFile decoded;
        test.check(decoded.load(data.get()) && decoded.nsamples && *decoded.nsamples == 128
                   && decoded.sampleMaterialBuffer == terrain.sampleMaterialBuffer,
                   "mixed namespaces do not alias Core/TSRE terrain children");
    }
    {
        // No compatibility reader: these literal IDs are deliberately independent.
        const QByteArray old = block(100009, string("prototype.pmap"))
                + block(100010, string("v1:pending")) + block(100011, uints({1, 0, 17}));
        auto data = buffer(extraSamples(ordinary, old));
        TFile decoded;
        test.check(decoded.load(data.get()) && decoded.sampleMaterialBuffer.isEmpty()
                   && decoded.bakedMaterialInfo.isEmpty() && !decoded.materialUidMapPresent,
                   "prototype terrain IDs have no read aliases");
    }
    for (const QByteArray& payload : {QByteArray(), uints({257}), uints({1, 256, 2}),
             uints({1, 2, 0}), uints({2, 2, 1, 2, 2}), uints({1, 1, 2, 3})}) {
        auto data = buffer(extraSamples(ordinary, block(TS::TSRETerrainMaterialMap, payload)));
        TFile decoded;
        test.check(decoded.load(data.get()) && decoded.materialUidMapPresent && !decoded.materialUidMapValid,
                   "invalid material map stays present and invalid");
    }
    {
        auto data = buffer(extraSamples(ordinary, block(TS::TSRETerrainMaterialBuffer, QByteArray())));
        TFile decoded;
        test.check(decoded.load(data.get()) && !decoded.sampleMaterialBuffer.isEmpty(),
                   "malformed procedural reference cannot silently disable feature");
    }
    {
        auto data = buffer(file(block(TS::terrain,
                block(TS::terrain_samples, block(TS::terrain_nsamples, uints({128})), "samples")
                + block(TS::terrain_alwaysselect_maxdist, floats({22}), "field"), "root")));
        TFile decoded;
        test.check(decoded.load(data.get()) && decoded.nsamples && *decoded.nsamples == 128
                   && decoded.alwaysselectMaxdist && *decoded.alwaysselectMaxdist == 22,
                   "terrain root, container and scalar nonempty labels");
    }
    {
        auto data = buffer(file(block(TS::terrain, block(TS::terrain_samples,
                block(TS::terrain_nsamples, QByteArray())))));
        TFile decoded;
        test.check(!decoded.load(data.get()) && !decoded.loaded, "actual terrain reader rejects truncated scalar");
    }
    for (const QString& label : {QString(), QString("water")}) {
        for (int count : {1, 4}) {
            const auto heights = count == 1 ? floats({60}) : floats({1125, 1065, 1140, 1141});
            auto data = buffer(file(block(TS::terrain,
                    block(TS::terrain_water_height_offset, heights, label)
                    + block(TS::terrain_alwaysselect_maxdist, floats({22})))));
            TFile decoded;
            test.check(decoded.load(data.get()) && decoded.waterLevel
                       && decoded.WSW == (count == 1 ? 60 : 1125)
                       && decoded.WSE == (count == 1 ? 60 : 1065)
                       && decoded.WNE == (count == 1 ? 60 : 1140)
                       && decoded.WNW == (count == 1 ? 60 : 1141)
                       && decoded.alwaysselectMaxdist && *decoded.alwaysselectMaxdist == 22,
                       QString("water height: %1 float(s), label '%2', following sibling intact")
                           .arg(count).arg(label));
        }
        for (int bytes : {0, 3, 8, 12, 20}) {
            auto data = buffer(file(block(TS::terrain,
                    block(TS::terrain_water_height_offset, QByteArray(bytes, '\0'), label)
                    + block(TS::terrain_alwaysselect_maxdist, floats({22})))));
            TFile decoded;
            test.check(!decoded.load(data.get()) && !decoded.loaded && !decoded.waterLevel,
                       QString("reject malformed water payload: %1 bytes, label '%2'")
                           .arg(bytes).arg(label));
        }
    }
    QTemporaryDir temporary;
    test.check(temporary.isValid(), "temporary fixture directory");
    for (bool compress : {false, true}) {
        QFile input(temporary.filePath(compress ? "compressed.t" : "plain.t"));
        if (!input.open(QIODevice::WriteOnly)) {
            test.check(false, "terrain fixture write open");
            continue;
        }
        input.write(compress ? compressed(full) : full);
        input.close();
        TFile decoded;
        test.check(decoded.readT(input.fileName()) && saveTerrain(decoded) == full,
                   compress ? "real compressed SIMISA terrain path" : "real plain SIMISA terrain path");
    }

    const TS::TokenId messages[] = {TS::TSRE_Requested_Terrain_tFile, TS::TSRE_Requested_Terrain_RawFile,
        TS::TSRE_Requested_Terrain_FtFile, TS::TSRE_Terrain_tFile, TS::TSRE_Terrain_RawFile,
        TS::TSRE_Terrain_FtFile, TS::TSRE_Requested_TD_File, TS::TSRE_Requested_TD_Lo_File};
    for (int i = 0; i < 8; ++i) {
        const QByteArray bytes = fields([&](QDataStream& s) {
            NetworkToken::write(s, messages[i]);
            s << quint32(0) << quint8(0) << qint32(-12) << qint32(34);
            s.writeRawData(full.constData(), full.size());
        });
        auto data = buffer(bytes);
        QString error;
        TS::TokenId id = 0;
        const bool ok = NetworkToken::read(data.get(), id, error);
        test.check(ok && id == messages[i] && id == 0x00060001u + i
                   && bytes.mid(1, 4) == uints({quint32(0x00060001u + i)})
                   && bytes.mid(18) == full, "network golden token and unchanged nested file bytes");
        if (ok) {
            data->off += 13;
            TFile nested;
            test.check(nested.load(data.get()) && nested.materialUids == terrain.materialUids,
                       "real terrain reader at network payload offset");
        }
    }
    for (quint32 id : {100001u, 100008u, 0x00050001u, 0x80000001u}) {
        auto data = buffer(QByteArray(1, 'B') + uints({id}) + QByteArray(13, '\0'));
        TS::TokenId read = 0;
        QString error;
        test.check(!NetworkToken::read(data.get(), read, error) && !error.isEmpty(),
                   "old or foreign namespace peer rejected with diagnostic");
    }
    for (int length = 0; length < 18; ++length) {
        auto data = buffer((QByteArray(1, 'B') + uints({0x00060001u}) + QByteArray(13, '\0')).left(length));
        TS::TokenId id = 0;
        QString error;
        test.check(!NetworkToken::read(data.get(), id, error), "truncated network envelope rejected");
    }
    return test.finish();
}
