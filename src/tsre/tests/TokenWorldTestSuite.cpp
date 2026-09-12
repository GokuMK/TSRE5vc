#include <tsre/tests/TokenIdTestSuite.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/fileFunctions/SimisReader.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/world/Tile.h>
#include <tsre/world/objects/WorldObj.h>
#include <tsre/world/objects/TelepoleObj.h>
#include <tsre/world/objects/TrWatermarkObj.h>
#include <tsre/world/objects/DynTrackObj.h>
#include <tsre/world/objects/SignalObj.h>
#include <tsre/shape/SFile.h>
#include <tsre/shape/SFileC.h>
#include <QTemporaryDir>
#include <QFile>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QLocale>

namespace {
using namespace TokenTest;
QByteArray uid(quint32 value) { return block(TS::UiD, uints({value}), "uid"); }
QByteArray position() { return block(TS::Position, floats({1, 2, 3}), "position"); }
std::unique_ptr<FileBuffer> unicodeBuffer(const QString& text) {
    return buffer(fields([&](QDataStream& s) { for (QChar c : text) s << c.unicode(); }));
}
QString saveObject(WorldObj& object) {
    QString text;
    QTextStream out(&text);
    // A Telepole must not inherit a caller's lossy precision or decimal comma.
    out.setRealNumberPrecision(3);
    out.setLocale(QLocale(QLocale::Polish));
    object.save(&out);
    return text;
}
QByteArray shapeSections() {
    QByteArray matrices = floats({1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
    const auto point = [](float x, float y, float z) { return block(TS::point, floats({x, y, z}), "point"); };
    return block(TS::shader_names, uints({1}) + block(TS::named_shader, string("TexDiff")), "shaders")
        + block(TS::points, uints({3}) + point(1, 2, 3) + point(4, 5, 6) + point(7, 8, 9))
        + block(TS::uv_points, uints({1}) + block(TS::uv_point, floats({0, 1})))
        + block(TS::normals, uints({1}) + block(TS::vector, floats({0, 1, 0})))
        + block(TS::matrices, uints({1}) + block(TS::matrix, matrices, "MAIN"))
        + block(TS::images, uints({1}) + block(TS::image, string("dummy.ace")))
        + block(TS::textures, uints({1}) + block(TS::texture, uints({0, 0, 0, 0})))
        + block(TS::vtx_states, uints({1}) + block(TS::vtx_state, uints({0, 0, 0, 0, 0})))
        + block(TS::prim_states, uints({1}) + block(TS::prim_state, uints({0, 0})
                + block(TS::tex_idxs, uints({1, 0})) + uints({0, 0, 0, 0, 0}), "primitive"));
}
QByteArray shapeLod(int subObjId = -1) {
    QByteArray vertices;
    for (quint32 i = 0; i < 3; ++i)
        vertices += block(TS::vertex, uints({0, i, 0, 0, 0}) + block(TS::vertex_uvs, uints({1, 0})), "vertex");
    const auto sub = block(TS::sub_object,
        block(TS::sub_object_header, QByteArray(20, '\0') + block(TS::geometry_info,
            QByteArray(40, '\0') + block(TS::geometry_nodes, uints({0}))
                + block(TS::geometry_node_map, uints({1, 0})))
            + (subObjId < 0 ? QByteArray() :
                block(TS::subobject_shaders, uints({1, 0}))
                + block(TS::subobject_light_cfgs, uints({1, 0}))
                + uints({quint32(subObjId)})))
        + block(TS::vertices, uints({3}) + vertices)
        + block(TS::primitives, uints({3}) + block(TS::prim_state_idx, uints({0}))
            + block(0x80000800u, "ignored primitive")
            + block(TS::indexed_trilist, block(TS::vertex_idxs, uints({3, 0, 1, 2}))
                + block(TS::normal_idxs, uints({1, 0})) + block(TS::flags, uints({1, 0})))));
    const auto level = block(TS::distance_level,
        block(TS::distance_level_header, block(TS::dlevel_selection, floats({1000}))
            + block(TS::hierarchy, uints({1, 0xFFFFFFFFu})))
        + block(TS::sub_objects, uints({1}) + sub), "level");
    return block(TS::lod_controls, uints({1}) + block(TS::lod_control,
        block(TS::distance_levels_header, uints({0}))
        + block(TS::distance_levels, uints({1}) + level)));
}
}

int TsreTests::runTokenWorldSuite(bool verbose, bool withGl) {
    using namespace TokenTest;
    Suite test{"[tests:token-world]", verbose};
    const TS::TokenId forms[] = {TS::Static, TS::TrackObj, TS::Dyntrack, TS::Forest,
        TS::CollideObject, TS::Signal, TS::Gantry, TS::CarSpawner, TS::Pickup,
        TS::Platform, TS::Siding, TS::LevelCr, TS::Transfer, TS::Speedpost, TS::Hazard, TS::Telepole};
    for (auto id : forms) {
        Tile binary, unicode;
        auto input = buffer(file(block(TS::Tr_Worldfile, block(id, uid(35) + position(), "object")), 'w'));
        QString error;
        const bool ok = binary.loadBinaryData(input.get(), false, &error);
        // The Unicode path is used directly with the content of Tr_Worldfile.
        const QString text = QStringLiteral(" %1 ( UiD ( 35 ) Position ( 1 2 3 ) ) )")
                .arg(QString::fromLatin1(TS::name(id)));
        const QByteArray textBytes = fields([&](QDataStream& s) { for (QChar c : text) s << c.unicode(); });
        auto textInput = buffer(textBytes);
        unicode.loadUtf16Data(textInput.get());
        test.check(ok && binary.jestObiektow == 1 && unicode.jestObiektow == 1
                   && binary.obiekty[0]->typeID == unicode.obiekty[0]->typeID
                   && binary.obiekty[0]->type == unicode.obiekty[0]->type
                   && binary.obiekty[0]->UiD == 35 && binary.obiekty[0]->position[2] == 3,
                   "binary/Unicode object dispatch: " + QString::fromLatin1(TS::name(id)));
    }
    {
        const std::pair<TS::TokenId, QByteArray> properties[] = {
            {TS::UiD, uints({35})}, {TS::Population, uints({3})},
            {TS::StartPosition, floats({-101.123456f, 22.9824f, 500.1234f})},
            {TS::EndPosition, floats({-100.123456f, 25.125f, 520.1234f})},
            {TS::StartType, uints({0})}, {TS::EndType, uints({7})},
            {TS::StartDirection, floats({90})}, {TS::EndDirection, floats({-45.125f})},
            {TS::Config, uints({0})}, {TS::Quality, uints({0xFFFFFFFFu})},
            {TS::Position, floats({1.12345678f, -2.12345678f, 539.465f})},
            {TS::Direction, floats({1.25e-12f, -0.25f, 1.25e+12f})},
            {TS::MaxVisDistance, floats({1234.56789f})}, {TS::VDbId, uints({0xFFFFFFFFu})}
        };
        QByteArray payload;
        for (const auto& property : properties)
            payload += block(property.first, property.second, "property");
        Tile tile;
        auto input = buffer(file(block(TS::Tr_Worldfile, block(TS::Telepole, payload, "span")), 'w'));
        const bool ok = tile.loadBinaryData(input.get()) && tile.jestObiektow == 1;
        test.check(ok, "Telepole reads all 14 grammar fields with binary labels");
        if (ok) {
            WorldObj& object = *tile.obiekty[0];
            test.check(saveObject(object).isEmpty(), "unloaded Telepole is not serialized");
            object.load(12, -34);
            const QString saved = saveObject(object);
            bool allFields = true;
            for (const auto& property : properties)
                allFields &= saved.contains(QString::fromLatin1(TS::name(property.first)) + " (", Qt::CaseInsensitive);
            test.check(allFields && saved.contains("Quality ( 4294967295 )")
                       && saved.contains("VDbId ( 4294967295 )") && saved.contains("StartType ( 0 )")
                       && saved.contains("Config ( 0 )") && saved.contains("1.24999998e+12"),
                       "Telepole saves every field, zeroes, full-width integers and float precision");
            auto text = unicodeBuffer(saved + ")");
            Tile reloaded;
            reloaded.loadUtf16Data(text.get());
            test.check(reloaded.jestObiektow == 1, "Telepole saved text dispatch");
            if (reloaded.jestObiektow == 1) {
                auto& other = *reloaded.obiekty[0];
                other.load(12, -34);
                test.check(saveObject(other) == saved && other.position[2] == object.position[2],
                           "Telepole binary/text round trip preserves all values and coordinate conversion");
            }
            std::unique_ptr<WorldObj> copy(object.clone());
            copy->load(12, -34);
            test.check(saveObject(*copy) == saved && !copy->allowNew(),
                       "Telepole clone/repeated load retains fields without a second Z flip");
        }
        // Every schema field must remain bounded by its own binary block.
        for (const auto& property : properties) {
            Tile damaged;
            auto data = buffer(file(block(TS::Tr_Worldfile,
                block(TS::Telepole, uid(1) + block(property.first, property.second.chopped(1)))
                + block(TS::Telepole, uid(2))), 'w'));
            QString error;
            test.check(damaged.loadBinaryData(data.get(), false, &error)
                       && damaged.binaryLoadState() == Tile::BinaryLoadState::Recovered
                       && damaged.jestObiektow == 1 && damaged.obiekty[0]->UiD == 2 && !error.isEmpty(),
                       "truncated Telepole property cannot consume next object: "
                           + QString::fromLatin1(TS::name(property.first)));
        }
        Tile minimal;
        auto text = unicodeBuffer("Telepole ( UiD ( 7 ) ) )");
        minimal.loadUtf16Data(text.get());
        test.check(minimal.jestObiektow == 1, "minimal Telepole loads");
        if (minimal.jestObiektow == 1) {
            minimal.obiekty[0]->load(0, 0);
            test.check(saveObject(*minimal.obiekty[0]) == "\tTelepole (\n\t\tUiD ( 7 )\n\t)\n",
                       "Telepole save does not invent absent properties");
        }
        TelepoleObj invalid;
        for (const QString value : {QString(")"), QString("4294967296 )"), QString("-1 )")}) {
            auto data = unicodeBuffer(value);
            test.check(rejects([&] { invalid.set(QString("population"), data.get()); }),
                       "Telepole rejects missing/overflow/negative unsigned text values");
        }
    }
    // Optional private corpus test; original MSTS files never enter the repository.
    const QString telepoleWorld = qEnvironmentVariable("TSRE_TEST_TELEPOLE_WORLD");
    if (!telepoleWorld.isEmpty()) {
        QFile source(telepoleWorld);
        const bool opened = source.open(QIODevice::ReadOnly);
        test.check(opened, "Telepole native sample opens read-only");
        if (opened) {
            std::unique_ptr<FileBuffer> input(ReadFile::read(&source));
            Tile native;
            QString error;
            const bool ok = input && native.loadBinaryData(input.get(), false, &error)
                    && native.binaryLoadState() == Tile::BinaryLoadState::Complete;
            test.check(ok, "Telepole native world parses completely: " + error);
            if (ok) {
                int count = 0;
                for (int i = 0; i < native.jestObiektow; ++i) {
                    auto& object = *native.obiekty[i];
                    if (object.typeID != WorldObj::telepole) continue;
                    ++count;
                    object.load(0, 0);
                    const QString saved = saveObject(object);
                    auto text = unicodeBuffer(saved + ")");
                    Tile reloaded;
                    reloaded.loadUtf16Data(text.get());
                    const bool found = reloaded.jestObiektow == 1;
                    if (found) reloaded.obiekty[0]->load(0, 0);
                    test.check(found && saveObject(*reloaded.obiekty[0]) == saved,
                               QString("native Telepole %1 binary/text round trip").arg(object.UiD));
                }
                test.check(count > 0, QString("native Telepole count: %1").arg(count));
            }
        }
    }
    {
        const QByteArray sphere = block(TS::ViewDbSphere, block(TS::VDbId, uints({8}))
                + position() + block(TS::Radius, floats({9}))
                + block(TS::ViewDbSphere, block(TS::VDbId, uints({10}))), "sphere");
        const QByteArray objects = block(0x80000003u, "not a Static")
                + block(TS::Ruler, "unsupported binary extension")
                + block(TS::Tr_Watermark, uints({8}), "control")
                + block(TS::VDbIdCount, uints({2})) + sphere
                + block(TS::Static, block(TS::pack(6, TS::localId(TS::UiD)), uints({999}))
                    + block(0xFFFFFFFFu, "unknown property") + uid(66) + position()
                    + block(TS::FileName, string("shape.s"), "filename"));
        Tile tile;
        auto data = buffer(file(block(TS::Tr_Worldfile, objects, "root"), 'w'));
        test.check(tile.loadBinaryData(data.get()) && tile.jestObiektow == 2
                   && dynamic_cast<TrWatermarkObj*>(tile.obiekty[0])
                   && tile.obiekty[1]->UiD == 66 && tile.obiekty[1]->fileName == "shape.s"
                   && tile.vDbIdCount == 2 && tile.viewDbSphere.size() == 1
                   && tile.viewDbSphere[0].viewDbSphere.size() == 1
                   && tile.viewDbSphere[0].viewDbSphere[0].vDbId == 10,
                   "mixed namespace objects/properties, watermark ordering, nested view spheres");
    }
    {
        QByteArray sections;
        for (int i = 0; i < 5; ++i)
            sections += block(TS::TrackSection, block(TS::SectionCurve, uints({quint32(i % 2)}), "curve")
                    + uints({quint32(100 + i)}) + floats({float(i + 1), 22}), "section");
        const QByteArray signal = uid(3) + block(TS::SignalUnits, uints({1})
                + block(TS::SignalUnit, uints({2}) + block(TS::TrItemId, uints({0, 123}), "item"), "unit"));
        auto data = buffer(file(block(TS::Tr_Worldfile,
                block(TS::Dyntrack, uid(2) + block(TS::TrackSections, sections)) + block(TS::Signal, signal)), 'w'));
        Tile tile;
        const bool ok = tile.loadBinaryData(data.get());
        auto* track = ok ? dynamic_cast<DynTrackObj*>(tile.obiekty[0]) : nullptr;
        auto* sign = ok ? dynamic_cast<SignalObj*>(tile.obiekty[1]) : nullptr;
        test.check(track && sign && track->sections[4].sectIdx == 104 && track->sections[4].a == 5
                   && sign->containsTrackItem(0, 123), "labeled nested dynamic track and signal units");
    }
    {
        Tile tile;
        auto data = buffer(file(block(TS::Tr_Worldsoundfile,
                block(TS::Soundsource, uid(5) + position() + block(TS::FileName, string("ambient.sms")))), 'w'));
        test.check(tile.loadBinaryData(data.get(), true) && tile.jestObiektow == 1
                   && tile.obiekty[0]->typeID == WorldObj::soundsource
                   && tile.obiekty[0]->fileName == "ambient.sms", "distinct WS root and existing binary sound-source reader");
        data->off = 0;
        test.check(!tile.loadBinaryData(data.get(), false), "WS root not accepted as W root");
    }
    {
        Tile tile;
        auto data = buffer(file(block(TS::Tr_Worldfile,
                block(TS::Static, uid(1))
                + block(TS::Static, block(TS::UiD))
                + block(TS::Static, uid(3))), 'w'));
        QString error;
        test.check(tile.loadBinaryData(data.get(), false, &error)
                   && tile.binaryLoadState() == Tile::BinaryLoadState::Recovered
                   && tile.jestObiektow == 2 && tile.obiekty[0]->UiD == 1
                   && tile.obiekty[1]->UiD == 3
                   && error.contains("static", Qt::CaseInsensitive)
                   && error.contains("continued"),
                   "bad object payload is discarded at its known boundary and later object loads");
        test.check(!tile.save(), "recovered W load cannot overwrite its source");
    }
    {
        const QByteArray invalidHeader = uints({quint32(TS::Static), 0xFFFFFFFFu});
        Tile tile;
        auto data = buffer(file(block(TS::Tr_Worldfile,
                block(TS::Static, uid(1)) + invalidHeader), 'w'));
        QString error;
        test.check(tile.loadBinaryData(data.get(), false, &error)
                   && tile.binaryLoadState() == Tile::BinaryLoadState::Recovered
                   && tile.jestObiektow == 1 && tile.obiekty[0]->UiD == 1
                   && error.contains("framing") && error.contains("stopped"),
                   "unsafe top-level framing stops without rolling back completed objects");
    }
    {
        Tile tile;
        auto* existing = new TrWatermarkObj(4);
        tile.obiekty[0] = existing;
        tile.jestObiektow = 1;
        tile.vDbIdCount = 7;
        Tile::ViewDbSphere sphere{};
        sphere.vDbId = 9;
        tile.viewDbSphere.push_back(sphere);
        auto data = buffer(file(uints({quint32(TS::Tr_Worldfile), 0xFFFFFFFFu}), 'w'));
        QString error;
        test.check(!tile.loadBinaryData(data.get(), false, &error)
                   && tile.binaryLoadState() == Tile::BinaryLoadState::Failed
                   && tile.jestObiektow == 1 && tile.obiekty[0] == existing
                   && tile.vDbIdCount == 7 && tile.viewDbSphere.size() == 1
                   && tile.viewDbSphere[0].vDbId == 9 && error.contains("root"),
                   "malformed root preserves all pre-existing tile state");
    }
    {
        const QByteArray badSphere = block(TS::ViewDbSphere,
                block(TS::VDbId, uints({8}))
                + block(TS::Position, floats({1, 2})), "bad-sphere");
        Tile tile;
        auto data = buffer(file(block(TS::Tr_Worldfile,
                block(TS::Static, uid(1)) + badSphere + block(TS::Static, uid(3))), 'w'));
        QString error;
        test.check(tile.loadBinaryData(data.get(), false, &error)
                   && tile.binaryLoadState() == Tile::BinaryLoadState::Recovered
                   && tile.jestObiektow == 2 && tile.obiekty[0]->UiD == 1
                   && tile.obiekty[1]->UiD == 3 && tile.viewDbSphere.isEmpty()
                   && error.contains("viewdbsphere", Qt::CaseInsensitive)
                   && error.contains("continued"),
                   "bad ViewDbSphere is transactional and does not discard sibling objects");
    }
    {
        Tile tile;
        auto data = buffer(file(block(TS::Tr_Worldsoundfile,
                block(TS::Soundsource, uid(1))
                + block(TS::Soundsource, block(TS::UiD))
                + block(TS::Soundsource, uid(3))), 'w'));
        QString error;
        test.check(tile.loadBinaryData(data.get(), true, &error)
                   && tile.binaryLoadState(true) == Tile::BinaryLoadState::Recovered
                   && tile.jestObiektow == 2 && tile.obiekty[0]->UiD == 1
                   && tile.obiekty[1]->UiD == 3 && error.contains("continued"),
                   "WS object recovery follows the same transactional policy");
    }
    {
        auto data = buffer(block(TS::shader_names, uints({1}) + block(0xFFFF0800u, "unknown")
                + block(TS::named_shader, string(QString(180, 'a')), "name"), "container"));
        const auto header = data->readBlock();
        FileBuffer::ScopedLimit scope(*data, header.end);
        SFile shape;
        SFileC::odczytajshaders(data.get(), &shape);
        test.check(shape.ishaders == 1 && shape.shader[0].name.size() == 180,
                   "actual shape shader reader: unknown sibling, long name and labels");
    }
    {
        const QByteArray keys = block(TS::linear_pos, uints({2})
                + block(0x80000800u, "unknown key")
                + block(TS::linear_key, uints({3}) + floats({4, 5, 6}), "key"));
        auto data = buffer(block(TS::animation, uints({8, 30})
                + block(TS::anim_nodes, uints({1}) + block(TS::anim_node,
                    block(TS::controllers, uints({1}) + keys, "controllers"), "MAIN")), "animation"));
        const auto header = data->readBlock();
        SFile::Animation animation;
        animation.loadC(data.get(), header.end);
        test.check(animation.frames == 8 && animation.node.size() == 1
                   && animation.node[0].linearKey.size() == 1
                   && animation.node[0].linearKey[0].frame == 3
                   && animation.node[0].linearKey[0].pos[2] == 6, "actual shape animation reader: scoped keys and labels");
    }
    {
        const QByteArray rotation = block(TS::tcb_rot, uints({2})
            + block(TS::tcb_key, uints({1}) + floats({1, 2, 3, 4, 5, 6, 7, 8, 9}), "tcb")
            + block(TS::slerp_rot, uints({2}) + floats({1, 2, 3, 4}), "slerp"));
        auto data = buffer(block(TS::animation, uints({8, 30})
            + block(TS::anim_nodes, uints({1}) + block(TS::anim_node,
                block(TS::controllers, uints({1}) + rotation), "MAIN"))));
        const auto header = data->readBlock();
        SFile::Animation animation;
        animation.loadC(data.get(), header.end);
        test.check(animation.node.size() == 1 && animation.node[0].tcbKey.size() == 1
            && animation.node[0].slerpRot.size() == 1
            && animation.node[0].tcbKey[0].quat[0] == -1
            && animation.node[0].tcbKey[0].param[4] == 9
            && animation.node[0].slerpRot[0].quat[2] == -3,
            "TCB and slerp token dispatch preserves quaternion convention");
    }
    {
        auto data = buffer(block(TS::points, uints({1}) + block(TS::point, floats({1, 2})))
            + block(TS::normals, uints({0})));
        const auto header = data->readBlock();
        FileBuffer::ScopedLimit scope(*data, header.end);
        SFile shape;
        test.check(rejects([&] { SFileC::odczytajpunktyc(data.get(), &shape); }),
            "actual shape point reader rejects missing coordinate without consuming sibling");
    }
    QTemporaryDir temporary;
    test.check(temporary.isValid(), "world/shape fixture directory");
    for (bool compress : {false, true}) {
        QFile input(temporary.filePath(compress ? "compressed.w" : "plain.w"));
        const QByteArray bytes = file(block(TS::Tr_Worldfile, block(TS::Static, uid(5) + position())), 'w');
        if (!input.open(QIODevice::WriteOnly)) {
            test.check(false, "world fixture write open");
            continue;
        }
        input.write(compress ? compressed(bytes) : bytes);
        input.close();
        if (!input.open(QIODevice::ReadOnly)) {
            test.check(false, "world fixture read open");
            continue;
        }
        std::unique_ptr<FileBuffer> data(ReadFile::read(&input));
        Tile tile;
        test.check(tile.loadBinaryData(data.get()) && tile.jestObiektow == 1,
                   compress ? "compressed world through real envelope reader" : "plain world through real envelope reader");
    }
    if (withGl) {
        QOffscreenSurface surface;
        surface.create();
        QOpenGLContext context;
        context.setFormat(surface.format());
        if (!context.create() || !context.makeCurrent(&surface)) {
            test.check(false, "OpenGL context required for shape mesh test");
        } else {
            for (bool compress : {false, true}) {
                for (int subObjId : {-1, 0, 1, 2}) {
                    QFile input(temporary.filePath(compress ? "compressed.s" : "plain.s"));
                    const QByteArray bytes = file(block(TS::shape,
                            block(0x80000800u, "unknown root child") + shapeSections() + shapeLod(subObjId), "shape"), 's');
                    if (!input.open(QIODevice::WriteOnly)) {
                        test.check(false, "shape fixture write open");
                        continue;
                    }
                    input.write(compress ? compressed(bytes) : bytes);
                    input.close();
                    SFile shape(input.fileName(), "fixture.s", temporary.path());
                    shape.load();
                    bool ok = shape.loaded == 1 && shape.iloscd == 1 && shape.distancelevel[0].iloscs == 1;
                    if (ok) {
                        auto& sub = shape.distancelevel[0].subobiekty[0];
                        float vertices[27] = {};
                        ok = sub.iloscc == 1 && sub.czesci[0].iloscv == 3 && sub.VBO.bind()
                                && sub.VBO.read(0, vertices, sizeof(vertices));
                        sub.VBO.release();
                        ok = ok && vertices[0] == 7 && vertices[1] == 8 && vertices[2] == 9
                                && vertices[18] == 1 && vertices[19] == 2 && vertices[20] == 3;
                    }
                    test.check(ok, QString("%1 shape full LOD mesh VBO bytes, SubObjID %2")
                            .arg(compress ? "compressed" : "plain")
                            .arg(subObjId < 0 ? "absent" : QString::number(subObjId)));
                }
            }
        }
    }
    return test.finish();
}
