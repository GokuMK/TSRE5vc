#include <tsre/tests/ShapeEmittersTestSuite.h>

#include <QDebug>
#include <QFile>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <cmath>
#include <tsre/ErrorMessage.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/renderer/ExhaustParticles.h>
#include <tsre/renderer/OpenGL3Renderer.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/shape/SFileLegacy.h>
#include <tsre/shape/ShapeEmitters.h>

namespace {

// Two quads: MAIN, and PART 5 m up under it.
const char Shape[] = R"(SIMISA@@@@@@@@@@JINX0s1t______
shape (
 shape_header ( 00000000 00000000 )
 shader_names ( 1 named_shader ( TexDiff ) )
 texture_filter_names ( 1 named_filter_mode ( MipLinear ) )
 points ( 8 point ( 0 0 0 ) point ( 1 0 0 ) point ( 1 1 0 ) point ( 0 1 0 )
  point ( 2 0 0 ) point ( 3 0 0 ) point ( 3 1 0 ) point ( 2 1 0 ) )
 uv_points ( 4 uv_point ( 0 1 ) uv_point ( 1 1 ) uv_point ( 1 0 ) uv_point ( 0 0 ) )
 normals ( 1 vector ( 0 0 1 ) )
 sort_vectors ( 0 ) colours ( 0 )
 matrices ( 2 matrix MAIN ( 1 0 0 0 1 0 0 0 1 0 0 0 ) matrix PART ( 1 0 0 0 1 0 0 0 1 0 5 0 ) )
 images ( 1 image ( body.ace ) )
 textures ( 1 texture ( 0 0 0 ff000000 ) )
 light_materials ( 0 )
 light_model_cfgs ( 1 light_model_cfg ( 00000000 uv_ops ( 1 uv_op_copy ( 1 0 ) ) ) )
 vtx_states ( 2 vtx_state ( 00000000 0 -5 0 00000002 ) vtx_state ( 00000000 1 -5 0 00000002 ) )
 prim_states ( 2 prim_state Body ( 00000000 0 tex_idxs ( 1 0 ) 0 0 0 0 1 )
  prim_state Part ( 00000000 0 tex_idxs ( 1 0 ) 0 1 0 0 1 ) )
 lod_controls ( 1 lod_control ( distance_levels_header ( 0 ) distance_levels ( 1
 distance_level ( distance_level_header ( dlevel_selection ( 1000 ) hierarchy ( 2 -1 0 ) )
 sub_objects ( 1 sub_object ( sub_object_header ( 00000400 -1 -1 000001d2 000001c4
  geometry_info ( 4 2 0 12 0 0 2 0 0 0 geometry_nodes ( 1 geometry_node ( 2 0 0 0 0 cullable_prims ( 2 4 12 ) ) )
   geometry_node_map ( 2 0 0 ) )
  subobject_shaders ( 1 0 ) subobject_light_cfgs ( 1 0 ) 0 )
 vertices ( 8 vertex ( 00000000 0 0 ffffffff ff000000 vertex_uvs ( 1 0 ) )
  vertex ( 00000000 1 0 ffffffff ff000000 vertex_uvs ( 1 1 ) )
  vertex ( 00000000 2 0 ffffffff ff000000 vertex_uvs ( 1 2 ) )
  vertex ( 00000000 3 0 ffffffff ff000000 vertex_uvs ( 1 3 ) )
  vertex ( 00000000 4 0 ffffffff ff000000 vertex_uvs ( 1 0 ) )
  vertex ( 00000000 5 0 ffffffff ff000000 vertex_uvs ( 1 1 ) )
  vertex ( 00000000 6 0 ffffffff ff000000 vertex_uvs ( 1 2 ) )
  vertex ( 00000000 7 0 ffffffff ff000000 vertex_uvs ( 1 3 ) ) )
 vertex_sets ( 1 vertex_set ( 0 0 8 ) )
 primitives ( 4
  prim_state_idx ( 0 ) indexed_trilist ( vertex_idxs ( 6 0 1 2 0 2 3 ) normal_idxs ( 2 0 3 0 3 ) flags ( 2 00000000 00000000 ) )
  prim_state_idx ( 1 ) indexed_trilist ( vertex_idxs ( 6 4 5 6 4 6 7 ) normal_idxs ( 2 0 3 0 3 ) flags ( 2 00000000 00000000 ) ) )
 ) ) ) ) ) )
) )";

// Emitters: a chimney on PART 1 m above it with every value given; a stack
// by its kind's defaults turned forward (Rotation); a safety valve turned
// back (QDirection); a kind unknown, both turns and a colour not hex; one
// off; one on a matrix the shape has not.
const char Sd[] =
    "SIMISA@@@@@@@@@@JINX0t1t______\r\n\r\n"
    "shape ( emitters.s\r\n"
    "\tESD_Detail_Level ( 0 )\r\n"
    "\tESD_TSREEmitter (\r\n"
    "\t\tName ( \"CHIMNEY\" )\r\n"
    "\t\tShapeHierarchy ( \"PART\" )\r\n"
    "\t\tPosition ( 0 1 0 )\r\n"
    "\t\tDiameter ( 0.4 )\r\n"
    "\t\tKind ( Exhaust )\r\n"
    "\t\tFlow ( 0.05 )\r\n"
    "\t\tExitSpeed ( 2 )\r\n"
    "\t\tTemperature ( 120 )\r\n"
    "\t\tVapour ( 0.3 )\r\n"
    "\t\tSoot ( 0.5 )\r\n"
    "\t\tColorRGBA ( 404040ff )\r\n"
    "\t\tVariation ( 0.3 )\r\n"
    "\t)\r\n"
    "\tESD_TSREEmitter ( Name ( \"STACK\" ) Kind ( Stack ) Rotation ( 90 0 0 ) )\r\n"
    "\tESD_TSREEmitter ( Name ( \"VALVE\" ) Kind ( SafetyValves ) QDirection ( -0.7071068 0 0 0.7071068 ) )\r\n"
    "\tESD_TSREEmitter ( Name ( \"ODD\" ) Kind ( Fire ) Rotation ( 90 0 0 ) QDirection ( 0 0 0 1 ) ColorRGBA ( zz ) )\r\n"
    "\tESD_TSREEmitter ( Name ( \"OFF\" ) On ( 0 ) )\r\n"
    "\tESD_TSREEmitter ( Name ( \"LOST\" ) ShapeHierarchy ( \"NOSUCH\" ) )\r\n"
    ")\r\n";

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool near(float a, float b, float tolerance = 1e-3f) {
    return std::abs(a - b) <= tolerance;
}

bool near3(const float *a, float x, float y, float z) {
    return near(a[0], x) && near(a[1], y) && near(a[2], z);
}

const ShapeEmitters::Emitter *named(const QVector<ShapeEmitters::Emitter> &emitters, const QString &name) {
    for (const ShapeEmitters::Emitter &e : emitters)
        if (e.name == name)
            return &e;
    return nullptr;
}

bool same(const ShapeEmitters::Emitter &a, const ShapeEmitters::Emitter &b) {
    bool equal = a.name == b.name && a.hierarchy == b.hierarchy && a.kind == b.kind && near(a.diameter, b.diameter)
            && near(a.wetness, b.wetness) && near(a.variation, b.variation) && a.on == b.on
            && near(a.output.flow, b.output.flow) && near(a.output.speed, b.output.speed)
            && near(a.output.temperature, b.output.temperature) && near(a.output.steam, b.output.steam)
            && near(a.output.soot, b.output.soot) && near(a.output.beats, b.output.beats)
            && a.output.color == b.output.color;
    for (int c = 0; c < 3; ++c)
        equal = equal && near(a.position[c], b.position[c]) && near(a.direction[c], b.direction[c]);
    return equal;
}

} // namespace

int TsreTests::runShapeEmittersSuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    auto check = [&](bool condition, const char *name) {
        if (condition) {
            ++passed;
            if (verbose)
                qInfo() << "[tests:shape-emitters] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:shape-emitters] FAIL" << name;
        }
    };
    QScopedValueRollback<QString> backend(Game::renderBackend, QStringLiteral("qrhi"));
    OpenGL3Renderer frames;
    frames.resetFrame();

    QTemporaryDir dir;
    const QString source = dir.filePath("emitters.s");
    check(dir.isValid() && writeFile(source, QByteArray(Shape)), "write-test-shape");
    SFileComplex complex(source, "emitters.s", dir.path());
    complex.loadData();
    // The legacy backend reads it as TSRE saves it.
    const QString saved = dir.filePath("saved.s");
    QString error;
    const bool written = complex.save(saved, SFileComplex::Format::Text, false, &error)
            && writeFile(dir.filePath("saved.sd"), QByteArray(Sd)) && writeFile(dir.filePath("emitters.sd"), QByteArray(Sd));
    SFileComplex reread(source, "emitters.s", dir.path());
    reread.loadData();
    SFileLegacy legacy(saved, "saved.s", dir.path());
    legacy.load();
    check(written && reread.isLoaded() && legacy.isLoaded(), "test-shape-loads-in-both-backends");

    const QVector<ShapeEmitters::Emitter> &emitters = reread.emitters();
    QStringList names;
    for (const ShapeEmitters::Emitter &e : emitters)
        names << e.name;
    check(names == QStringList({"CHIMNEY", "STACK", "VALVE", "ODD", "OFF", "LOST"}), "blocks-read");
    const ShapeEmitters::Emitter *chimney = named(emitters, "CHIMNEY");
    check(chimney != nullptr && chimney->kind == "exhaust" && chimney->hierarchy == "PART"
          && near3(chimney->position, 0, 1, 0) && near3(chimney->direction, 0, 1, 0) && near(chimney->diameter, 0.4f)
          && near(chimney->output.flow, 0.05f) && near(chimney->output.speed, 2.0f)
          && near(chimney->output.temperature, 120.0f) && near(chimney->output.steam, 0.3f)
          && near(chimney->output.soot, 0.5f) && chimney->output.color == 0xff404040u
          && near(chimney->variation, 0.3f) && chimney->on && chimney->wetness == 0.0f,
          "values-given-override-the-kind-colour-as-argb");
    const ShapeEmitters::Emitter *stack = named(emitters, "STACK");
    check(stack != nullptr && near(stack->output.flow, 2.0f) && near(stack->output.beats, 4.0f)
          && near(stack->output.steam, 0.8f) && near(stack->wetness, 0.1f) && near3(stack->direction, 0, 0, 1)
          && near(stack->diameter, 0.2f),
          "kind-defaults-wetness-and-rotation-from-up");
    const ShapeEmitters::Emitter *valve = named(emitters, "VALVE");
    check(valve != nullptr && near(valve->output.speed, 100.0f) && valve->wetness == 0.0f
          && near3(valve->direction, 0, 0, -1),
          "qdirection-turns-up");
    const ShapeEmitters::Emitter *odd = named(emitters, "ODD");
    const QStringList &problems = reread.emitterProblems();
    int oddProblems = 0;
    for (const QString &p : problems)
        oddProblems += p.startsWith("ODD: ") ? 1 : 0;
    check(odd != nullptr && odd->kind == "steam" && near3(odd->direction, 0, 1, 0) && odd->output.color == 0
          && problems.size() == 3 && oddProblems == 3,
          "unknown-kind-steam-later-turn-bad-colour-named-problems");
    check(named(emitters, "OFF") != nullptr && !named(emitters, "OFF")->on, "on-0-read");
    bool equal = legacy.emitters().size() == emitters.size() && legacy.emitterProblems() == problems;
    for (int i = 0; equal && i < emitters.size(); ++i)
        equal = same(legacy.emitters()[i], emitters[i]);
    check(equal, "both-backends-read-alike");

    // A placed object's emitters into the scenery's pool: the chimney on
    // PART (5 m up) 1 m above it, x through the root's mirror; the one off
    // gives none; out of distance none.
    ExhaustParticles &particles = ExhaustParticles::instance();
    particles.clear();
    particles.setLimit(ExhaustParticles::Pool::Trains, 100);
    particles.setLimit(ExhaustParticles::Pool::Scenery, 1000);
    float model[16];
    Mat4::identity(model);
    model[12] = 10.0f;
    const float camera[3] = {0.0f, 0.0f, 0.0f};
    static int objectA, objectB;
    auto runShape = [&](const ComplexShape &shape, int count, const void *owner, quint32 uid, const float *at) {
        for (int f = 0; f < count; ++f) {
            frames.resetFrame();
            particles.beginFrame(1.0f / 30.0f, ExhaustParticles::Air());
            ShapeEmitters::giveOut(shape, 0, at, camera, owner, uid);
            particles.endFrame();
        }
    };
    auto runFrames = [&](int count, const void *owner, quint32 uid, const float *at) {
        runShape(reread, count, owner, uid, at);
    };
    runFrames(30, &objectA, 1, model);
    bool chimneyAbove = false;
    for (const ExhaustParticles::Particle &p : particles.particles())
        // The chimney's puffs rise from 6 m (the others leave the root).
        chimneyAbove = chimneyAbove || (std::abs(p.position[0] - 10.0f) < 0.6f && p.position[1] > 5.8f
                                        && p.position[1] < 7.5f && p.age < 0.5f);
    check(particles.count(ExhaustParticles::Pool::Scenery) > 0 && particles.count(ExhaustParticles::Pool::Trains) == 0
          && chimneyAbove,
          "placed-object-emits-into-the-scenery-pool-on-its-part");
    const quint64 before = particles.emitted();
    float far[16];
    Mat4::identity(far);
    far[12] = 2500.0f;
    runFrames(30, &objectB, 2, far);
    check(particles.emitted() == before, "none-beyond-the-distance");
    // Variation: two objects of one shape give out unlike flows.
    particles.clear();
    const quint64 startA = particles.emitted();
    runFrames(600, &objectA, 1, model);
    const quint64 a = particles.emitted() - startA;
    particles.clear();
    const quint64 start = particles.emitted();
    runFrames(600, &objectB, 7, model);
    const quint64 b = particles.emitted() - start;
    check(a != b, "variation-differs-by-object");
    // The Shape Viewer's (uid 0): no Variation, the shape's own flow, as
    // the same shape without Variation gives.
    QByteArray plainSd(Sd);
    plainSd.replace("\t\tVariation ( 0.3 )\r\n", "");
    const QString plainPath = dir.filePath("plain.s");
    const bool plainWritten = QFile::copy(source, plainPath) && writeFile(dir.filePath("plain.sd"), plainSd);
    SFileComplex plain(plainPath, "plain.s", dir.path());
    plain.loadData();
    particles.clear();
    quint64 mark = particles.emitted();
    runShape(reread, 600, &objectA, 0, model);
    const quint64 viewer = particles.emitted() - mark;
    particles.clear();
    mark = particles.emitted();
    runShape(plain, 600, &objectA, 0, model);
    const quint64 own = particles.emitted() - mark;
    check(plainWritten && plain.emitters().size() == emitters.size() && viewer > 0 && viewer == own,
          "uid-0-gives-the-shapes-own-flow");
    particles.clear();

    // Its author's messages: three problems, a matrix it has not.
    int messages = 0;
    for (const ErrorMessage *message : ErrorMessagesLib::ErrorMessages)
        messages += message != nullptr && message->action == reread.getPathId() ? 1 : 0;
    check(messages == 4, "author-messages-once-per-shape");

    // Reloaded in place (the legacy backend without the loader's thread):
    // its .sd read again, not added to (one of each emitter and light), its
    // author's messages told again after each reload.
    QByteArray lampSd(Sd);
    lampSd.replace("\tESD_Detail_Level ( 0 )\r\n",
                   "\tESD_Detail_Level ( 0 )\r\n\tESD_TSRELight ( Name ( \"LAMP\" ) Intensity ( 100 ) )\r\n");
    const bool lampWritten = writeFile(dir.filePath("saved.sd"), lampSd);
    legacy.reload();
    legacy.load();
    ShapeEmitters::report(legacy);
    const int lamps = legacy.lights().size();
    legacy.reload();
    legacy.load();
    ShapeEmitters::report(legacy);
    ShapeEmitters::report(legacy);
    int told = 0;
    for (const ErrorMessage *message : ErrorMessagesLib::ErrorMessages)
        told += message != nullptr && message->action == legacy.getPathId() ? 1 : 0;
    if (!(lamps == 1 && legacy.lights().size() == 1 && legacy.emitters().size() == emitters.size() && told == 8))
        qWarning() << "[tests:shape-emitters] reloaded: lamps" << lamps << legacy.lights().size() << "emitters"
                   << legacy.emitters().size() << "problems" << legacy.emitterProblems().size() << "told" << told;
    check(lampWritten && legacy.isLoaded() && lamps == 1 && legacy.lights().size() == 1
          && legacy.emitters().size() == emitters.size() && legacy.emitterProblems().size() == 3 && told == 8,
          "legacy-reload-in-place-reads-the-sd-again");

    qInfo() << "[tests:shape-emitters] cases=" << (passed + failed) << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
