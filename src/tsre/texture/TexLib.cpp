/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/AceLib.h>
#include <tsre/texture/DdsLib.h>
#include <tsre/texture/ImageLib.h>
#include <tsre/texture/PaintTexLib.h>
#include <tsre/texture/MapLib.h>
#include <tsre/texture/Texture.h>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMutex>
#include <QImageReader>
#include <QtEndian>
#include <miniz/miniz.h>
#include <QOpenGLShaderProgram>
#include <QThreadPool>
#include <settings/SettingsAccess.h>
#include <tsre/Game.h>
#include <tsre/renderer/rhi/RhiTextures.h>
#include <algorithm>
#include <cstring>
#include <vector>

int TexLib::jesttextur = 0;
std::unordered_map<int, Texture*> TexLib::mtex;
QHash<int, int> TexLib::disabledTextures;

namespace {
bool textureIdentityMatches(const QString &key, const Texture &texture) {
    // Retired aliases stay retired. Identity is logical, never a filesystem probe.
    if(!texture.hashid.contains(key)) return false;
    // An ACE alias on a DDS asset is a fallback, not an existing ACE representation.
    return ContentPath::synthetic(key) || !texture.pathid.endsWith(".dds", Qt::CaseInsensitive)
            || key.endsWith(".dds", Qt::CaseInsensitive);
}
// The texture of a key: a usable one; with failed, else one that failed to
// load. A failure is remembered: asked for again, it is shared, not loaded
// again (a new entry and load each time before), until a reload tries it.
int findTexture(const QString &key, bool failed = false) {
    int failedId = -1;
    for(const auto &entry : TexLib::mtex) {
        const auto *texture = entry.second;
        if(!texture)
            continue;
        const bool usable = !texture->missing && !texture->error;
        if(!usable && (!failed || failedId >= 0))
            continue;
        if(!textureIdentityMatches(key, *texture))
            continue;
        if(usable)
            return entry.first;
        failedId = entry.first;
    }
    return failedId;
}

// Texture files load on a few pool threads, not a QThread each: a route asks
// for thousands, and every thread object started was kept. File textures
// are never deleted (delRef only drops them from mtex), so a queued load's
// texture outlives it. A load still queued when a newer one of the same
// texture is asked for (a reload, run at once) is skipped: it would write
// the texture while the render thread uploads it.
QMutex loadMutex;
QHash<Texture *, quint64> newestLoad;
quint64 loadRequests = 0;

QThreadPool &loaderPool() {
    static QThreadPool *pool = [] {
        QCoreApplication *application = QCoreApplication::instance();
        // Owned by the application, whose end waits for the running loads;
        // the queued ones are dropped when it quits.
        auto *created = new QThreadPool(application);
        // TSRE_TEXTURE_LOADERS sets the count (measurements).
        const int loaders = qEnvironmentVariableIntValue("TSRE_TEXTURE_LOADERS");
        created->setMaxThreadCount(loaders > 0 ? loaders : std::max(2, QThread::idealThreadCount() - 1));
        if(application)
            QObject::connect(application, &QCoreApplication::aboutToQuit, created, &QThreadPool::clear);
        return created;
    }();
    return *pool;
}

template<class Loader>
void loadTexture(Texture *texture, bool threaded) {
    quint64 request = 0;
    {
        QMutexLocker lock(&loadMutex);
        request = ++loadRequests;
        newestLoad[texture] = request;
    }
    auto load = [texture, request] {
        {
            QMutexLocker lock(&loadMutex);
            const auto found = newestLoad.constFind(texture);
            if(found == newestLoad.cend() || found.value() != request)
                return;
            newestLoad.remove(texture);
        }
        Loader loader;
        loader.texture = texture;
        loader.run();
    };
    if(threaded)
        loaderPool().start(load);
    else
        load();
}

// Residency: TexLib's clock (milliseconds, set once a frame), the last
// pass, the budget, and the textures released while holding a GPU handle.
constexpr qint64 ResidencyPassMs = 500;
constexpr qint64 ResidencyMinAgeMs = 30000;
constexpr int ResidencyMaxPerPass = 512;
QElapsedTimer residencyClock;
qint64 residencyNow = 0;
qint64 lastResidencyPass = -ResidencyPassMs;
qint64 budgetOverride = -1;
qint64 budgetFromSettings = -1;
qint64 gpuMemory = 0;
bool gpuDiscrete = false;
QHash<unsigned int, Texture *> evictedHandles;
// Releases since the last log line (one at most every 10 s).
constexpr qint64 ResidencyLogMs = 10000;
qint64 lastResidencyLog = -ResidencyLogMs;
int releasedSinceLog = 0;
qint64 releasedBytesSinceLog = 0;

qint64 installedMemory();

// What a texture holds in memory: its GPU texture once uploaded, its CPU
// content while it waits for its first draw.
qint64 residentBytes(const Texture &texture) {
    if (texture.glLoaded)
        return texture.estimatedVramBytes();
    return texture.loaded ? texture.estimatedCpuBytes() : 0;
}

// Only what loads back the same from its file: never edited (reloadable
// goes with any change of its pixels), cloned, generated or embedded content.
// Texture::editable only says its CPU pixels are there (loaders set it).
bool residencyCandidate(const Texture &texture) {
    return texture.reloadable && !texture.released && !texture.missing && !texture.error
            && texture.aceDocument == nullptr && (texture.glLoaded || texture.loaded);
}

// When it was last drawn, or asked for if it has not been yet.
qint64 lastUse(const Texture &texture) {
    if (texture.glLoaded && texture.tex != nullptr && texture.tex[0] != 0)
        return std::max(texture.requested, RhiTextures::lastUsed(texture.tex[0]));
    return texture.requested;
}

void releaseResident(Texture &texture) {
    const unsigned int handle = texture.glLoaded && texture.tex != nullptr ? texture.tex[0] : 0;
    texture.releaseContent(true);
    texture.released = true;
    if (handle != 0)
        evictedHandles.insert(handle, &texture);
}

// The loader of a texture file's type, false for none. Reloads run at once.
bool fileType(const QString &type) {
    return type == "ace" || type == "dds" || type == "png" || type == "bmp" || type == "jpg"
            || type == "jpeg" || type == "tga";
}

bool hasLoader(const QString &type) {
    return fileType(type) || type == ":painttex" || type == ":maptex";
}

void startLoad(Texture *texture, bool reload, TexLib::Loading loading = TexLib::Loading::Setting) {
    const QString type = QFileInfo(texture->pathid).suffix().toLower();
    // Loaded from its file: residency may release it and load it again.
    texture->reloadable = fileType(type);
    // On a loader thread when asked, or as the setting says (`setting`: the
    // loader's switch, which follows it); a reload loads at once.
    auto threaded = [&](bool setting) {
        return !reload && (loading == TexLib::Loading::Background
                           || (loading == TexLib::Loading::Setting && setting));
    };
    if(type == "ace"){
        loadTexture<AceLib>(texture, threaded(AceLib::IsThread));
    } else if(type == "dds"){
        loadTexture<DdsLib>(texture, threaded(DdsLib::IsThread));
    } else if(type == "png"||type == "bmp"||type == "jpg"||type == "jpeg"/*||type == "dds"*/||type == "tga"){
        loadTexture<ImageLib>(texture, threaded(ImageLib::IsThread));
    } else if(type == ":painttex"){
        loadTexture<PaintTexLib>(texture, false);
    } else if(type == ":maptex"){
        MapLib* t = new MapLib();
        t->texture = texture;
        t->start();
        if(reload) {
            t->wait();
            delete t;
        } else {
            QObject::connect(t,&QThread::finished,t,&QObject::deleteLater);
        }
    }
}
}

void Texture::readMipmapSettings() {
    shapeMipmaps.store(Settings::boolean("core.rendering.shapeTextures.mipmaps"), std::memory_order_relaxed);
    const QString source = Settings::string("core.rendering.shapeTextures.mipmapSource", SettingType::Enum);
    mipmapSource.store(int(source == "file" ? MipmapSource::File
                           : source == "regenerate" ? MipmapSource::Regenerate : MipmapSource::Improve),
                       std::memory_order_relaxed);
    lodBias = float(Settings::floating("core.rendering.textureLodBias"));
}

void TexLib::reset() {
    jesttextur = 0;
    mtex.clear();
}

void TexLib::dumpStats(QString label) {
    qint64 cpuPixelsBytes = 0;
    qint64 cpuEncodedBytes = 0;
    qint64 estimatedVramBytes = 0;
    int totalTextures = 0;
    int loadedTextures = 0;
    int glLoadedTextures = 0;
    int glCompressedTextures = 0;
    int editableTextures = 0;
    int missingTextures = 0;
    int errorTextures = 0;

    for (auto it = mtex.begin(); it != mtex.end(); ++it) {
        Texture* t = it->second;
        if (t == nullptr) {
            continue;
        }

        totalTextures++;
        if (t->loaded) loadedTextures++;
        if (t->glLoaded) glLoadedTextures++;
        if (t->editable) editableTextures++;
        if (t->missing) missingTextures++;
        if (t->error) errorTextures++;

        if (t->imageData != nullptr) {
            if (t->imageSize > 0) {
                cpuPixelsBytes += t->imageSize;
            } else if (t->width > 0 && t->height > 0 && t->bytesPerPixel > 0) {
                cpuPixelsBytes += qint64(t->width) * qint64(t->height) * qint64(t->bytesPerPixel);
            }
        }

        cpuEncodedBytes += t->estimatedCpuBytes() - (t->imageData ? qint64(t->width)*t->height*t->bytesPerPixel : 0);

        if (t->glLoaded) {
            estimatedVramBytes += t->estimatedVramBytes();
            if (t->gpuIsCompressed()) {
                glCompressedTextures++;
            }
        }
    }

    const double cpuPixelsMB = double(cpuPixelsBytes) / (1024.0 * 1024.0);
    const double cpuEncodedMB = double(cpuEncodedBytes) / (1024.0 * 1024.0);
    const double vramMB = double(estimatedVramBytes) / (1024.0 * 1024.0);

    if (!label.isEmpty()) {
        qDebug() << "TexLib stats (" << label << "):"
                 << "total:" << totalTextures
                 << "loaded:" << loadedTextures
                 << "gl:" << glLoadedTextures
                 << "glCompressed:" << glCompressedTextures
                 << "editable:" << editableTextures
                 << "missing:" << missingTextures
                 << "error:" << errorTextures
                 << "cpuPixelsMB:" << cpuPixelsMB
                 << "cpuEncodedMB:" << cpuEncodedMB
                 << "vramMB(est):" << vramMB;
    } else {
        qDebug() << "TexLib stats:"
                 << "total:" << totalTextures
                 << "loaded:" << loadedTextures
                 << "gl:" << glLoadedTextures
                 << "glCompressed:" << glCompressedTextures
                 << "editable:" << editableTextures
                 << "missing:" << missingTextures
                 << "error:" << errorTextures
                 << "cpuPixelsMB:" << cpuPixelsMB
                 << "cpuEncodedMB:" << cpuEncodedMB
                 << "vramMB(est):" << vramMB;
    }
}

void TexLib::enableTexture(int id){
    Texture* tex = mtex[id];
    if(tex != NULL)
        disabledTextures[tex->tex[0]] = 0;
}

void TexLib::disableTexture(int id){
    Texture* tex = mtex[id];
    if(tex != NULL)
        disabledTextures[tex->tex[0]] = 1;
}

void TexLib::delRef(int texx) {
    try {
        Texture* t = mtex.at(texx);
        t->ref--;
        if (t->ref <= 0) {
            //System.out.println("--refs: "+mtex.get(texx).ref);
            if (t->glLoaded) {
                // Its memory goes with it; the object stays (code may hold it).
                if (t->tex != nullptr)
                    evictedHandles.remove(t->tex[0]);
                t->releaseContent(false);
                t->delVBO();
                mtex.erase(texx);
            }
        }
    } catch (const std::out_of_range& oor) {

    }
}

void TexLib::addRef(int texx) {
    try {
        Texture* t = mtex.at(texx);
        t->ref++;
    } catch (const std::out_of_range& oor) {

    }
}

int TexLib::addTex(QString path, QString name, bool reload, Use use) {
    QString pathid = (path+"/"+name);
    pathid.replace("\\", "/");
    pathid = ContentPath::normalize(pathid);
    return addTex(pathid, reload, use);
}

int TexLib::getTex(QString pathid) {
    pathid = ContentPath::normalize(pathid);
    int id = findTexture(ContentPath::key(pathid));
    if(id < 0) {
        const QString fallback = ContentPath::textureSource(pathid);
        if(fallback != pathid) id = findTexture(ContentPath::key(fallback));
    }
    if(id >= 0) mtex[id]->ref++;
    return id;
}

int TexLib::addTex(Texture* texture, bool reload) {
    if (texture == nullptr) {
        return -1;
    }
    if (texture->hashid.isEmpty() && !texture->pathid.isEmpty()) {
        texture->hashid.push_back(texture->pathid);
    }
    for(auto &alias : texture->hashid) alias = ContentPath::key(alias);

    for ( auto it = mtex.begin(); it != mtex.end(); ++it ){
        Texture* existing = it->second;
        if(existing == nullptr) continue;

        for(int i = 0; i < existing->hashid.size(); i++){
            for(int j = 0; j < texture->hashid.size(); j++){
                if(existing->error || existing->missing)
                    continue;
                if(existing->hashid[i] != texture->hashid[j])
                    continue;
                if(!ContentPath::synthetic(texture->hashid[j])
                        && existing->pathid.endsWith(".dds", Qt::CaseInsensitive)
                            != texture->pathid.endsWith(".dds", Qt::CaseInsensitive))
                    continue;

                if(!reload){
                    existing->ref++;
                    if(texture->imageData != nullptr){
                        delete[] texture->imageData;
                        texture->imageData = nullptr;
                    }
                    if(texture->tex != nullptr){
                        delete[] texture->tex;
                        texture->tex = nullptr;
                    }
                    delete texture;
                    return (int)it->first;
                }

                existing->takeContentFrom(*texture);

                // Merge identities (keep existing stable ids, add new ones).
                for(int k = 0; k < texture->hashid.size(); k++){
                    bool found = false;
                    for(int u = 0; u < existing->hashid.size(); u++){
                        if(existing->hashid[u].length() == texture->hashid[k].length())
                            if(existing->hashid[u] == texture->hashid[k]){
                                found = true;
                                break;
                            }
                    }
                    if(!found)
                        existing->hashid.push_back(texture->hashid[k]);
                }

                if(texture->tex != nullptr){
                    delete[] texture->tex;
                    texture->tex = nullptr;
                }
                delete texture;

                existing->ref++;
                return (int)it->first;
            }
        }
    }

    texture->ref++;
    mtex[jesttextur] = texture;
    return jesttextur++;
}

bool TexLib::decodeFromBytes(Texture* texture, const QByteArray& encodedBytes, QString* outError) {
    if (texture == nullptr) {
        if (outError) *outError = "Texture is null";
        return false;
    }
    if (encodedBytes.isEmpty()) {
        if (outError) *outError = "Encoded bytes are empty";
        texture->error = true;
        return false;
    }

    const QByteArray head = encodedBytes.left(16);
    if (head.size() >= 4 && std::memcmp(head.constData(), "DDS ", 4) == 0) {
        if (outError) *outError = "DDS from bytes is not supported yet";
        texture->error = true;
        return false;
    }
    static const unsigned char ktx2Magic[12] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    if (head.size() >= 12 && std::memcmp(head.constData(), ktx2Magic, 12) == 0) {
        if (outError) *outError = "KTX2 from bytes is not supported yet";
        texture->error = true;
        return false;
    }

    QImage img;
    if (!img.loadFromData(encodedBytes)) {
        if (outError) *outError = "Unsupported image format (Qt decoder)";
        texture->error = true;
        return false;
    }

    if (texture->imageData != nullptr) {
        delete[] texture->imageData;
        texture->imageData = nullptr;
    }
    texture->compressedData.clear();
    texture->compressedGLFormat = 0;

    if (img.hasAlphaChannel()) {
        texture->bytesPerPixel = 4;
        texture->type = GL_RGBA;
        img = img.convertToFormat(QImage::Format_RGBA8888);
    } else {
        texture->bytesPerPixel = 3;
        texture->type = GL_RGB;
        img = img.convertToFormat(QImage::Format_RGB888);
    }

    texture->width = img.width();
    texture->height = img.height();
    texture->imageSize = texture->width * texture->height * texture->bytesPerPixel;
    texture->bpp = texture->bytesPerPixel * 8;

    texture->imageData = new unsigned char[texture->imageSize];
    const int lineWidth = texture->width * texture->bytesPerPixel;
    if ((lineWidth % 4) == 0) {
        std::memcpy(texture->imageData, img.bits(), texture->imageSize);
    } else {
        int srcLineWidth = lineWidth + 4 - (lineWidth % 4);
        for (int y = 0; y < texture->height; y++) {
            std::memcpy(texture->imageData + y * lineWidth, img.bits() + y * srcLineWidth, lineWidth);
        }
    }

    texture->classifyAlpha();
    texture->loaded = true;
    texture->editable = true;
    texture->missing = false;
    texture->error = false;
    return true;
}

int TexLib::addTex(QString pathid, bool reload, Use use, Loading loading) {
    pathid = ContentPath::normalize(pathid);
    int texId = findTexture(ContentPath::key(pathid), true);
    if(texId < 0) {
        const QString fallback = ContentPath::textureSource(pathid);
        if(fallback != pathid) {
            pathid = fallback;
            texId = findTexture(ContentPath::key(pathid), true);
        }
    }
    Texture* newFile = texId < 0 ? nullptr : mtex[texId];
    if(newFile) {
        // Displayed until now and wanted editable: from now on its loads keep
        // the source, and one without it loads again (a load on its way may
        // have dropped it: it goes stale).
        const bool editableNow = use == Editable && newFile->displayOnly;
        if (editableNow)
            newFile->displayOnly = false;
        if (use == Data)
            newFile->data = true;
        if(!reload && !(editableNow && !newFile->released && newFile->imageData == nullptr
                        && newFile->compressedData.isEmpty() && newFile->reloadable)) {
            newFile->ref++;
            newFile->requested = residencyNow;
            reloadReleased(newFile);
            return texId;
        }
        if (!reload) {
            newFile->ref++;
            newFile->requested = residencyNow;
            startLoad(newFile, true);
            return texId;
        }
        pathid = newFile->pathid; // Reload the selected asset using its stored spelling.
    }
    if(!newFile) {
        newFile = new Texture(pathid);
        newFile->displayOnly = use == Display || use == Data;
        newFile->data = use == Data;
        newFile->ref++;
        texId = jesttextur++;
        mtex[texId] = newFile;
    } else {
        newFile->delVBO();
        newFile->pathid = pathid;
        if (newFile->released && newFile->tex != nullptr)
            evictedHandles.remove(newFile->tex[0]);
        newFile->released = false;
    }
    newFile->requested = residencyNow;
    startLoad(newFile, reload, loading);
    return texId;
}

QSize TexLib::imageSize(QString pathid) {
    const QString path = ContentPath::textureSource(ContentPath::normalize(pathid));
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return QSize();
    const QString type = QFileInfo(path).suffix().toLower();
    auto u32 = [](const QByteArray &bytes, int at) {
        return bytes.size() >= at + 4 ? qFromLittleEndian<quint32>(bytes.constData() + at) : 0u;
    };
    if(type == "dds"){
        // "DDS ", the header's size, flags, height, width.
        const QByteArray header = file.read(20);
        if(!header.startsWith("DDS "))
            return QSize();
        return QSize(int(u32(header, 16)), int(u32(header, 12)));
    }
    if(type == "ace"){
        // The SIMISA envelope, then the ACE header: version, flags, width,
        // height. A compressed envelope inflated only that far.
        const QByteArray start = file.read(4096);
        QByteArray body;
        if(start.startsWith("SIMISA@@@@@@@@@@")){
            body = start.mid(16, 16);
        } else if(start.startsWith("SIMISA@F") && start.size() > 16){
            body.resize(16);
            mz_stream z{};
            z.next_in = reinterpret_cast<const unsigned char *>(start.constData() + 16);
            z.avail_in = mz_uint32(start.size() - 16);
            z.next_out = reinterpret_cast<unsigned char *>(body.data());
            z.avail_out = 16;
            if(mz_inflateInit(&z) != MZ_OK)
                return QSize();
            mz_inflate(&z, MZ_SYNC_FLUSH);
            const bool ok = z.total_out == 16;
            mz_inflateEnd(&z);
            if(!ok)
                return QSize();
        }
        if(body.size() < 16 || u32(body, 0) != 1)
            return QSize();
        return QSize(int(u32(body, 8)), int(u32(body, 12)));
    }
    return QImageReader(&file).size();
}

bool TexLib::reloadTexIfPresent(QString pathid) {
    pathid=ContentPath::normalize(pathid);
    const int id=findTexture(ContentPath::key(pathid), true);
    if(id<0 || mtex[id]==nullptr)return false;
    addTex(mtex[id]->pathid,true);
    return true;
}

void TexLib::residencyFrame() {
    if (!residencyClock.isValid())
        residencyClock.start();
    residencyNow = residencyClock.elapsed();
    RhiTextures::setClock(residencyNow);
    if (residencyNow - lastResidencyPass < ResidencyPassMs)
        return;
    lastResidencyPass = residencyNow;
    enforceResidency(residencyNow, ResidencyMinAgeMs);
}

int TexLib::enforceResidency(qint64 now, qint64 minAgeMs) {
    const qint64 budget = residencyBudget();
    if (budget <= 0)
        return 0;
    struct Candidate { qint64 used; qint64 bytes; Texture *texture; };
    std::vector<Candidate> candidates;
    qint64 total = 0;
    for (const auto &entry : mtex) {
        Texture *texture = entry.second;
        if (texture == nullptr)
            continue;
        const qint64 bytes = residentBytes(*texture);
        total += bytes;
        if (bytes <= 0 || !residencyCandidate(*texture))
            continue;
        const qint64 used = lastUse(*texture);
        if (now - used >= minAgeMs)
            candidates.push_back({used, bytes, texture});
    }
    if (total <= budget)
        return 0;
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.used < b.used; });
    const qint64 target = budget - budget / 10;
    int count = 0;
    for (const Candidate &candidate : candidates) {
        if (total <= target || count >= ResidencyMaxPerPass)
            break;
        releaseResident(*candidate.texture);
        total -= candidate.bytes;
        releasedBytesSinceLog += candidate.bytes;
        ++count;
    }
    releasedSinceLog += count;
    if (releasedSinceLog > 0 && now - lastResidencyLog >= ResidencyLogMs) {
        qInfo().noquote() << "Texture memory budget" << (budget >> 20) << "MB:" << releasedSinceLog
                          << "textures released (" << (releasedBytesSinceLog >> 20) << "MB ), now"
                          << (total >> 20) << "MB";
        lastResidencyLog = now;
        releasedSinceLog = 0;
        releasedBytesSinceLog = 0;
    }
    return count;
}

qint64 TexLib::residencyBudget() {
    if (budgetOverride >= 0)
        return budgetOverride;
    if (budgetFromSettings < 0) {
        const qint64 megabytes = Settings::integer("core.rendering.textureMemoryBudget");
        const qint64 installed = installedMemory();
        constexpr qint64 GB = qint64(1) << 30;
        // Uploaded textures live in a discrete GPU's own memory, not in the
        // RAM (their CPU pixels go once uploaded); an integrated GPU's take
        // RAM.
        QString basis;
        if (megabytes > 0) {
            budgetFromSettings = megabytes << 20;
            basis = "the setting";
        } else if (gpuDiscrete && gpuMemory > 0) {
            budgetFromSettings = std::clamp(gpuMemory * 3 / 5, GB, 16 * GB);
            basis = QString("60% of the discrete GPU's %1 MB").arg(gpuMemory >> 20);
        } else {
            budgetFromSettings = installed > 0 ? std::clamp(installed / 8, GB, 4 * GB) : 2 * GB;
            basis = QString("an eighth of %1 MB installed").arg(installed >> 20);
        }
        qInfo().noquote() << "Texture memory budget" << (budgetFromSettings >> 20) << "MB:" << basis;
    }
    return budgetFromSettings;
}

void TexLib::setResidencyBudget(qint64 bytes) {
    budgetOverride = bytes;
}

void TexLib::setGpuMemory(qint64 bytes, bool discrete) {
    gpuMemory = bytes;
    gpuDiscrete = discrete;
    budgetFromSettings = -1;
}

void TexLib::reloadReleased(Texture *texture) {
    if (texture == nullptr || !texture->released)
        return;
    texture->released = false;
    if (texture->tex != nullptr)
        evictedHandles.remove(texture->tex[0]);
    texture->requested = residencyNow;
    startLoad(texture, false);
}

void TexLib::reloadNow(Texture *texture) {
    if (texture == nullptr || !texture->released)
        return;
    texture->released = false;
    if (texture->tex != nullptr)
        evictedHandles.remove(texture->tex[0]);
    texture->requested = residencyNow;
    startLoad(texture, true);
}

void TexLib::reloadEvicted(unsigned int handle) {
    const auto found = evictedHandles.constFind(handle);
    if (found != evictedHandles.cend())
        reloadReleased(found.value());
}

bool TexLib::request(int id) {
    const auto found = mtex.find(id);
    if (found == mtex.end() || found->second == nullptr)
        return false;
    reloadReleased(found->second);
    return found->second->loaded;
}

bool TexLib::retryIfFailed(int id) {
    const auto found = mtex.find(id);
    Texture *texture = found == mtex.end() ? nullptr : found->second;
    if(texture == nullptr || (!texture->missing && !texture->error)
            || ContentPath::synthetic(texture->pathid)
            || !hasLoader(QFileInfo(texture->pathid).suffix().toLower()))
        return false;
    texture->delVBO();
    startLoad(texture, true);
    return texture->loaded;
}

int TexLib::cloneTex(int id) {
    Texture* t = mtex[id];
    if(t == NULL) {
        qDebug() << "null texture " << id;
        return -2;
    }
    reloadNow(t);
    Texture* newFile = new Texture(t);
    newFile->ref++;
    mtex[jesttextur] = newFile;

    return jesttextur++;
}

void TexLib::save(QString type, QString path, int id){
    Texture* t = mtex.at(id);
    if(t == NULL)
        return;
    if(!t->editable)
        t->setEditable();
    AceLib::save(path, t);
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
// Installed memory in bytes, 0 when unknown.
qint64 installedMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX status {};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? qint64(status.ullTotalPhys) : 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && size > 0 ? qint64(pages) * size : 0;
#endif
}
}
