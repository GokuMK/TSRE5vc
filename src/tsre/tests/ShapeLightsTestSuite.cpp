#include <tsre/tests/ShapeLightsTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QHash>
#include <QTranslator>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <cmath>
#include <tsre/ErrorMessage.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/renderer/DiscMesh.h>
#include <tsre/renderer/LightGrid.h>
#include <tsre/renderer/OpenGL3Renderer.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/shape/SFileLegacy.h>
#include <tsre/shape/ShapeLights.h>
#include <tsre/shape/ShapeLod.h>
#include <tsre/trains/CarLights.h>
#include <tsre/trains/EngLights.h>

namespace {

// Records the packets a shape submits, and whether each may cast shadows.
struct Recorder : RenderQueue {
    QVector<RenderItem *> packets;
    QVector<bool> casting;
    using RenderQueue::submit;
    void submit(RenderItem *item, quint32 = 0, SubmitOrder = SUBMIT_GROUPED) override {
        packets.push_back(item);
        casting.push_back(shadowCastingEnabled());
    }
    void submit(const QVector<RenderItem *> &items, quint32 id = 0) override {
        for (RenderItem *item : items)
            submit(item, id);
    }
    void submitFrameItem(RenderItem *item) override { submit(item); }
    // Packets of lights (no mesh).
    QVector<RenderItem *> lights() const {
        QVector<RenderItem *> out;
        for (RenderItem *item : packets)
            if (!item->mesh.handle.valid() && !item->lights.isEmpty())
                out << item;
        return out;
    }
    int lightCount() const {
        int count = 0;
        for (const RenderItem *item : lights())
            count += item->lights.size();
        return count;
    }
    // Discs: the shared disc, unlit.
    QVector<int> discs() const {
        QVector<int> out;
        for (int i = 0; i < packets.size(); ++i)
            if (packets[i]->mesh.handle.valid() && packets[i]->mesh.count == DiscMesh::Vertices
                    && !packets[i]->material.lit)
                out << i;
        return out;
    }
};

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

// Lights: a spot on PART 1 m above it, shining down (Rotation); a green
// point turned by QDirection, at half strength by ColorRGBA's alpha (and
// its leading zeros); both of each pair (the later used); two left out (no
// Intensity, an unknown Type); a cone out of range; a matrix the shape has
// not; one off until switched (On ( 0 )). LAMP has a view distance, the
// shape's last level is drawn on without its lights (task shapes 16).
const char Sd[] =
    "SIMISA@@@@@@@@@@JINX0t1t______\r\n\r\n"
    "shape ( lamp.s\r\n"
    "\tESD_Detail_Level ( 0 )\r\n"
    "\tESD_Alternative_Texture ( 0 )\r\n"
    "\tESD_TSREDistanceLevelExtension ( MeshOnly )\r\n"
    "\tESD_TSRELightSchedule ( Always )\r\n"
    "\tESD_TSRELight (\r\n"
    "\t\tName ( \"LAMP\" )\r\n"
    "\t\tType ( Spot )\r\n"
    "\t\tShapeHierarchy ( \"PART\" )\r\n"
    "\t\tPosition ( 0 1 0 )\r\n"
    "\t\tRotation ( -90 0 0 )\r\n"
    "\t\tColor3F ( 1 0.5 0.25 )\r\n"
    "\t\tIntensity ( 270 )\r\n"
    "\t\tRange ( 40 )\r\n"
    "\t\tViewDistance ( 150 )\r\n"
    "\t\tCone ( 30 60 )\r\n"
    "\t\tRadius ( 0.1 )\r\n"
    "\t)\r\n"
    "\tESD_TSRELight (\r\n"
    "\t\tName ( \"GREEN\" )\r\n"
    "\t\tQDirection ( -0.7071068 0 0 0.7071068 )\r\n"
    "\t\tColorRGBA ( 00ff0080 )\r\n"
    "\t\tIntensity ( 100 )\r\n"
    "\t)\r\n"
    "\tESD_TSRELight ( Name ( \"BOTH\" ) Position ( 1 2 3 ) ColorRGBA ( ff0000ff ) Color3F ( 0 0 1 ) "
    "QDirection ( 0 0 0 1 ) Rotation ( 90 0 0 ) Intensity ( 10 ) )\r\n"
    "\tESD_TSRELight ( Name ( \"DARK\" ) Type ( Point ) )\r\n"
    "\tESD_TSRELight ( Name ( \"AREA\" ) Type ( Area ) Intensity ( 5 ) )\r\n"
    "\tESD_TSRELight ( Name ( \"WIDE\" ) Type ( Spot ) Intensity ( 5 ) Cone ( 50 40 ) )\r\n"
    "\tESD_TSRELight ( Name ( \"LOST\" ) ShapeHierarchy ( \"NOSUCH\" ) Intensity ( 5 ) )\r\n"
    "\tESD_TSRELight ( Name ( \"OFF\" ) On ( 0 ) Intensity ( 7 ) )\r\n"
    ")\r\n";

// A triangle, and the spot of the .sd's LAMP as a glTF light (at the same
// place, shining down; Open Rails' name in its extras) and a point light
// named by glTF only.
const char Gltf[] = R"({
 "asset": {"version": "2.0"},
 "extensionsUsed": ["KHR_lights_punctual"],
 "extensions": {"KHR_lights_punctual": {"lights": [
  {"type": "spot", "name": "Spot.001", "intensity": 270, "color": [1, 0.5, 0.25], "range": 40,
   "spot": {"innerConeAngle": 0.5235988, "outerConeAngle": 1.0471976},
   "extras": {"OPENRAILS_light_name": "HEAD", "TSRE_viewDistance": 150}},
  {"type": "point", "name": "Fill", "intensity": 5, "extras": {"TSRE_on": false}}]}},
 "scene": 0,
 "scenes": [{"nodes": [0, 1, 2]}],
 "nodes": [
  {"mesh": 0},
  {"translation": [0, 6, 0], "rotation": [-0.7071068, 0, 0, 0.7071068],
   "extensions": {"KHR_lights_punctual": {"light": 0}}},
  {"translation": [2, 1, 0], "extensions": {"KHR_lights_punctual": {"light": 1}}}],
 "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
 "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                "min": [0, 0, 0], "max": [1, 1, 0]}],
 "bufferViews": [{"buffer": 0, "byteLength": 36}],
 "buffers": [{"byteLength": 36, "uri": "tri.bin"}]
})";

// A road car (task traffic 01): head and tail lights (any case) and a brake
// light off when parked, one off that no name switches, one always on;
// turn indicators, one on when parked (a spawned car keeps them off), and
// one whose side is not a word of its own.
const char CarSd[] =
    "SIMISA@@@@@@@@@@JINX0t1t______\r\n\r\n"
    "shape ( car.s\r\n"
    "\tESD_Detail_Level ( 0 )\r\n"
    "\tESD_Alternative_Texture ( 0 )\r\n"
    "\tESD_TSRELight ( Name ( \"HEAD_L\" ) On ( 0 ) Intensity ( 10 ) )\r\n"
    "\tESD_TSRELight ( Name ( \"tail_r\" ) On ( 0 ) Intensity ( 20 ) )\r\n"
    "\tESD_TSRELight ( Name ( \"BRAKE_L\" ) On ( 0 ) Intensity ( 30 ) )\r\n"
    "\tESD_TSRELight ( Name ( \"LAMP1\" ) On ( 0 ) Intensity ( 40 ) )\r\n"
    "\tESD_TSRELight ( Name ( \"ROOF\" ) Intensity ( 50 ) )\r\n"
    "\tESD_TSRELight ( Name ( \"TURN_L\" ) On ( 0 ) Intensity ( 60 ) )\r\n"
    "\tESD_TSRELight ( Name ( \"Turn_Right_Rear\" ) Intensity ( 70 ) )\r\n"
    "\tESD_TSRELight ( Name ( \"TURN_REAR\" ) On ( 0 ) Intensity ( 80 ) )\r\n"
    ")\r\n";

// A glTF road car's lights: named by glTF only, by Open Rails' name, by the
// glTF name where Open Rails' is none of a road car's, and neither.
const char CarGltf[] = R"({
 "asset": {"version": "2.0"},
 "extensionsUsed": ["KHR_lights_punctual"],
 "extensions": {"KHR_lights_punctual": {"lights": [
  {"type": "point", "name": "BRAKE_L", "intensity": 1},
  {"type": "point", "name": "Light.001", "intensity": 1, "extras": {"OPENRAILS_light_name": "HEAD_R"}},
  {"type": "point", "name": "TAIL_L", "intensity": 1, "extras": {"OPENRAILS_light_name": "REAR"}},
  {"type": "point", "name": "Light.002", "intensity": 1}]}},
 "scene": 0,
 "scenes": [{"nodes": [0, 1, 2, 3, 4]}],
 "nodes": [
  {"mesh": 0},
  {"extensions": {"KHR_lights_punctual": {"light": 0}}},
  {"extensions": {"KHR_lights_punctual": {"light": 1}}},
  {"extensions": {"KHR_lights_punctual": {"light": 2}}},
  {"extensions": {"KHR_lights_punctual": {"light": 3}}}],
 "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
 "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                "min": [0, 0, 0], "max": [1, 1, 0]}],
 "bufferViews": [{"buffer": 0, "byteLength": 36}],
 "buffers": [{"byteLength": 36, "uri": "tri.bin"}]
})";

// An ENG's lights binding the .sd's: LAMP the front headlight, GREEN the
// rear one (first reversed); a cone not bound; glows naming a matrix and
// nothing.
const char BoundLights[] = R"(Lights ( 5
 Light ( Type ( 1 ) Conditions ( Headlight ( 3 ) Unit ( 2 ) ) ShapeHierarchy ( "lamp" )
  States ( 1 State ( LightColour ( ffffffff ) Position ( 0 3 8 ) Radius ( 70 ) Angle ( 30 ) ) ) )
 Light ( Type ( 1 ) Conditions ( Headlight ( 3 ) Unit ( 5 ) ) ShapeHierarchy ( "GREEN" )
  States ( 1 State ( LightColour ( ffffffff ) Position ( 0 3 -8 ) Radius ( 70 ) Angle ( 30 ) ) ) )
 Light ( Type ( 1 ) Conditions ( Headlight ( 3 ) )
  States ( 1 State ( LightColour ( ffffffff ) Position ( 0 3 8 ) Radius ( 70 ) Angle ( 30 ) ) ) )
 Light ( Type ( 0 ) ShapeHierarchy ( "PART" ) States ( 1 State ( LightColour ( ffff0000 ) Position ( 0 1 0 ) Radius ( 0.2 ) ) ) )
 Light ( Type ( 0 ) ShapeHierarchy ( "NOWHERE" ) States ( 1 State ( LightColour ( ffff0000 ) Position ( 0 1 0 ) Radius ( 0.2 ) ) ) )
))";

// The same cone without bindings: its derived beam.
const char PlainLights[] = R"(Lights ( 1
 Light ( Type ( 1 ) Conditions ( Headlight ( 3 ) )
  States ( 1 State ( LightColour ( ffffffff ) Position ( 0 3 8 ) Radius ( 70 ) Angle ( 30 ) ) ) )
))";

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool near(float a, float b, float tolerance = 1e-3f) {
    return std::abs(a - b) <= tolerance;
}

bool near3(const float *a, float x, float y, float z, float tolerance = 1e-3f) {
    return near(a[0], x, tolerance) && near(a[1], y, tolerance) && near(a[2], z, tolerance);
}

const ShapeLights::Light *named(const QVector<ShapeLights::Light> &lights, const QString &name) {
    for (const ShapeLights::Light &light : lights)
        if (light.name == name)
            return &light;
    return nullptr;
}

bool same(const ShapeLights::Light &a, const ShapeLights::Light &b) {
    bool equal = a.name == b.name && a.bindName == b.bindName && a.on == b.on && a.carRole == b.carRole
            && a.hierarchy == b.hierarchy
            && a.type == b.type && near(a.intensity, b.intensity)
            && near(a.range, b.range) && near(a.radius, b.radius) && near(a.innerCone, b.innerCone)
            && near(a.outerCone, b.outerCone) && near(a.viewDistance, b.viewDistance);
    for (int c = 0; c < 3; ++c)
        equal = equal && near(a.position[c], b.position[c]) && near(a.direction[c], b.direction[c])
                && near(a.color[c], b.color[c]);
    return equal;
}

// A packet light in the shape's space: its place and way through msMatrix.
RenderItem::Light inShape(const RenderItem &packet, const RenderItem::Light &light) {
    RenderItem::Light out = light;
    if (packet.msMatrix == nullptr)
        return out;
    const float *m = packet.msMatrix;
    for (int r = 0; r < 3; ++r) {
        out.position[r] = m[r] * light.position[0] + m[4 + r] * light.position[1] + m[8 + r] * light.position[2]
                + m[12 + r];
        out.direction[r] = m[r] * light.direction[0] + m[4 + r] * light.direction[1] + m[8 + r] * light.direction[2];
    }
    return out;
}

// The packet light that has this intensity (candela over pi), placed in the
// shape's space.
bool packetLight(const Recorder &recorder, float candela, RenderItem::Light &out) {
    for (const RenderItem *packet : recorder.lights())
        for (const RenderItem::Light &light : packet->lights)
            if (near(light.intensity, candela / float(M_PI))) {
                out = inShape(*packet, light);
                return true;
            }
    return false;
}

} // namespace

int TsreTests::runShapeLightsSuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    auto check = [&](bool condition, const char *name) {
        if (condition) {
            ++passed;
            if (verbose)
                qInfo() << "[tests:shape-lights] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:shape-lights] FAIL" << name;
        }
    };
    const float degrees = float(M_PI / 180.0);
    QScopedValueRollback<QString> backend(Game::renderBackend, QStringLiteral("qrhi"));
    QScopedValueRollback<bool> localLights(Game::localLightsEnabled, true);
    // Steps the frames (Renderer::frameNumber).
    OpenGL3Renderer frames;
    frames.resetFrame();

    QTemporaryDir dir;
    const QString source = dir.filePath("lamp.s");
    check(dir.isValid() && writeFile(source, QByteArray(Shape)) && writeFile(dir.filePath("lamp.sd"), QByteArray(Sd)),
          "write-test-shape-and-sd");
    SFileComplex complex(source, "lamp.s", dir.path());
    complex.loadData();
    // The legacy backend reads it as TSRE saves it.
    const QString saved = dir.filePath("saved.s");
    QString error;
    const bool written = complex.save(saved, SFileComplex::Format::Text, false, &error)
            && writeFile(dir.filePath("saved.sd"), QByteArray(Sd));
    SFileLegacy legacy(saved, "saved.s", dir.path());
    legacy.load();
    check(complex.isLoaded() && written && legacy.isLoaded(), "test-shape-loads-in-both-backends");

    // Reading.
    const QVector<ShapeLights::Light> &lights = complex.lights();
    QStringList names;
    for (const ShapeLights::Light &light : lights)
        names << light.name;
    check(names == QStringList({"LAMP", "GREEN", "BOTH", "WIDE", "LOST", "OFF"}), "blocks-read-and-left-out");
    const ShapeLights::Light *off = named(lights, "OFF");
    check(off != nullptr && !off->on && named(lights, "LAMP")->on && named(lights, "LAMP")->bindName == "LAMP",
          "on-and-bind-names-read");
    const ShapeLights::Light *lamp = named(lights, "LAMP");
    check(lamp != nullptr && lamp->type == ShapeLights::Light::Type::Spot && lamp->hierarchy == "PART"
          && near3(lamp->position, 0, 1, 0) && near3(lamp->direction, 0, -1, 0) && near3(lamp->color, 1, 0.5f, 0.25f)
          && near(lamp->intensity, 270) && near(lamp->range, 40) && near(lamp->radius, 0.1f)
          && near(lamp->innerCone, 30 * degrees) && near(lamp->outerCone, 60 * degrees),
          "spot-values-rotation-down");
    const ShapeLights::Light *green = named(lights, "GREEN");
    check(green != nullptr && green->type == ShapeLights::Light::Type::Point && green->hierarchy.isEmpty()
          && near3(green->direction, 0, -1, 0) && near3(green->color, 0, 1, 0)
          && near(green->intensity, 100.0f * 128.0f / 255.0f) && near(green->radius, 0.05f) && near(green->range, 0),
          "qdirection-colorrgba-alpha-and-defaults");
    const ShapeLights::Light *both = named(lights, "BOTH");
    check(both != nullptr && near3(both->color, 0, 0, 1) && near(both->intensity, 10)
          && near3(both->direction, 0, 1, 0),
          "the-later-of-each-pair-wins");
    const ShapeLights::Light *wide = named(lights, "WIDE");
    check(wide != nullptr && near(wide->innerCone, 39 * degrees) && near(wide->outerCone, 40 * degrees),
          "cone-kept-within-glTFs-limits");
    const ShapeLights::Light *lost = named(lights, "LOST");
    check(lamp != nullptr && lost != nullptr && ShapeLights::partOf(complex, *lamp) == 1
          && ShapeLights::partOf(complex, *lost) == 0,
          "matrices-found-by-name-the-root-otherwise");
    auto problemsOf = [](const QStringList &problems, const QString &name) {
        int count = 0;
        for (const QString &problem : problems)
            count += problem.startsWith(name + ": ") ? 1 : 0;
        return count;
    };
    const QStringList &problems = complex.lightProblems();
    check(problems.size() == 5 && problemsOf(problems, "BOTH") == 2 && problemsOf(problems, "DARK") == 1
          && problemsOf(problems, "AREA") == 1 && problemsOf(problems, "WIDE") == 1,
          "problems-named-per-light");
    bool equal = legacy.lights().size() == lights.size() && legacy.lightProblems() == problems;
    for (int i = 0; equal && i < lights.size(); ++i)
        equal = same(legacy.lights()[i], lights[i]);
    check(equal, "both-backends-read-alike");
    check(complex.distanceLevelExtension() == ShapeLod::Extension::MeshOnly
          && legacy.distanceLevelExtension() == ShapeLod::Extension::MeshOnly && lamp != nullptr
          && near(lamp->viewDistance, 150) && green != nullptr && green->viewDistance == 0.0f,
          "distance-level-extension-and-view-distance-read");
    check(complex.lightSchedule() == ShapeLights::Schedule::Always && legacy.lightSchedule() == ShapeLights::Schedule::Always,
          "light-schedule-read-both-backends");

    // A material edited in the Shape Viewer: the .sd written again keeps
    // the lights.
    {
        SFileComplex edited(source, "lamp.s", dir.path());
        edited.loadData();
        SFileComplex::MaterialInfo body;
        bool set = edited.materialInfo(0, body);
        body.shader = "PbrTexDiff";
        body.factors.clearcoat = 1.0f;
        set = set && edited.setMaterial(0, body, &error);
        const QString editedPath = dir.filePath("edited.s");
        const bool editedSaved = set && edited.save(editedPath, SFileComplex::Format::Text, false, &error)
                && edited.saveMetadataFor(editedPath, &error);
        SFileComplex again(editedPath, "edited.s", dir.path());
        again.loadData();
        bool kept = editedSaved && again.lights().size() == lights.size()
                && again.distanceLevelExtension() == ShapeLod::Extension::MeshOnly
                && again.lightSchedule() == ShapeLights::Schedule::Always;
        for (int i = 0; kept && i < lights.size(); ++i)
            kept = same(again.lights()[i], lights[i]);
        if (!kept) {
            QFile sd(dir.filePath("edited.sd"));
            const QByteArray bytes = sd.open(QIODevice::ReadOnly) ? sd.readAll() : QByteArray();
            qWarning().noquote() << "[tests:shape-lights] edited" << set << editedSaved << error << again.lights().size()
                                 << QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData() + 2),
                                                       (bytes.size() - 2) / 2);
        }
        check(kept, "materials-editor-save-keeps-the-lights");
    }

    // Light schedules (task renderer 46): the names; an object's over its
    // shape's; the Night schedule's lights by night only.
    {
        using ShapeLights::Schedule;
        check(ShapeLights::scheduleFrom("") == Schedule::Default && ShapeLights::scheduleFrom("night") == Schedule::Night
                      && ShapeLights::scheduleFrom(" ALWAYS ") == Schedule::Always
                      && ShapeLights::scheduleFrom("Dusk") == Schedule::Unknown
                      && ShapeLights::scheduleName(Schedule::Always) == "Always"
                      && ShapeLights::scheduleName(Schedule::Night) == "Night"
                      && ShapeLights::scheduleName(Schedule::Default).isEmpty(),
              "light-schedule-names");
        const bool was = ShapeLights::night();
        ShapeLights::setNight(false);
        // By day: the shape's Always, unless its object says Night.
        const bool day = ShapeLights::lit(Schedule::Default, legacy) && ShapeLights::lit(Schedule::Always, legacy)
                && !ShapeLights::lit(Schedule::Night, legacy);
        ShapeLights::setNight(true);
        const bool night = ShapeLights::lit(Schedule::Night, legacy) && ShapeLights::lit(Schedule::Default, legacy);
        ShapeLights::setNight(was);
        check(day && night, "light-schedule-object-over-shape");
    }

    // Drawn: packets of lights, a part's on its matrix, no discs.
    for (ComplexShape *shape : {static_cast<ComplexShape *>(&complex), static_cast<ComplexShape *>(&legacy)}) {
        const bool isComplex = shape == &complex;
        Recorder recorder;
        shape->pushRenderItem(recorder, 0, 0);
        const QVector<RenderItem *> packets = recorder.lights();
        const RenderItem *part = nullptr, *root = nullptr;
        for (const RenderItem *packet : packets)
            (packet->lights.size() == 1 ? part : root) = packet;
        RenderItem::Light placed;
        check(packets.size() == 2 && recorder.lightCount() == 6 && part != nullptr && root != nullptr
              && part->msMatrix != nullptr && near3(part->msMatrix + 12, 0, 5, 0) && recorder.discs().isEmpty(),
              isComplex ? "complex-packets-per-matrix" : "legacy-packets-per-matrix");
        // The root's lights through the shape's root, as its vertices: x
        // mirrored (BOTH at x 1 drawn at -1).
        RenderItem::Light both;
        check(root != nullptr && packetLight(recorder, 10, both) && near3(both.position, -1, 2, 3),
              isComplex ? "complex-root-lights-as-the-shapes-vertices" : "legacy-root-lights-as-the-shapes-vertices");
        check(packetLight(recorder, 270, placed) && placed.type == RenderItem::Light::SPOT
              && near3(placed.position, 0, 6, 0) && near3(placed.direction, 0, -1, 0)
              && near(placed.innerCone, 30 * degrees) && near(placed.range, 40),
              isComplex ? "complex-packet-light-placed-in-candela-over-pi" : "legacy-packet-light-placed-in-candela-over-pi");
        // Kept between pushes, not made again (a detached copy of the
        // shape's lights once made them again every push).
        frames.resetFrame();
        Recorder again;
        shape->pushRenderItem(again, 0, 0);
        check(again.lights() == packets,
              isComplex ? "complex-packets-kept-between-pushes" : "legacy-packets-kept-between-pushes");
        Recorder picked;
        shape->pushRenderItem(picked, SelectionIdCodec::shapePart(0, 0), 0);
        check(picked.lights().isEmpty() && picked.discs().isEmpty(),
              isComplex ? "complex-picking-parts-no-lights" : "legacy-picking-parts-no-lights");

        // A posed part moves its light, from the next frame.
        const unsigned int posed = shape->newState();
        float local[16];
        Mat4::identity(local);
        local[13] = 2.0f;
        shape->setPartTransform(posed, 1, local);
        frames.resetFrame();
        Recorder moved;
        shape->pushRenderItem(moved, 0, posed);
        float expected[16];
        const bool known = shape->partMatrix(posed, 1, expected);
        RenderItem::Light raised;
        check(known && !near(expected[13], 5) && packetLight(moved, 270, raised)
              && near(raised.position[1], expected[13] + 1),
              isComplex ? "complex-posed-part-moves-its-light" : "legacy-posed-part-moves-its-light");
    }

    // Discs where the scene has no local light: QRhi with local lights off,
    // and the OpenGL renderer.
    // The OpenGL renderer draws shapes with a context.
    QOpenGLContext context;
    QOffscreenSurface surface;
    const bool gl = context.create() && (surface.setFormat(context.format()), surface.create(), true)
            && context.makeCurrent(&surface);
    for (int mode = 0; mode < 2; ++mode) {
        if (mode == 1 && !gl) {
            qWarning() << "[tests:shape-lights] no OpenGL context: the OpenGL renderer's discs not checked";
            continue;
        }
        QScopedValueRollback<QString> discBackend(Game::renderBackend, mode == 0 ? QString("qrhi") : QString("opengl"));
        QScopedValueRollback<bool> discLights(Game::localLightsEnabled, mode != 0);
        frames.resetFrame();
        Recorder recorder;
        complex.pushRenderItem(recorder, 0, 0);
        const QVector<int> discs = recorder.discs();
        bool plain = discs.size() == 5, castNone = true, lampDisc = false;
        for (int i : discs) {
            const RenderItem *disc = recorder.packets[i];
            plain = plain && disc->mesh.layout == RenderItem::V && disc->material.doubleSided
                    && disc->material.textureObject == 0;
            castNone = castNone && !recorder.casting[i];
            // LAMP's: at its place 6 m up, Radius wide, facing down (the
            // disc faces -z: its z axis up).
            lampDisc = lampDisc || (near3(disc->msMatrix + 12, 0, 6, 0) && near3(disc->msMatrix + 8, 0, 0.1f, 0));
        }
        check(plain && castNone && lampDisc && recorder.lights().size() == 2,
              mode == 0 ? "discs-with-local-lights-off" : "discs-on-the-opengl-renderer");
    }

    // On ( 0 ): dark, no disc, until switched on; a released state's lights
    // back to their own.
    {
        const int offIndex = ShapeLights::bound(complex, "off");
        const unsigned int switched = complex.newState();
        auto offLight = [&](unsigned int state) {
            Recorder recorder;
            complex.pushRenderItem(recorder, 0, state);
            for (const RenderItem *packet : recorder.lights())
                if (packet->lights.size() == 5)
                    return packet->lights[4].intensity;
            return -1.0f;
        };
        const float dark = offLight(switched);
        complex.setLightStrength(switched, offIndex, 1.0f);
        const float lit = offLight(switched);
        complex.releaseState(switched);
        const unsigned int again = complex.newState();
        check(offIndex == 5 && dark == 0.0f && near(lit, 7.0f / float(M_PI)) && again == switched
              && offLight(again) == 0.0f,
              "on-0-dark-until-switched-released-state-its-own");
    }

    // ENG lights bind shape lights (the binding step): the front light by
    // Unit 2, the rear one by Unit 5, a cone's reversed copy binding nothing,
    // no derived beam from an ENG binding a cone, its author's messages.
    {
        EngLightsBlock block{QString(BoundLights)}, plainBlock{QString(PlainLights)};
        const EngLights *eng = block.lights(), *plain = plainBlock.lights();
        const unsigned int car = complex.newState();
        CarLights carLights;
        // Each end's light in the car's state: LAMP on PART, GREEN first on
        // the root.
        auto shining = [&](const EngLights::Train &train, float &lamp, float &green) {
            frames.resetFrame();
            carLights.switchShapeLights(complex, car, *eng, train, "TEST.ENG");
            Recorder recorder;
            complex.pushRenderItem(recorder, 0, car);
            lamp = green = -1.0f;
            for (const RenderItem *packet : recorder.lights())
                (packet->lights.size() == 1 ? lamp : green) = packet->lights[0].intensity;
        };
        EngLights::Train leading;
        leading.last = false;
        float lamp, green;
        shining(leading, lamp, green);
        check(eng != nullptr && ShapeLights::bound(complex, "lamp") == 0 && near(lamp, 270.0f / float(M_PI))
              && green == 0.0f && near(carLights.shapeLightReach(), 40.0f),
              "eng-binds-the-front-light-by-unit");
        EngLights::Train flipped = leading;
        flipped.reversed = true;
        shining(flipped, lamp, green);
        check(lamp == 0.0f && near(green, 100.0f * 128.0f / 255.0f / float(M_PI))
              && near(carLights.shapeLightReach(), 250.0f),
              "reversed-copy-binds-nothing-the-rear-light-leads");
        EngLights::Train dark = leading;
        dark.headlight = EngLights::Train::HeadlightOff;
        shining(dark, lamp, green);
        check(lamp == 0.0f && green == 0.0f && carLights.shapeLightReach() == 0.0f, "headlights-off-both-off");
        Recorder beams, plainBeams;
        carLights.switchShapeLights(complex, car, *eng, leading, "TEST.ENG");
        carLights.push(beams, *eng, leading, 10.0f, 0);
        CarLights plainCar;
        plainCar.switchShapeLights(complex, car, *plain, leading, "PLAIN.ENG");
        plainCar.push(plainBeams, *plain, leading, 10.0f, 0);
        check(beams.lights().isEmpty() && plainBeams.lights().size() == 1,
              "no-derived-beams-once-cones-are-bound");
        int engMessages = 0, plainMessages = 0;
        for (const ErrorMessage *message : ErrorMessagesLib::ErrorMessages) {
            engMessages += message != nullptr && message->action == "TEST.ENG" ? 1 : 0;
            plainMessages += message != nullptr && message->action == "PLAIN.ENG" ? 1 : 0;
        }
        check(engMessages == 3 && plainMessages == 0, "binding-messages-matrix-unknown-no-beams");
    }

    // Its author's messages, once a shape: five problems, a matrix it has
    // not.
    int messages = 0;
    for (const ErrorMessage *message : ErrorMessagesLib::ErrorMessages)
        messages += message != nullptr && message->action == complex.getPathId() ? 1 : 0;
    check(messages == 6, "author-messages-once-per-shape");

    // glTF: names (Open Rails' extras first), the same light as the .sd's,
    // and discs only, its own packet carrying its lights.
    const QString gltfPath = dir.filePath("lamp.gltf");
    const float triangle[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    check(writeFile(gltfPath, QByteArray(Gltf))
          && writeFile(dir.filePath("tri.bin"), QByteArray(reinterpret_cast<const char *>(triangle), sizeof(triangle))),
          "write-test-gltf");
    GltfShape gltf(gltfPath, "lamp.gltf", QString());
    gltf.load();
    const unsigned int state = gltf.newState();
    QStringList gltfNames;
    for (const ShapeLights::Light &light : gltf.lights())
        gltfNames << light.name;
    check(gltf.isLoaded() && gltfNames == QStringList({"HEAD", "Fill"}), "gltf-light-names");
    check(ShapeLights::bound(gltf, "head") == 0 && ShapeLights::bound(gltf, "Spot.001") == -1
          && ShapeLights::bound(gltf, "Fill") == -1 && !gltf.lights()[1].on,
          "gltf-binds-by-open-rails-name-only-tsre-on");
    frames.resetFrame();
    Recorder sdLit, gltfLit;
    complex.pushRenderItem(sdLit, 0, 0);
    gltf.pushRenderItem(gltfLit, 0, state);
    RenderItem::Light fromSd, fromGltf;
    check(packetLight(sdLit, 270, fromSd) && packetLight(gltfLit, 270, fromGltf)
          && near3(fromGltf.position, fromSd.position[0], fromSd.position[1], fromSd.position[2])
          && near3(fromGltf.direction, fromSd.direction[0], fromSd.direction[1], fromSd.direction[2])
          && near3(fromGltf.color, fromSd.color[0], fromSd.color[1], fromSd.color[2])
          && near(fromGltf.range, fromSd.range) && near(fromSd.viewDistance, 150)
          && near(fromGltf.viewDistance, fromSd.viewDistance) && near(fromGltf.innerCone, fromSd.innerCone)
          && near(fromGltf.outerCone, fromSd.outerCone) && fromGltf.type == fromSd.type
          && gltfLit.discs().isEmpty(),
          "sd-light-as-the-gltf-light");
    {
        QScopedValueRollback<bool> off(Game::localLightsEnabled, false);
        frames.resetFrame();
        Recorder unlit;
        gltf.pushRenderItem(unlit, 0, state);
        check(unlit.discs().size() == 1 && unlit.lights().size() == 1 && unlit.lightCount() == 2,
              "gltf-discs-with-local-lights-off-lit-only");
    }

    // Road cars (task traffic 01): a car spawner's car lights its HEAD and
    // TAIL lights, its BRAKE lights while braking, the others as their On
    // says; the same shape parked keeps them as their On says. The lights
    // it leaves dark told to the author once a shape.
    {
        using Role = ShapeLights::CarRole;
        const QString carPath = dir.filePath("car.s");
        check(writeFile(carPath, QByteArray(Shape)) && writeFile(dir.filePath("car.sd"), QByteArray(CarSd)),
              "write-road-car");
        SFileComplex car(carPath, "car.s", dir.path());
        car.loadData();
        QVector<Role> roles;
        for (const ShapeLights::Light &light : car.lights())
            roles << light.carRole;
        check(car.isLoaded()
              && roles == QVector<Role>({Role::Running, Role::Running, Role::Brake, Role::None, Role::None,
                                         Role::TurnLeft, Role::TurnRight, Role::None}),
              "road-car-roles-by-name-any-case");
        // A turn indicator's side: a word of its own after TURN.
        bool sides = true;
        for (const auto &[name, role] : {std::pair{"TURN_L", Role::TurnLeft}, std::pair{"turn_left_front", Role::TurnLeft},
                                         std::pair{"TURNL", Role::TurnLeft}, std::pair{"TURN-R2", Role::TurnRight},
                                         std::pair{"TURN RIGHT", Role::TurnRight}, std::pair{"TURN_R_REAR", Role::TurnRight},
                                         std::pair{"TURN_REAR_L", Role::None}, std::pair{"TURN_LF", Role::None},
                                         std::pair{"TURNING", Role::None}, std::pair{"TURN", Role::None}})
            sides = sides && ShapeLights::carRole(QString::fromLatin1(name)) == role;
        check(sides, "road-car-turn-indicator-sides");
        // Each light's candela as drawn, in the .sd's order.
        auto shown = [&](unsigned int state) {
            frames.resetFrame();
            Recorder recorder;
            car.pushRenderItem(recorder, 0, state);
            QVector<int> candela;
            for (const RenderItem *packet : recorder.lights())
                for (const RenderItem::Light &light : packet->lights)
                    candela << int(std::lround(light.intensity * float(M_PI)));
            return candela;
        };
        const unsigned int parked = car.newState(), driving = car.newState();
        ShapeLights::switchCarLights(car, driving, false);
        const QVector<int> cruising = shown(driving);
        ShapeLights::switchCarLights(car, driving, true);
        const QVector<int> stopping = shown(driving);
        check(shown(parked) == QVector<int>({0, 0, 0, 0, 50, 0, 70, 0})
              && cruising == QVector<int>({10, 20, 0, 0, 50, 0, 0, 0})
              && stopping == QVector<int>({10, 20, 30, 0, 50, 0, 0, 0}),
              "road-car-lights-parked-driving-braking-turn-off");
        // The message as the error list shows it (without a catalogue,
        // qtTrId gives the id).
        QTranslator english;
        const bool translated = english.load(":/i18n/tsre_en.qm") && QCoreApplication::installTranslator(&english);
        ShapeLights::reportCarLights(car);
        ShapeLights::reportCarLights(car);
        if (translated)
            QCoreApplication::removeTranslator(&english);
        int carMessages = 0;
        bool namesDark = false;
        for (const ErrorMessage *message : ErrorMessagesLib::ErrorMessages)
            if (message != nullptr && message->action == car.getPathId()) {
                ++carMessages;
                namesDark = message->description.contains("lights LAMP1, TURN_REAR (On ( 0 ))");
            }
        check(translated && carMessages == 1 && namesDark, "road-car-dark-lights-told-once");

        // glTF: Open Rails' name or the glTF name, whichever is a road car's.
        const QString carGltfPath = dir.filePath("car.gltf");
        check(writeFile(carGltfPath, QByteArray(CarGltf)), "write-road-car-gltf");
        GltfShape gltfCar(carGltfPath, "car.gltf", QString());
        gltfCar.load();
        QHash<QString, Role> gltfRoles;
        for (const ShapeLights::Light &light : gltfCar.lights())
            gltfRoles.insert(light.name, light.carRole);
        check(gltfCar.isLoaded() && gltfRoles.size() == 4 && gltfRoles.value("BRAKE_L") == Role::Brake
              && gltfRoles.value("HEAD_R") == Role::Running && gltfRoles.value("REAR") == Role::Running
              && gltfRoles.value("Light.002", Role::Brake) == Role::None,
              "road-car-gltf-roles-by-either-name");
    }

    // Past the last level (task shapes 16; the test shape's one level reaches
    // 1000 m): what each extension draws there, on both backends; a name it
    // does not know told once a shape, as the default.
    if (ShapeLod::mode() != ShapeLod::Mode::Off) {
        struct Case {
            const char *value;
            ShapeLod::Extension extension;
            bool shape;
            bool lights;
        };
        const bool levels = ShapeLod::mode() == ShapeLod::Mode::Levels;
        const Case cases[] = {{nullptr, ShapeLod::Extension::Default, levels, levels},
                              {"Default", ShapeLod::Extension::Default, levels, levels},
                              {"All", ShapeLod::Extension::All, true, true},
                              {"meshonly", ShapeLod::Extension::MeshOnly, true, false},
                              {"None", ShapeLod::Extension::None, false, false},
                              {"Sideways", ShapeLod::Extension::Unknown, levels, levels}};
        bool all = true;
        // Without ESD_TSRELightSchedule: lights by night only (task renderer
        // 46).
        bool nightOnly = true;
        QStringList unknown;
        for (int i = 0; i < int(std::size(cases)); ++i) {
            const Case &c = cases[i];
            QByteArray sd = "SIMISA@@@@@@@@@@JINX0t1t______\r\n\r\nshape ( far.s\r\n";
            if (c.value != nullptr)
                sd += QByteArray("\tESD_TSREDistanceLevelExtension ( ") + c.value + " )\r\n";
            sd += ")\r\n";
            const QString stem = QStringLiteral("far%1").arg(i);
            const bool files = writeFile(dir.filePath(stem + ".s"), QByteArray(Shape))
                    && writeFile(dir.filePath(stem + ".sd"), sd) && QFile::copy(saved, dir.filePath(stem + "l.s"))
                    && writeFile(dir.filePath(stem + "l.sd"), sd);
            SFileComplex farComplex(dir.filePath(stem + ".s"), stem + ".s", dir.path());
            farComplex.loadData();
            SFileLegacy farLegacy(dir.filePath(stem + "l.s"), stem + "l.s", dir.path());
            farLegacy.load();
            for (ComplexShape *shape : {static_cast<ComplexShape *>(&farComplex), static_cast<ComplexShape *>(&farLegacy)}) {
                const unsigned int farState = shape->newState();
                const ShapeLod::Drawing nearby = ShapeLod::apply(*shape, farState, {10.0f, 0.0f});
                const ShapeLod::Drawing far = ShapeLod::apply(*shape, farState, {5000.0f, 0.0f});
                const ShapeLod::Drawing looked = ShapeLod::drawing(*shape, {5000.0f, 0.0f});
                const bool ok = files && shape->isLoaded() && shape->distanceLevelExtension() == c.extension && nearby.shape
                        && nearby.lights && far.shape == c.shape && far.lights == c.lights
                        && looked.shape == far.shape && looked.lights == far.lights
                        && ShapeLod::level(*shape, {5000.0f, 0.0f}) == (c.shape ? 0 : -1);
                if (!ok)
                    qWarning() << "[tests:shape-lights] extension" << (c.value ? c.value : "left out") << shape->getPathId()
                               << int(shape->distanceLevelExtension()) << nearby.shape << nearby.lights << far.shape
                               << far.lights << ShapeLod::level(*shape, {5000.0f, 0.0f});
                all = all && ok;
                const bool was = ShapeLights::night();
                ShapeLights::setNight(false);
                const bool byDay = ShapeLights::lit(ShapeLights::Schedule::Default, *shape);
                ShapeLights::setNight(true);
                const bool byNight = ShapeLights::lit(ShapeLights::Schedule::Default, *shape);
                ShapeLights::setNight(was);
                nightOnly = nightOnly && shape->lightSchedule() == ShapeLights::Schedule::Default && !byDay && byNight;
                if (c.extension == ShapeLod::Extension::Unknown)
                    unknown << shape->getPathId();
            }
        }
        check(all, "distance-level-extension-each-value-both-backends");
        check(nightOnly, "light-schedule-default-night-only");
        int told = 0;
        for (const ErrorMessage *message : ErrorMessagesLib::ErrorMessages)
            told += message != nullptr && unknown.contains(message->action) ? 1 : 0;
        check(unknown.size() == 2 && told == 2, "distance-level-extension-unknown-name-told-once-a-shape");
    }

    // As the renderer gathers them (task shapes 16): an object's lights it
    // drops light nothing, their glows stay; a light farther than its view
    // distance lights nothing; a new frame shows lights again.
    {
        struct Gatherer : OpenGL3Renderer {
            using QueueRenderer::gatherLights;
            void placeView(float x, float y, float z) {
                viewPosition[0] = x;
                viewPosition[1] = y;
                viewPosition[2] = z;
            }
        };
        Gatherer renderer;
        renderer.resetFrame();
        renderer.placeView(0.0f, 0.0f, 0.0f);
        RenderItem::Light lamp;
        lamp.intensity = 10.0f;
        RenderItem::Light within = lamp;
        within.position[2] = 100.0f;
        within.viewDistance = 200.0f;
        RenderItem::Light beyond = within;
        beyond.position[2] = 300.0f;
        RenderItem::Light glow = lamp;
        glow.emissive = true;
        RenderItem shown, nearer, farther, dropped, glowing;
        shown.lights = {lamp};
        nearer.lights = {within};
        farther.lights = {beyond};
        dropped.lights = {lamp};
        glowing.lights = {glow};
        renderer.submit(&shown);
        renderer.submit(&nearer);
        renderer.submit(&farther);
        renderer.setLightsShown(false);
        renderer.submit(&dropped);
        renderer.submit(&glowing);
        std::vector<LightGrid::Light> lit, glows;
        renderer.gatherLights(lit, 1.0f, 1.0f);
        renderer.gatherLights(glows, 0.0f, 1.0f, true, 1.0f);
        renderer.resetFrame();
        check(lit.size() == 2 && glows.size() == 1 && renderer.lightsShown(),
              "gathered-lights-dropped-and-view-distance");
    }

    if (gl)
        context.doneCurrent();
    qInfo() << "[tests:shape-lights] cases=" << (passed + failed) << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
