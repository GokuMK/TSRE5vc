#include <tsre/tests/TokenIdTestSuite.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/fileFunctions/SimisReader.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/world/Tile.h>
#include <tsre/world/objects/WorldObj.h>
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

namespace {
using namespace TokenTest;
QByteArray uid(quint32 value) { return block(TS::UiD, uints({value}), "uid"); }
QByteArray position() { return block(TS::Position, floats({1, 2, 3}), "position"); }
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
        TS::Platform, TS::Siding, TS::LevelCr, TS::Transfer, TS::Speedpost, TS::Hazard};
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
                block(TS::Static, uid(1)) + block(TS::Static, block(TS::UiD))), 'w'));
        QString error;
        test.check(!tile.loadBinaryData(data.get(), false, &error) && tile.jestObiektow == 0
                   && !error.isEmpty(), "world payload truncation fails and rolls back partial objects");
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
