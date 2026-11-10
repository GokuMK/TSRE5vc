/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/E3dShape.h>
#include <tsre/ErrorMessage.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/maszyna/E3dFile.h>
#include <tsre/maszyna/MaszynaMaterial.h>
#include <tsre/maszyna/MaszynaPaths.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/renderer/Mesh.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/shape/MstsMaterials.h>
#include <tsre/shape/ShapeLoader.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>
#include <tsre/texture/TextureAlpha.h>
#include <shapeViewer/ContentHierarchyInfo.h>
#include <shapeViewer/ShapeHierarchyInfo.h>
#include <shapeViewer/ShapeTextureInfo.h>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr unsigned GlClampToEdge = 0x812F;
constexpr float PlainGrey = 0.5f;

// The renderer's copy of a model's geometry, freed with the model.
struct DrawnMesh {
    MeshHandle mesh;
    ~DrawnMesh() { Meshes::release(mesh); }
};

bool turnsInsideOut(const float *m) {
    const float det = m[0] * (m[5] * m[10] - m[6] * m[9])
            - m[4] * (m[1] * m[10] - m[2] * m[9])
            + m[8] * (m[1] * m[6] - m[2] * m[5]);
    return det < 0.0f;
}

void retire(QVector<RenderItem *> &items) {
    for (RenderItem *item : items)
        if (item != nullptr)
            Renderer::retirePacket(item);
    items.clear();
}

// A texture's alpha, Unknown until it has been read; one that will never
// load has nothing to wait for.
unsigned char alphaOf(int textureId) {
    const auto found = TexLib::mtex.find(textureId);
    if (found == TexLib::mtex.end() || found->second == nullptr)
        return TextureAlpha::Opaque;
    const Texture *texture = found->second;
    if (texture->missing || texture->error)
        return TextureAlpha::Opaque;
    return texture->loaded || texture->released ? texture->alphaClass.load() : TextureAlpha::Unknown;
}

}

E3dShape::E3dShape(QString pathid, QString name, QString texPath) {
    pathid.replace(QLatin1Char('\\'), QLatin1Char('/'));
    texPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    this->pathid = ContentPath::normalize(pathid);
    this->name = name;
    this->texPath = ContentPath::normalize(texPath);
    state.emplace_back();
}

E3dShape::~E3dShape() {
    ShapeLoader::cancel(this);
    retirePackets();
    for (State &s : state)
        resetState(s);
    for (Material &material : materials)
        releaseTextures(material);
    for (Repaint &repaint : repaints)
        for (Material &slot : repaint.slot)
            releaseTextures(slot);
}

float E3dShape::getSize() const {
    return model != nullptr ? model->radius * 2.0f : 0.0f;
}

const float *E3dShape::getBound() const {
    static const float none[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    return model != nullptr ? model->bound : none;
}

bool E3dShape::getBoxPoints(QVector<float> &points) {
    if (!isLoaded())
        return false;
    const float *bound = model->bound;
    for (int i = 0; i < 2; ++i)
        for (int j = 4; j < 6; ++j)
            points << bound[i] << bound[2] << bound[j] << bound[i] << bound[3] << bound[j];
    for (int i = 0; i < 2; ++i)
        for (int j = 2; j < 4; ++j)
            points << bound[i] << bound[j] << bound[4] << bound[i] << bound[j] << bound[5];
    for (int i = 4; i < 6; ++i)
        for (int j = 2; j < 4; ++j)
            points << bound[0] << bound[j] << bound[i] << bound[1] << bound[j] << bound[i];
    return true;
}

// The file and its materials: everything that does not need the main thread.
bool E3dShape::read() {
    QString error;
    model = E3dFile::read(pathid, &error, freshRead);
    if (model == nullptr) {
        failure = error;
        return false;
    }
    vehicleFolder = MaszynaPaths::vehicleFolder(pathid, texPath);
    materials.resize(model->materialNames.size());
    for (int i = 1; i < materials.size(); ++i) {
        materials[i].name = model->materialNames[i];
        materials[i].source = MaszynaMaterial::find(materials[i].name, vehicleFolder);
    }
    return true;
}

void E3dShape::registerTextures(Material &material) {
    if (material.registered || material.source == nullptr)
        return;
    material.registered = true;
    if (!material.source->base.isEmpty())
        material.texture = TexLib::addTex(material.source->base);
    if (!material.source->normal.isEmpty())
        material.normal = TexLib::addTex(material.source->normal, false, TexLib::Data);
    if (!material.source->orm.isEmpty())
        material.orm = TexLib::addTex(material.source->orm, false, TexLib::Data);
    else if (!material.source->specGlossMap.isEmpty())
        // MaSzyna's specgloss map in the metallic-roughness slot (task
        // maszyna 04), read by its own channels (addPacket).
        material.orm = TexLib::addTex(material.source->specGlossMap, false, TexLib::Data);
    if (!material.source->mask.isEmpty())
        material.mask = TexLib::addTex(material.source->mask, false, TexLib::Data);
}

void E3dShape::releaseTextures(Material &material) {
    for (int *id : {&material.texture, &material.normal, &material.orm, &material.mask})
        if (*id >= 0) {
            TexLib::delRef(*id);
            *id = -1;
        }
    material.registered = false;
}

void E3dShape::report(const Material &material) {
    if (material.source == nullptr || (material.source->found && (material.source->baseName.isEmpty() || !material.source->base.isEmpty())))
        return;
    const QByteArray topic = "material " + material.name.toUtf8();
    if (!firstReport(pathid, topic.constData()))
        return;
    ErrorMessagesLib::PushErrorMessage(new ErrorMessage(ErrorMessage::Type_Warning, ErrorMessage::Source_World,
            //% "Model %1: its material or texture %2 was not found (tried from %3 and the shared folders)."
            qtTrId("maszyna.e3d.material.missing").arg(pathid.section('/', -1), material.name, vehicleFolder), pathid));
}

// What needs the main thread: the mesh, once per model, and the textures.
void E3dShape::finishLoad() {
    const std::vector<E3dModel::Part> &parts = model->parts;
    restWorld.resize(parts.size() * 16);
    restShowing.resize(parts.size());
    for (size_t i = 0; i < parts.size(); ++i) {
        std::copy(parts[i].world, parts[i].world + 16, restWorld.data() + i * 16);
        restShowing[i] = !parts[i].startsHidden && (parts[i].parent < 0 || restShowing[size_t(parts[i].parent)]);
    }
    if (model->drawn == nullptr && !model->vertices.empty()) {
        MeshData data;
        data.layout = RenderItem::PBR;
        data.vertices = std::move(model->vertices);
        data.indices = std::move(model->indices);
        data.indexType = model->indexBytes == 4 ? RenderItem::INDEX_U32 : RenderItem::INDEX_U16;
        auto drawn = std::make_shared<DrawnMesh>();
        drawn->mesh = Meshes::create(std::move(data));
        model->drawn = drawn;
    }
    if (model->drawn != nullptr)
        mesh = static_cast<const DrawnMesh *>(model->drawn.get())->mesh;
    for (Material &material : materials) {
        registerTextures(material);
        report(material);
    }
}

void E3dShape::load() {
    if (!read()) {
        adopt(*this);
        return;
    }
    finishLoad();
    loaded = 1;
}

ComplexShape *E3dShape::detachedCopy() const {
    auto *copy = new E3dShape(pathid, name, texPath);
    copy->freshRead = freshRead;
    return copy;
}

void E3dShape::loadDetached() {
    loaded = read() ? 1 : 2;
}

void E3dShape::adopt(ComplexShape &copy) {
    auto *o = dynamic_cast<E3dShape *>(&copy);
    if (o == nullptr)
        return;
    if (o != this && (loaded != 0))
        return;
    if (o->model == nullptr) {
        loaded = 2;
        if (firstReport(pathid, "e3d load"))
            ErrorMessagesLib::PushErrorMessage(new ErrorMessage(ErrorMessage::Type_Error, ErrorMessage::Source_World,
                    //% "Model %1 could not be read: %2"
                    qtTrId("maszyna.e3d.load.failed").arg(pathid.section('/', -1), o->failure), pathid));
        return;
    }
    model = o->model;
    vehicleFolder = o->vehicleFolder;
    materials = o->materials;
    finishLoad();
    freshRead = false;
    loaded = 1;
}

void E3dShape::retirePool(Pool &pool) {
    retire(pool.items);
}

void E3dShape::retirePackets() {
    for (PacketSet &set : sets)
        retire(set.owned);
    sets.clear();
    for (State &s : state)
        for (Pool &pool : s.pools)
            retirePool(pool);
}

void E3dShape::resetState(State &s) {
    for (Pool &pool : s.pools)
        retirePool(pool);
    s = State();
}

void E3dShape::reload() {
    ShapeLoader::cancel(this);
    retirePackets();
    for (State &s : state) {
        const bool free = s.free;
        const int repaint = s.repaint;
        const QHash<QString, float> lights = s.lights;
        resetState(s);
        s.free = free;
        s.repaint = repaint;
        s.lights = lights;
        s.lightsStale = true;
    }
    for (Material &material : materials)
        releaseTextures(material);
    for (Repaint &repaint : repaints) {
        for (Material &slot : repaint.slot)
            releaseTextures(slot);
        repaint.made = false;
    }
    materials.clear();
    model.reset();
    mesh = MeshHandle();
    MaszynaMaterial::forget();
    freshRead = true;
    loaded = 0;
    forgetReports(pathid);
}

unsigned int E3dShape::newState() {
    for (size_t i = 1; i < state.size(); ++i)
        if (state[i].free) {
            state[i].free = false;
            return unsigned(i);
        }
    state.emplace_back();
    return unsigned(state.size() - 1);
}

void E3dShape::releaseState(unsigned int stateId) {
    if (stateId == 0 || stateId >= unsigned(state.size()))
        return;
    resetState(state[stateId]);
    state[stateId].free = true;
}

void E3dShape::setAnimated(unsigned int, bool) {
}

void E3dShape::updateSim(float, unsigned int) {
}

void E3dShape::setCurrentDistanceLevel(unsigned int stateId, int level) {
    if (stateId < unsigned(state.size()))
        state[stateId].level = level;
}

int E3dShape::distanceLevelCount() const {
    return model != nullptr ? int(model->levelLimits.size()) : 0;
}

// ShapeLod adds the shape's radius when it chooses; MaSzyna's limits are from
// its origin.
float E3dShape::distanceLevelDistance(int level) const {
    if (level < 0 || level >= distanceLevelCount())
        return 0.0f;
    return model->levelLimits[size_t(level)] - model->radius;
}

int E3dShape::levelOf(const State &s) const {
    return std::clamp(s.level, 0, std::max(distanceLevelCount() - 1, 0));
}

void E3dShape::setShown(State &s, int part, bool on) {
    const std::vector<E3dModel::Part> &parts = model->parts;
    if (s.shown.empty()) {
        s.shown.resize(parts.size());
        for (size_t i = 0; i < parts.size(); ++i)
            s.shown[i] = !parts[i].startsHidden;
        s.showing.resize(parts.size());
    }
    s.shown[size_t(part)] = on;
    for (size_t i = 0; i < parts.size(); ++i)
        s.showing[i] = s.shown[i] && (parts[i].parent < 0 || s.showing[size_t(parts[i].parent)]);
}

void E3dShape::enableSubObjByName(unsigned int stateId, const QString &name, bool val) {
    if (!isLoaded() || stateId >= unsigned(state.size()))
        return;
    const auto found = model->byName.constFind(name.toLower());
    if (found != model->byName.constEnd())
        setShown(state[stateId], found.value(), val);
}

void E3dShape::enableSubObjByNameQueue(unsigned int stateId, const QString &name, bool val) {
    enableSubObjByName(stateId, name, val);
}

void E3dShape::enablePart(unsigned int uid, unsigned int stateId) {
    if (isLoaded() && stateId < unsigned(state.size()) && uid < unsigned(model->parts.size()))
        setShown(state[stateId], int(uid), true);
}

void E3dShape::disablePart(unsigned int uid, unsigned int stateId) {
    if (isLoaded() && stateId < unsigned(state.size()) && uid < unsigned(model->parts.size()))
        setShown(state[stateId], int(uid), false);
}

void E3dShape::setRepaint(unsigned int stateId, const QString &repaint) {
    if (stateId >= unsigned(state.size()))
        return;
    if (repaint.isEmpty()) {
        state[stateId].repaint = -1;
        return;
    }
    const QString key = repaint.toLower();
    auto found = repaintByName.constFind(key);
    if (found == repaintByName.constEnd()) {
        Repaint made;
        made.name = key;
        repaints.push_back(made);
        found = repaintByName.insert(key, repaints.size() - 1);
    }
    state[stateId].repaint = found.value();
}

void E3dShape::setClassLight(unsigned int stateId, const QString &partClass, float level) {
    if (stateId >= unsigned(state.size()))
        return;
    State &s = state[stateId];
    const QString key = partClass.toLower();
    level = level > 0.0f ? std::min(level, 1.0f) : 0.0f;
    if (s.lights.value(key, 0.0f) == level)
        return;
    if (level > 0.0f)
        s.lights.insert(key, level);
    else
        s.lights.remove(key);
    s.lightsStale = true;
}

// The lights by name as the model's sections, once something changed.
void E3dShape::matchLights(State &s) const {
    if (!s.lightsStale)
        return;
    s.lightsStale = false;
    s.classLight.assign(size_t(model->sections.size()), 0.0f);
    s.litClasses = 0;
    for (auto light = s.lights.cbegin(); light != s.lights.cend(); ++light) {
        const qsizetype section = model->sections.indexOf(light.key());
        if (section >= 0) {
            s.classLight[size_t(section)] = light.value();
            ++s.litClasses;
        }
    }
}

// A repaint's slots, as material_data::assign fills them: a|b|c by position,
// else <skin>,1 to <skin>,4 while they exist, else <skin> in slot 1.
QStringList E3dShape::repaintMaterials(const QString &repaint) const {
    QStringList names;
    if (repaint.contains(QLatin1Char('|')))
        return repaint.split(QLatin1Char('|'));
    for (int slot = 1; slot <= SlotCount; ++slot) {
        const QString name = repaint + QLatin1Char(',') + QString::number(slot);
        if (!MaszynaMaterial::exists(name, vehicleFolder))
            break;
        names << name;
    }
    if (names.isEmpty())
        names << repaint;
    return names;
}

void E3dShape::makeRepaint(Repaint &repaint) {
    repaint.made = true;
    const QStringList names = repaintMaterials(repaint.name);
    for (int slot = 0; slot < SlotCount && slot < names.size(); ++slot) {
        Material &material = repaint.slot[slot];
        material.name = names[slot];
        material.source = MaszynaMaterial::find(names[slot], vehicleFolder);
        registerTextures(material);
        report(material);
    }
}

int E3dShape::partCount() const {
    return model != nullptr ? int(model->parts.size()) : 0;
}

QString E3dShape::partName(int part) const {
    if (part < 0 || part >= partCount())
        return QString();
    const int index = model->parts[size_t(part)].name;
    return index >= 0 ? model->names[index] : QString();
}

int E3dShape::partParent(int part) const {
    return part >= 0 && part < partCount() ? model->parts[size_t(part)].parent : -1;
}

bool E3dShape::partRest(int part, float *out) const {
    if (part < 0 || part >= partCount())
        return false;
    std::copy(model->parts[size_t(part)].rest, model->parts[size_t(part)].rest + 16, out);
    return true;
}

void E3dShape::setPartTransform(unsigned int stateId, int part, const float *local) {
    if (stateId >= unsigned(state.size()) || part < 0 || part >= partCount())
        return;
    State &s = state[stateId];
    const std::vector<E3dModel::Part> &parts = model->parts;
    if (local == nullptr) {
        if (s.posed.empty() || !s.posed[size_t(part)])
            return;
        s.posed[size_t(part)] = 0;
        --s.posedParts;
    } else {
        if (s.posed.empty()) {
            s.posed.assign(parts.size(), 0);
            s.locals.resize(parts.size() * 16);
        }
        float *kept = s.locals.data() + size_t(part) * 16;
        if (s.posed[size_t(part)]) {
            if (std::memcmp(kept, local, 16 * sizeof(float)) == 0)
                return;
            std::copy(local, local + 16, kept);
            s.worldStale = true;
            return;
        }
        std::copy(local, local + 16, kept);
        s.posed[size_t(part)] = 1;
        ++s.posedParts;
    }
    s.moved.assign(parts.size(), 0);
    for (size_t i = 0; i < parts.size(); ++i)
        s.moved[i] = s.posed[i] || (parts[i].parent >= 0 && s.moved[size_t(parts[i].parent)]);
    s.worldStale = true;
}

void E3dShape::clearPose(unsigned int stateId) {
    if (stateId >= unsigned(state.size()))
        return;
    State &s = state[stateId];
    for (Pool &pool : s.pools)
        retirePool(pool);
    s.pools.clear();
    s.posed.clear();
    s.locals.clear();
    s.moved.clear();
    s.world.clear();
    s.posedParts = 0;
    s.worldStale = false;
}

// One forward pass over the parts, only when something changed.
void E3dShape::updateWorld(State &s) const {
    if (!s.worldStale)
        return;
    s.worldStale = false;
    const std::vector<E3dModel::Part> &parts = model->parts;
    s.world.resize(parts.size() * 16);
    for (size_t i = 0; i < parts.size(); ++i) {
        float *world = s.world.data() + i * 16;
        const E3dModel::Part &part = parts[i];
        const float *local = s.posed[i] ? s.locals.data() + i * 16 : part.rest;
        if (part.parent >= 0)
            Mat4::multiply(world, s.world.data() + size_t(part.parent) * 16, const_cast<float *>(local));
        else
            std::copy(local, local + 16, world);
    }
}

bool E3dShape::partMatrix(unsigned int stateId, int part, float *out) const {
    if (!isLoaded() || stateId >= unsigned(state.size()) || part < 0 || part >= partCount())
        return false;
    State &s = const_cast<State &>(state[stateId]);
    if (s.posedParts > 0) {
        updateWorld(s);
        if (!s.world.empty()) {
            std::copy(s.world.data() + size_t(part) * 16, s.world.data() + size_t(part) * 16 + 16, out);
            return true;
        }
    }
    std::copy(restWorld.data() + size_t(part) * 16, restWorld.data() + size_t(part) * 16 + 16, out);
    return true;
}

bool E3dShape::matrixByName(const QString &name, float *out) const {
    if (!isLoaded())
        return false;
    const auto found = model->byName.constFind(name.toLower());
    if (found == model->byName.constEnd())
        return false;
    std::copy(restWorld.data() + size_t(found.value()) * 16, restWorld.data() + size_t(found.value()) * 16 + 16, out);
    return true;
}

QStringList E3dShape::imageNames() const {
    QStringList names;
    for (const Material &material : materials)
        if (material.source != nullptr && !material.source->base.isEmpty())
            names << QFileInfo(material.source->base).fileName();
    return names;
}

bool E3dShape::usedFiles(const QString &repaint, QStringList &found, QStringList &missing) const {
    if (!isLoaded())
        return true;
    QVector<std::shared_ptr<const MaszynaMaterial>> sources;
    QStringList names;
    for (const Material &material : materials)
        if (material.source != nullptr) {
            sources.push_back(material.source);
            names.push_back(material.name);
        }
    if (!repaint.isEmpty())
        for (const QString &name : repaintMaterials(repaint.toLower())) {
            sources.push_back(MaszynaMaterial::find(name, vehicleFolder));
            names.push_back(name);
        }
    const auto add = [&](const QString &written, const QString &path) {
        if (!path.isEmpty())
            found << path;
        else if (!written.isEmpty())
            missing << MaszynaPaths::expectedPlace(MaszynaPaths::clean(written).text, vehicleFolder) + QLatin1String(".dds");
    };
    for (int i = 0; i < sources.size(); ++i) {
        const MaszynaMaterial &source = *sources[i];
        if (!source.file.isEmpty())
            found << source.file;
        if (source.baseName.isEmpty() && !source.found)
            add(names[i], QString());
        add(source.baseName, source.base);
        add(source.normalName, source.normal);
        add(source.ormName, source.orm);
    }
    return true;
}

const E3dShape::Material *E3dShape::materialOf(int partMaterial, int repaint) const {
    if (partMaterial > 0)
        return partMaterial < materials.size() ? &materials[partMaterial] : nullptr;
    if (partMaterial < 0 && partMaterial >= -SlotCount && repaint >= 0) {
        const Material &slot = repaints[repaint].slot[-partMaterial - 1];
        return slot.source != nullptr ? &slot : nullptr;
    }
    return nullptr;
}

void E3dShape::addPacket(PacketSet &set, int part, const Material *material, int pass, bool alphaTest) {
    const E3dModel::Part &p = model->parts[size_t(part)];
    auto *item = new RenderItem();
    item->shared = true;
    item->mesh.handle = mesh;
    item->mesh.layout = RenderItem::PBR;
    item->mesh.primitive = RenderItem::PRIMITIVE_TRIANGLES;
    if (p.indexCount > 0) {
        item->mesh.indexed = true;
        item->mesh.indexType = model->indexBytes == 4 ? RenderItem::INDEX_U32 : RenderItem::INDEX_U16;
        item->mesh.indexOffset = p.firstIndex * unsigned(model->indexBytes);
        item->mesh.baseVertex = int(p.firstVertex);
        item->mesh.count = p.indexCount;
    } else {
        item->mesh.first = p.firstVertex;
        item->mesh.count = p.vertexCount;
    }

    const MaszynaMaterial *source = material != nullptr ? material->source.get() : nullptr;
    MstsMaterials::Factors factors;
    if (source != nullptr) {
        factors.metalness = source->metalness;
        factors.roughness = !std::isnan(source->roughness) ? source->roughness : source->presetRoughness();
    }
    const bool hasTexture = material != nullptr && material->texture >= 0;
    const bool blended = pass == E3dModel::Blended;
    if (material == nullptr) {
        // A colour part, or a skin slot with no repaint: plain grey.
        if (p.material == 0)
            std::copy(p.diffuse, p.diffuse + 4, factors.tint);
        else
            std::fill(factors.tint, factors.tint + 3, PlainGrey);
    }
    MstsMaterials::fill(*item, factors, material != nullptr ? material->orm : -1,
                        material != nullptr ? material->normal : -1,
                        blended ? MstsMaterials::Shader::PbrBlended : MstsMaterials::Shader::Pbr, alphaTest);
    if (alphaTest)
        item->pbr.alphaCutoff = source != nullptr && source->hasOpacity ? source->opacity : 0.5f;
    // Its MaSzyna shader family's reflection (task maszyna 04): none for its
    // default and colour materials, a map's alpha times it for reflective
    // ones.
    item->pbr.specular = source != nullptr ? source->reflectionStrength() : 0.0f;
    if (material != nullptr && material->mask >= 0 && item->pbr.specular > 0.0f)
        item->pbr.textures[RenderItem::Pbr::MAP_SPECULAR] = material->mask;
    // A specgloss map in the ORM's place (its own tsre_orm none): R the
    // highlights' strength, G gloss, B metal; no occlusion, no clearcoat mask
    // (RenderItem::Pbr::specGlossInMetallicRoughness).
    if (material != nullptr && material->orm >= 0 && source != nullptr && source->orm.isEmpty()
            && !source->specGlossMap.isEmpty()) {
        item->pbr.specGlossInMetallicRoughness = true;
        item->pbr.textures[RenderItem::Pbr::MAP_OCCLUSION] = -1;
        item->pbr.clearcoatInMetallicRoughnessAlpha = false;
    }
    if (blended && p.name >= 0 && model->names[p.name].compare(QLatin1String("cien"), Qt::CaseInsensitive) == 0)
        item->pbr.glass = false;
    if (source != nullptr && (source->clampS || source->clampT))
        for (int map : {int(RenderItem::Pbr::MAP_BASE_COLOR), int(RenderItem::Pbr::MAP_NORMAL)}) {
            item->pbr.wrap[map][0] = source->clampS ? GlClampToEdge : 0;
            item->pbr.wrap[map][1] = source->clampT ? GlClampToEdge : 0;
        }

    // Its glow, by MaSzyna's light threshold (selfillum; task maszyna 01),
    // and how it meets the exposure (Pbr::emissiveExposure, task maszyna
    // 04): a lamp (1 or more: MaSzyna's always lit, _on parts shown by
    // state) as painted, read as on by day; a dusk part (between 0 and 1:
    // MaSzyna lights it below that light level) as a night texture, its
    // light following the exposure; a lit class's part its class's light
    // (MaSzyna sets a section's each frame, 0 with the lights off), drawn by
    // the state's copies, never the shared packets, following the exposure
    // as a lit interior does. Its texture glows, or its colour without one.
    const float illumination = source != nullptr && !std::isnan(source->selfIllum) ? source->selfIllum : p.lightThreshold;
    const bool glows = illumination >= 1.0f;
    const bool dusk = illumination > 0.0f && illumination < 1.0f;
    if ((glows || dusk) && p.section < 0) {
        if (hasTexture) {
            std::fill(item->pbr.emissive, item->pbr.emissive + 3, 1.0f);
            item->pbr.textures[RenderItem::Pbr::MAP_EMISSIVE] = material->texture;
        } else {
            std::copy(item->pbr.baseColor, item->pbr.baseColor + 3, item->pbr.emissive);
        }
        item->pbr.emissiveExposure = glows ? 0.0f : 1.0f;
    }
    if (glows && p.section >= 0)
        item->pbr.emissiveExposure = 1.0f;

    if (hasTexture)
        item->enableTextureId(material->texture);
    else if (material != nullptr && source != nullptr && !source->baseName.isEmpty())
        item->disableTextures(1.0f, 0.0f, 1.0f, 1.0f);
    else
        item->disableTextures(1.0f, 1.0f, 1.0f, 1.0f);

    float *world = restWorld.data() + size_t(part) * 16;
    item->msMatrix = world;
    item->material.mirrored = turnsInsideOut(world);
    item->setBounds(p.boundCenter, p.boundRadius, world);
    item->setSelectionId(0);

    set.owned.push_back(item);
    set.all.push_back(item);
    set.parts.push_back(part);
    set.glows.push_back(glows && p.section >= 0);
    if (restShowing[size_t(part)])
        set.atRest.push_back(item);
}

void E3dShape::startSet(PacketSet &set) {
    retire(set.owned);
    set.all.clear();
    set.parts.clear();
    set.glows.clear();
    set.atRest.clear();
    set.waiting.clear();
    set.generation = ++generations;
    set.built = true;
}

void E3dShape::addPart(PacketSet &set, int part, int repaint) {
    const E3dModel::Part &p = model->parts[size_t(part)];
    const Material *material = materialOf(p.material, repaint);
    const MaszynaMaterial *source = material != nullptr ? material->source.get() : nullptr;
    const unsigned char alpha = material != nullptr && material->texture >= 0 ? alphaOf(material->texture)
                                                                              : (unsigned char)TextureAlpha::Opaque;
    if (alpha == TextureAlpha::Unknown && !set.waiting.contains(material->texture))
        set.waiting.push_back(material->texture);

    unsigned char passes = p.passes & (E3dModel::Opaque | E3dModel::Blended);
    if ((p.passes & E3dModel::BySlot) != 0) {
        // As MaSzyna's alpha masks: drawn when a slot it names has a texture
        // whose file has an alpha channel, else not at all. Its pixels only
        // say whether anything is left for the blended pass.
        bool channel = false;
        for (int slot = 0; slot < SlotCount && repaint >= 0; ++slot) {
            const Material &named = repaints[repaint].slot[slot];
            channel = channel || ((p.slotMask >> slot) & 1 && named.source != nullptr && named.source->alphaChannel);
        }
        if (channel) {
            passes |= E3dModel::Opaque;
            if (alpha != TextureAlpha::Opaque)
                passes |= E3dModel::Blended;
        }
    } else if (passes == 0) {
        // No pass in the file: only a part with a material of its own gets
        // one, from its opacity or else the texture's alpha channel.
        if (p.material <= 0)
            return;
        const bool cut = source != nullptr && (source->hasOpacity ? source->opacity == 0.0f : source->alphaChannel);
        passes = cut ? E3dModel::Blended : E3dModel::Opaque;
    }
    if ((passes & E3dModel::Opaque) != 0) {
        // A texture whose alpha is not known yet is cut as if it had some.
        const bool noCut = source != nullptr && source->hasOpacity && source->opacity <= 0.0f;
        addPacket(set, part, material, E3dModel::Opaque, !noCut && alpha != TextureAlpha::Opaque);
    }
    if ((passes & E3dModel::Blended) != 0)
        addPacket(set, part, material, E3dModel::Blended, false);
}

void E3dShape::build(PacketSet &set, int level, int repaint, const PacketSet *common) {
    startSet(set);
    if (repaint >= 0 && !repaints[repaint].made)
        makeRepaint(repaints[repaint]);
    int k = 0;
    for (int part : model->levelParts[size_t(level)]) {
        if (model->parts[size_t(part)].material < 0) {
            if (common != nullptr)
                addPart(set, part, repaint);
            continue;
        }
        if (common == nullptr) {
            addPart(set, part, NoSkins);
            continue;
        }
        for (; k < common->all.size() && common->parts[size_t(k)] == part; ++k) {
            set.all.push_back(common->all[k]);
            set.parts.push_back(part);
            set.glows.push_back(common->glows[size_t(k)]);
            if (restShowing[size_t(part)])
                set.atRest.push_back(common->all[k]);
        }
    }
    if (common != nullptr)
        set.commonGeneration = common->generation;
}

// The set made, or made again once what it waited for is known.
E3dShape::PacketSet &E3dShape::packets(int level, int repaint) {
    PacketSet &common = sets[keyOf(level, NoSkins)];
    const auto waitingOver = [](PacketSet &set) {
        if (set.waiting.isEmpty() || set.checkedFrame == Renderer::frameNumber())
            return false;
        set.checkedFrame = Renderer::frameNumber();
        for (int texture : set.waiting)
            if (alphaOf(texture) == TextureAlpha::Unknown)
                return false;
        return true;
    };
    if (!common.built || waitingOver(common))
        build(common, level, NoSkins, nullptr);
    if (repaint == NoSkins)
        return common;
    PacketSet &set = sets[keyOf(level, repaint)];
    if (!set.built || set.commonGeneration != common.generation || waitingOver(set))
        build(set, level, repaint, &common);
    return set;
}

void E3dShape::pushRenderItem(RenderQueue &queue) {
    pushRenderItem(queue, 0, 0);
}

void E3dShape::pushRenderItem(RenderQueue &queue, quint32 selectionId, unsigned int stateId) {
    if (loaded == 2 || stateId >= unsigned(state.size()))
        return;
    if (loaded == 0) {
        const ShapeLoader::Request request = ShapeLoader::request(this);
        if (request == ShapeLoader::Request::Wait) {
            queue.noteShapeWaiting();
            return;
        }
        if (request == ShapeLoader::Request::LoadHere) {
            load();
            return;
        }
        if (loaded != 1)
            return;
    }
    if (distanceLevelCount() == 0 || !mesh.valid())
        return;

    State &s = state[stateId];
    const int level = levelOf(s);
    PacketSet &set = packets(level, s.repaint);
    const bool picking = SelectionIdCodec::isKind(selectionId, SelectionIdCodec::Kind::ShapePart);
    if (!isCustom(s) && !picking) {
        if (!set.atRest.isEmpty())
            queue.submit(set.atRest, selectionId);
        return;
    }

    if (s.posedParts > 0)
        updateWorld(s);
    if (!s.lights.isEmpty())
        matchLights(s);
    const std::vector<unsigned char> &showing = s.showing.empty() ? restShowing : s.showing;
    Pool *pool = nullptr;
    if (s.posedParts > 0 || s.litClasses > 0) {
        pool = &s.pools[keyOf(level, s.repaint)];
        if (pool->generation != set.generation) {
            retirePool(*pool);
            pool->generation = set.generation;
            pool->items.fill(nullptr, set.all.size());
        }
    }
    for (int k = 0; k < set.all.size(); ++k) {
        const int part = set.parts[size_t(k)];
        if (!showing[size_t(part)])
            continue;
        RenderItem *item = set.all[k];
        const E3dModel::Part &p = model->parts[size_t(part)];
        const bool moved = s.posedParts > 0 && s.moved[size_t(part)];
        const bool lit = s.litClasses > 0 && set.glows[size_t(k)];
        const float light = lit ? s.classLight[size_t(p.section)] : 0.0f;
        if (pool != nullptr && (moved || light > 0.0f)) {
            RenderItem *&own = pool->items[k];
            if (own == nullptr)
                own = new RenderItem(*item);
            own->msMatrix = (moved ? s.world.data() : restWorld.data()) + size_t(part) * 16;
            if (set.glows[size_t(k)]) {
                // As MaSzyna lights a section: its texture times the light
                // (following the exposure: the shared packet's).
                std::fill(own->pbr.emissive, own->pbr.emissive + 3, light);
                own->pbr.textures[RenderItem::Pbr::MAP_EMISSIVE] =
                        light > 0.0f && own->material.textured ? own->material.textureId : -1;
            }
            own->setBounds(p.boundCenter, p.boundRadius, own->msMatrix);
            item = own;
        }
        queue.submit(item, picking ? SelectionIdCodec::forShapePart(selectionId, k) : selectionId);
    }
}

int E3dShape::drawnPrimitiveCount(unsigned int stateId) const {
    if (!isLoaded() || stateId >= unsigned(state.size()))
        return 0;
    const auto found = sets.constFind(keyOf(levelOf(state[stateId]), state[stateId].repaint));
    return found != sets.constEnd() ? found->all.size() : 0;
}

int E3dShape::drawnPrimitiveMaterial(unsigned int stateId, int primitive) const {
    if (!isLoaded() || stateId >= unsigned(state.size()) || primitive < 0)
        return -1;
    const auto found = sets.constFind(keyOf(levelOf(state[stateId]), state[stateId].repaint));
    if (found == sets.constEnd() || primitive >= found->all.size())
        return -1;
    return model->parts[size_t(found->parts[size_t(primitive)])].material;
}

void E3dShape::pushPrimitiveHighlight(RenderQueue &queue, unsigned int stateId,
                                      const QVector<int> &primitives, const float *color) {
    if (primitives.isEmpty() || !isLoaded() || stateId >= unsigned(state.size()))
        return;
    State &s = state[stateId];
    const auto found = sets.constFind(keyOf(levelOf(s), s.repaint));
    if (found == sets.constEnd())
        return;
    if (s.posedParts > 0)
        updateWorld(s);
    for (int k : primitives) {
        if (k < 0 || k >= found->all.size())
            continue;
        const int part = found->parts[size_t(k)];
        auto *item = new RenderItem(*found->all[k]);
        item->shared = false;
        const float *world = s.posedParts > 0 && !s.world.empty() ? s.world.data() + size_t(part) * 16
                                                                  : restWorld.data() + size_t(part) * 16;
        item->msMatrix = queue.frameMatrix(world);
        item->pbr = RenderItem::Pbr();
        item->pbr.enabled = true;
        item->pbr.unlit = true;
        item->pbr.blend = true;
        std::copy(color, color + 4, item->pbr.baseColor);
        item->disableTextures(1.0f, 1.0f, 1.0f, 1.0f);
        item->material.surface = RenderItem::SURFACE_BLENDED;
        item->material.decal = true;
        item->material.doubleSided = true;
        queue.submitFrameItem(item);
    }
}

void E3dShape::invalidateRenderState(bool) {
    retirePackets();
    for (State &s : state)
        s.pools.clear();
}

void E3dShape::fillShapeTextureInfo(QHash<int, ShapeTextureInfo*> &list, unsigned int stateId) {
    if (!isLoaded())
        return;
    QVector<int> ids;
    for (const Material &material : materials)
        if (material.texture >= 0)
            ids.push_back(material.texture);
    const int repaint = stateId < unsigned(state.size()) ? state[stateId].repaint : -1;
    if (repaint >= 0)
        for (const Material &slot : repaints[repaint].slot)
            if (slot.texture >= 0)
                ids.push_back(slot.texture);
    for (int id : ids) {
        if (list.contains(id))
            continue;
        auto *info = new ShapeTextureInfo();
        info->shapeName = name;
        info->textureId = id;
        const auto it = TexLib::mtex.find(id);
        const Texture *texture = it == TexLib::mtex.end() ? nullptr : it->second;
        if (texture == nullptr) {
            info->textureName = QString("tex:%1").arg(id);
            info->loaded = "MISSING";
        } else {
            info->textureName = texture->pathid;
            if (texture->missing)
                info->loaded = "MISSING";
            else if (texture->error)
                info->loaded = "ERROR";
            else if (!texture->loaded || !texture->glLoaded)
                info->loading = true;
            else {
                info->loaded = "YES";
                info->resolution = QString::number(texture->width) + "x" + QString::number(texture->height);
                info->format = texture->gpuIsCompressed() ? "GPU compressed" : texture->bytesPerPixel == 4 ? "RGBA" : "RGB";
            }
        }
        list[id] = info;
    }
}

void E3dShape::fillShapeHierarchyInfo(ShapeHierarchyInfo *info, unsigned int stateId) {
    if (info == nullptr || !isLoaded())
        return;
    info->hierarchy.clear();
    info->matrices.clear();
    info->parts.clear();
    const std::vector<E3dModel::Part> &parts = model->parts;
    for (size_t i = 0; i < parts.size(); ++i) {
        info->hierarchy.push_back(parts[i].parent);
        info->matrices.push_back(partName(int(i)));
    }
    if (distanceLevelCount() == 0 || stateId >= unsigned(state.size()))
        return;
    const State &s = state[stateId];
    const std::vector<unsigned char> &showing = s.showing.empty() ? restShowing : s.showing;
    for (int part : model->levelParts[size_t(levelOf(s))]) {
        const E3dModel::Part &p = parts[size_t(part)];
        ShapeHierarchyInfo::ShapePart entry;
        entry.matrixId = part;
        entry.uid = unsigned(part);
        entry.enabled = showing[size_t(part)];
        entry.polyCount = int((p.indexCount > 0 ? p.indexCount : p.vertexCount) / 3);
        const Material *material = materialOf(p.material, s.repaint);
        if (material != nullptr && material->texture >= 0) {
            const auto it = TexLib::mtex.find(material->texture);
            if (it != TexLib::mtex.end() && it->second != nullptr)
                entry.textureName = it->second->pathid;
        }
        info->parts.push_back(entry);
    }
}

void E3dShape::fillContentHierarchyInfo(QVector<ContentHierarchyInfo*> &list, int parent) {
    if (!isLoaded())
        return;
    auto *info = new ContentHierarchyInfo();
    info->parent = parent;
    info->name = name.isEmpty() ? QFileInfo(pathid).fileName() : name;
    info->distanceLevelId = -1;
    info->shape = this;
    info->type = "shape";
    list.push_back(info);
    parent = list.size() - 1;
    for (int i = 0; i < distanceLevelCount() && distanceLevelCount() > 1; ++i) {
        auto *level = new ContentHierarchyInfo();
        level->parent = parent;
        level->name = QString("LOD %1: to %2 m").arg(i).arg(double(model->levelLimits[size_t(i)]));
        level->distanceLevelId = i;
        level->shape = this;
        level->type = "shape";
        list.push_back(level);
    }
}
