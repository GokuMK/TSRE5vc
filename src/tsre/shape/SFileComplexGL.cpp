#include "SFileComplexData.h"
#include <QOpenGLFunctions>
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
    for (auto &l : d->lods)
        for (auto &m : l.meshes)
            m.gpu.reset();
    d->context.clear();
    d->gpuState = GpuState::NotInitialized;
}
bool SFileComplex::initGL() {
    if (!d->loaded || d->health == Health::Broken)
        return false;
    auto *context = QOpenGLContext::currentContext();
    if (!context)
        return false;
    if (d->gpuState == GpuState::Ready && d->context == context)
        return true;
    if (d->retention == Retention::Compact && !d->sourceAvailable) {
        if (d->edited) {
            d->diagnostics << "Cannot rebuild Compact shape with unsaved edits";
            return false;
        }
        SFileComplex replacement(d->path, d->name, d->textureRoot);
        replacement.setLoadOptions(d->options);
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
    auto *f = context->functions();
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
            }
            auto gpu = std::make_unique<Data::Gpu>();
            if (!gpu->vao.create() || !gpu->vbo.create())
                return fail("Cannot create shape VAO/VBO");
            QOpenGLVertexArrayObject::Binder binder(&gpu->vao);
            if (!gpu->vbo.bind())
                return fail("Cannot bind shape VBO");
            gpu->bytes = packed.size() * sizeof(float);
            gpu->vbo.allocate(packed.data(), gpu->bytes);
            if (gpu->vbo.size() != gpu->bytes)
                return fail("Shape buffer allocation failed");
            const int sizes[] = {3, 2, 3, 1}, offsets[] = {0, 6, 3, 8};
            for (int i = 0; i < 4; ++i) {
                f->glEnableVertexAttribArray(i);
                f->glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                                         reinterpret_cast<void *>(offsets[i] * sizeof(float)));
            }
            gpu->vbo.release();
            mesh.gpu = std::move(gpu);
        }
    d->context = context;
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
    if (id >= d->states.size() || !QOpenGLContext::currentContext())
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
void SFileComplex::render() { render(0, 0); }
void SFileComplex::render(quint32 selection, unsigned int id) {
    if (!prepare(id))
        return;
    auto &s = d->states[id];
    auto *gluu = GLUU::get();
    auto *f = QOpenGLContext::currentContext()->functions();
    if (!gluu->currentShader)
        return;
    gluu->setSelectionId(selection);
    for (auto &m : d->lods[s.lod].meshes) {
        if ((m.subobject < 32 && !(s.enabled & (quint32(1) << m.subobject))) ||
            s.disabledSubs.contains(m.subobject))
            continue;
        QOpenGLVertexArrayObject::Binder binder(&m.gpu->vao);
        for (auto &p : m.parts) {
            auto &mat = d->materials[p.material];
            const auto &matrix = s.matrices[mat.matrix];
            gluu->currentMsMatrinxHash = 0;
            gluu->currentShader->setUniformValue(
                gluu->currentShader->msMatrixUniform,
                *reinterpret_cast<const float (*)[4][4]>(matrix.constData()));
            if (mat.light < -7)
                gluu->disableNormals();
            else
                gluu->enableNormals();
            gluu->setBrightness(mat.light == -12 ? 0.5f : 1.0f);
            int addr = mat.image >= 0 ? d->images[mat.image].address : -1;
            if (selection == 0) {
                if (addr >= 0 && !s.disabledParts.contains(p.uid) &&
                    TexLib::disabledTextures.value(addr) != 1)
                    gluu->bindTexture(f, addr);
                else
                    gluu->disableTextures(1, 0, 1, 1);
            }
            f->glDrawArrays(p.mode, p.offset, p.count);
            if (selection == 0)
                gluu->enableTextures();
        }
    }
    gluu->setBrightness(1);
}
void SFileComplex::pushRenderItem() { pushRenderItem(0, 0); }
void SFileComplex::pushRenderItem(quint32 selection, unsigned int id) {
    if (!Game::currentRenderer || !prepare(id))
        return;
    auto &s = d->states[id];
    auto &lod = d->lods[s.lod];
    size_t count = 0;
    for (auto &m : lod.meshes)
        count += m.parts.size();
    while (s.packets.size() < count)
        s.packets.push_back(std::make_unique<RenderItem>());
    size_t at = 0;
    for (auto &m : lod.meshes)
        for (auto &p : m.parts) {
            auto *item = s.packets[at++].get();
            if ((m.subobject < 32 && !(s.enabled & (quint32(1) << m.subobject))) ||
                s.disabledSubs.contains(m.subobject))
                continue;
            auto &mat = d->materials[p.material];
            item->shared = true;
            item->setVertexAttributes(RenderItem::VNTA);
            item->VBO = &m.gpu->vbo;
            item->VAO = &m.gpu->vao;
            item->msMatrix = s.matrices[mat.matrix].data();
            item->vertOffset = p.offset;
            item->vertCount = p.count;
            item->itemType = p.mode == GL_POINTS ? RenderItem::Points : p.mode;
            item->polygonMode = 0;
            item->normalsEnabled = mat.light >= -7;
            item->brightness = mat.light == -12 ? 0.5f : 1.0f;
            int addr = mat.image >= 0 ? d->images[mat.image].address : -1;
            if (addr >= 0 && !s.disabledParts.contains(p.uid) &&
                TexLib::disabledTextures.value(addr) != 1)
                item->enableTextures(addr);
            else
                item->disableTextures(1, 0, 1, 1);
            item->setSelectionId(selection);
            Game::currentRenderer->pushItem(item, Game::currentRenderer->mvMatrix);
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
