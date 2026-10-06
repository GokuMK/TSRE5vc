#include <tsre/tests/GltfPbrTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <vector>

#include <tsre/math3d/GLMatrix.h>
#include <tsre/renderer/Mesh.h>
#include <tsre/renderer/OpenGL3Renderer.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/shape/GltfShape.h>

namespace {

// Records the packets a shape submits.
struct PacketRecorder : RenderQueue {
    QVector<RenderItem *> packets;
    using RenderQueue::submit;
    void submit(RenderItem *item, quint32 = 0, SubmitOrder = SUBMIT_GROUPED) override {
        packets.push_back(item);
    }
    void submit(const QVector<RenderItem *> &items, quint32 = 0) override {
        packets += items;
    }
    void submitFrameItem(RenderItem *item) override {
        packets.push_back(item);
    }
};

struct LoadedShape {
    std::unique_ptr<GltfShape> shape;
    PacketRecorder recorder;
};

std::unique_ptr<LoadedShape> load(const QString &root, const QString &model) {
    auto loaded = std::make_unique<LoadedShape>();
    QString path = QDir(root).filePath("Models/" + model + "/glTF-Binary/" + model + ".glb");
    if (!QFileInfo::exists(path))
        path = QDir(root).filePath("Models/" + model + "/glTF/" + model + ".gltf");
    loaded->shape = std::make_unique<GltfShape>(path, QFileInfo(path).fileName(), QString());
    loaded->shape->load();
    const unsigned int state = loaded->shape->newState();
    loaded->shape->pushRenderItem(loaded->recorder, 0, state);
    return loaded;
}

// The uploaded vertices of a packet (PBR layout).
std::vector<float> vertices(QOpenGLContext &context, const RenderItem *item) {
    std::vector<float> values;
    Meshes::Buffers buffers;
    if (!Meshes::prepare(item->mesh.handle, context.functions(), buffers))
        return values;
    QOpenGLExtraFunctions *e = context.extraFunctions();
    e->glBindBuffer(GL_COPY_READ_BUFFER, buffers.vertexBuffer);
    GLint size = 0;
    e->glGetBufferParameteriv(GL_COPY_READ_BUFFER, GL_BUFFER_SIZE, &size);
    if (size > 0) {
        if (const void *mapped = e->glMapBufferRange(GL_COPY_READ_BUFFER, 0, size, GL_MAP_READ_BIT)) {
            values.resize(size_t(size) / sizeof(float));
            std::memcpy(values.data(), mapped, values.size() * sizeof(float));
            e->glUnmapBuffer(GL_COPY_READ_BUFFER);
        }
    }
    e->glBindBuffer(GL_COPY_READ_BUFFER, 0);
    return values;
}

}

int TsreTests::runGltfPbrGlSuite(bool verbose) {
    const QString root = qEnvironmentVariable("TSRE_GLTF_SAMPLE_ASSETS");
    if (root.isEmpty() || !QFileInfo::exists(root + "/Models")) {
        qWarning() << "[tests:gltf-pbr-gl] set TSRE_GLTF_SAMPLE_ASSETS to a clone of"
                   << "KhronosGroup/glTF-Sample-Assets";
        return 2;
    }
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name) {
        if (ok) {
            ++passed;
            if (verbose) qInfo() << "[tests:gltf-pbr-gl] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:gltf-pbr-gl] FAIL" << name;
        }
    };
    QOpenGLContext context;
    if (!context.create()) return 2;
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!context.makeCurrent(&surface)) return 2;
    const int stride = RenderItem::PBR;

    {
        auto spheres = load(root, "MetalRoughSpheresNoTextures");
        std::set<float> metallic, roughness;
        bool pbr = !spheres->recorder.packets.isEmpty();
        for (const RenderItem *item : spheres->recorder.packets) {
            pbr &= item->pbr.enabled && item->mesh.layout == RenderItem::PBR;
            metallic.insert(item->pbr.metallic);
            roughness.insert(item->pbr.roughness);
        }
        check(pbr, "glTF packets use the PBR layout and material");
        check(metallic.size() >= 2 && roughness.size() >= 5
              && *roughness.begin() >= 0.0f && *roughness.rbegin() <= 1.0f,
              "metallic and roughness factors reach the packets");
    }
    {
        auto settings = load(root, "TextureSettingsTest");
        bool clamp = false, mirror = false, singleSided = false, doubleSided = false;
        for (const RenderItem *item : settings->recorder.packets) {
            for (int axis = 0; axis < 2; ++axis) {
                clamp |= item->pbr.wrap[RenderItem::Pbr::MAP_BASE_COLOR][axis] == GL_CLAMP_TO_EDGE;
                mirror |= item->pbr.wrap[RenderItem::Pbr::MAP_BASE_COLOR][axis] == GL_MIRRORED_REPEAT;
            }
            (item->material.doubleSided ? doubleSided : singleSided) = true;
        }
        check(clamp && mirror, "sampler wrap modes reach the packets");
        check(singleSided && doubleSided, "doubleSided reaches the packets");
    }
    {
        auto multiUv = load(root, "MultiUVTest");
        bool secondSet = false;
        for (const RenderItem *item : multiUv->recorder.packets)
            secondSet |= item->pbr.texCoords[RenderItem::Pbr::MAP_EMISSIVE] == 1
                    && item->pbr.textures[RenderItem::Pbr::MAP_EMISSIVE] >= 0;
        check(secondSet, "a map can use the second texture coordinates");
    }
    {
        auto emissive = load(root, "EmissiveStrengthTest");
        float strongest = 0.0f;
        for (const RenderItem *item : emissive->recorder.packets)
            for (float c : item->pbr.emissive)
                strongest = std::max(strongest, c);
        check(strongest > 1.0f, "emissive strength scales the emissive factor");
        auto unlit = load(root, "UnlitTest");
        bool allUnlit = !unlit->recorder.packets.isEmpty();
        for (const RenderItem *item : unlit->recorder.packets)
            allUnlit &= item->pbr.unlit;
        check(allUnlit, "KHR_materials_unlit is recognised");
        auto blending = load(root, "AlphaBlendModeTest");
        bool blend = false, mask = false;
        for (const RenderItem *item : blending->recorder.packets) {
            blend |= item->pbr.blend && item->material.surface == RenderItem::SURFACE_BLENDED;
            mask |= item->pbr.alphaCutoff >= 0.0f;
        }
        check(blend && mask, "BLEND and MASK alpha modes reach the packets");
    }
    {
        auto colors = load(root, "VertexColorTest");
        bool coloured = false;
        for (const RenderItem *item : colors->recorder.packets) {
            const std::vector<float> data = vertices(context, item);
            for (size_t v = 0; v + stride <= data.size() && !coloured; v += stride)
                coloured = data[v + 15] < 0.5f || data[v + 16] < 0.5f || data[v + 17] < 0.5f;
        }
        check(coloured, "vertex colours reach the vertices");
    }
    {
        auto transforms = load(root, "TextureTransformTest");
        bool offset = false, rotation = false;
        for (const RenderItem *item : transforms->recorder.packets) {
            const float *rows = item->pbr.uvTransforms[RenderItem::Pbr::MAP_BASE_COLOR].rows;
            offset |= rows[0] == 1.0f && rows[2] == 0.5f && rows[5] == 0.0f;
            // 22.5 degrees turns the image clockwise: +sin in the first row.
            rotation |= std::abs(rows[1] - std::sin(0.3926991f)) < 1e-4f
                    && std::abs(rows[3] + std::sin(0.3926991f)) < 1e-4f;
        }
        check(offset && rotation, "KHR_texture_transform reaches the packets");
        auto coat = load(root, "ClearCoatTest");
        bool coated = false, uncoated = false;
        for (const RenderItem *item : coat->recorder.packets) {
            coated |= item->pbr.clearcoat == 1.0f && std::abs(item->pbr.clearcoatRoughness - 0.03f) < 1e-4f;
            uncoated |= item->pbr.clearcoat == 0.0f;
        }
        check(coated && uncoated, "KHR_materials_clearcoat reaches the packets");
    }
    {
        auto specular = load(root, "SpecularTest");
        bool weak = false, tinted = false;
        for (const RenderItem *item : specular->recorder.packets) {
            weak |= item->pbr.specular < 0.1f;
            tinted |= item->pbr.specularColor[2] < item->pbr.specularColor[0];
        }
        check(weak && tinted, "KHR_materials_specular reaches the packets");
        auto ior = load(root, "IORTestGrid");
        std::set<float> values;
        for (const RenderItem *item : ior->recorder.packets)
            values.insert(item->pbr.ior);
        check(values.count(1.0f) && values.count(1.33f) && values.count(2.42f),
              "KHR_materials_ior reaches the packets");
    }
    {
        auto transmissive = load(root, "TransmissionTest");
        bool through = false;
        for (const RenderItem *item : transmissive->recorder.packets)
            through |= item->pbr.transmission == 1.0f;
        check(through, "KHR_materials_transmission reaches the packets");
        auto volume = load(root, "CompareVolume");
        bool absorbing = false;
        for (const RenderItem *item : volume->recorder.packets)
            absorbing |= std::abs(item->pbr.thickness - 0.75f) < 1e-4f
                    && std::abs(item->pbr.attenuationDistance - 0.25f) < 1e-4f
                    && std::abs(item->pbr.attenuationColor[0] - 0.15f) < 1e-4f
                    && item->pbr.textures[RenderItem::Pbr::MAP_THICKNESS] >= 0;
        check(absorbing, "KHR_materials_volume reaches the packets");
        // Transmissive packets draw in their own pass, the rest as before.
        OpenGL3Renderer renderer;
        renderer.resetFrame();
        Mat4::identity(renderer.transform());
        const unsigned int state = volume->shape->newState();
        volume->shape->pushRenderItem(renderer, 0, state);
        check(renderer.queuedGroupedCount(Renderer::PASS_TRANSMISSION) == 2
              && renderer.queuedGroupedCount(Renderer::PASS_OPAQUE) > 0,
              "transmissive packets go to the transmission pass");
        renderer.resetFrame();
    }
    // Tangents: given (NormalTangentMirrorTest) or generated (DamagedHelmet)
    // must be unit length, across the normal, with a handedness sign.
    for (const char *model : {"NormalTangentMirrorTest", "DamagedHelmet"}) {
        auto shape = load(root, model);
        int good = 0, total = 0;
        for (const RenderItem *item : shape->recorder.packets) {
            if (item->pbr.textures[RenderItem::Pbr::MAP_NORMAL] < 0)
                continue;
            const std::vector<float> data = vertices(context, item);
            for (size_t v = 0; v + stride <= data.size(); v += stride * 7) {
                const float *n = &data[v + 3], *t = &data[v + 9];
                const float length = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
                const float across = std::abs(n[0] * t[0] + n[1] * t[1] + n[2] * t[2]);
                good += std::abs(length - 1.0f) < 0.02f && across < 0.05f
                        && std::abs(std::abs(t[3]) - 1.0f) < 1e-4f;
                ++total;
            }
        }
        check(total > 0 && good >= total * 0.98,
              model[0] == 'N' ? "file tangents are kept" : "missing tangents are generated");
    }
    check(context.functions()->glGetError() == GL_NO_ERROR, "no GL errors");
    context.doneCurrent();
    qInfo() << "[tests:gltf-pbr-gl] cases=" << (passed + failed)
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
