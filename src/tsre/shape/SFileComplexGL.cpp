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
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>

void SFileComplex::releaseGL() {
    for (auto &s : d->states)
        s.packets.clear();
    clearSharedPackets();
    for (auto &l : d->lods)
        for (auto &m : l.meshes)
            m.gpu.reset();
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
            {QString::fromLatin1(ShapeLoadOption::Compact), d->options.compact}
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
            image.id = TexLib::addTex(d->texturePath, image.name);
        auto it = TexLib::mtex.find(image.id);
        image.address = -1;
        if (it == TexLib::mtex.end() || !it->second)
            continue;
        auto *tex = it->second;
        if (tex->loaded && !tex->glLoaded && !tex->missing && !tex->error)
            tex->GLTextures();
        if (tex->glLoaded)
            image.address = tex->tex[0];
    }
}
bool SFileComplex::prepare(unsigned int id) {
    if (id >= d->states.size() || !RenderContext::ready())
        return false;
    if (!d->loaded) {
        if (d->attempted)
            return false;
        if (Game::objectLoadingTokens < 1)
            return false;
        Game::objectLoadingTokens -= 2;
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
    return true;
}
void SFileComplex::pushRenderItem(RenderQueue &queue) { pushRenderItem(queue, 0, 0); }
void SFileComplex::pushRenderItem(RenderQueue &queue, quint32 selection, unsigned int id) {
    if (!prepare(id))
        return;
    auto &s = d->states[id];
    auto &lod = d->lods[s.lod];
    // Animated states own their packets; static ones share theirs by key.
    const bool animated = s.animated && !d->animations.empty();
    Data::SharedPackets *shared = nullptr;
    if (!animated) {
        shared = &d->sharedPackets[packetKey(id)];
        if (shared->frame == Renderer::frameNumber()) {
            for (RenderItem *item : shared->active)
                queue.submit(item, selection);
            return;
        }
        shared->frame = Renderer::frameNumber();
        shared->active.clear();
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
            item->mesh.count = p.count;
            item->mesh.primitive = RenderItem::primitiveFromGl(p.mode);
            item->material.wireframe = false;
            item->material.lit = mat.light >= -7;
            item->material.brightness = mat.light == -12 ? 0.5f : 1.0f;
            // Same classes as the alpha written into the vertices.
            item->material.surface = mat.alpha == 1 ? RenderItem::SURFACE_OPAQUE
                    : mat.alphaTest ? RenderItem::SURFACE_ALPHA_TEST
                                    : RenderItem::SURFACE_BLENDED;
            int addr = mat.image >= 0 ? d->images[mat.image].address : -1;
            if (addr >= 0 && !s.disabledParts.contains(p.uid) &&
                TexLib::disabledTextures.value(addr) != 1)
                item->enableTextures(addr);
            else
                item->disableTextures(1, 0, 1, 1);
            // Persistent packet: selection goes on the queued instance.
            item->setSelectionId(0);
            if (shared) {
                // The static pose is fixed, so the bounds hold.
                item->setBounds(p.boundCenter, p.boundRadius, item->msMatrix);
                shared->active.push_back(item);
            } else
                item->setBounds(nullptr, -1.0f);
            queue.submit(item, selection);
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
