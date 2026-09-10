#include "Terrain.h"
#include "TerrainMaterialMap.h"
#include "TerrainMaterialSource.h"
#include "TerrainMaterialLibrary.h"
#include "TerrainMeshBackend.h"
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/UndoBuffer.h>
#include <tsre/texture/Brush.h>
#include <tsre/texture/Texture.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/AceLib.h>
#include <tsre/texture/DdsLib.h>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QUuid>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPointer>
#include <QThreadPool>
#include <QRunnable>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <future>

namespace {
// Dedicated pool: do not consume the normal ACE/DDS loader's thread budget.
constexpr int WorkerLimit = 4;
constexpr int OutstandingLimit = 4;
constexpr int UploadLimit = 2;
constexpr qint64 UploadBudgetNs = 2000000;
constexpr int RecipeLimit = 16;
constexpr qint64 RecipeBudgetNs = 2000000;
std::atomic<int> activeWorkers{0}, peakWorkers{0}, outstandingJobs{0};
std::atomic<int> miniatureJobs{0}, miniatureInFlight{0};
int uploadsThisFrame=0, recipesThisFrame=0, publishedJobs=0, discardedJobs=0;
qint64 uploadNsThisFrame=0, recipeNsThisFrame=0;
QVector<std::weak_ptr<TerrainProceduralState>> proceduralStates;
QThreadPool &materialPool() {
    static QThreadPool pool;
    static const bool configured = [] { pool.setMaxThreadCount(WorkerLimit); return true; }();
    Q_UNUSED(configured);
    return pool;
}
struct Material;
struct MaterialJob {
    std::atomic<bool> cancelled{false}, done{false};
    QImage rgb;
    QImage miniature;
    QByteArray blocks;
    QString outputKey;
    bool bc1=false, privateEdit=false;
    ~MaterialJob() { --outstandingJobs; }
};
// Separate edit queue: one latest request per patch, not one request per stroke.
// Submitted jobs use the same four-worker pool as background generation.
struct MiniatureJob {
    QImage rgb, miniature;
    QString outputKey;
    std::weak_ptr<Material> target;
    int side=0;
    bool started=false;
    std::atomic<bool> cancelled{false}, done{false};
    std::promise<void> completed;
    std::future<void> completion=completed.get_future();
    MiniatureJob() { ++miniatureJobs; }
    ~MiniatureJob() { --miniatureJobs; }
};
struct PendingTextureDelete { QPointer<QOpenGLContextGroup> group; unsigned int id; };
QVector<PendingTextureDelete> pendingDeletes;
QHash<int,QPointer<QOpenGLContextGroup>> textureGroups;
void flushTextureDeletes() {
    if (pendingDeletes.isEmpty()) return;
    auto *context=QOpenGLContext::currentContext();
    for (auto it=pendingDeletes.begin(); it!=pendingDeletes.end();) {
        if (it->group.isNull()) it=pendingDeletes.erase(it); // Context destruction already freed it.
        else if (context && context->shareGroup()==it->group) {
            context->functions()->glDeleteTextures(1,&it->id);
            it=pendingDeletes.erase(it);
        } else ++it;
    }
}
bool profileEnabled() { static const bool on = qEnvironmentVariableIntValue("TSRE_TERRAIN_MATERIAL_PROFILE") != 0; return on; }
bool useBC1() { static const bool on = !qEnvironmentVariableIsSet("TSRE_TERRAIN_MATERIAL_RGB"); return on; }
QString shaderKey(const TFile &file, int id) {
    auto it = file.materials.find(id);
    if (id < 0 || it == file.materials.end() || !it->second.tex[0] || it->second.count153 < 1) return {};
    const auto &m = it->second;
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << (m.name ? *m.name : QString()) << m.count153 << m.count155;
    for (int i = 0; i < std::min(2,m.count153); ++i) {
        out << (m.tex[i] ? m.tex[i]->toLower().replace('\\','/') : QString());
        for (int a : m.atex[i]) out << a;
    }
    for (int i = 0; i < std::min(2,m.count155); ++i) for (int a : m.itex[i]) out << a;
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString primaryName(const TFile &file, int id) {
    auto it = file.materials.find(id);
    return it != file.materials.end() && it->second.tex[0] ? *it->second.tex[0] : QString();
}
QString sourceName(const TFile &file, int id) {
    if (!file.materialUidMapPresent) return primaryName(file,id);
    if (!file.materialUidMapValid) return {};
    const auto material=TerrainMaterialLibrary::current()->find(file.materialUids.value(id));
    return material ? material->texture : QString();
}
QString sourceKey(const TFile &file, int id) {
    return file.materialUidMapPresent ? sourceName(file,id) : shaderKey(file,id);
}
QList<int> sourceSlots(const TFile &file) {
    if (file.materialUidMapPresent) return file.materialUids.keys();
    QList<int> ids;
    for (int i=1;i<file.materialsCount;++i) ids.push_back(i);
    return ids;
}
QString bakeSettingsKey(const TFile &file, const QString &directory, int patches) {
    QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly);
    out << TerrainMaterialMap::Side << TerrainMaterialMap::BakedSide << TerrainMaterialMap::OutputSide
        << TerrainMaterialMap::SamplingMode << patches << qint32(AceEncoding::Dxt1);
    for (int id : sourceSlots(file)) {
        if (file.materialUidMapPresent) out << id;
        out << sourceKey(file,id);
        QString path=QDir(directory).filePath(sourceName(file,id));
        const QString dds=path.left(path.size()-3)+"dds";
        if (path.endsWith(".ace",Qt::CaseInsensitive) && QFileInfo::exists(dds)) path=dds;
        const QFileInfo source(path);
        out << source.exists() << source.size() << source.lastModified().toMSecsSinceEpoch();
    }
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
QString storedBakeSettings(const QString &marker) {
    const auto parts=marker.split(':');
    return parts.size()==4 && (parts[1]=="unchecked" || parts[1]=="checked")
            && parts[2].size()==64 ? parts[2] : QString();
}
QString storedBakeValidation(const QString &marker) {
    const auto parts=marker.split(':');
    if (parts.size()==4 && parts[1]=="checked" && parts[3].size()==64) return "v1:"+parts[3];
    return parts.size()==2 && parts[1].size()==64 ? marker : QString(); // Old v1 signatures.
}
QImage loadBakeImage(const QString &path) {
    Texture texture(path);
    AceLoadOptions options; options.cpuPixels=true; options.stageMipmaps=false;
    QString error;
    AceLib::load(path,texture,options,error);
    QImage image;
    if (texture.loaded && texture.decodeToCpu() && texture.imageData && texture.bytesPerPixel==3
            && texture.width==TerrainMaterialMap::BakedSide && texture.height==TerrainMaterialMap::BakedSide)
        image=QImage(texture.imageData,texture.width,texture.height,texture.width*3,QImage::Format_RGB888).copy();
    delete[] texture.imageData; texture.imageData=nullptr;
    return image;
}
bool loadSource(const QString &directory, const QString &filename, QImage &image, QString &error) {
    QString path = QDir(directory).filePath(filename);
    if (Game::caseInsensitiveFS) path = path.toLower();
    // Match ordinary terrain's DDS preference without registering/uploading a source.
    const QString dds = path.left(path.size()-3) + "dds";
    if (path.endsWith(".ace",Qt::CaseInsensitive) && QFileInfo::exists(dds)) path = dds;
    Texture texture(path);
    if (path.endsWith(".ace", Qt::CaseInsensitive)) {
        AceLoadOptions options; options.cpuPixels=true; options.stageMipmaps=false;
        AceLib::load(path,texture,options,error);
    }
    else if (path.endsWith(".dds", Qt::CaseInsensitive)) { DdsLib loader; loader.texture=&texture; loader.run(); }
    else image = QImage(path);
    if (texture.loaded) texture.decodeToCpu(); // No GL upload/readback for sources.
    if (texture.loaded && texture.imageData && (texture.bytesPerPixel == 3 || texture.bytesPerPixel == 4)) {
        image = QImage(texture.imageData, texture.width, texture.height, texture.width*texture.bytesPerPixel,
                       texture.bytesPerPixel == 3 ? QImage::Format_RGB888 : QImage::Format_RGBA8888).copy();
    }
    delete[] texture.imageData;
    if (image.isNull()) { error = "Cannot decode procedural source texture: " + path; return false; }
    image = image.scaled(TerrainMaterialMap::OutputSide, TerrainMaterialMap::OutputSide,
                         Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
    return true;
}
// TexLib retains its existing API. Each Material owns one library reference;
// patches/cache share the Material, not additional untracked texture references.
struct Material {
    int textureId = -1;
    QImage miniature; // Reduced CPU image; independent of uploaded Texture pixels.
    QImage pendingImage; // Only until the edit worker has reduced/hashed this version.
    QString outputKey;
    bool privateEdit = false;
    bool immediateUpload = false; // Interactive edits bypass automatic streaming budgets.
    ~Material() {
        auto it = TexLib::mtex.find(textureId);
        if (it == TexLib::mtex.end() || !it->second) return;
        Texture *t = it->second;
        if (--t->ref > 0) return;
        const auto group=textureGroups.take(textureId);
        if (t->tex && !group.isNull()) {
            pendingDeletes.push_back({group,t->tex[0]});
            flushTextureDeletes();
        }
        delete[] t->tex;
        delete[] t->imageData;
        delete t;
        TexLib::mtex.erase(it);
    }
};
using MaterialPtr = std::shared_ptr<Material>;

int registerOutput(const QImage &rgb, bool privateEdit) {
    QElapsedTimer timer; timer.start();
    const QString key = privateEdit ? "terrain-proc:private:" + QUuid::createUuid().toString()
                                    : TerrainMaterialMap::textureKey(rgb,useBC1());
    const qint64 hashNs = timer.nsecsElapsed();
    if (!privateEdit) {
        const int existing = TexLib::getTex(key);
        if (existing >= 0) return existing;
    }
    auto *texture = new Texture(key);
    texture->width = rgb.width(); texture->height = rgb.height();
    texture->bpp = 24; texture->bytesPerPixel = 3; texture->type = GL_RGB;
    texture->typk = 0; texture->imageSize = rgb.width()*rgb.height()*3;
    texture->compressed = 0; texture->loaded = true;
    timer.restart();
    if (useBC1()) {
        texture->compressedData = TerrainMaterialMap::encodeBC1(rgb);
        texture->compressedGLFormat = 0x83F0; // GL_COMPRESSED_RGB_S3TC_DXT1_EXT
    } else {
        texture->imageData = new unsigned char[texture->imageSize];
        for (int y=0; y<rgb.height(); ++y) memcpy(texture->imageData+y*rgb.width()*3, rgb.constScanLine(y), rgb.width()*3);
    }
    if (profileEnabled()) qInfo() << "Terrain material hash ms" << hashNs/1e6 << "encode/copy ms" << timer.nsecsElapsed()/1e6;
    return TexLib::addTex(texture);
}
MaterialPtr makeMaterial(const QImage &rgb, bool privateEdit) {
    auto result = std::make_shared<Material>();
    result->privateEdit = privateEdit;
    result->textureId = registerOutput(rgb,privateEdit);
    return result;
}
// Only the owning/UI thread touches TexLib. Workers already computed both the
// content hash and compression; collecting a result must not repeat that work.
MaterialPtr registerJob(const MaterialJob &job) {
    auto material=std::make_shared<Material>();
    material->miniature=job.miniature;
    material->outputKey=job.outputKey;
    material->privateEdit=job.privateEdit;
    if (!job.privateEdit) {
        material->textureId=TexLib::getTex(job.outputKey);
        if (material->textureId>=0) return material;
    }
    const QString key=job.privateEdit ? "terrain-proc:private:"+QUuid::createUuid().toString() : job.outputKey;
    auto *texture=new Texture(key);
    texture->width=job.rgb.width(); texture->height=job.rgb.height();
    texture->bpp=24; texture->bytesPerPixel=3; texture->type=GL_RGB;
    texture->typk=0; texture->imageSize=texture->width*texture->height*3;
    texture->compressed=0; texture->loaded=true;
    if (job.bc1) {
        texture->compressedData=job.blocks;
        texture->compressedGLFormat=0x83F0;
    } else {
        texture->imageData=new unsigned char[texture->imageSize];
        for(int y=0;y<texture->height;++y)
            memcpy(texture->imageData+y*texture->width*3,job.rgb.constScanLine(y),texture->width*3);
    }
    material->textureId=TexLib::addTex(texture);
    return material;
}
MaterialPtr shareMaterial(const MaterialPtr &material) {
    if (!material || !material->privateEdit || material->outputKey.isEmpty()) return material;
    const int existing=TexLib::getTex(material->outputKey);
    if (existing>=0 && existing!=material->textureId) {
        auto shared=std::make_shared<Material>(); // Owns getTex's acquired reference.
        shared->textureId=existing;
        shared->outputKey=material->outputKey;
        shared->miniature=material->miniature;
        // If the identical texture has not been uploaded yet, do not expose a
        // fallback while it waits behind the automatic upload budget.
        shared->immediateUpload=true;
        return shared;
    }
    auto texture=TexLib::mtex.find(material->textureId);
    if (texture==TexLib::mtex.end() || !texture->second) return material;
    if (existing==material->textureId) --texture->second->ref;
    else {
        texture->second->hashid.clear();
        texture->second->hashid.push_back(material->outputKey);
    }
    // Unique output: publish its content identity without replacing CPU/GPU data.
    material->privateEdit=false;
    return material;
}
void reportToolError(const QString &error) {
    // At most once for identical repeated mouse-move failures; no modal dialog per stamp.
    static QString last;
    if (error == last) return;
    last = error;
    qWarning().noquote() << error;
    QMessageBox::warning(nullptr, "Experimental procedural terrain", error);
}
}

static int uploadProceduralOutput(const MaterialPtr &material, bool background, bool mipmaps=false);

struct TerrainProceduralState {
    quint64 libraryRevision=0;
    QMap<int,QString> resolvedSources;
    bool libraryCanRecover=false;
    void rememberLibrary(const TFile &file) {
        if (!file.materialUidMapPresent) return;
        libraryRevision=TerrainMaterialLibrary::current()->revision();
        resolvedSources.clear();
        for (int id : sourceIds) resolvedSources[id]=sourceName(file,id);
    }
    TerrainMaterialMap map;
    QHash<int,QImage> sources;
    QHash<QByteArray,MaterialPtr> cache;
    QVector<MaterialPtr> patches;
    QSet<int> sourceIds;
    QSet<int> editedPatches;
    QVector<QByteArray> patchKeys;
    QVector<QByteArray> recipeKeys; // Native recipe hashes survive GPU/cache eviction.
    QSet<QByteArray> failedRecipes;
    QHash<QByteArray,std::shared_ptr<MaterialJob>> pending;
    QHash<int,std::shared_ptr<MiniatureJob>> miniatureQueue;
    QByteArray prefetchBakeKey;
    QString prefetchBakePath;
    QString error;
    bool ready = false;
    bool changed = false;
    bool bakeAvailable = false, bakeCurrent = false, nearCamera = true;
    bool detailViewValid=false;
    double detailCameraX=0, detailCameraZ=0;
    bool fullBakeRequired=true;
    QString bakeSettings, pendingBakeSettings;
    QImage bakeImage, pendingBakeImage;
    bool detailedPatch(int patch, const TerrainGridLayout &grid, double cameraX, double cameraZ, bool valid) const {
        // Keep unbaked/unsaved painting visible instead of displaying stale data.
        if (!valid || !bakeAvailable || changed) return true;
        const double size=double(grid.terrainWorldSize)/grid.patchesPerSide;
        const double dx=(grid.patchColumn(patch)+0.5)*size-cameraX;
        const double dz=(grid.patchRow(patch)+0.5)*size-cameraZ;
        const double limit=std::max(0.0f,TerrainMaterialMap::DetailDistanceMeters);
        return dx*dx+dz*dz<=limit*limit;
    }
    QString savedBakePath, backupBakePath, previousBakeInfo;
    bool bakeWritten = false, bakeExisted = false;
    QVector<float> previousPatchData;
    int detailTextureId = -1;
    QString savedMapPath, oldMapPath, backupMapPath;
    bool mapWritten = false;
    bool rollbackMapSave() {
        // Copy back, rather than rename, so the one backup survives rollback.
        if (mapWritten && QFile::exists(savedMapPath) && !QFile::remove(savedMapPath))
            return false;
        mapWritten = false;
        if (!oldMapPath.isEmpty() && !QFile::copy(backupMapPath, oldMapPath))
            return false;
        savedMapPath.clear(); oldMapPath.clear(); backupMapPath.clear();
        return true;
    }
    ~TerrainProceduralState() {
        cancelPending();
        if (detailTextureId >= 0) TexLib::delRef(detailTextureId);
    }
    void cancelPending(bool includeMiniatures=true) {
        for (const auto &job : pending) job->cancelled.store(true);
        pending.clear();
        failedRecipes.clear();
        if (includeMiniatures) {
            for (const auto &job : miniatureQueue) job->cancelled.store(true);
            miniatureQueue.clear();
        }
    }
    void collectCompleted() {
        for (auto it=pending.begin();it!=pending.end();) {
            const auto &job=it.value();
            if (!job->done.load(std::memory_order_acquire)) { ++it; continue; }
            if (job->cancelled.load() || job->rgb.isNull()) {
                ++discardedJobs;
                if (!job->cancelled.load()) {
                    failedRecipes.insert(it.key());
                    qWarning() << "Procedural material generation failed; retaining fallback";
                }
            } else {
                cache.insert(it.key(),registerJob(*job));
                if (it.key().startsWith("B:")) bakeImage=job->rgb;
                ++publishedJobs;
            }
            it=pending.erase(it);
        }
        serviceMiniatures();
    }
    QByteArray recipe(int patch, int count) {
        if (recipeKeys.size()!=count*count) recipeKeys.fill({},count*count);
        auto &key=recipeKeys[patch];
        if (key.isEmpty()) key=map.patchKey(patch,count);
        return key;
    }
    void queueMiniature(int patch, int count, const MaterialPtr &material) {
        auto old=miniatureQueue.find(patch);
        if (old!=miniatureQueue.end()) {
            if (old.value()->target.lock()==material) return;
            old.value()->cancelled.store(true);
            miniatureQueue.erase(old);
        }
        if (material->pendingImage.isNull()) return;
        auto job=std::make_shared<MiniatureJob>();
        job->rgb=material->pendingImage;
        job->target=material;
        job->side=count>0 && TerrainMaterialMap::BakedSide%count==0 ? TerrainMaterialMap::BakedSide/count : 0;
        miniatureQueue.insert(patch,job);
        // Do not drop work if the pool is busy. The frame pump retries it.
    }
    void serviceMiniatures() {
        for (auto it=miniatureQueue.begin();it!=miniatureQueue.end();) {
            auto job=it.value();
            auto material=job->target.lock();
            if (!material || job->cancelled.load()) {
                job->cancelled.store(true);
                it=miniatureQueue.erase(it); continue;
            }
            if (job->done.load(std::memory_order_acquire)) {
                if (!job->miniature.isNull()) {
                    material->miniature=job->miniature;
                    material->outputKey=job->outputKey;
                    material->pendingImage=QImage();
                }
                it=miniatureQueue.erase(it); continue;
            }
            // Identical fills may share a Material across several patch slots.
            if (!material->miniature.isNull() && !material->outputKey.isEmpty()) {
                job->cancelled.store(true);
                it=miniatureQueue.erase(it); continue;
            }
            if (!job->started && miniatureInFlight.load()<WorkerLimit) {
                job->started=true;
                ++miniatureInFlight;
                materialPool().start(QRunnable::create([job] {
                    const int active=++activeWorkers;
                    int peak=peakWorkers.load();
                    while(active>peak && !peakWorkers.compare_exchange_weak(peak,active)) {}
                    try {
                        if (!job->cancelled.load() && job->side>0) {
                            job->miniature=job->rgb.scaled(job->side,job->side,
                                    Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
                            if (!job->cancelled.load()) job->outputKey=TerrainMaterialMap::textureKey(job->rgb,useBC1());
                        }
                    } catch (...) { job->miniature=QImage(); }
                    --activeWorkers;
                    --miniatureInFlight;
                    job->done.store(true,std::memory_order_release);
                    job->completed.set_value();
                }),1);
            }
            ++it;
        }
    }
    void finishMiniatures() {
        // Route save remains blocking. No event processing or editing while we
        // wait; preserve final painted images instead of regenerating them.
        while (!miniatureQueue.isEmpty()) {
            serviceMiniatures();
            if (miniatureQueue.isEmpty()) break;
            std::shared_ptr<MiniatureJob> running;
            for (const auto &job : miniatureQueue) if (job->started) { running=job; break; }
            if (running) running->completion.wait();
            else materialPool().waitForDone(); // Other tiles currently own the submitted slots.
        }
    }
    void request(const QByteArray &key, int patch, int count, bool privateEdit, const QString &bakePath = {}) {
        if (pending.contains(key) || failedRecipes.contains(key)) return;
        int outstanding=outstandingJobs.load();
        do {
            if (outstanding>=OutstandingLimit) return;
        } while(!outstandingJobs.compare_exchange_weak(outstanding,outstanding+1));
        auto job=std::make_shared<MaterialJob>();
        job->bc1=bakePath.isEmpty() && useBC1(); job->privateEdit=privateEdit;
        pending.insert(key,job);
        // Implicitly shared, immutable snapshots. A later paint detaches the ID
        // array; workers never hold a Terrain, TFile, Material or GL reference.
        materialPool().start(QRunnable::create([job,
                snapshot=bakePath.isEmpty()?map:TerrainMaterialMap(),
                images=bakePath.isEmpty()?sources:QHash<int,QImage>(),
                miniatureSide=count>0 && TerrainMaterialMap::BakedSide%count==0 ? TerrainMaterialMap::BakedSide/count : 0,
                patch, count, bakePath] {
            const int active=++activeWorkers;
            int peak=peakWorkers.load();
            while(active>peak && !peakWorkers.compare_exchange_weak(peak,active)) {}
            try {
                if (!job->cancelled.load()) {
                    if (bakePath.isEmpty()) job->rgb=snapshot.generate(patch,count,images);
                    else job->rgb=loadBakeImage(bakePath);
                }
                if (!job->cancelled.load() && !job->rgb.isNull()) {
                    if (bakePath.isEmpty() && miniatureSide>0)
                        job->miniature=job->rgb.scaled(miniatureSide,miniatureSide,
                                Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
                    job->outputKey=TerrainMaterialMap::textureKey(job->rgb,job->bc1);
                    if (!bakePath.isEmpty()) job->outputKey.prepend("base-level-bake:");
                    if (job->bc1 && !job->cancelled.load()) job->blocks=TerrainMaterialMap::encodeBC1(job->rgb);
                }
            } catch (...) {
                job->rgb=QImage();
            }
            --activeWorkers;
            job->done.store(true,std::memory_order_release);
        }));
    }
    void requestBake() {
        if (!prefetchBakeKey.isEmpty() && !cache.contains(prefetchBakeKey))
            request(prefetchBakeKey,0,0,false,prefetchBakePath);
    }
    bool ensureSource(const TFile &file, int id, const QString &directory) {
        if (id < 0 || id > 255 || sourceKey(file,id).isEmpty()) {
            error = QString("Missing/invalid procedural source for local ID %1 (route UiD %2)").arg(id).arg(file.materialUids.value(id)); return false;
        }
        if (sources.contains(id)) return true;
        QImage source;
        if (!loadSource(directory,sourceName(file,id),source,error)) return false;
        sources.insert(id,source); return true;
    }
    void pruneCache() {
        for (auto it=cache.begin(); it!=cache.end(); ) {
            // The bake is owned by the tile cache, not individual patch slots.
            if (it.value().use_count()==1 && !it.key().startsWith("B:")) it=cache.erase(it); else ++it;
        }
    }
    MaterialPtr sharedPatch(int patch, int count, bool privateEdit = false) {
        collectCompleted();
        // Keep edited outputs private until save, but share identical fills within this tile.
        const QByteArray key = QByteArray(1, privateEdit ? 'P' : 'S') + recipe(patch,count);
        auto it = cache.constFind(key);
        if (it != cache.constEnd()) {
            queueMiniature(patch,count,it.value());
            return it.value();
        }
        const QImage image = map.generate(patch,count,sources);
        if (image.isNull()) return {};
        auto material = makeMaterial(image,privateEdit);
        material->pendingImage=image;
        cache.insert(key,material);
        queueMiniature(patch,count,material);
        return material;
    }
};

// Only authoritative material state is captured, never generated images, jobs,
// height bounds or mesh buffers. Shader IDs and UVs must survive mode changes.
struct TerrainMaterialUndo : UndoSnapshot {
    QPointer<Terrain> terrain;
    quint64 epoch = 0;
    QString root, route, name, reference, bakeInfo;
    int samples = 0, patches = 0;
    float spacing = 0;
    QVector<std::shared_ptr<const TerrainMaterialSource>> palette;
    QVector<float> uv;
    bool uidMapPresent=false, uidMapValid=true;
    QMap<int,quint32> uids;
    bool targetValid() const override {
        return terrain && terrain->loaded && terrain->tfile
                && terrain->proceduralUndoEpoch == epoch
                && Game::root == root && Game::route == route && terrain->name == name
                && terrain->gridLayout.sampleCount == samples
                && terrain->gridLayout.patchesPerSide == patches
                && *terrain->tfile->sampleSize == spacing;
    }
    bool restore() override {
        if (!targetValid() || !Game::writeEnabled || !terrain->editable || Game::serverClient) return false;
        auto &t = *terrain;
        if (t.procedural && (!t.procedural->savedMapPath.isEmpty() || !t.procedural->savedBakePath.isEmpty()))
            return false; // Never discard recovery state from a failed save.
        auto next = std::make_shared<TerrainProceduralState>();
        QSet<int> dirty;
        bool samePalette = t.tfile->materialsCount == palette.size()
                && t.tfile->materialUidMapPresent==uidMapPresent && t.tfile->materialUids==uids;
        for (int i=0; samePalette && i<palette.size(); ++i) {
            const auto current = TerrainMaterialSource::capture(*t.tfile,i);
            samePalette = current && current->key() == palette[i]->key();
        }
        if (!reference.isEmpty()) {
            next->map.ids = buffer->bytes();
            if (!next->map.valid()) return false;
            next->sourceIds = next->map.usedIds();
            // Validate sources before changing either the map or its palette.
            for (int id : next->sourceIds) {
                const auto definition=uidMapPresent ? TerrainMaterialLibrary::current()->find(uids.value(id)) : nullptr;
                if (uidMapPresent ? (!uidMapValid || !definition) : (id<=0 || id>=palette.size())) return false;
                QImage image;
                if (!uidMapPresent && samePalette && t.procedural && t.procedural->sources.contains(id))
                    image = t.procedural->sources[id];
                else if (!loadSource(t.texturepath,uidMapPresent ? definition->texture : palette[id]->normal.textures[0],image,next->error)) return false;
                next->sources.insert(id,image);
            }
            next->patches.resize(patches*patches);
            next->patchKeys.resize(patches*patches);
            next->recipeKeys.resize(patches*patches);
            const bool reuse = samePalette && t.procedural && t.procedural->ready;
            if (reuse) next->cache = t.procedural->cache;
            for (int p=0; p<patches*patches; ++p) {
                // Keys include the sampling halo, so neighbours affected by a
                // restored border are regenerated, even if their IDs are equal.
                if (reuse && next->map.patchKey(p,patches) == t.procedural->map.patchKey(p,patches)) {
                    if (p<t.procedural->patches.size()) next->patches[p]=t.procedural->patches[p];
                    if (p<t.procedural->patchKeys.size()) next->patchKeys[p]=t.procedural->patchKeys[p];
                    if (p<t.procedural->recipeKeys.size()) next->recipeKeys[p]=t.procedural->recipeKeys[p];
                } else dirty.insert(p);
            }
            next->ready = next->changed = next->fullBakeRequired = true;
            next->bakeCurrent = false;
            next->bakeAvailable = bakeInfo != "v1:pending"
                    && QFileInfo::exists(QDir(t.texturepath).filePath(palette[0]->normal.textures[0]));
        }
        if (t.procedural) t.procedural->cancelPending();
        t.clearStaticTextureRefs();
        if (!samePalette) TerrainMaterialSource::restorePalette(*t.tfile,palette);
        t.tfile->sampleMaterialBuffer = reference;
        t.tfile->bakedMaterialInfo = bakeInfo;
        t.tfile->materialUidMapPresent=uidMapPresent;
        t.tfile->materialUidMapValid=uidMapValid;
        t.tfile->materialUids=uids;
        for (int p=0; p<patches*patches; ++p)
            std::copy_n(uv.constData()+p*7,7,t.tfile->tdata+p*13+6);
        t.procedural = reference.isEmpty() ? nullptr : next;
        if (t.procedural) {
            next->libraryCanRecover=true;
            next->rememberLibrary(*t.tfile);
            proceduralStates.push_back(next);
            for (int p : dirty) {
                next->editedPatches.insert(p);
                next->patches[p] = next->sharedPatch(p,patches,true);
                if (next->patches[p]) {
                    next->patches[p]->immediateUpload = true;
                    if (QOpenGLContext::currentContext()) t.proceduralTexture(p);
                }
            }
            next->pruneCache();
        }
        t.modified = true; // Disk may have been saved since the snapshot.
        t.invalidateAll(TerrainDirtyUvParams);
        t.refreshModified();
        return true;
    }
};
std::shared_ptr<UndoSnapshot> Terrain::captureProceduralUndo() {
    if (!TerrainMaterialMap::Enabled) return {};
    if (!tfile || (usesProceduralMaterial() && (!procedural || !procedural->ready))) return {};
    auto snapshot = std::make_shared<TerrainMaterialUndo>();
    snapshot->terrain=this; snapshot->epoch=proceduralUndoEpoch;
    snapshot->root=Game::root; snapshot->route=Game::route; snapshot->name=name;
    snapshot->samples=gridLayout.sampleCount; snapshot->patches=gridLayout.patchesPerSide;
    snapshot->spacing=*tfile->sampleSize;
    snapshot->reference=tfile->sampleMaterialBuffer; snapshot->bakeInfo=tfile->bakedMaterialInfo;
    snapshot->uidMapPresent=tfile->materialUidMapPresent;
    snapshot->uidMapValid=tfile->materialUidMapValid; snapshot->uids=tfile->materialUids;
    for (int id=0; id<tfile->materialsCount; ++id) {
        const auto source=TerrainMaterialSource::capture(*tfile,id);
        if (!source) return {};
        snapshot->palette.push_back(source);
    }
    snapshot->uv.resize(gridLayout.patchRecordCount()*7);
    for (int p=0; p<gridLayout.patchRecordCount(); ++p)
        std::copy_n(tfile->tdata+p*13+6,7,snapshot->uv.data()+p*7);
    if (usesProceduralMaterial()) snapshot->buffer=std::make_shared<UndoBuffer>(procedural->map.ids);
    return snapshot;
}

bool Terrain::usesProceduralMaterial() const { return tfile && !tfile->sampleMaterialBuffer.isEmpty(); }
void Terrain::synchronizeMaterialLibrary() {
    if (!TerrainMaterialMap::Enabled) return;
    if (!procedural || !tfile->materialUidMapPresent || !procedural->libraryCanRecover) return;
    const auto library=TerrainMaterialLibrary::current(); library->poll();
    if (procedural->libraryRevision==library->revision()) return;
    procedural->libraryRevision=library->revision();
    QMap<int,QString> names;
    bool valid=tfile->materialUidMapValid && procedural->map.valid();
    for (int id : procedural->sourceIds) {
        names[id]=sourceName(*tfile,id);
        valid &= !names[id].isEmpty();
    }
    if (names==procedural->resolvedSources && valid==procedural->ready) return;
    // Changes to other, unreferenced definitions do not invalidate this tile.
    // Workers own immutable snapshots; cancellation prevents stale publication.
    releaseProceduralTextures();
    procedural->recipeKeys.clear();
    procedural->resolvedSources=names;
    const bool modeChanged=procedural->ready!=valid;
    procedural->ready=valid;
    if (modeChanged) {
        // Legacy vertices and paged UV parameters differ for the saved bake.
        invalidateAll(TerrainDirtyUvParams);
        refreshModified();
    }
    if (!valid) {
        procedural->error="Missing/invalid route material UiD or material map; repair terrainmaterials.dat. Saved bake retained.";
        qWarning() << name << procedural->error; return;
    }
    procedural->error.clear();
    procedural->fullBakeRequired=true; procedural->bakeCurrent=false;
    modified=true;
}
void Terrain::beginProceduralFrame() {
    if (!TerrainMaterialMap::Enabled) return;
    uploadsThisFrame=recipesThisFrame=0;
    uploadNsThisFrame=recipeNsThisFrame=0;
    flushTextureDeletes();
    for (auto it=proceduralStates.begin();it!=proceduralStates.end();) {
        if (auto state=it->lock()) { state->collectCompleted(); ++it; }
        else it=proceduralStates.erase(it);
    }
    // Tile-load prefetch is independent of patch visibility. Retry requests that
    // encountered a full worker pool, and give completed bakes the first uploads
    // of the frame, before any near patch can consume the streaming budget.
    if (proceduralStates.isEmpty()) return;
    auto *context=QOpenGLContext::currentContext();
    auto *f=context ? context->functions() : nullptr;
    GLint activeUnit=GL_TEXTURE0, binding=0;
    if (f) {
        f->glGetIntegerv(GL_ACTIVE_TEXTURE,&activeUnit);
        f->glActiveTexture(GL_TEXTURE0);
        f->glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
    }
    for (const auto &weak : proceduralStates) if (auto state=weak.lock()) {
        state->requestBake();
        const auto found=state->cache.constFind(state->prefetchBakeKey);
        // A/B test: no bake mipmap generation on the render thread.
        if (found!=state->cache.constEnd()) uploadProceduralOutput(*found,true,false);
    }
    if (f) {
        f->glBindTexture(GL_TEXTURE_2D,binding);
        f->glActiveTexture(activeUnit);
    }
}
Terrain::ProceduralWorkStats Terrain::proceduralWorkStats() {
    return {activeWorkers.load(),peakWorkers.load(),outstandingJobs.load()+miniatureJobs.load(),uploadsThisFrame,publishedJobs,discardedJobs};
}
bool Terrain::rendersProceduralMaterial() const { return TerrainMaterialMap::Enabled && usesProceduralMaterial() && procedural && procedural->ready; }
bool Terrain::hasProceduralBake() const {
    return TerrainMaterialMap::Enabled && procedural && procedural->bakeAvailable;
}
void Terrain::clearStaticTextureRefs() {
    for (int p=0;p<gridLayout.patchRecordCount();++p) {
        if (texid[p]>=0) TexLib::delRef(texid[p]);
        if (texid2[p]>=0) TexLib::delRef(texid2[p]);
        texid[p]=texid2[p]=-1;
    }
}
bool Terrain::reserveProceduralBake(QString &error) {
    if (!tfile->bakedMaterialInfo.isEmpty()) {
        if (!tfile->bakedMaterialInfo.startsWith("v1:") || tfile->materialsCount<(tfile->materialUidMapPresent?1:2) || tfile->materialsCount>256
                || !TerrainMaterialSource::capture(*tfile,0)
                || primaryName(*tfile,0)!=name+(lowTile?"_lo_procedural.ace":"_procedural.ace")) {
            error="Unsupported or invalid baked terrain material marker/palette"; return false;
        }
        return true;
    }
    if (tfile->materialUidMapPresent) {
        error="Mapped procedural terrain requires its baked material marker; local byte IDs must not be shifted";
        return false;
    }
    const int count=tfile->materialsCount;
    if (count<1 || count>=256) {
        error="Procedural conversion needs one reserved bake slot and at most 255 source shaders"; return false;
    }
    for (int i=0;i<count;++i) if (!TerrainMaterialSource::capture(*tfile,i)) {
        error="Cannot convert an incomplete terrain shader palette"; return false;
    }
    if (procedural && procedural->map.valid()) {
        for (int id : procedural->map.usedIds()) if (id>=count || id==255) {
            error="Cannot migrate an invalid/out-of-range procedural source ID"; return false;
        }
    }
    const int bake=tfile->newMat();
    auto normal=tfile->materials[bake], auxiliary=tfile->amaterials[bake];
    *normal.tex[0]=*auxiliary.tex[0]=name+(lowTile?"_lo_procedural.ace":"_procedural.ace");
    const float detail=ProceduralDetailScale*gridLayout.patchesPerSide;
    memcpy(&normal.itex[1][3],&detail,sizeof(detail));
    for (int i=count-1;i>=0;--i) {
        tfile->materials[i+1]=tfile->materials[i];
        tfile->amaterials[i+1]=tfile->amaterials[i];
    }
    tfile->materials[0]=normal; tfile->amaterials[0]=auxiliary;
    for (int p=0;p<gridLayout.patchRecordCount();++p) tfile->tdata[p*13+6]+=1;
    tfile->bakedMaterialInfo="v1:pending";
    if (procedural && procedural->map.valid()) {
        for (char &id : procedural->map.ids) id=char(quint8(id)+1);
        procedural->changed=true;
    }
    clearStaticTextureRefs();
    modified=true;
    return true;
}
QString Terrain::proceduralBakeSignature() const {
    if (!procedural || !procedural->map.valid()) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(procedural->map.ids);
    QByteArray settings;
    QDataStream out(&settings,QIODevice::WriteOnly);
    out << qint32(1) << TerrainMaterialMap::BakedSide << TerrainMaterialMap::OutputSide
        << TerrainMaterialMap::SamplingMode << gridLayout.patchesPerSide;
    for (int i : sourceSlots(*tfile)) {
        if (tfile->materialUidMapPresent) out << i;
        out << sourceKey(*tfile,i);
        QString path=QDir(texturepath).filePath(sourceName(*tfile,i));
        const QString dds=path.left(path.size()-3)+"dds";
        if (path.endsWith(".ace",Qt::CaseInsensitive) && QFileInfo::exists(dds)) path=dds;
        const QFileInfo source(path);
        out << source.size() << source.lastModified().toMSecsSinceEpoch();
    }
    hash.addData(settings);
    return "v1:"+QString::fromLatin1(hash.result().toHex());
}
bool Terrain::proceduralNearCamera(const PatchVisibility &visibility) const {
    if (!procedural || !procedural->bakeAvailable || procedural->changed || !visibility.valid) return true;
    // Cheap tile rejection; individual patches still test their own centers.
    const double half=double(gridLayout.terrainWorldSize)/gridLayout.patchesPerSide/2;
    const double end=gridLayout.terrainWorldSize-half;
    const double dx=visibility.cameraLocalX-std::clamp(double(visibility.cameraLocalX),half,end);
    const double dz=visibility.cameraLocalZ-std::clamp(double(visibility.cameraLocalZ),half,end);
    const double limit=std::max(0.0f,TerrainMaterialMap::DetailDistanceMeters);
    return dx*dx+dz*dz<=limit*limit;
}
QVector3D Terrain::proceduralTextureRemap(int patch, int generatedTexture) const {
    if (!rendersProceduralMaterial() || generatedTexture>=0 || !hasProceduralBake()
            || tfile->tdata[patch*13+6]!=0) return {};
    const float p=gridLayout.patchesPerSide;
    return QVector3D(1.0f/p-1.0f,(patch%gridLayout.patchesPerSide)/p,(patch/gridLayout.patchesPerSide)/p);
}
int Terrain::proceduralResidentPatchCount() const {
    if (!procedural) return 0;
    return int(std::count_if(procedural->patches.cbegin(), procedural->patches.cend(),
                            [](const MaterialPtr &p) { return bool(p); }));
}
void Terrain::releaseProceduralTextures() {
    if (!procedural) return;
    procedural->prefetchBakeKey.clear();
    procedural->prefetchBakePath.clear();
    procedural->cancelPending();
    procedural->patchKeys.clear();
    procedural->detailViewValid=false;
    procedural->patches.clear();
    procedural->cache.clear();
    procedural->bakeImage=QImage();
    procedural->sources.clear();
    if (procedural->detailTextureId >= 0) {
        TexLib::delRef(procedural->detailTextureId);
        procedural->detailTextureId = -1;
    }
    // editedPatches, map and changed survive: generated images are not edit storage.
    flushTextureDeletes();
}
bool Terrain::proceduralToolAllowed() const {
    if (!usesProceduralMaterial()) return true;
    reportToolError("This static texture/UV tool is disabled on procedural terrain. Use Texture painting or switch the tile to static textures first.");
    return false;
}
void Terrain::loadProceduralMaterial(const QString &directory) {
    ++proceduralUndoEpoch;
    procedural.reset();
    if (!TerrainMaterialMap::Enabled || !usesProceduralMaterial()) return;
    procedural = std::make_shared<TerrainProceduralState>();
    proceduralStates.push_back(procedural);
    const QString ref = tfile->sampleMaterialBuffer;
    if (QFileInfo(ref).fileName() != ref || ref.contains('\\') || ref.contains(':'))
        procedural->error = "Unsafe procedural material bitmap reference";
    else if (TerrainMaterialMap::Side % gridLayout.patchesPerSide != 0)
        procedural->error = "Procedural bitmap is not divisible by this patch count";
    else if (procedural->map.read(QDir(directory).filePath(ref),procedural->error)) {
        procedural->ready = reserveProceduralBake(procedural->error);
        procedural->libraryCanRecover=procedural->ready;
        procedural->sourceIds = procedural->map.usedIds();
        for (int id : procedural->sourceIds) {
            if (!procedural->ready) break;
            if (id==0 && !tfile->materialUidMapPresent) { procedural->error="Reserved bake cannot be a procedural source"; procedural->ready=false; break; }
            if (!procedural->ensureSource(*tfile,id,texturepath)) { procedural->ready = false; break; }
        }
        procedural->bakeAvailable=tfile->bakedMaterialInfo!="v1:pending"
                && QFileInfo::exists(QDir(texturepath).filePath(primaryName(*tfile,0)));
        procedural->bakeCurrent=procedural->bakeAvailable
                && (!TerrainMaterialMap::ValidateBakeOnLoad
                    || storedBakeValidation(tfile->bakedMaterialInfo)==proceduralBakeSignature());
        procedural->bakeSettings=storedBakeSettings(tfile->bakedMaterialInfo);
        procedural->fullBakeRequired=!procedural->bakeCurrent || procedural->bakeSettings.isEmpty()
                || procedural->bakeSettings!=bakeSettingsKey(*tfile,texturepath,gridLayout.patchesPerSide);
        if (procedural->ready && procedural->bakeAvailable && !procedural->bakeCurrent)
            qWarning() << name << "Bake validation mismatch; retaining saved fallback and marking tile for rebake";
        if (procedural->ready && !procedural->bakeCurrent) modified=true;
        if (procedural->ready && procedural->bakeAvailable) {
            procedural->prefetchBakeKey="B:"+tfile->bakedMaterialInfo.toUtf8();
            procedural->prefetchBakePath=QDir(texturepath).filePath(primaryName(*tfile,0));
            procedural->requestBake(); // CPU only: tile loading need not own a GL context.
        }
    }
    if (!procedural->ready) qWarning() << name << procedural->error << "Procedural painting/save refused; static fallback retained";
    procedural->rememberLibrary(*tfile);
}
bool Terrain::setProceduralMaterial(bool enabled, QString &error, quint32 materialUid) {
    if (!TerrainMaterialMap::Enabled) { error="Procedural materials are disabled in settings. Enable them and restart TSRE first."; return false; }
    if (!Game::writeEnabled || !editable || Game::serverClient) { error = "Terrain is not writable/editable in this session"; return false; }
    if (enabled == usesProceduralMaterial()) return true;
    if (enabled) {
        if (Game::seasonalEditing && !Game::season.isEmpty()) { error="Procedural conversion/baking currently supports base-season editing only"; return false; }
        if (TerrainMaterialMap::Side % gridLayout.patchesPerSide != 0) { error = "Bitmap size must divide evenly into patches"; return false; }
        // Save outstanding static paint first, so switching cannot redirect ACE writes.
        for (int i=0; i<gridLayout.patchRecordCount(); ++i) if (texModified[i]) {
            error = "Save the tile's static texture edits before enabling procedural materials"; return false;
        }
        auto state = std::make_shared<TerrainProceduralState>();
        const int first=tfile->bakedMaterialInfo.isEmpty()?0:1;
        if (materialUid) {
            const auto definition=TerrainMaterialLibrary::current()->find(materialUid);
            QImage source;
            if (!definition) { error="Selected route material UiD does not exist"; return false; }
            if (!loadSource(texturepath,definition->texture,source,error)) return false;
            state->sources.insert(first,source);
        } else if (tfile->materialUidMapPresent) {
            error="Choose a route material before enabling procedural terrain"; return false;
        } else if (!state->ensureSource(*tfile,first,texturepath)) { error = state->error; return false; }
        if (!Undo::PushTerrainMaterial(this)) { error="Cannot capture procedural undo state"; return false; }
        if (materialUid) {
            // No source-palette migration or spare local shader slot is needed.
            // The old palette is already captured by undo; keep only a fresh
            // conventional baked shader pair in the descriptor.
            const int slot=tfile->newMat();
            *tfile->materials[slot].tex[0]=*tfile->amaterials[slot].tex[0]=name+(lowTile?"_lo_procedural.ace":"_procedural.ace");
            const float detail=ProceduralDetailScale*gridLayout.patchesPerSide;
            memcpy(&tfile->materials[slot].itex[1][3],&detail,sizeof(detail));
            const auto bake=TerrainMaterialSource::capture(*tfile,slot);
            TerrainMaterialSource::restorePalette(*tfile,{bake});
            if (!tfile->bakedMaterialInfo.startsWith("v1:")) tfile->bakedMaterialInfo="v1:pending";
            tfile->materialUidMapPresent=tfile->materialUidMapValid=true;
            tfile->materialUids={{1,materialUid}};
            for (int p=0;p<gridLayout.patchRecordCount();++p) tfile->tdata[p*13+6]=0;
            clearStaticTextureRefs();
        } else if (!reserveProceduralBake(error)) return false;
        state->sources.insert(1,state->sources.take(first));
        state->map.initialize(1); state->ready=true; state->changed=true;
        state->sourceIds.insert(1);
        state->libraryCanRecover=true;
        state->rememberLibrary(*tfile);
        state->bakeAvailable=tfile->bakedMaterialInfo!="v1:pending"
                && QFileInfo::exists(QDir(texturepath).filePath(primaryName(*tfile,0)));
        procedural = state;
        proceduralStates.push_back(procedural);
        tfile->sampleMaterialBuffer = name + "_materials.pmap";
    } else {
        if (!procedural || !procedural->bakeCurrent || procedural->changed
                || !QFileInfo::exists(QDir(texturepath).filePath(primaryName(*tfile,0)))) {
            error="Save the current procedural tile successfully before switching to static textures"; return false;
        }
        if (!Undo::PushTerrainMaterial(this)) { error="Cannot capture procedural undo state"; return false; }
        tfile->sampleMaterialBuffer.clear();
        procedural.reset();
        clearStaticTextureRefs();
    }
    modified = true;
    invalidateAll(TerrainDirtyUvParams);
    refreshModified();
    return true;
}
QVector<int> Terrain::proceduralRequestOrder(const PatchVisibility &visibility) const {
    QVector<int> order;
    if (!rendersProceduralMaterial()) return order;
    struct Candidate { int patch; double distance; };
    QVector<Candidate> candidates;
    const bool hasContext=QOpenGLContext::currentContext()!=nullptr;
    const double patchSize=gridLayout.terrainWorldSize/gridLayout.patchesPerSide;
    for(int patch=0;patch<gridLayout.patchRecordCount();++patch) {
        if (hidden[patch] || (tfile->flags[patch]&1) || !isPatchVisible(patch,visibility)) continue;
        if (!procedural->detailedPatch(patch,gridLayout,visibility.cameraLocalX,visibility.cameraLocalZ,visibility.valid)) continue;
        if (patch<procedural->patches.size() && procedural->patches[patch]) {
            const auto texture=TexLib::mtex.find(procedural->patches[patch]->textureId);
            if (texture!=TexLib::mtex.end() && texture->second
                    && (!hasContext || texture->second->glLoaded)) continue;
        }
        const bool bounded=patch<patchBounds.size() && patchBounds[patch].valid;
        const double x=bounded ? patchBounds[patch].centerX
                               : (gridLayout.patchColumn(patch)+0.5)*patchSize;
        const double z=bounded ? -patchBounds[patch].centerZ
                               : (gridLayout.patchRow(patch)+0.5)*patchSize;
        const double dx=x-visibility.cameraLocalX, dz=z-visibility.cameraLocalZ;
        const double distance=dx*dx+dz*dz;
        candidates.push_back({patch,std::isfinite(distance) ? distance : std::numeric_limits<double>::infinity()});
    }
    std::sort(candidates.begin(),candidates.end(),[](const Candidate &a,const Candidate &b) {
        return a.distance!=b.distance ? a.distance<b.distance : a.patch<b.patch;
    });
    order.reserve(candidates.size());
    for(const Candidate &candidate : candidates) order.push_back(candidate.patch);
    return order;
}
void Terrain::prepareVisibleProceduralTextures(const PatchVisibility &visibility) {
    synchronizeMaterialLibrary();
    if (!rendersProceduralMaterial()) return;
    procedural->nearCamera=proceduralNearCamera(visibility);
    procedural->detailViewValid=visibility.valid;
    procedural->detailCameraX=visibility.cameraLocalX;
    procedural->detailCameraZ=visibility.cameraLocalZ;
    const auto order=procedural->nearCamera ? proceduralRequestOrder(visibility) : QVector<int>();
    if (procedural->nearCamera && order.isEmpty()) return;
    // Only resource requests/uploads are sorted. Restore texture state so this
    // prepass cannot invalidate GLUU/the Gather renderer's binding caches.
    auto *context=QOpenGLContext::currentContext();
    auto *f=context ? context->functions() : nullptr;
    GLint activeUnit=GL_TEXTURE0, binding=0;
    if (f) {
        f->glGetIntegerv(GL_ACTIVE_TEXTURE,&activeUnit);
        f->glActiveTexture(GL_TEXTURE0);
        f->glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
    }
    proceduralFallbackTexture(); // One bounded file job before near-image jobs.
    for(int patch : order) proceduralTexture(patch,true);
    if (f) {
        f->glBindTexture(GL_TEXTURE_2D,binding);
        f->glActiveTexture(activeUnit);
    }
}
int Terrain::proceduralTexture(int patch, bool background) {
    if (!TerrainMaterialMap::Enabled) return -1;
    flushTextureDeletes();
    if (!usesProceduralMaterial() || !procedural || !procedural->ready) return -1;
    if (patch < 0 || patch >= gridLayout.patchRecordCount()) return -1;
    if (background && !procedural->detailedPatch(patch,gridLayout,procedural->detailCameraX,
                                              procedural->detailCameraZ,procedural->detailViewValid)) return -1;
    if (procedural->patches.isEmpty())
        procedural->patches.resize(gridLayout.patchRecordCount());
    if (!procedural->patches[patch]) {
        // Sources may have been released when this tile left the resident region.
        for (int id : procedural->sourceIds)
            if (!procedural->ensureSource(*tfile,id,texturepath)) return -1;
        if (background) {
            if (procedural->patchKeys.isEmpty()) procedural->patchKeys.resize(gridLayout.patchRecordCount());
            auto &key=procedural->patchKeys[patch];
            if (key.isEmpty() && recipesThisFrame<RecipeLimit && recipeNsThisFrame<RecipeBudgetNs) {
                QElapsedTimer timer; timer.start();
                key=QByteArray(1,procedural->editedPatches.contains(patch)?'P':'S')
                        +procedural->recipe(patch,gridLayout.patchesPerSide);
                ++recipesThisFrame; recipeNsThisFrame+=timer.nsecsElapsed();
            }
            if (!key.isEmpty()) {
                const auto found=procedural->cache.constFind(key);
                if (found!=procedural->cache.constEnd()) {
                    procedural->patches[patch]=found.value();
                } else procedural->request(key,patch,gridLayout.patchesPerSide,procedural->editedPatches.contains(patch));
            }
        } else {
            procedural->patches[patch]=procedural->sharedPatch(
                patch,gridLayout.patchesPerSide,procedural->editedPatches.contains(patch));
        }
    }
    const auto &material = procedural->patches[patch];
    return uploadProceduralOutput(material,background);
}
int Terrain::proceduralFallbackTexture() {
    if (!TerrainMaterialMap::Enabled) return -1;
    if (!rendersProceduralMaterial() || !hasProceduralBake()) return -1;
    const QByteArray key="B:"+tfile->bakedMaterialInfo.toUtf8();
    if (procedural->failedRecipes.contains(key)) {
        procedural->bakeAvailable=procedural->bakeCurrent=false;
        procedural->nearCamera=true; modified=true;
        return -1; // Missing/failed file returns to generation, not permanent blank far terrain.
    }
    const auto found=procedural->cache.constFind(key);
    if (found==procedural->cache.constEnd()) {
        procedural->prefetchBakeKey=key;
        procedural->prefetchBakePath=QDir(texturepath).filePath(primaryName(*tfile,0));
        procedural->requestBake();
        return -1;
    }
    return uploadProceduralOutput(*found,true,false);
}
static int uploadProceduralOutput(const MaterialPtr &material, bool background, bool mipmaps) {
    if (!material) return -1;
    auto it = TexLib::mtex.find(material->textureId);
    if (it == TexLib::mtex.end() || !it->second) return -1;
    if (!it->second->glLoaded && QOpenGLContext::currentContext()) {
        const bool budgetedUpload=background && !material->immediateUpload;
        if (budgetedUpload && (uploadsThisFrame>=UploadLimit || uploadNsThisFrame>=UploadBudgetNs)) return -1;
        QElapsedTimer timer; timer.start();
        textureGroups[material->textureId]=QOpenGLContext::currentContext()->shareGroup();
        it->second->GLTextures(mipmaps);
        if (it->second->glLoaded) {
            auto *f=QOpenGLContext::currentContext()->functions();
            f->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            f->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }
        if (profileEnabled()) qInfo() << "Terrain material upload ms" << timer.nsecsElapsed()/1e6;
        if (budgetedUpload) { ++uploadsThisFrame; uploadNsThisFrame+=timer.nsecsElapsed(); }
        if (it->second->glLoaded) material->immediateUpload=false;
    }
    if (QOpenGLContext::currentContext() && !it->second->glLoaded) return -1;
    return material->textureId;
}
int Terrain::proceduralDetailTexture() {
    if (!TerrainMaterialMap::Enabled) return -1;
    auto *context = QOpenGLContext::currentContext();
    if (!rendersProceduralMaterial() || !context) return -1;
    // Same route/season texture lookup and mipmapped upload as static terrain.
    // Own one ordinary TexLib reference per tile, not one per generated patch.
    if (procedural->detailTextureId < 0)
        procedural->detailTextureId = TexLib::addTex(texturepath, QStringLiteral("microtex.ace"));
    auto it = TexLib::mtex.find(procedural->detailTextureId);
    if (it == TexLib::mtex.end() || !it->second || !it->second->loaded) return -1;
    Texture *texture = it->second;
    if (!texture->glLoaded) {
        auto *f = context->functions();
        GLint activeUnit, previousBinding;
        f->glGetIntegerv(GL_ACTIVE_TEXTURE, &activeUnit);
        f->glActiveTexture(GL_TEXTURE1);
        f->glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousBinding);
        texture->GLTextures(true);
        f->glBindTexture(GL_TEXTURE_2D, previousBinding);
        f->glActiveTexture(activeUnit);
    }
    return texture->glLoaded ? procedural->detailTextureId : -1;
}
int Terrain::proceduralSourceTexture(int x, int z, float posx, float posz) {
    if (!TerrainMaterialMap::Enabled) return -1;
    if (!procedural || !procedural->ready) return -1;
    getLocalCoords(x,z,posx,posz);
    const int id = procedural->map.at(int(posx*TerrainMaterialMap::Side/gridLayout.terrainWorldSize),
                                     int(posz*TerrainMaterialMap::Side/gridLayout.terrainWorldSize));
    return TexLib::addTex(texturepath,sourceName(*tfile,id));
}
void Terrain::rememberProceduralSource(Brush *brush, int x, int z, float posx, float posz) {
    if (!TerrainMaterialMap::Enabled) return;
    if (!brush || !loaded) return;
    getPatchCoords(x,z,posx,posz);
    int id = int(tfile->patchValue(z*gridLayout.patchesPerSide+x,TFile::PatchField::ShaderIndex));
    if (usesProceduralMaterial() && procedural && procedural->ready)
        id = procedural->map.at(int(posx*TerrainMaterialMap::Side/gridLayout.terrainWorldSize),int(posz*TerrainMaterialMap::Side/gridLayout.terrainWorldSize));
    brush->terrainShaderKey = shaderKey(*tfile,id);
    brush->terrainMaterialUid=usesProceduralMaterial() && tfile->materialUidMapPresent ? tfile->materialUids.value(id) : 0;
    brush->terrainMaterialRoute=TerrainMaterialLibrary::current()->path();
    brush->terrainShaderIsBake = id==0 && !tfile->bakedMaterialInfo.isEmpty();
    brush->terrainShaderSource = TerrainMaterialSource::capture(*tfile,id);
    brush->terrainShaderTextureId = brush->texId;
    brush->terrainPickedShaderId = id;
    brush->terrainShaderTile = Game::route + "/" + (lowTile ? "lo/" : "hi/") + name;
}
void Terrain::paintProceduralMaterial(Brush *brush, int x, int z, float posx, float posz,
                                      float radiusMeters, int operation) {
    if (!TerrainMaterialMap::Enabled) return;
    if (!usesProceduralMaterial()) return;
    synchronizeMaterialLibrary();
    if (operation<TerrainMaterialMap::TexturePaint || operation>TerrainMaterialMap::FloodFill) return;
    if (!Game::writeEnabled || !editable || Game::serverClient || !brush) return;
    if (!procedural || !procedural->ready) { reportToolError(procedural ? procedural->error : "Procedural bitmap unavailable"); return; }
    if (!brush->useTexture || !brush->tex) { reportToolError("Procedural terrain paints shader IDs, not colors. Select an existing terrain shader texture."); return; }
    const bool exactPick = brush->terrainShaderTextureId == brush->texId && !brush->terrainShaderKey.isEmpty();
    int id = -1;
    const bool global=tfile->materialUidMapPresent;
    const auto library=TerrainMaterialLibrary::current();
    const auto definition=library->find(brush->terrainMaterialUid);
    if (global) {
        if (!tfile->materialUidMapValid || !definition || brush->terrainMaterialRoute!=library->path()) {
            reportToolError("Choose an existing material from this route's global list first"); return;
        }
        for (auto it=tfile->materialUids.cbegin();it!=tfile->materialUids.cend();++it)
            if (it.value()==definition->uid) { id=it.key(); break; }
    } else {
    if (brush->terrainMaterialUid) {
        reportToolError("This legacy procedural tile has no UiD table. Save it, switch to static, then enable with a global material to start a new map."); return;
    }
    const QString filename = QFileInfo(brush->tex->pathid).fileName();
    if (exactPick && brush->terrainShaderIsBake) {
        reportToolError("The baked composite is not a procedural source; pick an original source shader"); return;
    }
    for (int i=1; i<std::min(256,tfile->materialsCount); ++i) {
        bool matches = exactPick ? shaderKey(*tfile,i) == brush->terrainShaderKey
                                      : primaryName(*tfile,i).compare(filename,Qt::CaseInsensitive) == 0;
        if (matches && exactPick && brush->terrainShaderSource) {
            const auto target = TerrainMaterialSource::capture(*tfile,i);
            matches = target && target->key() == brush->terrainShaderSource->key();
        }
        if (!matches) continue;
        if (id >= 0 && shaderKey(*tfile,id) != shaderKey(*tfile,i)) { reportToolError("Ambiguous source texture: pick the intended shader from terrain first"); return; }
        id=i;
    }
    if (exactPick && brush->terrainShaderTile == Game::route + "/" + (lowTile ? "lo/" : "hi/") + name
        && brush->terrainPickedShaderId >= 1 && brush->terrainPickedShaderId < 256
        && shaderKey(*tfile,brush->terrainPickedShaderId) == brush->terrainShaderKey)
        id = brush->terrainPickedShaderId;
    }
    const bool importing = id < 0;
    if (importing && global) {
        for (int slot=0;slot<256;++slot) if (!tfile->materialUids.contains(slot)) { id=slot; break; }
        if (id<0) { reportToolError("Tile material map is full (256 local IDs)"); return; }
    } else if (importing) {
        if (!exactPick || !brush->terrainShaderSource) {
            reportToolError("Pick a source shader from a terrain palette tile first; its complete definition is needed for import."); return;
        }
        if (tfile->materialsCount < 0 || tfile->materialsCount >= 256) {
            reportToolError("Procedural shader palette is full (256 entries). No existing shader was replaced."); return;
        }
        id = tfile->materialsCount;
    }
    getLocalCoords(x,z,posx,posz);
    const double scale = double(TerrainMaterialMap::Side)/gridLayout.terrainWorldSize;
    QSet<int> locked;
    for (int i=0; i<gridLayout.patchRecordCount(); ++i) if (texLocked[i]) locked.insert(i);
    auto apply=[&](bool dryRun) {
        if (operation==TerrainMaterialMap::TexturePaint)
            return procedural->map.paint(posx*scale,posz*scale,radiusMeters*scale,id,gridLayout.patchesPerSide,
                                          brush->brushshape ? *brush->brushshape : QImage(),locked,dryRun);
        if (!std::isfinite(posx) || !std::isfinite(posz) || posx<0 || posz<0
                || posx>=gridLayout.terrainWorldSize || posz>=gridLayout.terrainWorldSize)
            return QSet<int>();
        return procedural->map.fill(int(posx*scale),int(posz*scale),id,gridLayout.patchesPerSide,
                                     operation==TerrainMaterialMap::FillPatch,locked,dryRun);
    };
    const bool capture = Undo::NeedsTerrainMaterialSnapshot(this);
    if ((capture || importing) && apply(true).isEmpty()) return;
    QImage importedSource;
    if (importing && global) {
        if (!loadSource(texturepath,definition->texture,importedSource,procedural->error)) {
            reportToolError(procedural->error); return;
        }
    } else if (importing) {
        // Import only for a real, unlocked, nonwhite paint stamp. Existing IDs
        // and cached outputs remain stable because the shader pair is appended.
        if (tfile->materials.count(id) || tfile->amaterials.count(id)) {
            reportToolError("Cannot append to an inconsistent terrain shader table"); return;
        }
        if (!loadSource(texturepath,brush->terrainShaderSource->normal.textures[0],importedSource,procedural->error)) {
            reportToolError(procedural->error); return;
        }
    } else if (!procedural->ensureSource(*tfile,id,texturepath)) {
        reportToolError(procedural->error); return;
    }
    // A retained editable tile may have released its source-image cache.
    // Synchronous painting needs every existing source before changing IDs.
    for (int sourceId : procedural->sourceIds) {
        if (!procedural->ensureSource(*tfile,sourceId,texturepath)) {
            reportToolError(procedural->error); return;
        }
    }
    // Capture after validation, before palette/ID mutation, once per action.
    if (capture && !Undo::PushTerrainMaterial(this)) {
        reportToolError("Cannot capture procedural undo state"); return;
    }
    if (importing) {
        if (global) tfile->materialUids.insert(id,definition->uid);
        else id = brush->terrainShaderSource->appendTo(*tfile,procedural->error);
        if (id < 0) { reportToolError(procedural->error); return; }
        procedural->sources.insert(id,importedSource);
    }
    const auto changed = apply(false);
    if (changed.isEmpty()) return;
    procedural->cancelPending(false); // Edits replace only their own miniature requests.
    procedural->sourceIds.insert(id);
    procedural->rememberLibrary(*tfile);
    procedural->changed = true;
    procedural->bakeCurrent = false;
    modified = true;
    if (procedural->patches.isEmpty()) procedural->patches.resize(gridLayout.patchRecordCount());
    QElapsedTimer timer; timer.start();
    for (int patch : changed) {
        procedural->editedPatches.insert(patch);
        if (!procedural->patchKeys.isEmpty()) procedural->patchKeys[patch].clear();
        if (!procedural->recipeKeys.isEmpty()) procedural->recipeKeys[patch].clear();
        procedural->patches[patch]=procedural->sharedPatch(patch,gridLayout.patchesPerSide,true);
        if (procedural->patches[patch]) {
            procedural->patches[patch]->immediateUpload=true;
            // Upload immediately if the edit already owns a GL context; otherwise
            // the next draw uploads this result without the two-upload frame cap.
            if (QOpenGLContext::currentContext()) proceduralTexture(patch);
        }
    }
    procedural->pruneCache();
    if (profileEnabled()) qInfo() << "Terrain material synchronously painted patches" << changed.size()
                                << "generation+upload ms" << timer.nsecsElapsed()/1e6;
}
QHash<QByteArray,QImage> Terrain::proceduralBakeMiniatures() const {
    QHash<QByteArray,QImage> miniatures;
    if (!procedural || gridLayout.patchesPerSide<=0
            || TerrainMaterialMap::BakedSide%gridLayout.patchesPerSide) return miniatures;
    const int side=TerrainMaterialMap::BakedSide/gridLayout.patchesPerSide;
    for (auto it=procedural->cache.constBegin();it!=procedural->cache.constEnd();++it) {
        if ((it.key().startsWith('S') || it.key().startsWith('P'))
                && it.value()->miniature.size()==QSize(side,side))
            miniatures.insert(it.key().mid(1),it.value()->miniature);
    }
    return miniatures;
}
bool Terrain::saveProceduralBake() {
    if (!TerrainMaterialMap::Enabled) return true;
    if (!usesProceduralMaterial()) return true;
    synchronizeMaterialLibrary();
    if (!procedural || !procedural->ready) return false;
    if (QFileInfo(primaryName(*tfile,0)).fileName()!=primaryName(*tfile,0)
            || primaryName(*tfile,0).contains('\\') || primaryName(*tfile,0).contains(':')) {
        qWarning() << "Refusing unsafe terrain bake filename"; return false;
    }
    if (Game::seasonalEditing && !Game::season.isEmpty()) {
        qWarning() << "Procedural baking supports base-season editing only"; return false;
    }
    if (!procedural->savedBakePath.isEmpty() || !procedural->savedMapPath.isEmpty()) {
        qWarning() << "Previous procedural save needs recovery before another save"; return false;
    }
    const QString settings=bakeSettingsKey(*tfile,texturepath,gridLayout.patchesPerSide);
    const QString signature=TerrainMaterialMap::ValidateBakeOnLoad ? proceduralBakeSignature() : QString();
    const QString target=QDir(texturepath).filePath(primaryName(*tfile,0));
    bool fullBake=procedural->fullBakeRequired || settings!=procedural->bakeSettings
            || !procedural->bakeAvailable || !QFileInfo(target).isFile();
    if (TerrainMaterialMap::ValidateBakeOnLoad && storedBakeValidation(tfile->bakedMaterialInfo)!=signature)
        fullBake=true;
    if (!procedural->changed && !fullBake) {
        procedural->bakeAvailable=procedural->bakeCurrent=true;
        return true;
    }
    // Check current source pixels too when writing a bake. No full ID-map hash
    // is needed: tracked patch edits and compact source/settings metadata suffice.
    // Decode only used sources, with no dependency on resident output or GL.
    QHash<int,QImage> bakeSources;
    const auto used=procedural->map.usedIds();
    bool sourceChanged=false;
    for (int id : used) {
        QImage image;
        if (sourceName(*tfile,id).isEmpty() || !loadSource(texturepath,sourceName(*tfile,id),image,procedural->error)) {
            qWarning() << procedural->error; return false;
        }
        sourceChanged |= procedural->sources.contains(id) && procedural->sources[id]!=image;
        bakeSources.insert(id,image);
    }
    if (sourceChanged) {
        releaseProceduralTextures();
        fullBake=procedural->fullBakeRequired=true;
        procedural->bakeCurrent=false; modified=true;
    }
    procedural->sources=std::move(bakeSources); procedural->sourceIds=used;
    procedural->collectCompleted();
    procedural->finishMiniatures();
    const auto miniatures=proceduralBakeMiniatures();
    if (profileEnabled()) qInfo() << "Terrain bake reusable miniature recipes" << miniatures.size();
    QElapsedTimer timer; timer.start();
    // Saving already waits for the bake. Assemble it (and generate/reduce any
    // cache misses) in the same four-worker pool, never in the render/upload path.
    struct BakeResult {
        QImage image;
        QVector<QByteArray> keys;
        AceDocument document;
        QString error;
    };
    auto bakeJob=std::make_shared<std::packaged_task<BakeResult()>>(
            [map=procedural->map, sources=procedural->sources, miniatures,
             keys=procedural->recipeKeys, previous=procedural->bakeImage,
             dirty=procedural->editedPatches, fullBake, target,
             count=gridLayout.patchesPerSide]() mutable {
                QImage base=fullBake ? QImage() : previous;
                if (!fullBake && base.isNull()) base=loadBakeImage(target);
                if (profileEnabled()) qInfo() << "Terrain bake mode" << (base.isNull()?"full":"incremental")
                                             << "dirty patches" << dirty.size();
                BakeResult output;
                output.image=map.bake(count,sources,miniatures,base,dirty,&keys);
                output.keys=std::move(keys);
                if (output.image.isNull()) return output;
                const QImage rgb=output.image.convertToFormat(QImage::Format_RGB888);
                AceWriteOptions options; options.encoding=AceEncoding::Dxt1;
                if (!AceDocument::fromPixels(rgb.constBits(),rgb.sizeInBytes(),rgb.width(),rgb.height(),
                                             3,options,output.document,output.error)) return output;
                // Patch miniatures are 4x4-block aligned. Preserve old compressed
                // blocks outside edited patches/halos, avoiding lossy recompression
                // drift after the CPU bake has been evicted and decoded from disk.
                AceDocument previousDocument;
                QString readError;
                if (!base.isNull() && count>0 && rgb.width()%count==0 && (rgb.width()/count)%4==0
                        && AceDocument::read(target,previousDocument,readError)
                        && previousDocument.surface()==18 && !previousDocument.hasAlpha()
                        && previousDocument.levels.size()==1 && previousDocument.levels[0].raw
                        && previousDocument.levels[0].width==rgb.width()
                        && previousDocument.levels[0].height==rgb.height()
                        && previousDocument.levels[0].data.size()==output.document.levels[0].data.size()) {
                    const int blocks=rgb.width()/4, patchSide=rgb.width()/count;
                    auto &encoded=output.document.levels[0].data;
                    const auto &old=previousDocument.levels[0].data;
                    for (int z=0;z<rgb.height()/4;++z) for (int x=0;x<blocks;++x)
                        if (!dirty.contains((z*4/patchSide)*count+x*4/patchSide))
                            memcpy(encoded.data()+(z*blocks+x)*8,old.constData()+(z*blocks+x)*8,8);
                }
                return output;
             });
    auto result=bakeJob->get_future();
    materialPool().start(QRunnable::create([bakeJob] { (*bakeJob)(); }),1);
    QImage image;
    AceDocument bakeDocument;
    try {
        auto output=result.get();
        if (!output.error.isEmpty()) { qWarning() << output.error; return false; }
        image=std::move(output.image);
        bakeDocument=std::move(output.document);
        procedural->recipeKeys=std::move(output.keys);
    }
    catch (...) { qWarning() << "Cannot generate terrain bake" << name; return false; }
    if (image.isNull()) { qWarning() << "Cannot synthesize terrain bake" << name; return false; }
    const QString backup=target+".bk";
    const bool existed=QFileInfo::exists(target);
    if (existed && (!QFileInfo(target).isFile()
            || (QFile::exists(backup) && !QFile::remove(backup)) || !QFile::copy(target,backup))) {
        qWarning() << "Cannot back up terrain bake" << target; return false;
    }
    if (!bakeDocument.write(target,false,procedural->error)) {
        qWarning() << procedural->error; return false; // QSaveFile left old ACE intact.
    }
    procedural->savedBakePath=target; procedural->backupBakePath=backup;
    procedural->bakeWritten=true; procedural->bakeExisted=existed;
    procedural->previousBakeInfo=tfile->bakedMaterialInfo;
    procedural->pendingBakeImage=image;
    procedural->pendingBakeSettings=settings;
    procedural->previousPatchData=QVector<float>(tfile->tdata,tfile->tdata+gridLayout.patchRecordCount()*13);
    for (int patch=0;patch<gridLayout.patchRecordCount();++patch) {
        float *d=tfile->tdata+patch*13;
        d[6]=0; d[7]=float(patch%gridLayout.patchesPerSide)/gridLayout.patchesPerSide;
        d[8]=float(patch/gridLayout.patchesPerSide)/gridLayout.patchesPerSide;
        d[9]=d[12]=1.0f/gridLayout.sampleCount; d[10]=d[11]=0;
    }
    // Preserve v1 palette semantics, but never present a stale validation hash
    // as current. Unchecked saves get a cheap unique output revision instead.
    tfile->bakedMaterialInfo=TerrainMaterialMap::ValidateBakeOnLoad
            ? "v1:checked:"+settings+":"+signature.mid(3)
            : "v1:unchecked:"+settings+":"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (profileEnabled()) qInfo() << "Terrain bake synthesis+DXT1 ACE save ms" << timer.nsecsElapsed()/1e6;
    return true;
}
bool Terrain::saveProceduralMap(const QString &directory, QString &previousReference) {
    previousReference = tfile->sampleMaterialBuffer;
    if (!TerrainMaterialMap::Enabled) return true;
    if (!usesProceduralMaterial()) return true;
    if (!procedural || !procedural->ready) { qWarning() << "Refusing to save unavailable procedural map" << name; return false; }
    if (!procedural->savedMapPath.isEmpty() && !procedural->rollbackMapSave()) {
        qWarning() << "Cannot restore procedural map; refusing to overwrite its backup" << procedural->backupMapPath;
        return false;
    }
    const QString ref = name + "_materials.pmap";
    if (!procedural->changed && previousReference == ref && QFileInfo::exists(QDir(directory).filePath(ref))) return true;
    const QString target = QDir(directory).filePath(ref);
    const QString backup = target + ".bk";
    const QString previous = QDir(directory).filePath(previousReference);
    // Also migrate the previously referenced hash-named map on the next edit/save.
    const QString old = QFile::exists(previous) ? previous
            : (QFile::exists(target) ? target : QString());
    if (old != target && QFileInfo(target).isFile()) {
        qWarning() << "Refusing to overwrite a different existing procedural map" << target;
        return false;
    }
    if (!old.isEmpty()) {
        if (!QFileInfo(old).isFile()
                || (QFile::exists(backup) && !QFile::remove(backup))
                || !QFile::rename(old, backup)) {
            qWarning() << "Cannot rotate procedural map backup" << old << backup;
            return false;
        }
    }
    procedural->savedMapPath = target;
    procedural->oldMapPath = old;
    procedural->backupMapPath = backup;
    if (!procedural->map.write(target,procedural->error)) {
        qWarning() << procedural->error;
        proceduralSaveFailed();
        return false;
    }
    procedural->mapWritten = true;
    tfile->sampleMaterialBuffer = ref;
    return true;
}
void Terrain::proceduralSaveFailed() {
    if (procedural && !procedural->savedMapPath.isEmpty() && !procedural->rollbackMapSave())
        qWarning() << "Procedural map rollback failed; recover from" << procedural->backupMapPath;
    if (!procedural || procedural->savedBakePath.isEmpty()) return;
    bool restored=true;
    if (procedural->bakeWritten) {
        restored=!QFile::exists(procedural->savedBakePath) || QFile::remove(procedural->savedBakePath);
        if (restored && procedural->bakeExisted)
            restored=QFile::copy(procedural->backupBakePath,procedural->savedBakePath);
    }
    if (!procedural->previousPatchData.isEmpty()) {
        std::copy(procedural->previousPatchData.cbegin(),procedural->previousPatchData.cend(),tfile->tdata);
        tfile->bakedMaterialInfo=procedural->previousBakeInfo;
    }
    if (!restored) { qWarning() << "Terrain bake rollback failed; recover from" << procedural->backupBakePath; return; }
    procedural->savedBakePath.clear(); procedural->backupBakePath.clear();
    procedural->pendingBakeImage=QImage(); procedural->pendingBakeSettings.clear();
    procedural->previousPatchData.clear(); procedural->bakeWritten=false;
}
void Terrain::proceduralSaveCompleted() {
    if (!procedural || !procedural->ready) return;
    // A successful save can replace the stable file. Never retry its old recipe.
    procedural->prefetchBakeKey.clear();
    procedural->prefetchBakePath.clear();
    if (!procedural->savedBakePath.isEmpty()) {
        // Never destroy/mutate an asynchronously loading ordinary Texture. Retire
        // its lookup key; existing users keep their object, new requests load the
        // newly committed file. This is deliberately not a general TexLib rewrite.
        const QString path=QDir::cleanPath(procedural->savedBakePath).replace('\\','/');
        for (auto &entry : TexLib::mtex) if (entry.second) {
            auto &keys=entry.second->hashid; // UI-owned lookup aliases, not loader-owned path/pixels.
            keys.erase(std::remove_if(keys.begin(),keys.end(),[&](const QString &key) {
                return QDir::cleanPath(key).replace('\\','/').compare(path,Qt::CaseInsensitive)==0;
            }),keys.end());
        }
        clearStaticTextureRefs();
        procedural->savedBakePath.clear(); procedural->backupBakePath.clear();
        for (int patch=0;patch<gridLayout.patchRecordCount();++patch) uniqueTex[patch]=false;
        for (auto it=procedural->cache.begin();it!=procedural->cache.end();) {
            if (it.key().startsWith("B:")) it=procedural->cache.erase(it); else ++it;
        }
        procedural->previousPatchData.clear(); procedural->bakeWritten=false;
        procedural->bakeAvailable=procedural->bakeCurrent=true;
        procedural->bakeImage=std::move(procedural->pendingBakeImage);
        procedural->bakeSettings=std::move(procedural->pendingBakeSettings);
        procedural->fullBakeRequired=false;
    }
    procedural->cancelPending(false);
    procedural->finishMiniatures();
    procedural->patchKeys.clear();
    procedural->savedMapPath.clear();
    procedural->oldMapPath.clear();
    procedural->backupMapPath.clear();
    procedural->mapWritten = false;
    if (!procedural->patches.isEmpty()) {
        for (int i=0; i<procedural->patches.size(); ++i) {
            auto &material=procedural->patches[i];
            if (!material || !material->privateEdit) continue;
            material=shareMaterial(material);
            if (!material->privateEdit)
                procedural->cache.insert("S"+procedural->recipe(i,gridLayout.patchesPerSide),material);
        }
        for (auto it=procedural->cache.begin();it!=procedural->cache.end();) {
            if (it.key().startsWith('P')) it=procedural->cache.erase(it); else ++it;
        }
        // Drop obsolete immutable entries after deduplication.
        procedural->pruneCache();
    }
    procedural->editedPatches.clear();
    procedural->changed=false;
}
