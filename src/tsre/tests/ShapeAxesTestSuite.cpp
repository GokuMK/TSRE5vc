#include <tsre/tests/ShapeAxesTestSuite.h>
#include <tsre/tests/TokenTestSupport.h>

#include <QFile>
#include <QMatrix4x4>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/renderer/OpenGL3Renderer.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/shape/SFileLegacy.h>
#include <tsre/shape/ShapeAxes.h>
#include <tsre/shape/ShapeMetadata.h>
#include <tsre/shape/ShapeSeats.h>
#include <tsre/ui/UiShapeDisplays.h>

namespace {

// One quad on MAIN (as the shape-emitters suite's, its PART left out).
const char Shape[] = R"(SIMISA@@@@@@@@@@JINX0s1t______
shape (
 shape_header ( 00000000 00000000 )
 shader_names ( 1 named_shader ( TexDiff ) )
 texture_filter_names ( 1 named_filter_mode ( MipLinear ) )
 points ( 4 point ( 0 0 0 ) point ( 1 0 0 ) point ( 1 1 0 ) point ( 0 1 0 ) )
 uv_points ( 4 uv_point ( 0 1 ) uv_point ( 1 1 ) uv_point ( 1 0 ) uv_point ( 0 0 ) )
 normals ( 1 vector ( 0 0 1 ) )
 sort_vectors ( 0 ) colours ( 0 )
 matrices ( 1 matrix MAIN ( 1 0 0 0 1 0 0 0 1 0 0 0 ) )
 images ( 1 image ( body.ace ) )
 textures ( 1 texture ( 0 0 0 ff000000 ) )
 light_materials ( 0 )
 light_model_cfgs ( 1 light_model_cfg ( 00000000 uv_ops ( 1 uv_op_copy ( 1 0 ) ) ) )
 vtx_states ( 1 vtx_state ( 00000000 0 -5 0 00000002 ) )
 prim_states ( 1 prim_state Body ( 00000000 0 tex_idxs ( 1 0 ) 0 0 0 0 1 ) )
 lod_controls ( 1 lod_control ( distance_levels_header ( 0 ) distance_levels ( 1
 distance_level ( distance_level_header ( dlevel_selection ( 1000 ) hierarchy ( 1 -1 ) )
 sub_objects ( 1 sub_object ( sub_object_header ( 00000400 -1 -1 000001d2 000001c4
  geometry_info ( 2 1 0 6 0 0 1 0 0 0 geometry_nodes ( 1 geometry_node ( 1 0 0 0 0 cullable_prims ( 1 2 6 ) ) )
   geometry_node_map ( 1 0 ) )
  subobject_shaders ( 1 0 ) subobject_light_cfgs ( 1 0 ) 0 )
 vertices ( 4 vertex ( 00000000 0 0 ffffffff ff000000 vertex_uvs ( 1 0 ) )
  vertex ( 00000000 1 0 ffffffff ff000000 vertex_uvs ( 1 1 ) )
  vertex ( 00000000 2 0 ffffffff ff000000 vertex_uvs ( 1 2 ) )
  vertex ( 00000000 3 0 ffffffff ff000000 vertex_uvs ( 1 3 ) ) )
 vertex_sets ( 1 vertex_set ( 0 0 4 ) )
 primitives ( 2
  prim_state_idx ( 0 ) indexed_trilist ( vertex_idxs ( 6 0 1 2 0 2 3 ) normal_idxs ( 2 0 3 0 3 ) flags ( 2 00000000 00000000 ) ) )
 ) ) ) ) ) )
) )";

// Every block with a position, after `axes` (an ESD_TSREAxes line or none).
QByteArray sdText(const char *model, const char *axes) {
    QByteArray text = "SIMISA@@@@@@@@@@JINX0t1t______\r\n\r\nshape ( ";
    text += model;
    text += "\r\n\tESD_Detail_Level ( 0 )\r\n";
    text += axes;
    text += "\tESD_Bounding_Box ( -1 0 -2 3 1 2 )\r\n"
            "\tESD_TSRELight ( Name ( \"LAMP\" ) Type ( Spot ) Position ( 1 2 3 ) Rotation ( 0 90 0 ) Intensity ( 10 ) )\r\n"
            "\tESD_TSREEmitter ( Name ( \"CHIMNEY\" ) Position ( 0.5 4 -6 ) Rotation ( 0 0 30 ) )\r\n"
            "\tESD_TSREDisplay ( Document ( \"d.rml\" ) Position ( 1.44 1.6 10 ) Rotation ( 10 90 20 ) Size ( 0.5 0.2 0 ) )\r\n"
            "\tESD_TSRESeats (\r\n"
            "\t\tAisle 1 ( 0.1 1 -5 5 )\r\n"
            "\t\tPassage ( 0.1 1 0 0.7 )\r\n"
            "\t\tSeat 1 ( 0.6 1.5 -3 90 )\r\n"
            "\t\tDoor 1 ( -1.4 1 2 1.3 )\r\n"
            "\t)\r\n"
            ")\r\n";
    return text;
}

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }

bool near3(const float *a, float x, float y, float z) { return near(a[0], x) && near(a[1], y) && near(a[2], z); }

// The light's and the emitter's directions as written (read beside an .s
// from the MSTS .sd), to compare the mirrored ones with.
struct Written {
    float light[3] = {0, 0, 0};
    float emitter[3] = {0, 0, 0};
};

// Whether a read .sd holds every block as written (mirrored false) or
// mirrored in x (true).
bool holds(const ShapeMetadata &m, const Written &w, bool mirrored) {
    const float s = mirrored ? -1.0f : 1.0f;
    if (m.boxes.size() != 1 || m.lights.size() != 1 || m.emitters.size() != 1 || m.displays.size() != 1
            || m.seats.seats.size() != 1 || m.seats.aisles.size() != 1 || m.seats.passages.size() != 1
            || m.seats.doors.size() != 1)
        return false;
    const auto &b = m.boxes[0];
    const bool box = mirrored ? (near(b[0], -3) && near(b[3], 1)) : (near(b[0], -1) && near(b[3], 3));
    const ShapeLights::Light &l = m.lights[0];
    const ShapeEmitters::Emitter &e = m.emitters[0];
    const ShapeDisplays::Display &d = m.displays[0];
    const ShapeSeats::Seats &seats = m.seats;
    return box && near(b[1], 0) && near(b[2], -2) && near(b[4], 1) && near(b[5], 2)
            && near3(l.position, s * 1, 2, 3) && near3(l.direction, s * w.light[0], w.light[1], w.light[2])
            && near3(e.position, s * 0.5f, 4, -6) && near3(e.direction, s * w.emitter[0], w.emitter[1], w.emitter[2])
            && near3(d.position, s * 1.44f, 1.6f, 10) && near3(d.rotation, 10, s * 90, s * 20)
            && near3(seats.seats[0].position, s * 0.6f, 1.5f, -3) && near(seats.seats[0].heading, s * 90)
            && near(seats.aisles[0].x, s * 0.1f) && near3(seats.passages[0].position, s * 0.1f, 1, 0)
            && near3(seats.doors[0].position, s * -1.4f, 1, 2);
}

bool sameSeats(const ShapeSeats::Seats &a, const ShapeSeats::Seats &b) {
    return a.seats.size() == 1 && b.seats.size() == 1 && a.doors.size() == 1 && b.doors.size() == 1
            && near3(a.seats[0].position, b.seats[0].position[0], b.seats[0].position[1], b.seats[0].position[2])
            && near(a.seats[0].heading, b.seats[0].heading) && near(a.aisles[0].x, b.aisles[0].x)
            && near3(a.doors[0].position, b.doors[0].position[0], b.doors[0].position[1], b.doors[0].position[2]);
}

// A shape for the display squares: an S file's (its root part mirroring x,
// as SFileLegacy's and SFileComplex's matrix 0) or an .e3d's (no parts, its
// own space).
class SquareShape : public ComplexShape {
public:
    explicit SquareShape(const QString &p) : path(p) {}
    QString path;
    float bounds[6] = {0, 0, 0, 0, 0, 0};
    bool msts() const { return ShapeAxes::ofModel(path) == ShapeAxes::Axes::Msts; }
    const QString &getPathId() const override { return path; }
    bool isLoaded() const override { return true; }
    float getSize() const override { return 1; }
    const float *getBound() const override { return bounds; }
    bool getBoxPoints(QVector<float> &) override { return false; }
    void load() override {}
    void reload() override {}
    unsigned int newState() override { return 0; }
    void setAnimated(unsigned int, bool) override {}
    void updateSim(float, unsigned int) override {}
    void pushRenderItem(RenderQueue &) override {}
    void pushRenderItem(RenderQueue &, quint32, unsigned int) override {}
    void invalidateRenderState(bool) override {}
    int partCount() const override { return msts() ? 1 : 0; }
    int rootPart() const override { return msts() ? 0 : -1; }
    QString partName(int) const override { return QStringLiteral("MAIN"); }
    int partParent(int) const override { return -1; }
    bool partMatrix(unsigned int, int part, float *out) const override {
        if (!msts() || part != 0)
            return false;
        Mat4::identity(out);
        out[0] = -1.0f;
        return true;
    }
};

bool sameMatrix(const QMatrix4x4 &a, const QMatrix4x4 &b) {
    for (int i = 0; i < 16; ++i)
        if (!near(a.constData()[i], b.constData()[i]))
            return false;
    return true;
}

} // namespace

int TsreTests::runShapeAxesSuite(bool verbose) {
    TokenTest::Suite test{"[tests:shape-axes]", verbose};
    using ShapeAxes::Axes;

    // The names and a model's own axes.
    test.check(ShapeAxes::fromName("") == Axes::Msts && ShapeAxes::fromName(" msts ") == Axes::Msts
                       && ShapeAxes::fromName("GLTF") == Axes::Gltf && ShapeAxes::fromName("glTF") == Axes::Gltf
                       && ShapeAxes::fromName("Blender") == Axes::Unknown,
               "names: MSTS when absent, any case, others unknown");
    test.check(ShapeAxes::ofModel("A/b.s") == Axes::Msts && ShapeAxes::ofModel("A/B.S") == Axes::Msts
                       && ShapeAxes::ofModel("a/b.e3d") == Axes::Gltf && ShapeAxes::ofModel("a/b.glb") == Axes::Gltf
                       && ShapeAxes::ofModel("a/b.gltf") == Axes::Gltf && ShapeAxes::ofModel("a.s/b") == Axes::Gltf,
               "a model's own axes by its extension: .s MSTS, others glTF");
    test.check(!ShapeAxes::mirrors(Axes::Msts, Axes::Msts) && ShapeAxes::mirrors(Axes::Msts, Axes::Gltf)
                       && ShapeAxes::mirrors(Axes::Gltf, Axes::Msts) && !ShapeAxes::mirrors(Axes::Gltf, Axes::Gltf)
                       && ShapeAxes::mirrors(Axes::Unknown, Axes::Gltf) && !ShapeAxes::mirrors(Axes::Unknown, Axes::Msts),
               "mirrored where the .sd's axes are not the model's (unknown as MSTS)");

    QTemporaryDir dir;
    const auto at = [&dir](const char *name) { return dir.filePath(QString::fromLatin1(name)); };
    const bool written = dir.isValid() && writeFile(at("msts.sd"), sdText("msts.s", ""))
            && writeFile(at("gltf.sd"), sdText("gltf.s", "\tESD_TSREAxes ( glTF )\r\n"))
            && writeFile(at("odd.sd"), sdText("odd.s", "\tESD_TSREAxes ( Blender )\r\n"));
    test.check(written, "test .sd files written");

    // ShapeMetadata (SFileComplex's and E3dShape's reader), beside an .s and
    // an .e3d of one name.
    ShapeMetadata mstsS, mstsE, gltfS, gltfE, oddE;
    const bool read = ShapeMetadata::read(at("msts.s"), mstsS) && ShapeMetadata::read(at("msts.e3d"), mstsE)
            && ShapeMetadata::read(at("gltf.s"), gltfS) && ShapeMetadata::read(at("gltf.e3d"), gltfE)
            && ShapeMetadata::read(at("odd.e3d"), oddE);
    Written w;
    if (read && !mstsS.lights.isEmpty() && !mstsS.emitters.isEmpty()) {
        std::copy(mstsS.lights[0].direction, mstsS.lights[0].direction + 3, w.light);
        std::copy(mstsS.emitters[0].direction, mstsS.emitters[0].direction + 3, w.emitter);
    }
    test.check(read && std::fabs(w.light[0]) > 0.5f && std::fabs(w.emitter[0]) > 0.1f,
               "read; the turned light and emitter point across the car");
    test.check(holds(mstsS, w, false) && mstsS.axes == Axes::Msts, "MSTS .sd beside an .s: as written");
    test.check(holds(mstsE, w, true), "MSTS .sd beside an .e3d: every block mirrored in x");
    test.check(holds(gltfS, w, true) && gltfS.axes == Axes::Gltf, "glTF .sd beside an .s: every block mirrored");
    test.check(holds(gltfE, w, false), "glTF .sd beside an .e3d: as written");
    const auto told = [](const QStringList &problems) {
        return problems.size() == 1 && problems[0].contains(QLatin1String("ESD_TSREAxes ( Blender )"));
    };
    test.check(holds(oddE, w, true) && oddE.axes == Axes::Unknown && told(oddE.lightProblems)
                       && told(oddE.emitterProblems) && told(oddE.displayProblems) && told(oddE.seatProblems),
               "an unknown ESD_TSREAxes read as MSTS, told with each block it moved");

    // ShapeSeats::readFor (people, characters): the model's own axes; drawn
    // (an S file's through its root) the same place for an .s and an .e3d.
    ShapeSeats::Seats seatsS, seatsE, seatsGltfE, seatsOdd;
    QStringList seatProblems;
    const bool seatsRead = ShapeSeats::readFor(at("msts.s"), seatsS) && ShapeSeats::readFor(at("msts.e3d"), seatsE)
            && ShapeSeats::readFor(at("gltf.e3d"), seatsGltfE) && ShapeSeats::readFor(at("odd.e3d"), seatsOdd, &seatProblems);
    test.check(seatsRead && sameSeats(seatsS, mstsS.seats) && sameSeats(seatsE, mstsE.seats)
                       && sameSeats(seatsGltfE, gltfE.seats) && told(seatProblems),
               "seats read for a model as ShapeMetadata reads them; an unknown axes told");
    test.check(sameSeats(ShapeSeats::drawn(seatsS, at("msts.s")), ShapeSeats::drawn(seatsE, at("msts.e3d"))),
               "one MSTS .sd: the .s's and the .e3d's seats drawn in one place");

    // Both MSTS backends read ESD_TSREAxes alike (SFileLegacy has its own
    // .sd loop).
    QScopedValueRollback<QString> backend(Game::renderBackend, QStringLiteral("qrhi"));
    OpenGL3Renderer frames;
    frames.resetFrame();
    const bool shapes = writeFile(at("msts.s"), QByteArray(Shape)) && writeFile(at("gltf.s"), QByteArray(Shape));
    SFileComplex complexGltf(at("gltf.s"), "gltf.s", dir.path());
    complexGltf.loadData();
    const QString saved = at("saved.s");
    QString error;
    const bool savedOk = complexGltf.save(saved, SFileComplex::Format::Text, false, &error)
            && writeFile(at("saved.sd"), sdText("saved.s", "\tESD_TSREAxes ( glTF )\r\n"));
    SFileLegacy legacyGltf(saved, "saved.s", dir.path());
    legacyGltf.load();
    test.check(shapes && savedOk && complexGltf.isLoaded() && legacyGltf.isLoaded(), "test shape loads in both backends");
    const auto sameBlocks = [&](const ComplexShape &shape) {
        return shape.lights().size() == 1 && shape.emitters().size() == 1 && shape.displays().size() == 1
                && near3(shape.lights()[0].position, -1, 2, 3)
                && near3(shape.lights()[0].direction, -w.light[0], w.light[1], w.light[2])
                && near3(shape.emitters()[0].position, -0.5f, 4, -6)
                && near3(shape.emitters()[0].direction, -w.emitter[0], w.emitter[1], w.emitter[2])
                && near3(shape.displays()[0].position, -1.44f, 1.6f, 10)
                && near3(shape.displays()[0].rotation, 10, -90, -20);
    };
    test.check(sameBlocks(complexGltf), "SFileComplex: a glTF .sd's blocks in the S file's axes");
    test.check(sameBlocks(legacyGltf), "SFileLegacy: a glTF .sd's blocks in the S file's axes");
    test.check(legacyGltf.esdBoundingBox.size() == 1 && near(legacyGltf.esdBoundingBox[0].shape[0], -3)
                       && near(legacyGltf.esdBoundingBox[0].shape[3], 1),
               "SFileLegacy: a glTF .sd's box mirrored");
    // One place: an MSTS light beside an .s drawn through its root part
    // (which mirrors x) where the same .sd puts it beside an .e3d.
    SFileComplex complexMsts(at("msts.s"), "msts.s", dir.path());
    complexMsts.loadData();
    const unsigned int state = complexMsts.newState();
    float root[16];
    bool rooted = complexMsts.isLoaded() && complexMsts.rootPart() == 0 && complexMsts.partMatrix(state, 0, root);
    float drawn[3] = {0, 0, 0};
    if (rooted && !complexMsts.lights().isEmpty()) {
        const float *p = complexMsts.lights()[0].position;
        for (int r = 0; r < 3; ++r)
            drawn[r] = root[r] * p[0] + root[4 + r] * p[1] + root[8 + r] * p[2] + root[12 + r];
    }
    test.check(rooted && !mstsE.lights.isEmpty() && near3(drawn, mstsE.lights[0].position[0], mstsE.lights[0].position[1],
                                                         mstsE.lights[0].position[2]),
               "one MSTS .sd: the .s's light (through its root) and the .e3d's in one place");

    // Display squares: without ShapeHierarchy on the root; the .s's square
    // and the .e3d's (its values mirrored at read) the same transform, face
    // included.
    SquareShape onS(at("x.s")), onE(at("x.e3d"));
    ShapeDisplays::Display d;
    d.square = true;
    d.position[0] = 1.0f;
    d.position[1] = 2.0f;
    d.position[2] = 3.0f;
    d.rotation[0] = 10.0f;
    d.rotation[1] = 30.0f;
    d.rotation[2] = 20.0f;
    d.size[0] = 0.5f;
    d.size[1] = 0.2f;
    ShapeDisplays::Display mirrored = d;
    ShapeDisplays::mirror(mirrored);
    const QMatrix4x4 squareS = Ui::ShapeDisplays::squareMatrix(onS, 0, Ui::ShapeDisplays::squarePart(onS, d), d);
    const QMatrix4x4 squareE = Ui::ShapeDisplays::squareMatrix(onE, 0, Ui::ShapeDisplays::squarePart(onE, mirrored), mirrored);
    const QVector3D centre = squareS.map(QVector3D());
    test.check(near(centre.x(), -1) && near(centre.y(), 2) && near(centre.z(), 3),
               "a square without ShapeHierarchy hangs on the root (an S file's mirrors x)");
    test.check(sameMatrix(squareS, squareE), "one MSTS display: the .s's square and the .e3d's alike, its face too");
    return test.finish();
}
