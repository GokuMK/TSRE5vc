#include <tsre/renderer/RenderContext.h>
#include "SFileComplexData.h"
#include <QOpenGLFunctions>
#include <algorithm>
#include <cmath>
#include <limits>
#include <shapeViewer/ShapeTextureInfo.h>
#include <tsre/Game.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/shape/ShapeLoader.h>
#include <tsre/shape/NightTextures.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>

void SFileComplex::releaseGL() {
    for (auto &s : d->states)
        s.packets.clear();
    clearSharedPackets();
    for (auto &l : d->lods)
        for (auto &m : l.meshes) {
            m.gpu.reset();
            m.pbrGpu.reset();
        }
    d->gpuState = GpuState::NotInitialized;
}
bool SFileComplex::initGL() {
    if (!d->loaded || d->health == Health::Broken)
        return false;
    // Builds renderer-owned meshes; no GL context is needed.
    if (d->gpuState == GpuState::Ready)
        return true;
    if (d->retention == Retention::Compact && !d->sourceAvailable) {
        if (d->edited) {
            d->diagnostics << "Cannot rebuild Compact shape with unsaved edits";
            return false;
        }
        SFileComplex replacement(d->path, d->name, d->textureRoot);
        replacement.setLoadOptions({
            {QString::fromLatin1(ShapeLoadOption::FirstLodOnly), d->options.firstLodOnly},
            {QString::fromLatin1(ShapeLoadOption::Compact), d->options.compact},
            {QString::fromLatin1(ShapeLoadOption::DoubleSided), d->options.doubleSided}
        });
        if (!replacement.loadData() || replacement.health() == Health::Broken ||
            !replacement.initGL()) {
            d->gpuState = GpuState::Failed;
            d->diagnostics << "Compact buffer rebuild could not reload the source";
            d->diagnostics.append(replacement.diagnostics());
            return false; // Keep the loaded bounds and runtime state for inspection/retry.
        }
        replacement.compact();
        auto states = std::move(d->states);
        releaseGL();
        releaseTextures();
        d = std::move(replacement.d);
        d->states = std::move(states);
        for (auto &state : d->states) {
            state.packets.clear();
            state.matrices.clear();
            state.dirty = state.namesDirty = true;
            if (state.lod >= int(d->lods.size()))
                state.lod = 0;
        }
        return true;
    } else
        releaseGL();
    auto fail = [&](const QString &reason) {
        releaseGL();
        d->gpuState = GpuState::Failed;
        d->diagnostics << reason;
        return false;
    };
    for (auto &lod : d->lods)
        for (auto &mesh : lod.meshes) {
            size_t count = 0;
            for (auto &p : mesh.parts)
                count += p.indices.size();
            if (count > size_t(std::numeric_limits<int>::max() / 36))
                return fail("Mesh buffer exceeds GL allocation range");
            std::vector<float> packed;
            packed.reserve(count * 9);
            for (auto &part : mesh.parts) {
                part.offset = packed.size() / 9;
                auto &mat = d->materials[part.material];
                // Preserve source indices; only the renderer cache reverses winding.
                for (auto it = part.indices.rbegin(); it != part.indices.rend(); ++it) {
                    auto &v = mesh.vertices[*it];
                    auto p = d->points[v.point], n = d->normals[v.normal];
                    QVector2D uv = v.uv >= 0 ? d->uvs[v.uv] : QVector2D();
                    float alpha = mat.alpha == 1  ? 1
                                  : mat.alphaTest ? -0.51f
                                                  : -GLUU::get()->alphaTest;
                    packed.insert(packed.end(), {p.x(), p.y(), p.z(), n.x(), n.y(), n.z(), uv.x(),
                                                 uv.y(), alpha});
                }
                // Bounds sphere of the part's positions, for view culling.
                const size_t first = size_t(part.offset) * 9, last = packed.size();
                part.boundRadius = -1.0f;
                if (last > first) {
                    float low[3] = {packed[first], packed[first + 1], packed[first + 2]};
                    float high[3] = {low[0], low[1], low[2]};
                    for (size_t v = first; v < last; v += 9)
                        for (int c = 0; c < 3; ++c) {
                            low[c] = std::min(low[c], packed[v + c]);
                            high[c] = std::max(high[c], packed[v + c]);
                        }
                    float radius = 0.0f;
                    for (int c = 0; c < 3; ++c)
                        part.boundCenter[c] = 0.5f * (low[c] + high[c]);
                    for (size_t v = first; v < last; v += 9) {
                        float distance = 0.0f;
                        for (int c = 0; c < 3; ++c) {
                            const float delta = packed[v + c] - part.boundCenter[c];
                            distance += delta * delta;
                        }
                        radius = std::max(radius, distance);
                    }
                    part.boundRadius = std::sqrt(radius);
                }
            }
            auto gpu = std::make_unique<Data::Gpu>();
            gpu->bytes = packed.size() * sizeof(float);
            MeshData data;
            data.layout = RenderItem::VNTA;
            data.vertices = std::move(packed);
            gpu->mesh = Meshes::create(std::move(data));
            mesh.gpu = std::move(gpu);
            buildPbrBuffer(size_t(&lod - d->lods.data()), size_t(&mesh - lod.meshes.data()));
        }
    d->gpuState = GpuState::Ready;
    for (auto &s : d->states)
        s.dirty = true;
    if (d->options.compact)
        compact();
    return true;
}
void SFileComplex::syncTextures() {
    for (auto &image : d->images) {
        if (image.id == -1)
            image.id = TexLib::addTex(d->texturePath, image.name, false,
                                      image.data ? TexLib::Data : TexLib::Display);
        auto it = TexLib::mtex.find(image.id);
        image.address = -1;
        if (it == TexLib::mtex.end() || !it->second)
            continue;
        auto *tex = it->second;
        TexLib::reloadReleased(tex);
        if (tex->loaded && !tex->glLoaded && !tex->missing && !tex->error)
            tex->GLTextures(Texture::shapeMipmaps.load(std::memory_order_relaxed));
        if (tex->glLoaded) {
            image.address = tex->tex[0];
            image.alpha = tex->alphaClass.load();
        }
    }
}
bool SFileComplex::prepare(unsigned int id) {
    if (id >= d->states.size() || !RenderContext::ready())
        return false;
    if (!d->loaded) {
        if (d->attempted)
            return false;
        const ShapeLoader::Request request = ShapeLoader::request(this);
        if (request == ShapeLoader::Request::Wait)
            return false;
        if (request == ShapeLoader::Request::LoadHere)
            load();
    }
    if (!d->loaded || d->health == Health::Broken || !initGL() || d->lods.empty())
        return false;
    auto &s = d->states[id];
    auto &lod = d->lods[s.lod];
    if (s.namesDirty) {
        for (auto it = s.names.begin(); it != s.names.end(); ++it) {
            int matrix = -1;
            for (int i = 0; i < int(d->matrices.size()); ++i)
                if (d->matrices[i].name.compare(it.key(), Qt::CaseInsensitive) == 0) {
                    matrix = i;
                    break;
                }
            if (matrix < 0 || lod.meshes.empty() ||
                matrix >= int(lod.meshes[0].geometryMap.size()) ||
                lod.meshes[0].geometryMap[matrix] > 0)
                continue;
            for (int j = 1; j < int(lod.meshes.size()); ++j)
                if (matrix < int(lod.meshes[j].geometryMap.size()) &&
                    lod.meshes[j].geometryMap[matrix] == 0) {
                    if (it.value())
                        s.disabledSubs.remove(j);
                    else
                        s.disabledSubs.insert(j);
                }
        }
        s.namesDirty = false;
    }
    updateMatrices(id);
    syncTextures();
    // Night textures (task 35): held while the frame wants them, released by
    // day; the packets take them as they are filled.
    const bool night = (d->alternative & NightTextures::AlternativeFlag) != 0 && NightTextures::frame().wanted;
    if (night != d->nightHeld) {
        d->nightHeld = night;
        if (!night)
            for (auto &image : d->images)
                if (image.nightTex >= 0) {
                    TexLib::delRef(image.nightTex);
                    image.nightTex = -1;
                }
    }
    if (d->nightHeld)
        for (auto &image : d->images) {
            if (image.night == 0) {
                image.nightFile = NightTextures::find(d->texturePath, image.name);
                image.night = image.nightFile.isEmpty() ? -1 : 1;
            }
            if (image.night > 0 && image.nightTex < 0)
                image.nightTex = TexLib::addTex(image.nightFile);
        }
    return true;
}
void SFileComplex::pushRenderItem(RenderQueue &queue) { pushRenderItem(queue, 0, 0); }
void SFileComplex::pushRenderItem(RenderQueue &queue, quint32 selection, unsigned int id) {
    if (!prepare(id)) {
        // Still loading (not failed).
        if (!d->loaded && !d->attempted && RenderContext::ready())
            queue.noteShapeWaiting();
        return;
    }
    auto &s = d->states[id];
    auto &lod = d->lods[s.lod];
    // Animated or posed states own their packets; static ones share theirs
    // by key.
    const bool animated = (s.animated && !d->animations.empty()) || s.pose.posed;
    Data::SharedPackets *shared = nullptr;
    if (!animated) {
        shared = &d->sharedPackets[packetKey(id)];
        if (shared->frame == Renderer::frameNumber()) {
            for (size_t k = 0; k < shared->active.size(); ++k)
                queue.submit(shared->active[k], SelectionIdCodec::forShapePart(selection, shared->activeNumbers[k]));
            return;
        }
        shared->frame = Renderer::frameNumber();
        shared->active.clear();
        shared->activeNumbers.clear();
    }
    auto &packets = shared ? shared->packets : s.packets;
    const std::vector<QMatrix4x4> &matrices = shared ? staticMatrices(s.lod) : s.matrices;
    size_t count = 0;
    for (auto &m : lod.meshes)
        count += m.parts.size();
    while (packets.size() < count)
        packets.emplace_back(new RenderItem());
    size_t at = 0;
    for (auto &m : lod.meshes)
        for (auto &p : m.parts) {
            const int number = int(at);
            auto *item = packets[at++].get();
            if ((m.subobject < 32 && !(s.enabled & (quint32(1) << m.subobject))) ||
                s.disabledSubs.contains(m.subobject))
                continue;
            auto &mat = d->materials[p.material];
            item->shared = true;
            item->setVertexAttributes(RenderItem::VNTA);
            item->mesh.handle = m.gpu->mesh;
            item->msMatrix = const_cast<float *>(matrices[mat.matrix].constData());
            item->mesh.first = p.offset;
            const bool pbr = MstsMaterials::pbr(mat.pbr) && m.pbrGpu && p.pbrOffset >= 0;
            item->mesh.count = p.count;
            item->mesh.primitive = RenderItem::primitiveFromGl(p.mode);
            item->material.wireframe = false;
            item->material.lit = mat.light >= -7;
            item->material.brightness = mat.light == -12 ? 0.5f : 1.0f;
            // Cruciform light materials: trees, bushes, hedges (task renderer 37).
            item->material.vegetation = mat.light == -9 || mat.light == -10;
            // MSTS specular light materials (task shapes 13), as Open Rails
            // reads them: -6 Specular 25, -7 Specular 750.
            item->material.specularPower = mat.light == -6 ? 25.0f : mat.light == -7 ? 750.0f : 0.0f;
            // Same classes as the alpha written into the vertices.
            item->material.surface = mat.alpha == 1 ? RenderItem::SURFACE_OPAQUE
                    : mat.alphaTest ? RenderItem::SURFACE_ALPHA_TEST
                                    : RenderItem::SURFACE_BLENDED;
            item->material.doubleSided = d->options.doubleSided;
            int addr = mat.image >= 0 ? d->images[mat.image].address : -1;
            item->material.nightTextureId = -1;
            if (addr >= 0 && !s.disabledParts.contains(p.uid) &&
                TexLib::disabledTextures.value(addr) != 1) {
                item->enableTextures(addr);
                item->material.textureAlpha = d->images[mat.image].alpha;
                if (d->nightHeld)
                    item->material.nightTextureId = d->images[mat.image].nightTex;
            } else {
                item->disableTextures(1, 0, 1, 1);
            }
            // PBR materials (task shapes 13): their PBR buffer and shading.
            if (pbr) {
                item->setVertexAttributes(RenderItem::PBR);
                item->mesh.handle = m.pbrGpu->mesh;
                item->mesh.first = p.pbrOffset;
                auto imageId = [&](int image) { return image >= 0 ? d->images[image].id : -1; };
                MstsMaterials::fill(*item, mat.factors, imageId(mat.ormImage), imageId(mat.normalImage), mat.pbr,
                                    mat.alphaTest);
            } else if (item->pbr.enabled) {
                item->pbr = RenderItem::Pbr();
            }
            // Persistent packet: selection goes on the queued instance.
            item->setSelectionId(0);
            if (shared) {
                // The static pose is fixed, so the bounds hold.
                item->setBounds(p.boundCenter, p.boundRadius, item->msMatrix);
                shared->active.push_back(item);
                shared->activeNumbers.push_back(number);
            } else
                item->setBounds(nullptr, -1.0f);
            queue.submit(item, SelectionIdCodec::forShapePart(selection, number));
        }
}
// The mesh's PBR parts in the PBR layout (task shapes 13): position, normal,
// texture coordinates, alpha, tangent with handedness (from the texture
// coordinates of their triangles, per source vertex), the texture
// coordinates again and a white colour; in the VNTA buffer's order.
void SFileComplex::buildPbrBuffer(size_t lod, size_t index) {
    Data::Mesh &mesh = d->lods[lod].meshes[index];
    mesh.pbrGpu.reset();
    std::vector<int> triangles;
    size_t count = 0;
    for (auto &part : mesh.parts) {
        part.pbrOffset = -1;
        if (!MstsMaterials::pbr(d->materials[part.material].pbr))
            continue;
        if (part.mode == 0x0004)
            triangles.insert(triangles.end(), part.indices.begin(), part.indices.end());
        count += part.indices.size();
    }
    if (count == 0)
        return;
    auto source = [&](int v, float *position, float *normal, float *uv) {
        if (v < 0 || v >= int(mesh.vertices.size()))
            return false;
        const auto &vertex = mesh.vertices[v];
        if (vertex.point < 0 || vertex.point >= int(d->points.size()) || vertex.normal < 0
                || vertex.normal >= int(d->normals.size()))
            return false;
        const QVector3D p = d->points[vertex.point], n = d->normals[vertex.normal];
        const QVector2D t = vertex.uv >= 0 && vertex.uv < int(d->uvs.size()) ? d->uvs[vertex.uv] : QVector2D();
        position[0] = p.x(); position[1] = p.y(); position[2] = p.z();
        normal[0] = n.x(); normal[1] = n.y(); normal[2] = n.z();
        uv[0] = t.x(); uv[1] = t.y();
        return true;
    };
    std::vector<float> tangents;
    MstsMaterials::tangents(int(mesh.vertices.size()), source, triangles, tangents);
    const int stride = RenderItem::PBR;
    std::vector<float> packed(count * stride, 0.0f);
    size_t at = 0;
    for (auto &part : mesh.parts) {
        if (!MstsMaterials::pbr(d->materials[part.material].pbr))
            continue;
        part.pbrOffset = int(at);
        // Reversed, as the VNTA buffer is.
        for (auto it = part.indices.rbegin(); it != part.indices.rend(); ++it, ++at) {
            float *vertex = &packed[at * stride];
            if (!source(*it, vertex, vertex + 3, vertex + 6))
                continue;
            vertex[8] = 1.0f;
            std::copy_n(&tangents[size_t(*it) * 4], 4, vertex + 9);
            vertex[13] = vertex[6];
            vertex[14] = vertex[7];
            std::fill(vertex + 15, vertex + 19, 1.0f);
        }
    }
    auto gpu = std::make_unique<Data::Gpu>();
    gpu->bytes = int(packed.size() * sizeof(float));
    MeshData data;
    data.layout = RenderItem::PBR;
    data.vertices = std::move(packed);
    gpu->mesh = Meshes::create(std::move(data));
    mesh.pbrGpu = std::move(gpu);
}
int SFileComplex::drawnPrimitiveCount(unsigned int id) const {
    if (!d->loaded || id >= d->states.size() || d->lods.empty())
        return 0;
    int count = 0;
    for (const auto &m : d->lods[d->states[id].lod].meshes)
        count += int(m.parts.size());
    return count;
}
int SFileComplex::drawnPrimitiveMaterial(unsigned int id, int primitive) const {
    if (!d->loaded || id >= d->states.size() || d->lods.empty() || primitive < 0)
        return -1;
    for (const auto &m : d->lods[d->states[id].lod].meshes) {
        if (primitive < int(m.parts.size()))
            return m.parts[primitive].material;
        primitive -= int(m.parts.size());
    }
    return -1;
}
void SFileComplex::pushPrimitiveHighlight(RenderQueue &queue, unsigned int id, const QVector<int> &primitives,
                                          const float *color) {
    if (primitives.isEmpty() || d->gpuState != GpuState::Ready || id >= d->states.size() || d->lods.empty())
        return;
    auto &s = d->states[id];
    const bool animated = (s.animated && !d->animations.empty()) || s.pose.posed;
    const std::vector<QMatrix4x4> &matrices = animated ? s.matrices : staticMatrices(s.lod);
    int number = 0;
    for (auto &m : d->lods[s.lod].meshes)
        for (auto &p : m.parts) {
            const bool picked = primitives.contains(number++);
            if (!picked || !m.gpu || p.count <= 0)
                continue;
            const auto &mat = d->materials[p.material];
            if (mat.matrix < 0 || mat.matrix >= int(matrices.size()))
                continue;
            auto *item = new RenderItem();
            item->setVertexAttributes(RenderItem::VNTA);
            item->mesh.handle = m.gpu->mesh;
            item->mesh.first = p.offset;
            item->mesh.count = p.count;
            item->mesh.primitive = RenderItem::primitiveFromGl(p.mode);
            item->msMatrix = queue.frameMatrix(matrices[mat.matrix].constData());
            item->material.lit = false;
            item->disableTextures(color[0], color[1], color[2], 1.0f);
            item->material.opacity = color[3];
            item->material.surface = RenderItem::SURFACE_BLENDED;
            item->material.decal = true;
            item->material.doubleSided = true;
            queue.submitFrameItem(item);
        }
}
void SFileComplex::fillShapeTextureInfo(QHash<int, ShapeTextureInfo *> &out, unsigned int id) {
    if (!d->loaded || id >= d->states.size())
        return;
    for (auto &image : d->images) {
        auto *info = new ShapeTextureInfo;
        info->textureName = image.name;
        info->textureId = image.id;
        info->enabled = true;
        info->loaded = "NONE";
        auto it = TexLib::mtex.find(image.id);
        if (it != TexLib::mtex.end() && it->second) {
            auto *t = it->second;
            info->loaded = t->missing    ? "MISSING"
                           : t->error    ? "ERROR"
                           : t->glLoaded ? "YES"
                                         : "NONE";
            info->loading = !t->missing && !t->error && !t->glLoaded;
            info->resolution = QString("%1x%2").arg(t->width).arg(t->height);
        } else
            info->loading = image.id == -1;
        int key = image.id >= 0 ? image.id : -int(&image - d->images.data()) - 1;
        if (out.contains(key))
            delete out.take(key);
        out[key] = info;
    }
}
