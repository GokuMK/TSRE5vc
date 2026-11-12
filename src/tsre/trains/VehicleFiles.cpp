/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#define MINIZ_HEADER_FILE_ONLY
#include <miniz/miniz.h>

#include <tsre/trains/VehicleFiles.h>
#include <tsre/Game.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/maszyna/MaszynaPaths.h>
#include <tsre/maszyna/MaszynaSound.h>
#include <tsre/maszyna/MaszynaText.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/shape/NightTextures.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/VehicleDefinition.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>
#include <shapeViewer/ShapeTextureInfo.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <algorithm>

namespace {

class Collected {
public:
    void add(const QString &path, bool notRead = false) {
        if (path.isEmpty())
            return;
        const QString clean = QDir::cleanPath(path);
        if (seen.contains(clean))
            return;
        seen.insert(clean);
        VehicleFiles::File file;
        file.path = clean;
        file.notRead = notRead;
        const QFileInfo info(clean);
        file.missing = !info.isFile();
        file.bytes = file.missing ? 0 : info.size();
        files.push_back(file);
    }
    // A shape and the .sd beside it, which it may not have.
    void addShape(const QString &path) {
        if (path.isEmpty())
            return;
        add(path);
        const QString sd = ContentPath::withExtension(path, "sd");
        if (QFileInfo(sd).isFile())
            add(sd);
    }
    QVector<VehicleFiles::File> files;

private:
    QSet<QString> seen;
};

void addShapeFiles(Collected &out, ComplexShape &shape, const QString &repaint) {
    QStringList found, missing;
    if (shape.usedFiles(repaint, found, missing)) {
        for (const QString &file : found)
            out.add(file);
        for (const QString &file : missing)
            out.add(file);
        return;
    }
    const QString folder = shape.getTexPath();
    const QStringList images = shape.imageNames();
    for (const QString &image : images) {
        out.add(ContentPath::textureSource(ContentPath::join(folder, image)));
        out.add(NightTextures::find(folder, image));
    }
    if (!images.isEmpty())
        return;
    // Those that do not name them: as drawn.
    QHash<int, ShapeTextureInfo *> textures;
    shape.fillShapeTextureInfo(textures);
    for (auto it = textures.begin(); it != textures.end(); ++it) {
        const auto found = TexLib::mtex.find(it.key());
        if (found != TexLib::mtex.end() && found->second != nullptr)
            out.add(found->second->pathid);
        delete it.value();
    }
}

void addVehicle(Collected &out, Eng &eng) {
    const VehicleDefinition &def = *eng.definition;
    const QStringList read = def.files();
    for (const QString &file : read)
        out.add(file);
    // A MaSzyna vehicle: every member of its random include sets too
    // (include [ horn_set1.inc ... horn_set7.inc ] end), with what they
    // include. Reading it took the first only, while each car picks one:
    // archives held set 1 alone and most ET22s lost their horn
    // (2026-11-12). The members named but not there are listed, missing.
    // Its sounds: every word naming one, each set's members too.
    QStringList sounds;
    if (def.maszyna != nullptr) {
        QStringList found, notFound;
        const MaszynaVehicle &v = *def.maszyna;
        MaszynaText::allFiles(v.folder, v.type + QLatin1String(".fiz"), {}, found, notFound);
        // A word split as the sound reader splits it: a set's members at ,
        // and a soundset's begin|main|end at |. Not at [ or ]: names hold
        // them (9629]201e_rp1_start_1.ogg); a set's brackets stand apart.
        const auto sound = [&sounds](QByteArrayView word) {
            QByteArray part;
            const auto take = [&sounds, &part]() {
                const QByteArray lower = part.toLower();
                if (lower.endsWith(".wav") || lower.endsWith(".ogg") || lower.endsWith(".flac")) {
                    const QString name = MaszynaText::decode(part).replace(QLatin1Char('\\'), QLatin1Char('/'));
                    if (!sounds.contains(name))
                        sounds << name;
                }
                part.clear();
            };
            for (const char c : word) {
                if (c == ',' || c == '|')
                    take();
                else
                    part += c;
            }
            take();
        };
        MaszynaText::allFiles(v.folder, v.type + QLatin1String(".mmd"),
                              {QStringLiteral("(p1)"), v.type, QStringLiteral("(p3)")}, found, notFound, sound);
        for (const QString &file : found + notFound)
            out.add(file);
    }
    if (def.maszyna == nullptr && !read.contains(eng.pathid))
        out.add(eng.pathid, true);
    out.addShape(def.shapePath);
    for (const VehicleDefinition::FreightAnim &anim : def.freightAnims)
        out.addShape(anim.path);
    out.addShape(def.cab3DShapePath);
    // A MaSzyna vehicle's cab models (task maszyna 04).
    if (const std::shared_ptr<const VehicleDefinition::Cab3D> cab = def.maszyna != nullptr ? def.cab3DOf() : nullptr)
        for (const VehicleDefinition::Cab3D::Viewpoint &viewpoint : cab->viewpoints)
            out.addShape(viewpoint.shape);
    out.add(def.soundPath);
    out.add(def.cabSoundPath);
    // MaSzyna's sounds where its sound reader finds them (the vehicle's
    // folder, then SOUND/MASZYNA; an .ogg for a .wav written); one not
    // there where the sound folder would hold it.
    if (!sounds.isEmpty()) {
        const QString soundFolder = MaszynaPaths::soundFolder();
        for (const QString &name : sounds) {
            const QString path = MaszynaSound::find(name, def.maszyna->folder, soundFolder);
            out.add(path.isEmpty() ? soundFolder + QLatin1Char('/') + name : path);
        }
    }
    out.add(def.cabViewPath);
    out.add(def.cab3DViewPath);
    for (ComplexShape *shape : eng.usedShapes())
        addShapeFiles(out, *shape, def.skin);
}

QString rootOf(const QString &gameRoot) {
    return QDir::cleanPath(gameRoot);
}

// A file's main folder from the game root: a vehicle's folder in the
// trainset (one group folder deep at most), one of TEXTURES/MASZYNA's, else
// its first two levels.
QString mainFolder(const QString &relative, const QString &root) {
    const QStringList parts = relative.split(QLatin1Char('/'));
    const auto first = [&parts](int n) { return QStringList(parts.mid(0, n)).join(QLatin1Char('/')); };
    if (parts.size() > 3 && parts[0].compare("TRAINS", Qt::CaseInsensitive) == 0
            && parts[1].compare("TRAINSET", Qt::CaseInsensitive) == 0) {
        const QDir top(root + QLatin1Char('/') + first(3));
        const bool vehicle = !top.entryList(QStringList() << "*.eng" << "*.wag" << "*.mmd", QDir::Files).isEmpty();
        return first(!vehicle && parts.size() > 4 ? 4 : 3);
    }
    if (parts.size() > 3 && parts[1].compare("MASZYNA", Qt::CaseInsensitive) == 0)
        return first(3);
    return first(std::min(int(parts.size()) - 1, 2));
}

size_t writeToFile(void *opaque, mz_uint64 offset, const void *data, size_t size) {
    auto *file = static_cast<QFile *>(opaque);
    if (qint64(offset) != file->pos() && !file->seek(qint64(offset)))
        return 0;
    return size_t(file->write(static_cast<const char *>(data), qint64(size)));
}

}

namespace VehicleFiles {

bool File::text() const {
    static const QSet<QString> kinds = {"eng", "wag", "inc", "sd", "sms", "cvf", "fiz", "mmd", "mat", "con", "txt"};
    return kinds.contains(QFileInfo(path).suffix().toLower());
}

QVector<File> ofVehicle(Eng &eng) {
    Collected out;
    addVehicle(out, eng);
    return out.files;
}

QVector<File> ofTrain(Consist &train) {
    Collected out;
    out.add(train.pathid);
    for (const Consist::EngItem &item : train.engItems) {
        if (item.eng < 0)
            continue;
        const auto found = Game::currentEngLib->eng.find(item.eng);
        if (found != Game::currentEngLib->eng.end() && found->second != nullptr)
            addVehicle(out, *found->second);
    }
    return out.files;
}

QString fromRoot(const QString &path, const QString &gameRoot) {
    const QString root = rootOf(gameRoot) + QLatin1Char('/');
    return path.startsWith(root, Qt::CaseInsensitive) ? path.mid(root.size()) : QString();
}

QVector<Folder> byFolder(const QVector<File> &files, const QString &gameRoot) {
    QVector<Folder> folders;
    QHash<QString, int> index;
    for (const File &file : files) {
        const QString relative = fromRoot(file.path, gameRoot);
        const QString name = relative.isEmpty() ? QFileInfo(file.path).path() : mainFolder(relative, rootOf(gameRoot));
        auto found = index.constFind(name);
        if (found == index.constEnd()) {
            found = index.insert(name, folders.size());
            folders.push_back(Folder());
            folders.back().name = name;
        }
        Folder &folder = folders[found.value()];
        folder.files.push_back(file);
        folder.bytes += file.bytes;
        folder.missing += file.missing ? 1 : 0;
    }
    if (folders.size() > 2)
        std::sort(folders.begin() + 1, folders.end(), [](const Folder &a, const Folder &b) {
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
    for (Folder &folder : folders)
        std::stable_sort(folder.files.begin(), folder.files.end(), [](const File &a, const File &b) {
            if (a.text() != b.text())
                return a.text();
            return a.path.compare(b.path, Qt::CaseInsensitive) < 0;
        });
    return folders;
}

Archived archive(const QVector<File> &files, const QString &gameRoot, const QString &zipPath,
                 const std::function<bool(int, int)> &progress) {
    Archived result;
    QFile out(zipPath);
    if (!out.open(QIODevice::WriteOnly)) {
        result.error = out.errorString();
        return result;
    }
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    zip.m_pWrite = writeToFile;
    zip.m_pIO_opaque = &out;
    if (!mz_zip_writer_init(&zip, 0)) {
        result.error = QString::fromLatin1(mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        return result;
    }
    bool stopped = false;
    for (int i = 0; i < files.size() && !stopped && result.error.isEmpty(); ++i) {
        if (progress && !progress(i, files.size())) {
            stopped = true;
            break;
        }
        const QString relative = fromRoot(files[i].path, gameRoot);
        QFile in(files[i].path);
        if (files[i].missing || relative.isEmpty() || !in.open(QIODevice::ReadOnly)) {
            ++result.left;
            continue;
        }
        const QByteArray data = in.readAll();
        if (!mz_zip_writer_add_mem(&zip, relative.toUtf8().constData(), data.constData(), size_t(data.size()), MZ_BEST_SPEED)) {
            result.error = QString::fromLatin1(mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
            break;
        }
        ++result.written;
    }
    if (!stopped && result.error.isEmpty() && !mz_zip_writer_finalize_archive(&zip))
        result.error = QString::fromLatin1(mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    mz_zip_writer_end(&zip);
    out.close();
    if (stopped || !result.error.isEmpty()) {
        out.remove();
        return result;
    }
    result.ok = true;
    return result;
}

}
