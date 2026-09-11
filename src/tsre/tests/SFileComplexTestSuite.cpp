#include <QDataStream>
#include <cstdio>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>
#include <QJsonArray>
#include "SFileComplexTestSuite.h"
#include "TokenTestSupport.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <tsre/shape/ShapeLib.h>
#include <QTemporaryDir>
#include <QThread>
#include <array>
#include <cmath>
#include <type_traits>
#include <shapeViewer/ShapeHierarchyInfo.h>
#include <tsre/Game.h>
#include <tsre/fileFunctions/SimisTextReader.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/renderer/OpenGL3Renderer.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/SelectionRenderer.h>
#include <tsre/shape/SFile.h>
#include <tsre/shape/SFileLegacy.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/shape/SFileDocument.h>
#include <tsre/texture/AceLib.h>
#include <tsre/texture/DdsLib.h>
#include <tsre/texture/ImageLib.h>
using namespace TokenTest;
namespace {
ShapeLoadOptions shapeLoadOptions(bool firstLodOnly = false, bool compact = false) {
    return {
        {QString::fromLatin1(ShapeLoadOption::FirstLodOnly), firstLodOnly},
        {QString::fromLatin1(ShapeLoadOption::Compact), compact}
    };
}

const QByteArray triangle = R"(SIMISA@@@@@@@@@@JINX0s1t______
shape (
 shape_header ( 00000000 00000000 )
 shader_names ( 1 named_shader ( TexDiff ) )
 points ( 3 point ( 1 2 3 ) point ( 4 5 7 ) point ( 7 8 9 ) )
 uv_points ( 1 uv_point ( 0 0 ) ) normals ( 1 vector ( 0 1 0 ) )
 matrices ( 1 matrix MAIN ( 1 0 0 0 1 0 0 0 1 0 0 0 ) )
 images ( 0 ) textures ( 0 )
 vtx_states ( 1 vtx_state ( 00000000 0 -5 0 00000002 ) )
 prim_states ( 1 prim_state ( 00000000 0 tex_idxs ( 0 ) 0 0 0 0 1 ) )
 lod_controls ( 1 lod_control ( distance_levels_header ( 0 ) distance_levels ( 1
 distance_level ( distance_level_header ( dlevel_selection ( 1000 ) hierarchy ( 1 -1 ) )
 sub_objects ( 1 sub_object ( sub_object_header ( 00000400 -1 -1 000001d2 000001c4
 geometry_info ( 0 0 0 0 0 0 0 0 0 0 geometry_nodes ( 0 ) geometry_node_map ( 1 0 ) ) 1 )
 vertices ( 3 vertex ( 00000000 0 0 ffffffff ff000000 vertex_uvs ( 1 0 ) )
 vertex ( 00000000 1 0 ffffffff ff000000 vertex_uvs ( 1 0 ) )
 vertex ( 00000000 2 0 ffffffff ff000000 vertex_uvs ( 1 0 ) ) )
 vertex_sets ( 1 vertex_set ( 0 0 3 ) )
 primitives ( 1 prim_state_idx ( 0 ) indexed_trilist ( vertex_idxs ( 3 0 1 2 ) normal_idxs ( 1 0 3 ) flags ( 1 00000000 ) ) )
 ) ) ) ) ) )
) )";
bool write(const QString &p, const QByteArray &b) {
    QFile f(p);
    return f.open(QIODevice::WriteOnly) && f.write(b) == b.size();
}
// Estimate accessible retained payload without changing the legacy loaders.
// In particular, tpoints and czes::idx are dangling after upload; never read them.
// Private state allocations, unknown spare primitive slots, Qt/allocator/driver
// overhead and shared textures are excluded. This is not a peak-memory counter.
QJsonObject legacyStorage(SFile &shape) {
    quint64 cpu = sizeof(SFile);
    auto arrayBytes = [](int count, size_t element) {
        return quint64(std::max(0, count)) * element;
    };
    auto vectorBytes = [](const auto &v) {
        return quint64(v.capacity()) * sizeof(typename std::decay_t<decltype(v)>::value_type);
    };
    auto stringBytes = [](const QString &s) { return quint64(s.size()) * sizeof(QChar); };
    for (const auto *s : {&shape.pathid, &shape.sciezka, &shape.nazwa, &shape.texPath,
                          &shape.sdName})
        cpu += stringBytes(*s);
    cpu += arrayBytes(shape.ishaders, sizeof(SFile::fshader));
    cpu += arrayBytes(shape.iloscm + 1, sizeof(SFile::matrt));
    cpu += arrayBytes(shape.ilosci + 1, sizeof(SFile::imgs));
    cpu += arrayBytes(shape.ilosct, sizeof(SFile::text));
    cpu += arrayBytes(shape.iloscv, sizeof(SFile::vtxs));
    cpu += arrayBytes(shape.iloscps, sizeof(SFile::primst));
    cpu += arrayBytes(shape.iloscd, sizeof(SFile::dist));
    for (int i = 0; i < shape.ishaders; ++i)
        cpu += stringBytes(shape.shader[i].name);
    for (int i = 0; i < shape.iloscm; ++i)
        cpu += stringBytes(shape.macierz[i].name);
    for (int i = 0; i < shape.ilosci; ++i)
        cpu += stringBytes(shape.image[i].name);
    cpu += vectorBytes(shape.esdBoundingBox) + vectorBytes(shape.animations);
    quint64 animation = vectorBytes(shape.animations);
    for (const auto &a : shape.animations) {
        quint64 bytes = vectorBytes(a.node);
        for (const auto &n : a.node)
            bytes += vectorBytes(n.tcbKey) + vectorBytes(n.tcbId) + vectorBytes(n.slerpRot) +
                     vectorBytes(n.slerpbId) + vectorBytes(n.linearKey) + vectorBytes(n.linearId);
        animation += bytes;
        cpu += bytes;
    }
    auto *f = QOpenGLContext::currentContext()->functions();
    GLint previousBuffer = 0;
    f->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    quint64 gpu = 0, expectedGpu = 0;
    bool queried = true;
    for (int l = 0; l < shape.iloscd; ++l) {
        auto &level = shape.distancelevel[l];
        cpu += arrayBytes(level.ilosch + 1, sizeof(int));
        cpu += arrayBytes(level.iloscs + 1, sizeof(SFile::sub));
        for (int m = 0; m < level.iloscs; ++m) {
            auto &mesh = level.subobiekty[m];
            cpu += vectorBytes(mesh.header.geometryNodeMap);
            // Allocation uses the source primitive count, which includes state
            // changes. Only the number of populated triangle parts survives.
            cpu += arrayBytes(mesh.iloscc + 1, sizeof(SFile::czes));
            for (int p = 0; p < mesh.iloscc; ++p)
                expectedGpu += arrayBytes(mesh.czesci[p].iloscv, 9 * sizeof(GLfloat));
            if (!mesh.VBO.isCreated() || !mesh.VBO.bind()) {
                queried = false;
                continue;
            }
            const int bytes = mesh.VBO.size();
            queried &= bytes >= 0;
            if (bytes >= 0)
                gpu += quint64(bytes);
        }
    }
    f->glBindBuffer(GL_ARRAY_BUFFER, GLuint(previousBuffer));
    return {{"legacy_retained_cpu_estimate_bytes", double(cpu)},
            {"legacy_animation_estimate_bytes", double(animation)},
            {"legacy_gpu_bytes", queried ? QJsonValue(double(gpu)) : QJsonValue()},
            {"legacy_gpu_size_valid", queried && gpu == expectedGpu}};
}
struct MatrixProbe : Renderer {
    std::vector<std::array<float, 16>> transforms;
    std::vector<std::vector<float>> buffers;
    bool captureGeometry = false;
    QVector<QWeakPointer<RenderItem>> packetOwners;
    ~MatrixProbe() override {
        for (auto *matrix : mvMatrixDelete)
            delete[] matrix;
    }
    void pushItem(RenderItem *item, float *) override {
        packetOwners.push_back(item->cacheOwner);
        std::array<float, 16> matrix{};
        if (item->msMatrix)
            std::copy_n(item->msMatrix, 16, matrix.begin());
        transforms.push_back(matrix);
        if (captureGeometry) {
            std::vector<float> vertices(item->vertCount * 9);
            if (item->VBO && item->VBO->bind()) {
                if (!item->VBO->read(item->vertOffset * 9 * sizeof(float), vertices.data(),
                                     vertices.size() * sizeof(float)))
                    vertices.clear();
                item->VBO->release();
            } else
                vertices.clear();
            buffers.push_back(std::move(vertices));
        }
        if (!item->shared)
            delete item;
    }
    void pushItemVNTA(RenderItem *item, float *mv) override { pushItem(item, mv); }
    void pushItemsVNTA(QVector<RenderItem *> &items, float *mv) override {
        for (auto *item : items)
            pushItem(item, mv);
    }
};
struct RenderProbe {
    QOpenGLFramebufferObject fbo{192, 192, QOpenGLFramebufferObject::CombinedDepthStencil};
    struct OrderedRenderer : OpenGL3Renderer {
        void pushItemsVNTA(QVector<RenderItem *> &items, float *matrix) override {
            for (auto *item : items) pushItem(item, matrix);
        }
    } orderedRenderer;
    bool orderedGather = false;
    OpenGL3Renderer renderer;
    GLUU *gl = GLUU::get();
    ComplexShape *cameraReference = nullptr;
    RenderProbe() {
        Game::shadowsEnabled = 0;
        gl->initShader();
        gl->currentShader = gl->shaders["StandardFog"];
        gl->fogDensity = 0;
    }
    void setup(ComplexShape &shape) {
        gl->currentShader->bind();
        QMatrix4x4 projection, view, identity;
        auto &cameraShape = cameraReference ? *cameraReference : shape;
        float extent = std::max(cameraShape.getSize(), 1.0f);
        const float *b = cameraShape.getBound();
        QVector3D center(-(b[0] + b[1]) / 2, (b[2] + b[3]) / 2, (b[4] + b[5]) / 2);
        const auto camera = QJsonDocument::fromJson(qgetenv("TSRE_COMPAT_CAMERA")).object();
        if (!camera.isEmpty()) {
            extent = std::max(float(camera["size"].toDouble()), 1.0f);
            const auto a = camera["bounds"].toArray();
            if (a.size() == 6) {
                float v[6]; for (int i=0;i<6;++i) v[i]=float(a[i].toDouble());
                center = QVector3D(-(v[0]+v[1])/2,(v[2]+v[3])/2,(v[4]+v[5])/2);
            }
        }
        projection.perspective(40, 1, extent / 1000, extent * 10);
        view.lookAt(center + QVector3D(extent * .8f, extent * .4f, extent * .8f), center,
                    {0, 1, 0});
        std::memcpy(gl->pMatrix, projection.constData(), 64);
        std::memcpy(gl->mvMatrix, view.constData(), 64);
        for (auto *m : {gl->fMatrix, gl->objStrMatrix, gl->pShadowMatrix, gl->pShadowMatrix2})
            std::memcpy(m, identity.constData(), 64);
        gl->setMatrixUniforms();
        renderer.mvMatrix = gl->mvMatrix;
        orderedRenderer.mvMatrix = gl->mvMatrix;
    }
    QImage image(ComplexShape &shape, bool gather, bool readback = true, unsigned int state = 0) {
        fbo.bind();
        auto *f = QOpenGLContext::currentContext()->functions();
        f->glViewport(0, 0, 192, 192);
        f->glEnable(GL_DEPTH_TEST);
        f->glDisable(GL_CULL_FACE);
        f->glEnable(GL_BLEND);
        f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        f->glClearColor(.08f, .08f, .08f, 1);
        f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl->currentShader = gl->shaders["StandardFog"];
        setup(shape);
        if (gather) {
            auto *saved = Game::currentRenderer;
            auto &active = orderedGather ? static_cast<OpenGL3Renderer &>(orderedRenderer) : renderer;
            Game::currentRenderer = &active;
            shape.pushRenderItem(0, state);
            active.renderFrame();
            Game::currentRenderer = saved;
        } else
            shape.render(0, state);
        if (!readback)
            return {};
        f->glFinish();
        return fbo.toImage();
    }
    QJsonObject submission(ComplexShape &shape, bool gather) {
        std::vector<double> samples;
        auto *f = QOpenGLContext::currentContext()->functions();
        for (int i = 0; i < 24; ++i) {
            f->glFinish();
            QElapsedTimer timer;
            timer.start();
            image(shape, gather, false);
            samples.push_back(timer.nsecsElapsed() / 1e6);
        }
        f->glFinish();
        std::sort(samples.begin(), samples.end());
        return {{"median_ms", samples[12]}, {"p95_ms", samples[22]}, {"samples", 24}};
    }
    QByteArray selection(ComplexShape &shape, bool gather) {
        SelectionRenderer target;
        if (!target.begin(192, 192))
            return {};
        gl->currentShader = gl->shaders["Selection"];
        setup(shape);
        if (gather) {
            auto *saved = Game::currentRenderer;
            auto &active = orderedGather ? static_cast<OpenGL3Renderer &>(orderedRenderer) : renderer;
            Game::currentRenderer = &active;
            shape.pushRenderItem(0x12345678, 0);
            active.renderFrame();
            Game::currentRenderer = saved;
        } else
            shape.render(0x12345678, 0);
        QByteArray pixels(192 * 192 * 4, 0);
        auto *f = QOpenGLContext::currentContext()->functions();
        f->glReadPixels(0, 0, 192, 192, GL_RED_INTEGER, GL_UNSIGNED_INT, pixels.data());
        target.end();
        target.release();
        return pixels;
    }
};
int difference(const QImage &a, const QImage &b) {
    if (a.size() != b.size())
        return -1;
    int different = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (a.pixel(x, y) != b.pixel(x, y))
                ++different;
    return different;
}


QString compatHash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
void compatEmit(QJsonObject result) {
    const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
    std::fprintf(stdout, "COMPAT %s\n", bytes.constData());
    std::fflush(stdout);
}
template<class Shape> int legacyCompatSnapshot(const QString &path) {
    QScopedValueRollback<bool> filesystem(Game::caseInsensitiveFS, true);
    QScopedValueRollback<bool> aceThreads(AceLib::IsThread, false);
    QScopedValueRollback<bool> ddsThreads(DdsLib::IsThread, false);
    QScopedValueRollback<bool> imageThreads(ImageLib::IsThread, false);
    QOffscreenSurface surface; surface.create();
    QOpenGLContext context; context.setFormat(surface.format());
    if (!context.create() || !context.makeCurrent(&surface)) return 3;
    RenderProbe render;
    render.orderedGather = true;
    Shape shape(path, QFileInfo(path).fileName(), QFileInfo(path).absolutePath());
    QJsonObject result{{"path", path}, {"stage", "loading"}};
    compatEmit(result);
    QElapsedTimer timer; timer.start();
    if constexpr (std::is_same_v<Shape, SFileLegacy>) {
        context.doneCurrent();
        const bool cpu = shape.loadData();
        result["cpu_only"] = cpu && shape.isLoaded() && !shape.isGLReady() && !shape.initGL();
        context.makeCurrent(&surface);
        result["gpu_ready"] = shape.initGL();
    } else shape.load();
    result["load_gl_ms"] = timer.nsecsElapsed()/1e6;
    result["loaded"] = shape.isLoaded();
    result["stage"] = "loaded";
    compatEmit(result);
    if (!shape.isLoaded()) return 0;
    if constexpr (std::is_same_v<Shape, SFileLegacy>)
        if (!shape.isGLReady()) return 0;
    QByteArray metadata;
    QDataStream data(&metadata, QIODevice::WriteOnly);
    for(float v : shape.bound) data << v;
    data << shape.size << shape.esdDetailLevel << shape.esdAlternativeTexture << shape.sdName << shape.isSnapable();
    for(const auto &box : shape.esdBoundingBox) {
        for(float v:box.shape) data<<v;
        for(float v:box.rotation) data<<v;
        for(float v:box.translation) data<<v;
    }
    data << shape.ishaders << shape.iloscm << shape.ilosci << shape.ilosct << shape.iloscv << shape.iloscps;
    for(int i=0;i<shape.ishaders;++i) data << shape.shader[i].name << shape.shader[i].alpha;
    for(int i=0;i<shape.iloscm;++i) {
        data << shape.macierz[i].name;
        for(float v:shape.macierz[i].param) data<<v;
    }
    for(int i=0;i<shape.ilosci;++i) data << shape.image[i].name;
    for(int i=0;i<shape.ilosct;++i) {
        const auto &v=shape.texture[i]; data << v.image << v.arg1 << v.arg2 << v.arg3;
    }
    for(int i=0;i<shape.iloscv;++i) {
        const auto &v=shape.vtxstate[i]; data << v.arg1 << v.matrix << v.arg2 << v.arg3 << v.arg4;
    }
    for(int i=0;i<shape.iloscps;++i) {
        const auto &v=shape.primstate[i];
        data << v.vtx_state << v.arg1 << v.arg2 << v.arg3 << v.arg4 << v.arg5 << v.arg6 << v.arg7 << v.arg8;
    }
    data << qint64(shape.animations.size());
    for(const auto &a:shape.animations) {
        data << a.frames << a.fps << qint64(a.node.size());
        for(const auto &n:a.node) {
            data << qint64(n.tcbKey.size()) << qint64(n.slerpRot.size()) << qint64(n.linearKey.size());
            for(const auto &k:n.tcbKey) { data<<k.frame; for(float v:k.quat)data<<v; for(float v:k.param)data<<v; }
            for(const auto &k:n.slerpRot) { data<<k.frame; for(float v:k.quat)data<<v; }
            for(const auto &k:n.linearKey) { data<<k.frame; for(float v:k.pos)data<<v; }
            for(const auto *ids:{&n.tcbId,&n.slerpbId,&n.linearId}) {
                data<<qint64(ids->size());
                for(const auto &id:*ids) data<<id.id1<<id.id2<<id.offset;
            }
        }
    }
    result["metadata_hash"] = compatHash(metadata);
    result["lods"] = shape.iloscd;
    QJsonArray lods;
    for(int l=0;l<shape.iloscd;++l) {
        auto &level=shape.distancelevel[l];
        QByteArray layout; QDataStream out(&layout,QIODevice::WriteOnly);
        out<<level.levelSelection<<level.ilosch<<level.iloscs;
        for(int h=0;h<level.ilosch;++h)out<<level.hierarchia[h];
        QCryptographicHash buffers(QCryptographicHash::Sha256);
        qint64 bytes=0; bool gpuOk=true;
        for(int o=0;o<level.iloscs;++o) {
            auto &obj=level.subobiekty[o]; out<<obj.header.geometryNodeMap<<obj.iloscc;
            for(int k=0;k<obj.iloscc;++k) {const auto &p=obj.czesci[k];out<<p.iloscv<<p.offset<<p.prim_state_idx<<p.enabled;}
            if(!obj.VBO.bind()) {gpuOk=false;continue;}
            const int length=obj.VBO.size();
            if(length<0) {gpuOk=false;obj.VBO.release();continue;}
            QByteArray buffer(length,0);
            gpuOk &= obj.VBO.read(0,buffer.data(),length);
            obj.VBO.release(); bytes+=length; out<<length;
            buffers.addData(buffer);
        }
        QJsonObject row{{"layout",compatHash(layout)}, {"buffer",QString::fromLatin1(buffers.result().toHex())},
                        {"bytes",double(bytes)}, {"gpu_ok",gpuOk}};
        lods.append(row);
    }
    result["geometry"]=lods; result["stage"]="geometry"; compatEmit(result);
    if(qEnvironmentVariableIsSet("TSRE_LEGACY_COMPAT_GEOMETRY_ONLY")) return 0;
    render.cameraReference=&shape;
    QJsonArray pictures;
    auto picture=[&](bool gather,unsigned state=0) {
        auto img=render.image(shape,gather,true,state).convertToFormat(QImage::Format_RGBA8888);
        return compatHash(QByteArray::fromRawData(reinterpret_cast<const char*>(img.constBits()),img.sizeInBytes()));
    };
    for(int l=0;l<shape.iloscd;++l) {
        shape.setCurrentDistanceLevel(0,l);
        // Both legacy implementations cache matrices independent of LOD.
        // Recompute after changing LOD, and prime direct hashes before gathering.
        shape.invalidateRenderState();
        render.image(shape,false,false);
        render.image(shape,false,false);
        QCoreApplication::processEvents();
        QJsonObject row{{"direct",picture(false)}, {"gather",picture(true)},
                        {"pick",compatHash(render.selection(shape,false))}};
        pictures.append(row);
        result["render"]=pictures; result["stage"]="render"; compatEmit(result);
    }
    if(!shape.animations.empty() && shape.animations[0].frames>0 && shape.animations[0].fps>0) {
        const unsigned state=shape.newState(); shape.setAnimated(state,true);
        const float duration=shape.animations[0].frames/shape.animations[0].fps;
        QJsonArray frames;
        for(float delta:{0.0f,duration*.25f,duration*.5f}) {
            shape.updateSim(delta,state);
            frames.append(picture(false,state));
        }
        result["animation"]=frames;
    }
    int ready=0,missing=0;
    for(int i=0;i<shape.ilosci;++i) {
        auto it=TexLib::mtex.find(shape.image[i].tex);
        if(it!=TexLib::mtex.end() && it->second) {
            ready+=it->second->glLoaded;
            missing+=it->second->missing || it->second->error;
        }
    }
    result["texture_images"]=shape.ilosci;
    result["textures_ready"]=ready; result["textures_missing_or_error"]=missing;
    result["stage"]="complete"; compatEmit(result);
    return 0;
}

template<class Shape> int threeCompatSnapshot(const QString &path, bool compact = false) {
    QScopedValueRollback<bool> filesystem(Game::caseInsensitiveFS, true);
    QScopedValueRollback<bool> aceThreads(AceLib::IsThread, false);
    QScopedValueRollback<bool> ddsThreads(DdsLib::IsThread, false);
    QScopedValueRollback<bool> imageThreads(ImageLib::IsThread, false);
    QOffscreenSurface surface; surface.create();
    QOpenGLContext context; context.setFormat(surface.format());
    if (!context.create() || !context.makeCurrent(&surface)) return 3;
    RenderProbe render; render.orderedGather = true;
    Shape shape(path, QFileInfo(path).fileName(), QFileInfo(path).absolutePath());
    if constexpr (std::is_same_v<Shape, SFileComplex>)
        shape.setLoadOptions(shapeLoadOptions(false, compact));
    QJsonObject result{{"stage","loading"}}; compatEmit(result);
    context.doneCurrent();
    QElapsedTimer timer; timer.start();
    bool loaded=shape.loadData();
    result["cpu_ms"]=timer.nsecsElapsed()/1e6;
    result["cpu_only"]=loaded && shape.isLoaded() && !shape.initGL();
    context.makeCurrent(&surface); timer.restart();
    bool ready=shape.initGL(); context.functions()->glFinish();
    result["init_gl_ms"]=timer.nsecsElapsed()/1e6;
    result["load_gl_ms"]=result["cpu_ms"].toDouble()+result["init_gl_ms"].toDouble();
    result["loaded"]=loaded; result["gpu_ready"]=ready;
    result["stage"]="loaded";
    int count=0;
    double duration=qEnvironmentVariable("TSRE_COMPAT_ANIMATION_DURATION").toDouble();
    if constexpr (std::is_same_v<Shape,SFileLegacy>) {
        count=shape.iloscd;
        if(!shape.animations.empty() && shape.animations[0].fps>0 && shape.animations[0].frames>0)
            duration=shape.animations[0].frames/shape.animations[0].fps;
    } else {
        count=shape.statistics().lods;
        result["health"]=int(shape.health()); result["retention"]=int(shape.retention());
        result["diagnostics"]=shape.diagnostics().join("; ");
        auto s=shape.statistics();
        result["document_bytes"]=double(s.documentBytes);
        result["source_geometry_bytes"]=double(s.sourceGeometryBytes);
        result["runtime_bytes"]=double(s.runtimeBytes);
    }
    result["animation_duration"]=duration; result["lods"]=count;
    compatEmit(result);
    if(!loaded || !ready) return 0;
    QJsonArray bounds; for(int i=0;i<6;++i)bounds.append(shape.getBound()[i]);
    result["bounds"]=bounds; result["size"]=static_cast<const ComplexShape &>(shape).getSize();
    result["detail"]=shape.getEsdDetailLevel(); result["snap"]=shape.isSnapable();
    const auto dump=qEnvironmentVariable("TSRE_COMPAT_DUMP");
    auto picture=[&](bool gather,unsigned state,const QString &name) {
        auto img=render.image(shape,gather,true,state).convertToFormat(QImage::Format_RGBA8888);
        if(!dump.isEmpty()) {QDir().mkpath(dump);img.save(dump+"/"+name+".png");}
        return compatHash(QByteArray::fromRawData(reinterpret_cast<const char*>(img.constBits()),img.sizeInBytes()));
    };
    QJsonArray levels;
    for(int l=0;l<count;++l) {
        shape.setCurrentDistanceLevel(0,l); shape.invalidateRenderState();
        render.image(shape,false,false);render.image(shape,false,false);
        QCoreApplication::processEvents();
        ShapeHierarchyInfo h;shape.fillShapeHierarchyInfo(&h,0);
        QJsonArray parents,names,parts;
        for(int p:h.hierarchy)parents.append(p);
        for(auto &n:h.matrices)names.append(n);
        for(auto &p:h.parts)parts.append(QJsonObject{{"matrix",p.matrixId},{"triangles",p.polyCount},{"texture",p.textureName.toLower()},{"enabled",p.enabled}});
        MatrixProbe probe;probe.captureGeometry=true;
        auto *previous=Game::currentRenderer;Game::currentRenderer=&probe;
        shape.pushRenderItem(0,0);Game::currentRenderer=previous;
        if (!dump.isEmpty()) {
            QDir().mkpath(dump);
            for (size_t part=0; part<probe.buffers.size(); ++part) {
                const auto &vertices=probe.buffers[part];
                write(dump+"/"+QString::number(l)+"-part-"+QString::number(part)+".f32",
                      QByteArray::fromRawData(reinterpret_cast<const char*>(vertices.data()),
                                             vertices.size()*sizeof(float)));
            }
        }
        QJsonArray buffers,transforms;
        for(auto &b:probe.buffers)buffers.append(QJsonObject{{"bytes",double(b.size()*sizeof(float))},{"hash",compatHash(QByteArray::fromRawData(reinterpret_cast<const char*>(b.data()),b.size()*sizeof(float)))}});
        for(auto &m:probe.transforms){QJsonArray values;for(float v:m)values.append(v);transforms.append(values);}
        const auto prefix=QString::number(l);
        levels.append(QJsonObject{{"parents",parents},{"matrices",names},{"parts",parts},{"buffers",buffers},{"transforms",transforms},
            {"direct",picture(false,0,prefix+"-direct")},{"gather",picture(true,0,prefix+"-gather")},{"pick",compatHash(render.selection(shape,false))}});
        result["levels"]=levels;result["stage"]="render";compatEmit(result);
    }
    if(duration>0) {
        auto state=shape.newState();shape.setAnimated(state,true);QJsonArray frames;
        int index=0;
        for(float delta:{0.f,float(duration*.25),float(duration*.5)}) {
            shape.updateSim(delta,state);frames.append(picture(false,state,"animation-"+QString::number(index++)));
        }
        result["animation"]=frames;
    }
    int readyTextures=0,missing=0;
    for(auto &entry:TexLib::mtex)if(entry.second){readyTextures+=entry.second->glLoaded;missing+=entry.second->missing||entry.second->error;}
    result["textures_ready"]=readyTextures;result["textures_missing_or_error"]=missing;
    result["stage"]="complete";compatEmit(result);return 0;
}

} // namespace
int TsreTests::runSFileComplexSuite(bool verbose, bool gl) {
    Suite t{"shape-complex", verbose};
    using R = SimisTextReader;
    R reader("1e+3 \"a\" + \"b\" -1 4294967296");
    double number = 0;
    quint32 u = 0;
    t.check(R::number(reader.next(), number) && number == 1000, "positive exponent");
    t.check(reader.next().text == "ab", "whitespace concatenation");
    t.check(!R::unsignedInteger(reader.next(), u), "negative unsigned rejected");
    t.check(!R::unsignedInteger(reader.next(), u), "unsigned overflow rejected");
    R blockReader("\"a)b\" ) sibling ( 1 )");
    t.check(blockReader.skipBlock() && blockReader.next().text == "sibling",
            "quoted block boundaries");
    R truncated("\"unfinished");
    t.check(truncated.next().kind == R::Kind::Error && !truncated.diagnostics().empty(),
            "truncated string diagnosed");
    QString unicode = "Zażółć 日本語 \\\"\n";
    R quoted(R::quote(unicode));
    t.check(quoted.next().text == unicode, "Unicode and escapes round trip");
    SFileDetail::Document doc;
    t.check(doc.readBytes(triangle) && !doc.damaged, "synthetic shape document");
    for (bool bin : {false, true})
        for (bool zip : {false, true}) {
            QString error;
            auto b = doc.encode(bin, zip, error);
            SFileDetail::Document copy;
            t.check(error.isEmpty() && copy.readBytes(b) && !copy.damaged,
                    "text/binary/compressed round trip " + error);
        }
    SFileDetail::Document unknown;
    t.check(unknown.readBytes("shape ( extra ( \"a)b\" nested ( 42 ) ) )"), "unknown text block");
    QString error;
    auto saved = unknown.encode(false, false, error);
    SFileDetail::Document again;
    t.check(again.readBytes(saved) && again.root.child("extra"), "unknown block retained");
    auto opaque = file(TokenTest::block(TS::shape, TokenTest::block(0xffff1234, "abc")), 's');
    SFileDetail::Document binary;
    t.check(binary.readBytes(opaque) && binary.encode(true, false, error).mid(32) == opaque.mid(32),
            "unknown binary bytes retained");
    binary.encode(false, false, error);
    t.check(!error.isEmpty(), "opaque conversion refused");
    SFileDetail::Document cut;
    cut.readBytes("shape ( points ( 1 point ( 1 2");
    t.check(cut.damaged && !cut.root.name().isEmpty(), "truncated document retained");
    QTemporaryDir tmp;
    QString path = tmp.filePath("fixture.s");
    t.check(write(path, triangle), "write fixture");
    {
        const bool hadBackend = qEnvironmentVariableIsSet("TSRE_MSTS_SHAPE_BACKEND");
        const QByteArray previousBackend = qgetenv("TSRE_MSTS_SHAPE_BACKEND");
        const auto restoreBackend = qScopeGuard([&] {
            if (hadBackend) qputenv("TSRE_MSTS_SHAPE_BACKEND", previousBackend);
            else qunsetenv("TSRE_MSTS_SHAPE_BACKEND");
        });
        qunsetenv("TSRE_MSTS_SHAPE_BACKEND");
        QScopedValueRollback<bool> caseSensitivePaths(Game::caseInsensitiveFS, false);
        const QString factoryPath = tmp.filePath("factory.s");
        const bool factoryFixtureWritten = write(factoryPath, doc.encode(true, false, error));
        ShapeLib library;
        const int id = library.addShape(factoryPath, tmp.path());
        std::unique_ptr<ComplexShape> asset(library.shape.at(id));
        auto *legacy = dynamic_cast<SFileLegacy *>(asset.get());
        t.check(factoryFixtureWritten && legacy && legacy->loadData() && legacy->isLoaded() && !legacy->isGLReady(),
                "default factory loads SFileLegacy before GL initialization");
        library.shape.clear();
        qputenv("TSRE_MSTS_SHAPE_BACKEND", "old");
        ShapeLib fallback;
        const int oldId = fallback.addShape(path, tmp.path());
        std::unique_ptr<ComplexShape> oldAsset(fallback.shape.at(oldId));
        t.check(dynamic_cast<SFile *>(oldAsset.get()) != nullptr,
                "explicit old factory fallback remains available");
        fallback.shape.clear();
        qputenv("TSRE_MSTS_SHAPE_BACKEND", "complex-compact");
        ShapeLib compactFactory;
        const int compactId = compactFactory.addShape(factoryPath, tmp.path());
        std::unique_ptr<ComplexShape> compactAsset(compactFactory.shape.at(compactId));
        auto *compactShape = dynamic_cast<SFileComplex *>(compactAsset.get());
        t.check(compactShape && compactShape->loadData() &&
                    compactShape->retention() == SFileComplex::Retention::Compact &&
                    compactShape->statistics().documentBytes == 0,
                "generic factory load options reach SFileComplex backend");
        compactFactory.shape.clear();
    }
    SFileComplex shape(path, "fixture.s", tmp.path());
    t.check(shape.loadData() && shape.isLoaded(), "CPU loaded before GL");
    t.check(shape.health() != SFileComplex::Health::Broken, shape.diagnostics().join("; "));
    t.check(shape.gpuState() == SFileComplex::GpuState::NotInitialized, "separate GPU state");
    t.check(shape.getBound()[0] == 7 && shape.getBound()[1] == 1 && shape.statistics().parts == 1,
            "CPU bounds and parts");
    t.check(shape.setField("points/point[0]", 0, "-2", &error) && shape.getBound()[1] == -2,
            "Complete edit updates CPU data");
    t.check(shape.save(tmp.filePath("edited.s"), SFileComplex::Format::Binary, true, &error),
            "save edited shape " + error);
    SFileComplex edited(tmp.filePath("edited.s"), "edited", tmp.path());
    t.check(edited.loadData() && edited.getBound()[1] == -2, "saved edit persists");
    auto invalid = triangle;
    invalid.replace("vertex_idxs ( 3 0 1 2 )", "vertex_idxs ( 3 0 1 999 )");
    write(tmp.filePath("bad.s"), invalid);
    SFileComplex bad(tmp.filePath("bad.s"), "bad", tmp.path());
    t.check(bad.loadData() && bad.isLoaded() && bad.health() == SFileComplex::Health::Broken,
            "broken retained after CPU load");
    for (bool missingCoordinate : {false, true}) {
        auto bytes = triangle;
        if (missingCoordinate)
            bytes.replace("point ( 1 2 3 )", "point ( 1 2 )");
        else
            bytes.replace("vertex ( 00000000 0 0", "vertex ( 00000000 999 0");
        SFileDetail::Document invalidSource;
        invalidSource.readBytes(bytes);
        for (bool binary : {false, true}) {
            const QString badPath = tmp.filePath("bad-compact.s");
            write(badPath, invalidSource.encode(binary, false, error));
            SFileComplex brokenCompact(badPath, "bad compact", tmp.path());
            brokenCompact.setLoadOptions(shapeLoadOptions(false, true));
            t.check(brokenCompact.loadData() && brokenCompact.isLoaded() &&
                        brokenCompact.health() == SFileComplex::Health::Broken &&
                        brokenCompact.statistics().documentBytes > 0 && !brokenCompact.initGL(),
                    "packed and fallback Compact geometry keep invalid shapes Broken before GL");
        }
    }
    auto spacedMatrix = triangle;
    spacedMatrix.replace("matrix MAIN (", "matrix MAIN component 1 (");
    const auto spacedPath = tmp.filePath("spaced-matrix.s");
    write(spacedPath, spacedMatrix);
    for (bool compact : {false, true}) {
        SFileComplex named(spacedPath, "spaced label", tmp.path());
        named.setLoadOptions(shapeLoadOptions(false, compact));
        t.check(named.loadData() && named.health() == SFileComplex::Health::Recovered,
                "multi-word label recovery produces a renderable Recovered shape");
        if (!compact) {
            const auto saved = tmp.filePath("normalized-matrix.s");
            t.check(named.save(saved, SFileComplex::Format::Text, false, &error),
                    "Complete can save recovered multi-word labels");
            SFileComplex reread(saved, "normalized label", tmp.path());
            t.check(reread.loadData() && reread.health() == SFileComplex::Health::Valid,
                    "normalized label reload needs no recovery");
        }
    }
    // Missing anim_node close consumes the outer shape close, as in ET41.
    auto damagedAnimation = triangle;
    damagedAnimation.insert(damagedAnimation.lastIndexOf(')'),
        "animations ( 1 animation ( 10 30 anim_nodes ( 2 "
        "anim_node A ( controllers ( 0 ) "
        "anim_node B ( controllers ( 0 ) ) ) ) ) ");
    const auto animationPath = tmp.filePath("damaged-animation.s");
    write(animationPath, damagedAnimation);
    for (bool compact : {false, true}) {
        SFileComplex recovered(animationPath, "damaged animation", tmp.path());
        recovered.setLoadOptions(shapeLoadOptions(false, compact));
        t.check(recovered.loadData() && recovered.isLoaded() &&
                    recovered.health() == SFileComplex::Health::Recovered &&
                    recovered.statistics().animations == 0 && recovered.statistics().lods == 1,
                "damaged animation tail permits static geometry in both modes");
        t.check(!recovered.save(tmp.filePath("damaged-save.s"), SFileComplex::Format::Text,
                                false, &error), "damaged animation source cannot silently save");
        auto badGeometry = damagedAnimation;
        badGeometry.replace("point ( 1 2 3 )", "point ( 1 2 )");
        const auto badPath = tmp.filePath("damaged-animation-and-geometry.s");
        write(badPath, badGeometry);
        SFileComplex broken(badPath, "broken geometry", tmp.path());
        broken.setLoadOptions(shapeLoadOptions(false, compact));
        t.check(broken.loadData() && broken.health() == SFileComplex::Health::Broken &&
                    !broken.initGL(), "animation recovery cannot hide invalid static geometry");
    }
    SFileDetail::Document animationDoc;
    t.check(animationDoc.readBytes(damagedAnimation) && animationDoc.damaged &&
                animationDoc.damageConfinedToAnimations() && animationDoc.original == damagedAnimation,
            "Complete document retains damaged animation source bytes");
    auto unrelatedDamage = damagedAnimation;
    unrelatedDamage.insert(unrelatedDamage.indexOf("shape (") + 7, "( ) ");
    t.check(animationDoc.readBytes(unrelatedDamage) &&
                !animationDoc.damageConfinedToAnimations(),
            "unrelated syntax damage is not downgraded by an animation tail");
    auto multi = doc;
    auto levels = multi.root.child("lod_controls")->child("lod_control")->child("distance_levels");
    levels.appendCopy(levels.children().front());
    levels.setScalar(0, "2");
    QString multiPath = tmp.filePath("multi.s");
    write(multiPath, multi.encode(false, false, error));
    SFileComplex partial(multiPath, "multi", tmp.path());
    partial.setLoadOptions(shapeLoadOptions(true, false));
    t.check(partial.loadData() && partial.statistics().lods == 1 &&
                partial.retention() == SFileComplex::Retention::Partial,
            "first LOD skips later level storage");
    t.check(!partial.save(tmp.filePath("partial.s"), SFileComplex::Format::Text, false, &error),
            "Partial save requires full reload");
    SFileComplex firstCompact(multiPath, "first compact", tmp.path());
    firstCompact.setLoadOptions(shapeLoadOptions(true, true));
    t.check(firstCompact.loadData() && firstCompact.statistics().lods == 1 &&
                firstCompact.retention() == SFileComplex::Retention::Compact &&
                firstCompact.statistics().documentBytes == 0,
            "packed Compact tables respect pre-load first-LOD selection");
    t.check(partial.reloadComplete() && partial.statistics().lods == 2,
            "Complete reload restores skipped LODs");
    write(path + "d", "SIMISA@@@@@@@@@@JINX0t1t______\nshape ( fixture.s esd_detail_level ( 4 ) "
                      "extra ( \"a)b\" ) )");
    SFileComplex metadata(path, "fixture", tmp.path());
    t.check(metadata.loadData() && metadata.getEsdDetailLevel() == 4,
            "CPU metadata scalar before child blocks");
    t.check(metadata.setField("sd/esd_detail_level", 0, "7", &error) &&
                metadata.getEsdDetailLevel() == 7 &&
                metadata.saveMetadata(tmp.filePath("saved.sd"), false, &error),
            "metadata edit and save");
    SFileComplex requestedCompact(path, "fixture compact", tmp.path());
    t.check(requestedCompact.setLoadOptions(shapeLoadOptions(false, true)) && requestedCompact.loadData() &&
                requestedCompact.isLoaded() &&
                requestedCompact.retention() == SFileComplex::Retention::Compact &&
                requestedCompact.gpuState() == SFileComplex::GpuState::NotInitialized &&
                requestedCompact.statistics().documentBytes == 0 &&
                requestedCompact.statistics().sourceGeometryBytes > 0 &&
                requestedCompact.statistics().skippedSourceBlocks > 0,
            "pre-load Compact request skips source records before GL and retains upload arrays");
    t.check(!requestedCompact.save(tmp.filePath("too-early.s"), SFileComplex::Format::Text, false,
                                   &error) &&
                !requestedCompact.setLoadOptions(shapeLoadOptions()),
            "Compact is not saveable or relabeled Complete before upload");
    t.check(requestedCompact.getEsdDetailLevel() == 4 && requestedCompact.getBound()[0] == 7,
            "Compact retains CPU metadata and bounds");
    if (gl) {
        QOffscreenSurface surface;
        surface.create();
        QOpenGLContext context;
        context.setFormat(surface.format());
        t.check(context.create() && context.makeCurrent(&surface), "GL context");
        for (bool compact : {false, true}) {
            SFileComplex recovered(animationPath, "damaged animation GL", tmp.path());
            recovered.setLoadOptions(shapeLoadOptions(false, compact));
            t.check(recovered.loadData() && recovered.initGL(),
                    "damaged animation shape initializes static GL buffers");
        }
        QFile::rename(path, path + ".away");
        t.check(requestedCompact.initGL() && requestedCompact.statistics().sourceGeometryBytes == 0,
                "initial Compact upload uses retained CPU arrays without reopening source");
        QFile::rename(path + ".away", path);
        requestedCompact.releaseGL();
        t.check(requestedCompact.initGL() &&
                    requestedCompact.retention() == SFileComplex::Retention::Compact,
                "Compact context rebuild reapplies pre-load mode without recursive reload");
        requestedCompact.releaseGL();
        t.check(shape.initGL() && shape.statistics().sourceGeometryBytes > 0,
                "Complete keeps indices after initGL");
        t.check(!bad.initGL(), "Broken rejects unsafe upload");
        t.check(!shape.compact(), "unsaved edits block compaction");
        t.check(edited.initGL(), "saved fixture upload");
        t.check(edited.compact() && edited.retention() == SFileComplex::Retention::Compact &&
                    edited.statistics().documentBytes == 0 &&
                    edited.statistics().sourceGeometryBytes == 0,
                "Compact releases document and source geometry");
        t.check(!edited.save(tmp.filePath("compact.s"), SFileComplex::Format::Text, false, &error),
                "Compact refuses save");
        shape.releaseGL();
        t.check(shape.isLoaded() && shape.gpuState() == SFileComplex::GpuState::NotInitialized,
                "GPU invalidation preserves CPU loaded");
        edited.releaseGL();
        const QString editedPath = tmp.filePath("edited.s");
        QFile::rename(editedPath, editedPath + ".away");
        t.check(!edited.initGL() && edited.isLoaded() &&
                    edited.retention() == SFileComplex::Retention::Compact,
                "failed Compact rebuild preserves loaded CPU bounds");
        QFile::rename(editedPath + ".away", editedPath);
        t.check(edited.initGL() && edited.isLoaded(), "Compact rebuild reloads original source");
        t.check(edited.reloadComplete() && edited.retention() == SFileComplex::Retention::Complete,
                "Compact save workflow restores Complete");
        edited.releaseGL();
        shape.releaseGL();
        for (bool binaryFormat : {false, true}) {
            const QString legacyPath = tmp.filePath(binaryFormat ? "legacy-b.s" : "legacy-t.s");
            QString encodingError;
            SFileDetail::Document legacyDoc;
            legacyDoc.readBytes(QByteArray(triangle)
                .replace("primitives ( 1 prim_state_idx", "primitives ( 2 prim_state_idx")
                .replace("images ( 0 ) textures ( 0 )",
                         "images ( 1 image ( unused.ace ) ) textures ( 1 texture ( 0 0 0 0 ) )"));
            t.check(write(legacyPath, legacyDoc.encode(binaryFormat, false, encodingError)),
                    "write legacy fixture");
            SFileLegacy legacy(legacyPath, "fixture", tmp.path());
            context.doneCurrent();
            t.check(legacy.loadData() && legacy.isLoaded() && !legacy.isGLReady() && !legacy.initGL(),
                    "legacy CPU load without context");
            context.makeCurrent(&surface);
            if (!legacy.isLoaded() || legacy.iloscd == 0) continue;
            auto &testPart = legacy.distancelevel[0].subobiekty[0].czesci[0];
            const int validIndex = testPart.idx[0];
            QOpenGLBuffer sentinel;
            sentinel.create();
            sentinel.bind();
            sentinel.allocate(16);
            QOpenGLVertexArrayObject sentinelVao;
            sentinelVao.create();
            sentinelVao.bind();
            testPart.idx[0] = -1;
            t.check(!legacy.initGL() && legacy.isLoaded() && !legacy.isGLReady() &&
                        legacy.tpoints.points != nullptr,
                    "legacy failed upload preserves indexed source for retry");
            GLint boundBuffer = 0, boundVao = 0;
            context.functions()->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &boundBuffer);
            context.functions()->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVao);
            t.check(GLuint(boundBuffer) == sentinel.bufferId() &&
                        GLuint(boundVao) == sentinelVao.objectId() &&
                        !legacy.distancelevel[0].subobiekty[0].VBO.isCreated() &&
                        !legacy.distancelevel[0].subobiekty[0].VAO.isCreated(),
                    "failed legacy upload rolls back resources and restores bindings");
            sentinelVao.release();
            sentinel.release();
            testPart.idx[0] = validIndex;
            QFile::rename(legacyPath, legacyPath + ".away");
            t.check(legacy.initGL() && legacy.initGL(), "legacy upload independent of source and idempotent");
            t.check(legacy.tpoints.points == nullptr &&
                        legacy.distancelevel[0].subobiekty[0].vertices.empty() &&
                        legacy.distancelevel[0].subobiekty[0].czesci[0].idx == nullptr,
                    "legacy source arrays released after upload");
            QFile::rename(legacyPath + ".away", legacyPath);
            legacy.reload();
            t.check(legacy.loadData() && legacy.initGL(), "legacy reload rebuilds source and GL");
            {
                QOpenGLContext other;
                other.setFormat(surface.format());
                t.check(other.create() && other.makeCurrent(&surface), "second legacy GL context");
                t.check(!legacy.isGLReady() && !legacy.initGL(), "legacy rejects foreign context");
                context.makeCurrent(&surface);
                t.check(legacy.isGLReady(), "legacy readiness returns in owning context");
                other.makeCurrent(&surface);
                legacy.reload();
                t.check(legacy.loadData() && legacy.initGL(), "explicit reload uploads in new context");
                other.doneCurrent();
            }
            context.makeCurrent(&surface);
            t.check(!legacy.isGLReady() && !legacy.initGL(), "destroyed context requires explicit reload");
            legacy.reload();
            t.check(legacy.loadData() && legacy.initGL(), "reload recovers after context destruction");
            {
                RenderProbe packetRenderer;
                packetRenderer.setup(legacy);
                MatrixProbe probe;
                auto *previous = Game::currentRenderer;
                Game::currentRenderer = &probe;
                legacy.pushRenderItem(0, 0);
                auto owner = probe.packetOwners.value(0);
                t.check(!owner.isNull(), "legacy cache owns packets");
                Game::currentRenderer = &packetRenderer.renderer;
                legacy.pushRenderItem(0, 0);
                legacy.invalidateRenderState();
                t.check(!owner.isNull(), "queued packet survives cache invalidation");
                packetRenderer.renderer.renderFrame();
                t.check(owner.isNull(), "queued packet released after frame");
                Game::currentRenderer = &probe;
                legacy.pushRenderItem(0, 0);
                auto unqueued = probe.packetOwners.back();
                legacy.invalidateRenderState();
                t.check(unqueued.isNull(), "unqueued cache packets released immediately");
                Game::currentRenderer = previous;
            }
            if (binaryFormat) {
                // One valid list followed by an incomplete list: first indices must
                // already be included in the count used by cleanup.
                auto bytes = legacyDoc.encode(true, false, encodingError);
                const auto primitives = legacyDoc.root.child("lod_controls").child("lod_control")
                    .child("distance_levels").child("distance_level").child("sub_objects")
                    .child("sub_object").child("primitives");
                auto list = primitives.child("indexed_trilist");
                auto truncatedDoc = legacyDoc;
                auto target = truncatedDoc.root.child("lod_controls").child("lod_control")
                    .child("distance_levels").child("distance_level").child("sub_objects")
                    .child("sub_object").child("primitives");
                target.appendCopy(list);
                target.setScalar(0, "3");
                // Alter the final child's index count to exceed its bounded payload.
                SFileDetail::Node tail;
                for (auto child : target.children()) tail = child;
                tail.child("vertex_idxs").setScalar(0, "1000000");
                t.check(write(legacyPath, truncatedDoc.encode(true, false, encodingError)), "write partial legacy binary");
                legacy.reload();
                t.check(!legacy.loadData() && legacy.distancelevel[0].subobiekty[0].iloscc == 2,
                        "partial binary parse publishes allocated part ownership");
                t.check(write(legacyPath, bytes), "restore legacy fixture");
                t.check(legacy.loadData() && legacy.initGL(), "retry cleans partial binary allocation");
            }

        }
        RenderProbe renderer;
        SFileComplex visible(path, "visible", tmp.path());
        visible.loadData();
        visible.initGL();
        auto base = renderer.image(visible, false);
        auto instance = visible.newState();
        visible.setEnabledSubObjs(instance, 0);
        t.check(difference(base, renderer.image(visible, false, true, instance)) > 0 &&
                    difference(base, renderer.image(visible, false)) == 0,
                "visibility is isolated per instance");
        t.check(difference(base, renderer.image(visible, true)) == 0 &&
                    renderer.selection(visible, false) == renderer.selection(visible, true),
                "synthetic direct/gather and integer picking");
        for (bool points : {false, true}) {
            auto primitive = triangle;
            primitive.replace("indexed_trilist ( vertex_idxs ( 3 0 1 2 ) normal_idxs ( 1 0 3 ) "
                              "flags ( 1 00000000 ) )",
                              points ? "point_list ( 0 3 )"
                                     : "indexed_line_list ( vertex_idxs ( 2 0 1 ) )");
            auto primitivePath = tmp.filePath(points ? "points.s" : "lines.s");
            write(primitivePath, primitive);
            SFileComplex asset(primitivePath, "primitive", tmp.path());
            asset.loadData();
            t.check(asset.initGL() &&
                        difference(renderer.image(asset, false), renderer.image(asset, true)) == 0,
                    points ? "point primitive packet" : "line primitive packet");
            asset.releaseGL();
        }
        visible.releaseGL();
    }
    return t.finish();
}
int TsreTests::runSFileComplexCorpus(const QString &input, bool gl) {
    const auto three = qEnvironmentVariable("TSRE_THREE_COMPAT");
    if (three == "legacy") return threeCompatSnapshot<SFileLegacy>(input);
    if (three == "compact" || three == "complete") return threeCompatSnapshot<SFileComplex>(input, three == "compact");
    const auto compatibility = qEnvironmentVariable("TSRE_LEGACY_COMPAT");
    if (compatibility == "old") return legacyCompatSnapshot<SFile>(input);
    if (compatibility == "new") return legacyCompatSnapshot<SFileLegacy>(input);
    QScopedValueRollback<bool> filesystem(Game::caseInsensitiveFS, false);
    QScopedValueRollback<bool> aceThreads(AceLib::IsThread, false);
    QScopedValueRollback<bool> ddsThreads(DdsLib::IsThread, false);
    QScopedValueRollback<bool> imageThreads(ImageLib::IsThread, false);
    QOffscreenSurface surface;
    QOpenGLContext context;
    if (gl) {
        surface.create();
        context.setFormat(surface.format());
        if (!context.create() || !context.makeCurrent(&surface)) {
            qWarning() << "No GL context";
            return 1;
        }
    }
    std::unique_ptr<RenderProbe> render;
    if (gl)
        render = std::make_unique<RenderProbe>();
    QStringList paths;
    if (QFileInfo(input).isFile())
        paths << input;
    else {
        QDirIterator it(input, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            auto p = it.next();
            if (p.endsWith(".s", Qt::CaseInsensitive))
                paths << p;
        }
    }
    paths.sort();
    int failures = 0;
    QTemporaryDir temp;
    for (auto &path : paths) {
        QElapsedTimer timer;
        // Model Windows case-insensitive image lookup using private symlinks.
        // Corpus files themselves remain untouched.
        const QString textures = temp.filePath(QString::number(paths.indexOf(path)));
        QDir().mkpath(textures);
        QDir originalDir(QFileInfo(path).absolutePath());
        for (const auto &entry : originalDir.entryInfoList(QDir::Files)) {
            QFile::link(entry.absoluteFilePath(), textures + "/" + entry.fileName());
            if (entry.fileName() != entry.fileName().toLower())
                QFile::link(entry.absoluteFilePath(), textures + "/" + entry.fileName().toLower());
        }
        SFileDetail::Document imageNames;
        imageNames.read(path);
        if (auto images = imageNames.root.child("images"))
            for (auto image : images->children("image"))
                for (const auto &entry : originalDir.entryInfoList(QDir::Files))
                    if (entry.fileName().compare(image->scalar(0), Qt::CaseInsensitive) == 0)
                        QFile::link(entry.absoluteFilePath(), textures + "/" + image->scalar(0));
        imageNames = SFileDetail::Document();
        timer.start();
        SFileComplex shape(path, QFileInfo(path).fileName(), textures);
        bool loaded = shape.loadData();
        double cpu = timer.nsecsElapsed() / 1e6;
        QString error;
        bool valid = loaded && shape.health() != SFileComplex::Health::Broken;
        SFileDetail::Document source;
        const bool sourceRead = source.read(path);
        auto encoded = source.encode(source.binary, source.binary && source.compressed, error);
        SFileDetail::Document roundtrip;
        bool saved = error.isEmpty() && roundtrip.readBytes(encoded) && !roundtrip.damaged;
        if (saved) {
            const auto canonical = source.encode(source.binary, false, error);
            saved = error.isEmpty() && roundtrip.encode(source.binary, false, error) == canonical &&
                    error.isEmpty();
        }
        auto stats = shape.statistics();
        QJsonObject result{{"path", path},
                           {"cpu_ms", cpu},
                           {"loaded", loaded},
                           {"valid", valid},
                           {"roundtrip", saved},
                           {"source_read", sourceRead},
                           {"source_diagnostics", source.diagnostics.join("; ")},
                           {"roundtrip_error", error + roundtrip.diagnostics.join("; ")},
                           {"lods", stats.lods},
                           {"parts", stats.parts},
                           {"document_bytes", double(stats.documentBytes)},
                           {"geometry_bytes", double(stats.sourceGeometryBytes)},
                           {"runtime_bytes", double(stats.runtimeBytes)},
                           {"diagnostics", shape.diagnostics().join("; ")}};
        SFileComplex requested(path, QFileInfo(path).fileName(), textures);
        requested.setLoadOptions(shapeLoadOptions(false, true));
        timer.restart();
        bool requestedValid =
            requested.loadData() && requested.health() != SFileComplex::Health::Broken;
        result["requested_compact_cpu_ms"] = timer.nsecsElapsed() / 1e6;
        const auto compactStats = requested.statistics();
        result["requested_compact_stages_ms"] = QJsonObject{
            {"read", compactStats.readMs}, {"extract", compactStats.extractMs},
            {"metadata", compactStats.metadataMs}, {"cleanup", compactStats.cleanupMs}};
        result["requested_compact_load_document_bytes"] = double(compactStats.loadDocumentBytes);
        result["requested_compact_cpu_geometry_bytes"] = double(compactStats.sourceGeometryBytes);
        result["requested_compact_blocks"] = double(compactStats.sourceBlocks);
        result["requested_compact_scalars"] = double(compactStats.sourceScalars);
        result["requested_compact_skipped_blocks"] = double(compactStats.skippedSourceBlocks);
        result["requested_compact_cpu_valid"] =
            requestedValid && requested.retention() == SFileComplex::Retention::Compact &&
            compactStats.documentBytes == 0;
        if (!result["requested_compact_cpu_valid"].toBool())
            ++failures;
        {
            SFileComplex first(path, QFileInfo(path).fileName(), textures);
            first.setLoadOptions(shapeLoadOptions(true, false));
            timer.restart();
            first.loadData();
            result["first_lod_cpu_ms"] = timer.nsecsElapsed() / 1e6;
            result["first_lod_document_bytes"] = double(first.statistics().documentBytes);
        }
        if (gl && valid) {
            timer.restart();
            bool ready = shape.initGL();
            result["init_gl_ms"] = timer.nsecsElapsed() / 1e6;
            result["gpu_ready"] = ready;
            result["gpu_bytes"] = double(shape.statistics().gpuBytes);
            SFile old(path, QFileInfo(path).fileName(), textures);
            timer.restart();
            old.load();
            result["legacy_load_gl_ms"] = timer.nsecsElapsed() / 1e6;
            result["legacy_loaded"] = old.isLoaded();
            if (old.isLoaded()) {
                const auto storage = legacyStorage(old);
                for (auto it = storage.begin(); it != storage.end(); ++it)
                    result[it.key()] = it.value();
                if (!storage["legacy_gpu_size_valid"].toBool())
                    ++failures;
            }
            SFileLegacy joined(path, QFileInfo(path).fileName(), textures);
            auto *context = QOpenGLContext::currentContext();
            auto *surface = context->surface();
            context->doneCurrent();
            timer.restart();
            bool joinedLoaded = joined.loadData();
            result["joined_cpu_ms"] = timer.nsecsElapsed() / 1e6;
            bool cpuOnly = joinedLoaded && joined.isLoaded() && !joined.isGLReady() &&
                           !joined.initGL();
            for (int l = 0; l < joined.iloscd; ++l)
                for (int o = 0; o < joined.distancelevel[l].iloscs; ++o)
                    cpuOnly &= !joined.distancelevel[l].subobiekty[o].VBO.isCreated();
            context->makeCurrent(surface);
            timer.restart();
            bool joinedReady = joined.initGL();
            result["joined_init_gl_ms"] = timer.nsecsElapsed() / 1e6;
            result["joined_load_gl_ms"] = result["joined_cpu_ms"].toDouble() +
                                          result["joined_init_gl_ms"].toDouble();
            result["joined_cpu_only"] = cpuOnly;
            result["joined_ready"] = joinedReady && joined.initGL();
            bool joinedBounds = joinedLoaded && old.isLoaded();
            for (int axis = 0; axis < 6; ++axis)
                joinedBounds &= joined.bound[axis] == old.bound[axis];
            result["joined_bounds_equal"] = joinedBounds;
            bool joinedEqual = cpuOnly && joinedReady && joinedBounds;
            if (joinedReady && old.isLoaded()) {
                render->cameraReference = &old;
                for (int warm = 0; warm < 8; ++warm) {
                    render->image(old, false);
                    render->image(joined, false);
                    QCoreApplication::processEvents();
                }
                MatrixProbe baseline, candidate;
                baseline.captureGeometry = candidate.captureGeometry = true;
                auto *previous = Game::currentRenderer;
                Game::currentRenderer = &baseline;
                old.pushRenderItem(0, 0);
                Game::currentRenderer = &candidate;
                joined.pushRenderItem(0, 0);
                Game::currentRenderer = previous;
                bool geometry = baseline.buffers == candidate.buffers &&
                                baseline.transforms == candidate.transforms;
                result["joined_geometry_equal"] = geometry;
                auto originalImage = render->image(old, false);
                int pixels = difference(originalImage, render->image(joined, false));
                result["joined_different_pixels"] = pixels;
                bool picking = render->selection(old, false) == render->selection(joined, false);
                result["joined_selection_equal"] = picking;
                result["joined_gather_different_pixels"] =
                    difference(render->image(old, true), render->image(joined, true));
                render->orderedGather = true;
                int orderedPixels = difference(render->image(old, true), render->image(joined, true));
                result["joined_ordered_gather_different_pixels"] = orderedPixels;
                render->orderedGather = false;
                joinedEqual &= geometry && pixels == 0 && picking && orderedPixels == 0;
                bool allLods = old.iloscd == joined.iloscd;
                for (int l = 0; allLods && l < old.iloscd; ++l) {
                    auto &a = old.distancelevel[l];
                    auto &b = joined.distancelevel[l];
                    allLods &= a.iloscs == b.iloscs && a.ilosch == b.ilosch &&
                               a.levelSelection == b.levelSelection;
                    for (int h = 0; allLods && h < a.ilosch; ++h)
                        allLods &= a.hierarchia[h] == b.hierarchia[h];
                    for (int o = 0; allLods && o < a.iloscs; ++o) {
                        auto readBuffer = [](QOpenGLBuffer &buffer) {
                            QByteArray bytes;
                            if (buffer.bind()) {
                                bytes.resize(buffer.size());
                                if (!buffer.read(0, bytes.data(), bytes.size())) bytes.clear();
                                buffer.release();
                            }
                            return bytes;
                        };
                        allLods &= readBuffer(a.subobiekty[o].VBO) == readBuffer(b.subobiekty[o].VBO);
                    }
                }
                result["joined_all_lods_equal"] = allLods;
                joinedEqual &= allLods;
                if (!old.animations.empty() && old.animations[0].frames > 0) {
                    const auto a = old.newState(), b = joined.newState();
                    old.setAnimated(a, true);
                    joined.setAnimated(b, true);
                    int animationPixels = 0;
                    for (float step : {0.0f, 0.013f, 0.071f}) {
                        old.updateSim(step, a);
                        joined.updateSim(step, b);
                        animationPixels += difference(render->image(old, false, true, a),
                                                      render->image(joined, false, true, b));
                    }
                    result["joined_animation_different_pixels"] = animationPixels;
                    joinedEqual &= animationPixels == 0;
                    old.setAnimated(a, false);
                    joined.setAnimated(b, false);
                    old.animated = joined.animated = false;
                }

            }
            if (!joinedEqual) ++failures;
            if (qEnvironmentVariableIsSet("TSRE_LEGACY_UPLOAD_BENCH")) {
                QJsonArray cpuSamples, uploadSamples;
                for (int iteration = -2; iteration < 15; ++iteration) {
                    SFileLegacy sample(path, QFileInfo(path).fileName(), textures);
                    timer.restart();
                    const bool loaded = sample.loadData();
                    const double cpuMs = timer.nsecsElapsed() / 1e6;
                    context->functions()->glFinish();
                    timer.restart();
                    const bool initialized = sample.initGL();
                    context->functions()->glFinish();
                    const double uploadMs = timer.nsecsElapsed() / 1e6;
                    if (!loaded || !initialized) ++failures;
                    if (iteration >= 0) {
                        cpuSamples.append(cpuMs);
                        uploadSamples.append(uploadMs);
                    }
                }
                result["joined_cpu_samples_ms"] = cpuSamples;
                result["joined_upload_samples_ms"] = uploadSamples;
            }
            const auto completeStorage = shape.statistics();
            result["complete_retained_cpu_estimate_bytes"] =
                double(completeStorage.documentBytes + completeStorage.sourceGeometryBytes +
                       completeStorage.runtimeBytes);
            bool bounds = true;
            for (int i = 0; i < 6; ++i)
                bounds &= std::abs(old.bound[i] - shape.getBound()[i]) < 0.001f;
            result["bounds_equal"] = bounds;
            ShapeHierarchyInfo legacy, newInfo;
            old.fillShapeHierarchyInfo(&legacy);
            shape.fillShapeHierarchyInfo(&newInfo);
            result["legacy_parts"] = legacy.parts.size();
            result["first_lod_parts_equal"] = legacy.parts.size() == newInfo.parts.size();
            if (ready && old.isLoaded()) {
                render->cameraReference = &old;
                result["size_delta"] =
                    double(shape.getSize() - static_cast<const ComplexShape &>(old).getSize());
                for (int warm = 0; warm < 8; ++warm) {
                    render->image(old, false);
                    render->image(shape, false);
                    QCoreApplication::processEvents();
                    QThread::msleep(2);
                }
                MatrixProbe oldStatic, newStatic;
                oldStatic.captureGeometry = newStatic.captureGeometry = true;
                auto *savedRenderer = Game::currentRenderer;
                Game::currentRenderer = &oldStatic;
                old.pushRenderItem(0, 0);
                Game::currentRenderer = &newStatic;
                shape.pushRenderItem(0, 0);
                Game::currentRenderer = savedRenderer;
                double matrixError = 0, geometryError = 0;
                if (oldStatic.transforms.size() != newStatic.transforms.size())
                    matrixError = geometryError = -1;
                else
                    for (size_t part = 0; part < oldStatic.transforms.size(); ++part) {
                        for (int axis = 0; axis < 16; ++axis)
                            matrixError = std::max(
                                matrixError, double(std::abs(oldStatic.transforms[part][axis] -
                                                             newStatic.transforms[part][axis])));
                        const auto &a = oldStatic.buffers[part], &b = newStatic.buffers[part];
                        if (a.size() != b.size())
                            geometryError = -1;
                        else
                            for (size_t i = 0; i < a.size(); ++i)
                                geometryError =
                                    std::max(geometryError, double(std::abs(a[i] - b[i])));
                    }
                int hashCollisions = 0;
                for (size_t i = 0; i < oldStatic.transforms.size(); ++i)
                    for (size_t j = 0; j < i; ++j)
                        if (oldStatic.transforms[i] != oldStatic.transforms[j] &&
                            GLUU::get()->getMatrixHash(oldStatic.transforms[i].data()) ==
                                GLUU::get()->getMatrixHash(oldStatic.transforms[j].data()))
                            ++hashCollisions;
                result["legacy_matrix_hash_collisions"] = hashCollisions;
                result["static_matrix_max_error"] = matrixError;
                result["geometry_max_error"] = geometryError;
                const auto oldImage = render->image(old, false),
                           newImage = render->image(shape, false);
                timer.restart();
                const bool compactReady = requested.initGL();
                result["requested_compact_gl_ms"] = timer.nsecsElapsed() / 1e6;
                const auto retainedCompact = requested.statistics();
                result["requested_compact_retained_cpu_estimate_bytes"] =
                    double(retainedCompact.documentBytes + retainedCompact.sourceGeometryBytes +
                           retainedCompact.runtimeBytes);
                result["requested_compact_gpu_bytes"] = double(retainedCompact.gpuBytes);
                result["requested_compact_render_equal"] =
                    compactReady && difference(newImage, render->image(requested, false)) == 0 &&
                    difference(newImage, render->image(requested, true)) == 0 &&
                    render->selection(shape, false) == render->selection(requested, false) &&
                    render->selection(shape, false) == render->selection(requested, true);
                if (!result["requested_compact_render_equal"].toBool())
                    ++failures;
                result["render_different_pixels"] = difference(oldImage, newImage);
                result["new_gather_different_pixels"] =
                    difference(newImage, render->image(shape, true));
                result["legacy_gather_different_pixels"] =
                    difference(oldImage, render->image(old, true));
                auto oldPick = render->selection(old, false),
                     newPick = render->selection(shape, false);
                result["selection_equal"] = oldPick == newPick;
                result["new_gather_selection_equal"] = newPick == render->selection(shape, true);
                if (hashCollisions) {
                    std::vector<long long> hashes;
                    for (int i = 0; i < old.iloscm; ++i) {
                        hashes.push_back(old.macierz[i].hash);
                        old.macierz[i].hash = i + 1;
                    }
                    result["legacy_unique_hash_pixels"] =
                        difference(newImage, render->image(old, false));
                    result["legacy_unique_hash_selection_equal"] =
                        newPick == render->selection(old, false);
                    for (int i = 0; i < old.iloscm; ++i)
                        old.macierz[i].hash = hashes[i];
                }
                const QString out = qEnvironmentVariable("TSRE_SHAPE_COMPARE_IMAGES");
                if (!out.isEmpty()) {
                    QDir().mkpath(out);
                    oldImage.save(out + "/" + QFileInfo(path).baseName() + "-old.png");
                    newImage.save(out + "/" + QFileInfo(path).baseName() + "-new.png");
                }
                auto measure = [&](ComplexShape &asset, bool gather) {
                    QElapsedTimer t;
                    t.start();
                    for (int i = 0; i < 12; ++i)
                        render->image(asset, gather);
                    return t.nsecsElapsed() / 1e6 / 12;
                };
                result["legacy_submission"] = render->submission(old, false);
                result["complete_submission"] = render->submission(shape, false);
                result["legacy_frame_ms"] = measure(old, false);
                result["complete_frame_ms"] = measure(shape, false);
                shape.compact();
                result["compact_submission"] = render->submission(shape, false);
                result["compact_gather_submission"] = render->submission(shape, true);
                result["compact_frame_ms"] = measure(shape, false);
                result["compact_gather_frame_ms"] = measure(shape, true);
                // Compare a serialized Complete document through the runtime as well.
                const QString roundtripPath = temp.filePath("roundtrip.s");
                bool roundtripRendered = source.save(roundtripPath, source.binary, false, error);
                if (roundtripRendered) {
                    SFileComplex reloaded(roundtripPath, "roundtrip", textures);
                    roundtripRendered = reloaded.loadData() && reloaded.initGL() &&
                                        difference(newImage, render->image(reloaded, false)) == 0;
                }
                result["saved_render_equal"] = roundtripRendered;
                if (!roundtripRendered)
                    ++failures;
                old.setAnimated(0, true);
                shape.setAnimated(0, true);
                old.updateSim(.17f);
                shape.updateSim(.17f);
                MatrixProbe oldMatrices, newMatrices;
                auto *previousRenderer = Game::currentRenderer;
                Game::currentRenderer = &oldMatrices;
                old.pushRenderItem(0, 0);
                Game::currentRenderer = &newMatrices;
                shape.pushRenderItem(0, 0);
                Game::currentRenderer = previousRenderer;
                double maxMatrixError = 0;
                if (oldMatrices.transforms.size() == newMatrices.transforms.size())
                    for (size_t part = 0; part < oldMatrices.transforms.size(); ++part)
                        for (int axis = 0; axis < 16; ++axis)
                            maxMatrixError =
                                std::max(maxMatrixError,
                                         double(std::abs(oldMatrices.transforms[part][axis] -
                                                         newMatrices.transforms[part][axis])));
                else
                    maxMatrixError = -1;
                result["animated_matrix_max_error"] = maxMatrixError;
                if (!old.animations.empty()) {
                    result["animation_frames"] = old.animations[0].frames;
                    result["animation_fps"] = old.animations[0].fps;
                }
                result["animated_different_pixels"] =
                    difference(render->image(old, false), render->image(shape, false));
                if (!old.animations.empty()) {
                    for (auto &node : old.animations[0].node) {
                        auto endpoints = [](auto &keys, auto &frames) {
                            if (keys.empty())
                                return;
                            for (int frame = keys.back().frame; frame < frames.size(); ++frame) {
                                frames[frame].id1 = frames[frame].id2 = keys.size() - 1;
                                frames[frame].offset = 0;
                            }
                        };
                        endpoints(node.tcbKey, node.tcbId);
                        endpoints(node.linearKey, node.linearId);
                    }
                    result["legacy_fixed_endpoint_pixels"] =
                        difference(render->image(old, false), render->image(shape, false));
                }
            }
            shape.compact();
            result["compact_document_bytes"] = double(shape.statistics().documentBytes);
            result["compact_runtime_bytes"] = double(shape.statistics().runtimeBytes);
            result["compact_geometry_bytes"] = double(shape.statistics().sourceGeometryBytes);
            if (!ready || !old.isLoaded() || !bounds ||
                result["new_gather_different_pixels"].toInt() != 0 ||
                !result["new_gather_selection_equal"].toBool())
                ++failures;
        }
        if (!valid || !saved)
            ++failures;
        qInfo().noquote() << QJsonDocument(result).toJson(QJsonDocument::Compact);
    }
    qInfo() << "Corpus shapes" << paths.size() << "failures" << failures;
    return paths.empty() || failures ? 1 : 0;
}
