#include <tsre/tests/ShapeMaterialsTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <cmath>
#include <memory>
#include <tsre/Game.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/shape/MstsMaterials.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/shape/SFileLegacy.h>
#include <shapeViewer/ShapeMaterialsWindow.h>
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QTreeWidget>

namespace {

// Records the packets a shape submits, with their selection ids.
struct Recorder : RenderQueue {
    QVector<RenderItem *> packets;
    QVector<quint32> ids;
    QVector<RenderItem *> owned;
    using RenderQueue::submit;
    void submit(RenderItem *item, quint32 id = 0, SubmitOrder = SUBMIT_GROUPED) override {
        packets.push_back(item);
        ids.push_back(id);
    }
    void submit(const QVector<RenderItem *> &items, quint32 id = 0) override {
        for (RenderItem *item : items)
            submit(item, id);
    }
    void submitFrameItem(RenderItem *item) override {
        packets.push_back(item);
        ids.push_back(item->selectionId);
        if (!item->shared)
            owned.push_back(item);
    }
    ~Recorder() override { qDeleteAll(owned); }
};

// Two quads, one a matrix (MAIN and PART under it), each its own state.
const char TwoParts[] = R"(SIMISA@@@@@@@@@@JINX0s1t______
shape (
 shape_header ( 00000000 00000000 )
 shader_names ( 1 named_shader ( TexDiff ) )
 texture_filter_names ( 1 named_filter_mode ( MipLinear ) )
 points ( 8 point ( 0 0 0 ) point ( 1 0 0 ) point ( 1 1 0 ) point ( 0 1 0 )
  point ( 2 0 0 ) point ( 3 0 0 ) point ( 3 1 0 ) point ( 2 1 0 ) )
 uv_points ( 4 uv_point ( 0 1 ) uv_point ( 1 1 ) uv_point ( 1 0 ) uv_point ( 0 0 ) )
 normals ( 1 vector ( 0 0 1 ) )
 sort_vectors ( 0 ) colours ( 0 )
 matrices ( 2 matrix MAIN ( 1 0 0 0 1 0 0 0 1 0 0 0 ) matrix PART ( 1 0 0 0 1 0 0 0 1 0 0 0 ) )
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

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool near(float a, float b, float tolerance = 1e-3f) {
    return std::abs(a - b) <= tolerance;
}

// The PBR packets of a recorded frame.
QVector<RenderItem *> pbrPackets(const Recorder &recorder) {
    QVector<RenderItem *> out;
    for (RenderItem *item : recorder.packets)
        if (item->pbr.enabled)
            out << item;
    return out;
}

} // namespace

int TsreTests::runShapeMaterialsSuite(const QString &imagePath, bool verbose) {
    int passed = 0;
    int failed = 0;
    auto check = [&](bool condition, const char *name) {
        if (condition) {
            ++passed;
            if (verbose)
                qInfo() << "[tests:shape-materials] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:shape-materials] FAIL" << name;
        }
    };
    using MstsMaterials::Factors;

    // Shader names and keys.
    check(MstsMaterials::shader("pbrtexdiff") == MstsMaterials::Shader::Pbr
          && MstsMaterials::shader("BLENDAPBRTEXDIFF") == MstsMaterials::Shader::PbrBlended
          && MstsMaterials::shader("TexDiff") == MstsMaterials::Shader::Other
          && MstsMaterials::key("Textures\\Body.ACE") == "body.ace",
          "pbr-shader-names-and-material-keys");

    // The .sd line's values: positional, defaults left out.
    {
        const Factors read = Factors::read({0.5f, 0.6f, 0.7f, 1.0f, 0.0f, 0.3f});
        const QVector<float> written = read.values(false);
        check(near(read.tint[1], 0.6f) && near(read.roughness, 0.3f) && read.clearcoat == 0.0f
              && written.size() == 6 && near(written[5], 0.3f),
              "factors-read-and-write-up-to-the-last-set");
        const Factors defaults;
        check(defaults.values(true).isEmpty() && defaults.values(false).isEmpty()
              && defaults.metalnessFor(true) == 1.0f && defaults.metalnessFor(false) == 0.0f
              && defaults.roughnessFor(false) == 0.5f,
              "unset-metalness-and-roughness-follow-the-orm");
        Factors coat;
        coat.clearcoat = 1.0f;
        coat.clearcoatRoughness = 0.05f;
        const QVector<float> v = coat.values(true);
        check(v.size() == 10 && near(v[4], 1.0f) && near(v[5], 1.0f) && near(v[8], 1.0f) && near(v[9], 0.05f),
              "clearcoat-line-writes-the-defaults-before-it");
    }

    // Tangents of a quad mapped as an image (v grows down the image, up
    // the quad's y): the tangent along +x, the bitangent up (+y).
    {
        const float points[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        const float uvs[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        auto source = [&](int v, float *p, float *n, float *uv) {
            std::copy(points[v], points[v] + 3, p);
            n[0] = 0.0f;
            n[1] = 0.0f;
            n[2] = 1.0f;
            std::copy(uvs[v], uvs[v] + 2, uv);
            return true;
        };
        std::vector<float> t;
        MstsMaterials::tangents(4, source, {0, 1, 2, 0, 2, 3}, t);
        bool right = t.size() == 16;
        for (int v = 0; right && v < 4; ++v) {
            const float *tangent = &t[size_t(v) * 4];
            // b = cross(n, t) * w with n = +z: (-t.y, t.x, 0) * w.
            const float by = tangent[0] * tangent[3];
            right = near(tangent[0], 1.0f) && near(tangent[1], 0.0f) && by > 0.99f;
        }
        check(right, "generated-bitangent-points-up-the-image");
    }

    QTemporaryDir dir;
    check(dir.isValid(), "temporary-directory");

    // ORM packing: maps and constants into one RGBA texture.
    {
        QImage occlusion(8, 8, QImage::Format_RGB32), roughness(4, 4, QImage::Format_Grayscale8);
        occlusion.fill(QColor(255, 255, 255));
        roughness.fill(64);
        const QString o = dir.filePath("ao.png"), r = dir.filePath("rough.png"), out = dir.filePath("orm.png");
        occlusion.save(o);
        roughness.save(r);
        MstsMaterials::OrmSources sources;
        sources.files[0] = o;
        sources.files[1] = r;
        sources.constants[2] = 0.0f;
        sources.constants[3] = 1.0f;
        QString error;
        const bool packed = MstsMaterials::packOrm(sources, out, &error);
        QImage orm(out);
        orm = orm.convertToFormat(QImage::Format_RGBA8888);
        const uchar *p = orm.isNull() ? nullptr : orm.constBits();
        check(packed && orm.width() == 8 && p != nullptr && p[0] == 255 && std::abs(int(p[1]) - 64) <= 1
              && p[2] == 0 && p[3] == 255,
              "orm-packs-maps-and-constants-scaled-to-the-largest");
    }

    // Editing a complete shape.
    QScopedValueRollback<QString> backend(Game::renderBackend, QStringLiteral("qrhi"));
    const QString source = dir.filePath("parts.s");
    check(writeFile(source, QByteArray(TwoParts)), "write-test-shape");
    SFileComplex shape(source, "parts.s", dir.path());
    check(shape.loadData() && shape.retention() == SFileComplex::Retention::Complete && shape.materialCount() == 2,
          "test-shape-loads-complete");
    SFileComplex::MaterialInfo body, part;
    check(shape.materialInfo(0, body) && body.name == "Body" && body.shader == "TexDiff"
          && body.textures == QStringList{"body.ace"} && body.lightMaterial == -5 && body.matrixName == "MAIN"
          && body.uses == 1 && shape.materialInfo(1, part) && part.matrixName == "PART",
          "material-info-reads-states");

    // A PBR material with factors in the .sd.
    SFileComplex::MaterialInfo pbr = body;
    pbr.shader = "PbrTexDiff";
    pbr.textures = {"body.ace", "body_orm.png", "body_n.png"};
    pbr.lightMaterial = -6;
    pbr.factors.clearcoat = 1.0f;
    pbr.factors.clearcoatRoughness = 0.05f;
    QString error;
    const bool set = shape.setMaterial(0, pbr, &error);
    SFileComplex::MaterialInfo edited;
    shape.materialInfo(0, edited);
    if (!set || edited.shader != "PbrTexDiff")
        qWarning() << "[tests:shape-materials] setMaterial" << set << error << edited.shader << edited.textures
                   << edited.lightMaterial << edited.factors.clearcoat << shape.diagnostics();
    check(set && shape.materialInfo(0, edited) && edited.shader == "PbrTexDiff" && edited.textures.size() == 3
          && edited.textures[2] == "body_n.png" && edited.lightMaterial == -6 && near(edited.factors.clearcoat, 1.0f)
          && shape.metadataEdited() && shape.materialInfo(1, part) && part.lightMaterial == -5,
          "set-material-writes-shader-textures-light-and-factors");

    // Its packets: the PBR one, each part with its number.
    {
        Recorder recorder;
        shape.pushRenderItem(recorder, SelectionIdCodec::shapePart(3, 0), 0);
        const QVector<RenderItem *> pbrItems = pbrPackets(recorder);
        check(pbrItems.size() == 1 && pbrItems[0]->mesh.layout == RenderItem::PBR
              && near(pbrItems[0]->pbr.clearcoat, 1.0f) && pbrItems[0]->pbr.textures[RenderItem::Pbr::MAP_NORMAL] >= 0
              && pbrItems[0]->pbr.clearcoatInMetallicRoughnessAlpha,
              "pbr-material-draws-through-the-pbr-packet");
        check(recorder.ids.size() == 2 && recorder.ids[0] == SelectionIdCodec::shapePart(3, 0)
              && recorder.ids[1] == SelectionIdCodec::shapePart(3, 1)
              && shape.drawnPrimitiveCount(0) == 2 && shape.drawnPrimitiveMaterial(0, 1) == 1,
              "shape-part-ids-number-each-primitive");
    }

    // The second part (matrix PART) given the first's material: a state of
    // its own for its matrix.
    {
        const bool assigned = shape.assignMaterial(0, {1}, 0, false, &error);
        SFileComplex::MaterialInfo made, old;
        const int state = shape.drawnPrimitiveMaterial(0, 1);
        check(assigned && shape.materialCount() == 3 && state == 2 && shape.materialInfo(2, made)
              && made.shader == "PbrTexDiff" && made.matrixName == "PART" && made.lightMaterial == -6
              && made.textures.size() == 3 && shape.materialInfo(1, old) && old.uses == 0,
              "assign-makes-a-state-for-the-parts-matrix");
        check(shape.removeMaterial(1, &error) && shape.materialCount() == 2
              && shape.drawnPrimitiveMaterial(0, 1) == 1,
              "remove-unused-material-renumbers-the-parts");
        check(!shape.removeMaterial(0, &error) && !error.isEmpty(), "used-material-is-not-removed");
    }

    // Undo.
    {
        const auto before = shape.snapshot();
        shape.addMaterial(0);
        const int added = shape.materialCount();
        check(added == 3 && shape.restore(*before) && shape.materialCount() == 2, "undo-restores-the-documents");
    }

    // Saved and loaded again, text and compressed binary, with the .sd.
    {
        const QString text = dir.filePath("saved.s");
        const bool saved = shape.save(text, SFileComplex::Format::Text, false, &error)
                && shape.saveMetadataFor(text, &error);
        QFile sd(dir.filePath("saved.sd"));
        const QByteArray sdText = sd.open(QIODevice::ReadOnly) ? sd.readAll() : QByteArray();
        const QString sdString = QString::fromUtf16(reinterpret_cast<const char16_t *>(sdText.constData() + 2),
                                                    (sdText.size() - 2) / 2);
        check(saved && sdString.contains("ESD_TSREMaterial", Qt::CaseInsensitive)
              && sdString.contains("saved.s"),
              "save-writes-the-shape-and-its-sd-line");
        SFileComplex again(text, "saved.s", dir.path());
        SFileComplex::MaterialInfo info;
        check(again.loadData() && again.materialCount() == 2 && again.materialInfo(1, info)
              && info.shader == "PbrTexDiff" && info.matrixName == "PART" && near(info.factors.clearcoat, 1.0f)
              && near(info.factors.clearcoatRoughness, 0.05f),
              "text-round-trip-keeps-materials-and-factors");
        const QString binary = dir.filePath("binary.s");
        SFileComplex::MaterialInfo b;
        const bool binarySaved = again.save(binary, SFileComplex::Format::Binary, true, &error);
        SFileComplex fromBinary(binary, "binary.s", dir.path());
        check(binarySaved && fromBinary.loadData() && fromBinary.materialInfo(0, b) && b.textures.size() == 3
              && b.shader == "PbrTexDiff",
              "compressed-binary-round-trip-keeps-the-textures");

        // The legacy backend (the route editor's) draws it alike.
        SFileLegacy legacy(text, "saved.s", dir.path());
        legacy.load();
        Recorder recorder;
        legacy.pushRenderItem(recorder, 0, 0);
        const QVector<RenderItem *> items = pbrPackets(recorder);
        bool legacyPbr = items.size() == 2;
        for (const RenderItem *item : items)
            legacyPbr = legacyPbr && item->mesh.layout == RenderItem::PBR && near(item->pbr.clearcoat, 1.0f)
                    && item->material.surface == RenderItem::SURFACE_OPAQUE;
        check(legacy.isLoaded() && legacyPbr, "legacy-backend-reads-pbr-states-and-sd-factors");
        Recorder picked;
        legacy.pushRenderItem(picked, SelectionIdCodec::shapePart(0, 0), 0);
        check(picked.ids.size() == 2 && picked.ids[1] == SelectionIdCodec::shapePart(0, 1),
              "legacy-backend-numbers-primitives");
        // Loaded on a worker, as the route view loads it (ShapeLoader): the
        // shape that adopts the copy keeps the .sd's factors.
        SFileLegacy adopting(text, "saved.s", dir.path());
        std::unique_ptr<ComplexShape> copy(adopting.detachedCopy());
        copy->loadDetached();
        adopting.adopt(*copy);
        Recorder adopted;
        adopting.pushRenderItem(adopted, 0, 0);
        const QVector<RenderItem *> adoptedItems = pbrPackets(adopted);
        bool adoptedCoat = adoptedItems.size() == 2;
        for (const RenderItem *item : adoptedItems)
            adoptedCoat = adoptedCoat && near(item->pbr.clearcoat, 1.0f) && near(item->pbr.clearcoatRoughness, 0.05f);
        check(adopting.isLoaded() && adoptedCoat, "legacy-backend-loaded-on-a-worker-keeps-sd-factors");
    }

    // MSTS specular light materials reach the packets.
    {
        Recorder recorder;
        SFileComplex again(dir.filePath("saved.s"), "saved.s", dir.path());
        again.loadData();
        SFileComplex::MaterialInfo info;
        again.materialInfo(0, info);
        info.shader = "TexDiff";
        info.textures = QStringList{"body.ace"};
        info.lightMaterial = -7;
        again.setMaterial(0, info, &error);
        again.pushRenderItem(recorder, 0, 0);
        bool glint = false;
        for (const RenderItem *item : recorder.packets)
            glint = glint || (!item->pbr.enabled && item->material.specularPower == 750.0f);
        check(glint, "specular-light-material-sets-the-power");
    }

    // The materials window (offscreen): a material picked, its shader
    // changed and applied, then undone.
    if (qobject_cast<QApplication *>(QCoreApplication::instance()) != nullptr) {
        const QString path = dir.filePath("window.s");
        writeFile(path, QByteArray(TwoParts));
        SFileComplex edited(path, "window.s", dir.path());
        edited.loadData();
        ShapeMaterialsWindow window(nullptr, nullptr);
        window.setShape(&edited, dir.path());
        auto *materials = window.findChild<QTreeWidget *>("materialsList");
        auto *shader = window.findChild<QComboBox *>("materialsShader");
        auto *applyButton = window.findChild<QPushButton *>("materialsApply");
        auto *undoButton = window.findChild<QPushButton *>("materialsUndo");
        const bool found = materials && shader && applyButton && undoButton
                && materials->topLevelItemCount() == 2;
        SFileComplex::MaterialInfo info;
        bool applied = false, undone = false;
        if (found) {
            materials->setCurrentItem(materials->topLevelItem(1));
            shader->setCurrentText("PbrTexDiff");
            // The window with a PBR material, for a look at its layout.
            if (!imagePath.isEmpty()) {
                window.resize(1180, 1000);
                check(window.grab().save(imagePath), "materials-window-image-saved");
            }
            applyButton->click();
            applied = edited.materialInfo(1, info) && info.shader == "PbrTexDiff"
                    && materials->topLevelItem(1)->text(2) == "PbrTexDiff" && undoButton->isEnabled()
                    && near(info.factors.occlusion, 0.6f);
            undoButton->click();
            undone = edited.materialInfo(1, info) && info.shader == "TexDiff";
        }
        check(found && applied && undone, "materials-window-applies-and-undoes-new-pbr-occlusion-0.6");
    }

    qInfo() << "[tests:shape-materials] cases=" << (passed + failed) << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
