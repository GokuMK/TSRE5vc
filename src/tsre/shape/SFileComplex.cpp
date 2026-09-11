#include "SFileComplexData.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <type_traits>
#include <shapeViewer/ContentHierarchyInfo.h>
#include <shapeViewer/ShapeHierarchyInfo.h>
#include <shapeViewer/ShapeTextureInfo.h>
#include <tsre/Game.h>
#include <tsre/fileFunctions/SimisTextReader.h>
#include <tsre/texture/TexLib.h>
using SFileDetail::Node;
namespace {
Node child(Node n, const char *name) { return n.child(name); }
int integer(Node n, int index, int fallback = 0) { return n.integer(index, fallback); }
std::vector<int> indices(Node n) {
    std::vector<int> out;
    out.reserve(std::max(0, n.scalarCount() - 1));
    for (int i = 1; i < n.scalarCount(); ++i)
        out.push_back(n.integer(i, -1));
    return out;
}
QVector3D vec(const Node &n, int start = 0) {
    return {float(n.number(start)), float(n.number(start + 1)), float(n.number(start + 2))};
}
Node locate(Node root, const QString &path) {
    auto n = root;
    static const QRegularExpression re("^([^\\[\\]]+)(?:\\[(\\d+)\\])?$");
    for (auto part : path.split('/', Qt::SkipEmptyParts)) {
        auto m = re.match(part);
        if (!m.hasMatch())
            return {};
        bool ok = true;
        int index = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt(&ok);
        if (!ok)
            return {};
        n = n.children(m.captured(1).toLower())[index];
        if (!n)
            return {};
    }
    return n;
}
} // namespace
SFileComplex::SFileComplex(QString path, QString name, QString textureRoot) : d(new Data) {
    d->path = std::move(path);
    d->name = std::move(name);
    d->textureRoot = std::move(textureRoot);
    d->texturePath = d->textureRoot;
}
SFileComplex::~SFileComplex() {
    if (d) {
        releaseGL();
        releaseTextures();
    }
}
void SFileComplex::releaseTextures() {
    for (auto &image : d->images) {
        if (image.id >= 0)
            TexLib::delRef(image.id);
        image.id = image.address = -1;
    }
}
const QString &SFileComplex::getPathId() const { return d->path; }
const QString &SFileComplex::getTexPath() const { return d->texturePath; }
int SFileComplex::getEsdDetailLevel() const { return d->detail; }
bool SFileComplex::isLoaded() const { return d->loaded; }
float SFileComplex::getSize() const { return d->size; }
const float *SFileComplex::getBound() const { return d->bound; }
SFileComplex::Retention SFileComplex::retention() const { return d->retention; }
SFileComplex::Health SFileComplex::health() const { return d->health; }
SFileComplex::GpuState SFileComplex::gpuState() const { return d->gpuState; }
QStringList SFileComplex::diagnostics() const { return d->diagnostics; }
bool SFileComplex::setLoadOptions(LoadOptions o) {
    if (d->attempted)
        return false;
    d->options = o;
    return true;
}
SFileComplex::Statistics SFileComplex::statistics() const {
    Statistics s;
    s.readMs = d->readMs;
    s.extractMs = d->extractMs;
    s.metadataMs = d->metadataMs;
    s.cleanupMs = d->cleanupMs;
    s.loadDocumentBytes = d->loadDocumentBytes;
    s.sourceBlocks = d->sourceBlocks;
    s.sourceScalars = d->sourceScalars;
    s.skippedSourceBlocks = d->skippedSourceBlocks;
    s.matrices = d->matrices.size();
    s.images = d->images.size();
    s.lods = d->lods.size();
    s.animations = d->animations.size();
    s.documentBytes = (d->document ? d->document->storageBytes() : 0) +
                      (d->metadata ? d->metadata->storageBytes() : 0);
    s.sourceGeometryBytes = d->points.capacity() * sizeof(QVector3D) +
                            d->normals.capacity() * sizeof(QVector3D) +
                            d->uvs.capacity() * sizeof(QVector2D);
    s.runtimeBytes = sizeof(Data) + d->matrices.capacity() * sizeof(Data::Matrix) +
                     d->images.capacity() * sizeof(Data::Image) +
                     d->materials.capacity() * sizeof(Data::Material) +
                     d->lods.capacity() * sizeof(Data::Lod) +
                     d->animations.capacity() * sizeof(Data::Animation) +
                     d->states.capacity() * sizeof(Data::State) +
                     d->boxes.capacity() * sizeof(std::array<float, 6>);
    for (const auto &m : d->matrices)
        s.runtimeBytes += m.name.size() * 2;
    for (const auto &image : d->images)
        s.runtimeBytes += image.name.size() * 2;
    for (const auto &a : d->animations) {
        s.runtimeBytes += a.channels.capacity() * sizeof(Data::Channel);
        for (const auto &c : a.channels)
            s.runtimeBytes += (c.position.capacity() + c.rotation.capacity()) * sizeof(Data::Key);
    }
    for (const auto &state : d->states)
        s.runtimeBytes += state.matrices.capacity() * sizeof(QMatrix4x4) +
                          state.packets.size() * sizeof(RenderItem);
    for (const auto &l : d->lods)
        s.runtimeBytes += (l.parents.capacity() + l.matrixOrder.capacity()) * sizeof(int) +
                          l.meshes.capacity() * sizeof(Data::Mesh);
    for (auto &l : d->lods)
        for (auto &m : l.meshes) {
            s.runtimeBytes +=
                m.parts.capacity() * sizeof(Data::Part) + m.geometryMap.capacity() * sizeof(int);
            ++s.subobjects;
            s.vertices += m.vertexCount;
            s.parts += m.parts.size();
            s.sourceGeometryBytes += m.vertices.capacity() * sizeof(Data::Vertex);
            if (m.gpu)
                s.gpuBytes += m.gpu->bytes;
            for (auto &p : m.parts)
                s.sourceGeometryBytes += p.indices.capacity() * sizeof(int);
        }
    return s;
}
bool SFileComplex::loadData() {
    if (d->loaded)
        return true;
    d->attempted = true;
    d->readMs = d->extractMs = d->metadataMs = d->cleanupMs = 0;
    QElapsedTimer stage;
    stage.start();
    d->document = std::make_unique<SFileDetail::Document>();
    bool read = d->document->read(d->path, d->options.firstLodOnly, d->options.compact);
    d->readMs = stage.nsecsElapsed() / 1e6;
    d->diagnostics = d->document->diagnostics;
    if (!read) {
        d->health = Health::Broken;
        return false;
    }
    d->retention = d->options.compact
                       ? Retention::Compact
                       : (d->document->partial ? Retention::Partial : Retention::Complete);
    d->health = d->document->damaged ? Health::Broken
                                     : (d->diagnostics.empty() ? Health::Valid : Health::Recovered);
    stage.restart();
    extract();
    d->extractMs = stage.nsecsElapsed() / 1e6;
    stage.restart();
    loadMetadata();
    d->metadataMs = stage.nsecsElapsed() / 1e6;
    stage.restart();
    d->loadDocumentBytes =
        d->document->storageBytes() + (d->metadata ? d->metadata->storageBytes() : 0);
    d->sourceBlocks = d->document->blockCount();
    d->sourceScalars = d->document->scalarCount();
    d->skippedSourceBlocks = d->document->skippedBlocks;
    d->sourceAvailable = true;
    // Compact has already skipped source-only blocks during parsing. After runtime
    // extraction this temporary typed document is no longer needed, even before GL.
    if (d->options.compact && d->health != Health::Broken) {
        d->document.reset();
        d->metadata.reset();
    }
    d->cleanupMs = stage.nsecsElapsed() / 1e6;
    d->loaded = true;
    return true;
}
void SFileComplex::load() {
    if (loadData() && QOpenGLContext::currentContext())
        initGL();
}
void SFileComplex::reload() {
    releaseGL();
    d->loaded = false;
    d->sourceAvailable = false;
    d->attempted = false;
    d->document.reset();
    d->metadata.reset();
    d->edited = false;
    d->diagnostics.clear();
    d->health = Health::Valid;
    d->retention = Retention::Unloaded;
    load();
}
bool SFileComplex::reloadComplete() {
    if (d->edited) {
        d->diagnostics << "Complete reload refused: unsaved edits";
        return false;
    }
    d->options = {};
    reload();
    return d->loaded && d->retention == Retention::Complete && d->document && !d->document->damaged;
}
bool SFileComplex::compact() {
    if (d->gpuState != GpuState::Ready)
        return false;
    if (d->edited) {
        d->diagnostics << "Compaction refused: unsaved edits require Complete storage";
        return false;
    }
    d->document.reset();
    d->metadata.reset();
    std::vector<QVector3D>().swap(d->points);
    std::vector<QVector3D>().swap(d->normals);
    std::vector<QVector2D>().swap(d->uvs);
    for (auto &l : d->lods)
        for (auto &m : l.meshes) {
            std::vector<Data::Vertex>().swap(m.vertices);
            for (auto &p : m.parts)
                std::vector<int>().swap(p.indices);
        }
    d->sourceAvailable = false;
    d->options.compact = true; // Subsequent context-loss reloads request Compact before parsing.
    d->retention = Retention::Compact;
    return true;
}
bool SFileComplex::save(const QString &path, Format f, bool compressed, QString *error) const {
    QString e;
    bool ok = false;
    if (d->retention != Retention::Complete || !d->document)
        e = "Saving requires a full reload into Complete mode";
    else
        ok = d->document->save(path, f == Format::Binary, compressed, e);
    if (error)
        *error = e;
    return ok;
}
bool SFileComplex::saveMetadata(const QString &path, bool compressed, QString *error) const {
    QString e;
    bool ok = false;
    if (d->retention != Retention::Complete || !d->metadata)
        e = "Complete metadata required for saving";
    else
        ok = d->metadata->save(path, false, compressed, e);
    if (error)
        *error = e;
    return ok;
}
QString SFileComplex::field(const QString &path, int i) const {
    auto *doc = path.startsWith("sd/") ? d->metadata.get() : d->document.get();
    if (!doc)
        return {};
    auto n = locate(doc->root, path.startsWith("sd/") ? path.mid(3) : path);
    return n ? n->scalar(i) : QString();
}
bool SFileComplex::setField(const QString &path, int i, const QString &value, QString *error) {
    auto fail = [&](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    if (d->retention != Retention::Complete || !d->document)
        return fail("Editing requires Complete mode");
    auto *doc = path.startsWith("sd/") ? d->metadata.get() : d->document.get();
    if (!doc)
        return fail("No metadata document");
    auto n = locate(doc->root, path.startsWith("sd/") ? path.mid(3) : path);
    if (!n)
        return fail("Unknown block path");
    if (!n.setScalar(i, value, error))
        return false;
    releaseGL();
    d->edited = true;
    if (path.startsWith("sd/"))
        loadMetadata(false);
    else
        extract();
    return true;
}
bool SFileComplex::extract() {
    auto &root = d->document->root;
    d->diagnostics = d->document->diagnostics;
    const bool discardAnimations = d->document->damageConfinedToAnimations();
    d->health = d->document->damaged ? (discardAnimations ? Health::Recovered : Health::Broken)
                                   : (d->diagnostics.empty() ? Health::Valid : Health::Recovered);
    if (discardAnimations)
        d->diagnostics << "Damaged animation section discarded for rendering; source remains damaged";
    d->points.clear();
    d->normals.clear();
    d->uvs.clear();
    d->matrices.clear();
    releaseTextures();
    d->images.clear();
    d->materials.clear();
    d->lods.clear();
    d->animations.clear();
    d->size = 0;
    std::fill(std::begin(d->bound), std::end(d->bound), 0);
    auto broken = [&](const QString &msg) {
        d->health = Health::Broken;
        if (!d->diagnostics.contains(msg))
            d->diagnostics << msg;
    };
    if (root.name() != "shape") {
        broken("Expected shape root");
        return false;
    }
    auto checkFloats = [&](const Node &n, int count) {
        for (int i = 0; i < count; ++i) {
            double v = n.number(i, std::numeric_limits<double>::quiet_NaN());
            if (!std::isfinite(v) || std::abs(v) > std::numeric_limits<float>::max())
                broken("Invalid/missing geometry coordinate");
        }
    };
    auto readCoordinates = [&](Node table, const char *rowName, auto &output, int width) {
        if (const auto *packed = table.packed()) {
            output.reserve(packed->rows());
            for (size_t row = 0; row < packed->rows(); ++row) {
                typename std::decay_t<decltype(output)>::value_type value;
                for (int axis = 0; axis < width; ++axis) {
                    const float coordinate = packed->number(row * width + axis);
                    if (!std::isfinite(coordinate))
                        broken("Invalid/missing geometry coordinate");
                    value[axis] = coordinate;
                }
                output.push_back(value);
            }
        } else {
            for (auto row : table.children(rowName)) {
                checkFloats(row, width);
                typename std::decay_t<decltype(output)>::value_type value;
                for (int axis = 0; axis < width; ++axis)
                    value[axis] = float(row.number(axis));
                output.push_back(value);
            }
        }
    };
    readCoordinates(child(root, "points"), "point", d->points, 3);
    readCoordinates(child(root, "normals"), "vector", d->normals, 3);
    readCoordinates(child(root, "uv_points"), "uv_point", d->uvs, 2);
    if (d->points.empty())
        broken("Missing points");
    else {
        auto min = d->points[0], max = min;
        for (auto p : d->points)
            for (int axis = 0; axis < 3; ++axis) {
                min[axis] = std::min(min[axis], p[axis]);
                max[axis] = std::max(max[axis], p[axis]);
            }
        for (int a = 0; a < 3; ++a) {
            d->bound[2 * a] = max[a];
            d->bound[2 * a + 1] = min[a];
        }
        d->size = (max - min).length();
    }
    for (auto p : child(root, "matrices").children("matrix")) {
        checkFloats(*p, 12);
        Data::Matrix m;
        m.name = p->label();
        float *v = m.transform.data();
        int j = 0;
        for (int i = 0; i < 16; ++i)
            v[i] = i == 15 ? 1 : ((i == 3 || i == 7 || i == 11) ? 0 : float(p->number(j++)));
        d->matrices.push_back(std::move(m));
    }
    if (d->matrices.empty())
        broken("Missing matrices");
    for (auto p : child(root, "images").children("image"))
        d->images.push_back({p->scalar(0), -1, -1});
    auto textures = child(root, "textures").children("texture");
    auto shaders = child(root, "shader_names").children("named_shader");
    auto vertices = child(root, "vtx_states").children("vtx_state");
    for (auto p : child(root, "prim_states").children("prim_state")) {
        Data::Material mat;
        int v = integer(*p, 3, -1); // scalar sequence: flags, shader, zBias,
                                    // vertex-state, alpha, light, z
        if (v < 0 || v >= int(vertices.size()))
            broken("Invalid primitive vertex-state reference");
        else {
            mat.matrix = integer(*vertices[v], 1, -1);
            mat.light = integer(*vertices[v], 2);
        }
        if (mat.matrix < 0 || mat.matrix >= int(d->matrices.size()))
            broken("Invalid matrix reference");
        int sh = integer(*p, 1, -1);
        mat.alpha = (sh >= 0 && sh < int(shaders.size()) &&
                     shaders[sh]->scalar(0).compare("texdiff", Qt::CaseInsensitive) == 0)
                        ? 1
                        : 0;
        mat.alphaTest = integer(*p, 4) == 1;
        auto tex = indices(child(*p, "tex_idxs"));
        if (!tex.empty()) {
            int t = tex[0];
            if (t >= 0 && t < int(textures.size()))
                mat.image = integer(*textures[t], 0, -1);
            if (mat.image < 0 || mat.image >= int(d->images.size())) {
                mat.image = -1;
                d->diagnostics << "Invalid texture reference; using fallback";
            }
        }
        d->materials.push_back(mat);
    }
    int control = 0;
    for (auto c : child(root, "lod_controls").children("lod_control")) {
        int sourceLevel = 0;
        for (auto p : child(*c, "distance_levels").children("distance_level")) {
            Data::Lod lod;
            lod.control = control;
            lod.sourceLevel = sourceLevel++;
            auto hdr = child(*p, "distance_level_header");
            lod.distance = child(hdr, "dlevel_selection").number(0);
            lod.parents = indices(child(hdr, "hierarchy"));
            if (lod.parents.size() != d->matrices.size())
                broken("Hierarchy/matrix count mismatch");
            std::vector<int> mark(d->matrices.size());
            // Iterative parent walk avoids recursion proportional to asset size.
            for (int start = 0; start < int(mark.size()); ++start) {
                std::vector<int> chain;
                int i = start;
                while (i >= 0 && i < int(mark.size()) && mark[i] == 0) {
                    mark[i] = 1;
                    chain.push_back(i);
                    int parent = i < int(lod.parents.size()) ? lod.parents[i] : -1;
                    i = (i == 0 && parent == 0) ? -1 : parent;
                }
                if (i >= int(mark.size()) || (i >= 0 && mark[i] == 1))
                    broken("Invalid/cyclic hierarchy");
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    mark[*it] = 2;
                    lod.matrixOrder.push_back(*it);
                }
            }
            int sub = 0;
            for (auto obj : child(*p, "sub_objects").children("sub_object")) {
                Data::Mesh mesh;
                mesh.subobject = sub++;
                mesh.geometryMap = indices(child(
                    child(child(*obj, "sub_object_header"), "geometry_info"), "geometry_node_map"));
                auto addVertex = [&](Data::Vertex vertex) {
                    if (vertex.point < 0 || vertex.point >= int(d->points.size()))
                        broken("Invalid point reference");
                    if (vertex.normal < 0 || vertex.normal >= int(d->normals.size()))
                        broken("Missing/invalid normal reference");
                    if (vertex.uv >= int(d->uvs.size()))
                        broken("Invalid UV reference");
                    mesh.vertices.push_back(vertex);
                };
                const auto vertexTable = child(*obj, "vertices");
                if (const auto *packed = vertexTable.packed()) {
                    mesh.vertices.reserve(packed->rows());
                    for (size_t row = 0; row < packed->rows(); ++row)
                        addVertex({packed->integer(row * 3), packed->integer(row * 3 + 1),
                                   packed->integer(row * 3 + 2)});
                } else {
                    for (auto v : vertexTable.children("vertex"))
                        addVertex({integer(v, 1, -1), integer(v, 2, -1),
                                   v.child(TS::vertex_uvs).integer(1, -1)});
                }
                mesh.vertexCount = mesh.vertices.size();
                int state = 0, partId = 0;
                for (auto prim : child(*obj, "primitives").children()) {
                    if (prim->name() == "prim_state_idx") {
                        state = integer(*prim, 0, -1);
                        continue;
                    }
                    if (prim->name() != "indexed_trilist" && prim->name() != "indexed_line_list" &&
                        prim->name() != "point_list")
                        continue;
                    Data::Part part;
                    part.material = state;
                    part.uid = mesh.subobject * 1000 + partId++;
                    part.mode = prim->name() == "indexed_trilist"     ? 0x0004
                                : prim->name() == "indexed_line_list" ? 0x0001
                                                                      : 0x0000;
                    if (part.mode == 0) {
                        int first = integer(*prim, 0, -1), count = integer(*prim, 1, -1);
                        if (first < 0 || count < 0 || first > int(mesh.vertices.size()) ||
                            count > int(mesh.vertices.size()) - first)
                            broken("Invalid point-list range");
                        else
                            for (int i = 0; i < count; ++i)
                                part.indices.push_back(first + i);
                    } else
                        part.indices = indices(child(*prim, "vertex_idxs"));
                    for (int i : part.indices)
                        if (i < 0 || i >= int(mesh.vertices.size())) {
                            broken("Invalid primitive index");
                            break;
                        }
                    if (state < 0 || state >= int(d->materials.size()))
                        broken("Invalid primitive state");
                    if (part.mode == 0x0004 && part.indices.size() % 3)
                        broken("Incomplete triangle");
                    if (part.mode == 0x0001 && part.indices.size() % 2)
                        broken("Incomplete line");
                    part.count = part.indices.size();
                    mesh.parts.push_back(std::move(part));
                }
                lod.meshes.push_back(std::move(mesh));
            }
            d->lods.push_back(std::move(lod));
        }
        ++control;
    }
    if (d->lods.empty())
        broken("Missing LOD geometry");
    if (!discardAnimations) {
        for (auto a : child(root, "animations").children("animation")) {
            Data::Animation anim;
            anim.frames = a->number(0);
            anim.fps = a->number(1);
            for (auto node : child(*a, "anim_nodes").children("anim_node")) {
                Data::Channel ch;
                for (auto ctrl : child(*node, "controllers").children()) {
                    for (auto key : ctrl->children()) {
                        Data::Key k;
                        k.frame = key->number(0);
                        if ((ctrl->name() == "linear_pos" && key->name() == "linear_key") ||
                            (ctrl->name() == "tcb_pos" && key->name() == "tcb_key")) {
                            k.position = vec(*key, 1);
                            ch.position.push_back(k);
                        } else if ((ctrl->name() == "tcb_rot" || ctrl->name() == "slerp_rot") &&
                                   (key->name() == "tcb_key" || key->name() == "slerp_rot")) {
                            k.rotation = QQuaternion(key->number(4, 1), -key->number(1), key->number(2),
                                                     -key->number(3));
                            ch.rotation.push_back(k);
                        }
                    }
                    if (ctrl->name() == "slerp_rot" && ctrl->children().empty() &&
                        !ctrl->scalar(4).isEmpty()) {
                        Data::Key k;
                        k.frame = ctrl->number(0);
                        k.rotation = QQuaternion(ctrl->number(4, 1), -ctrl->number(1), ctrl->number(2),
                                                 -ctrl->number(3));
                        ch.rotation.push_back(k);
                    }
                }
                auto less = [](auto &a, auto &b) { return a.frame < b.frame; };
                std::stable_sort(ch.position.begin(), ch.position.end(), less);
                std::stable_sort(ch.rotation.begin(), ch.rotation.end(), less);
                anim.channels.push_back(std::move(ch));
            }
            d->animations.push_back(std::move(anim));
        }
    }
    for (auto &s : d->states) {
        s.dirty = true;
        s.namesDirty = true;
        s.matrices.clear();
        s.packets.clear();
        if (s.lod >= int(d->lods.size()))
            s.lod = 0;
    }
    return d->health != Health::Broken;
}
void SFileComplex::loadMetadata(bool readFile) {
    d->detail = -1;
    d->alternative = 0;
    d->snap = false;
    d->boxes.clear();
    d->texturePath = d->textureRoot;
    if (readFile) {
        QString path = d->path + "d";
        if (!QFileInfo::exists(path))
            return;
        d->metadata = std::make_unique<SFileDetail::Document>();
        if (!d->metadata->read(path, false, d->options.compact)) {
            d->diagnostics << "Cannot read shape metadata";
            return;
        }
    }
    if (!d->metadata)
        return;
    const auto &root = d->metadata->root;
    d->detail = integer(child(root, "esd_detail_level"), 0, -1);
    d->alternative = integer(child(root, "esd_alternative_texture"), 0);
    d->snap = bool(root.child("esd_snapable"));
    auto box = [&](const Node &n, int start) {
        std::array<float, 6> b;
        for (int i = 0; i < 6; ++i) {
            if (n.scalar(start + i).isEmpty())
                return;
            b[i] = n.number(start + i);
        }
        d->boxes.push_back(b);
    };
    for (auto b : root.children("esd_bounding_box"))
        box(*b, 0);
    for (auto b : child(root, "esd_complex").children("esd_complex_box"))
        box(*b, 6);
    QString seasonPath;
    if ((d->alternative & Game::TextureFlags.value(Game::season)) != 0)
        seasonPath = '/' + Game::season.toLower();
    if (Game::season == "Winter" || Game::season.endsWith("Snow"))
        if ((d->alternative &
             (Game::TextureFlags.value("Snow") | Game::TextureFlags.value("SnowTrack"))) != 0)
            seasonPath = "/snow";
    d->texturePath += seasonPath;
    if (!readFile)
        releaseTextures();
}
unsigned int SFileComplex::newState() {
    d->states.emplace_back();
    return d->states.size() - 1;
}
void SFileComplex::setAnimated(unsigned int id, bool val) {
    if (id >= d->states.size())
        return;
    d->states[id].animated = val;
    d->states[id].dirty = true;
}
void SFileComplex::setEnabledSubObjs(unsigned int id, unsigned int val) {
    if (id >= d->states.size())
        return;
    d->states[id].enabled = val;
}
void SFileComplex::setCurrentDistanceLevel(unsigned int id, int level) {
    if (id >= d->states.size())
        return;
    auto &s = d->states[id];
    s.namesDirty = true;
    s.disabledSubs.clear();
    s.lod = level >= 0 && level < int(d->lods.size()) ? level : 0;
    s.dirty = true;
    s.packets.clear();
}
void SFileComplex::enableSubObjByNameQueue(unsigned int id, const QString &name, bool val) {
    if (id < d->states.size()) {
        d->states[id].names[name.toLower()] = val;
        d->states[id].namesDirty = true;
    }
}
void SFileComplex::enableSubObjByName(unsigned int id, const QString &name, bool val) {
    enableSubObjByNameQueue(id, name, val);
}
void SFileComplex::enablePart(unsigned int uid, unsigned int id) {
    if (id < d->states.size())
        d->states[id].disabledParts.remove(uid);
}
void SFileComplex::disablePart(unsigned int uid, unsigned int id) {
    if (id < d->states.size())
        d->states[id].disabledParts.insert(uid);
}
void SFileComplex::updateSim(float dt, unsigned int id) {
    if (id >= d->states.size() || !std::isfinite(dt))
        return;
    auto &s = d->states[id];
    if (s.animated && !d->animations.empty()) {
        auto &a = d->animations[0];
        s.frame = dt <= 0 ? 0 : (a.frames > 0 ? std::fmod(s.frame + dt * a.fps, a.frames) : 0);
        s.dirty = true;
    }
}
void SFileComplex::invalidateRenderState(bool) {
    for (auto &s : d->states)
        s.dirty = true;
}
void SFileComplex::updateMatrices(unsigned int id) {
    auto &s = d->states[id];
    if (!s.dirty)
        return;
    s.matrices.resize(d->matrices.size());
    const auto &lod = d->lods[s.lod];
    auto sample = [&](const std::vector<Data::Key> &keys) {
        auto next = std::upper_bound(keys.begin(), keys.end(), s.frame,
                                     [](float f, const Data::Key &k) { return f < k.frame; });
        int b = next == keys.end() ? int(keys.size()) - 1 : int(next - keys.begin());
        int a = std::max(0, b - 1);
        float span = keys[b].frame - keys[a].frame;
        return std::tuple<int, int, float>(
            a, b, span > 0 ? std::clamp((s.frame - keys[a].frame) / span, 0.0f, 1.0f) : 0);
    };
    for (int i : lod.matrixOrder) {
        QMatrix4x4 m = d->matrices[i].transform;
        if (s.animated && !d->animations.empty() && i < int(d->animations[0].channels.size())) {
            auto &c = d->animations[0].channels[i];
            if (!c.rotation.empty()) {
                auto [a, b, t] = sample(c.rotation);
                m.rotate(QQuaternion::slerp(c.rotation[a].rotation, c.rotation[b].rotation, t));
            }
            if (!c.position.empty()) {
                auto [a, b, t] = sample(c.position);
                auto p = c.position[a].position * (1 - t) + c.position[b].position * t;
                m(0, 3) = p.x();
                m(1, 3) = p.y();
                m(2, 3) = p.z();
            }
        }
        // Legacy MSTS convention: matrix 0 is the mirrored asset root.
        int parent = lod.parents[i];
        if (i == 0 || parent < 0) {
            s.matrices[i].setToIdentity();
            s.matrices[i].scale(-1, 1, 1);
            if (i != 0)
                s.matrices[i] *= m;
        } else
            s.matrices[i] = s.matrices[parent] * m;
    }
    s.dirty = false;
}
bool SFileComplex::getBoxPoints(QVector<float> &out) {
    if (!d->loaded)
        return false;
    auto add = [&](const float *b) {
        for (int axis = 0; axis < 3; ++axis)
            for (int a = 0; a < 2; ++a)
                for (int c = 0; c < 2; ++c) {
                    int x = (axis + 1) % 3, y = (axis + 2) % 3;
                    for (int end = 0; end < 2; ++end) {
                        float p[3];
                        p[axis] = b[axis * 2 + end];
                        p[x] = b[x * 2 + a];
                        p[y] = b[y * 2 + c];
                        out << -p[0] << p[1] << p[2];
                    }
                }
    };
    if (d->boxes.empty())
        add(d->bound);
    else
        for (auto &box : d->boxes) {
            float b[] = {box[0], box[3], box[1], box[4], box[2], box[5]};
            add(b);
        }
    return true;
}
void SFileComplex::getFloorBorderLinePoints(float *&out) {
    if (!d->loaded || !out)
        return;
    auto add = [&](const float *b) {
        QVector3D p[] = {{b[0], std::min(b[2], b[3]), b[4]},
                         {b[0], std::min(b[2], b[3]), b[5]},
                         {b[1], std::min(b[2], b[3]), b[5]},
                         {b[1], std::min(b[2], b[3]), b[4]}};
        for (int j = 0; j < 4; ++j) {
            auto v = p[(j + 1) % 4] - p[j];
            float length = v.length();
            if (length == 0)
                continue;
            v /= length;
            for (float x = 0; x < length + 2; x += 4) {
                auto q = p[j] + v * x;
                *out++ = q.x();
                *out++ = q.y();
                *out++ = q.z();
            }
        }
    };
    if (d->boxes.empty())
        add(d->bound);
    else
        for (auto &box : d->boxes) {
            float b[] = {box[0], box[3], box[1], box[4], box[2], box[5]};
            add(b);
        }
}
bool SFileComplex::isSnapable() const { return d->snap; }
void SFileComplex::addSnapablePoints(QVector<float> &out) {
    if (!d->loaded)
        return;
    const float x0 = d->boxes.empty() ? d->bound[0] : d->boxes[0][0],
                x1 = d->boxes.empty() ? d->bound[1] : d->boxes[0][3];
    const float z0 = d->boxes.empty() ? -d->bound[4] : d->boxes[0][2],
                z1 = d->boxes.empty() ? -d->bound[5] : d->boxes[0][5];
    if (std::abs(z1 - z0) > std::abs(x1 - x0) * 4)
        out << (x0 + x1) / 2 << 0 << z0 << (x0 + x1) / 2 << 0 << z1;
    else
        out << x0 << 0 << (z0 + z1) / 2 << x1 << 0 << (z0 + z1) / 2;
}
void SFileComplex::fillShapeHierarchyInfo(ShapeHierarchyInfo *info, unsigned int id) {
    if (!info || !d->loaded || d->health == Health::Broken || id >= d->states.size() ||
        d->lods.empty())
        return;
    info->hierarchy.clear();
    info->matrices.clear();
    info->parts.clear();
    auto &state = d->states[id];
    auto &l = d->lods[state.lod];
    for (int p : l.parents)
        info->hierarchy << p;
    for (auto &m : d->matrices)
        info->matrices << m.name;
    for (auto &m : l.meshes)
        for (auto &p : m.parts) {
            if (p.material < 0 || p.material >= int(d->materials.size()))
                continue;
            auto &mat = d->materials[p.material];
            ShapeHierarchyInfo::ShapePart part;
            part.uid = p.uid;
            part.matrixId = mat.matrix;
            part.polyCount = p.mode == 0x0004 ? p.count / 3 : 0;
            part.enabled = !state.disabledParts.contains(p.uid);
            if (mat.image >= 0)
                part.textureName = d->images[mat.image].name;
            info->parts << part;
        }
}
void SFileComplex::fillContentHierarchyInfo(QVector<ContentHierarchyInfo *> &out, int parent) {
    auto *n = new ContentHierarchyInfo;
    n->name = d->name;
    n->shape = this;
    n->type = "shape";
    n->parent = parent;
    int index = out.size();
    out << n;
    for (int i = 0; i < int(d->lods.size()); ++i) {
        auto *l = new ContentHierarchyInfo;
        l->name = QString("Distance Level: %1 m").arg(d->lods[i].distance);
        l->shape = this;
        l->type = "shape";
        l->parent = index;
        l->distanceLevelId = i;
        out << l;
    }
}
