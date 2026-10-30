/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/trains/VehiclePreload.h>
#include <tsre/trains/VehicleDefinition.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPair>
#include <QThread>
#include <QThreadPool>
#include <QVector>
#include <atomic>

namespace {

struct Held {
    std::shared_ptr<const VehicleDefinition> definition;
    QDateTime modified;
};

struct State {
    QMutex mutex;
    QHash<QString, Held> held;
    // The current read's: set when it is cancelled.
    std::shared_ptr<std::atomic<bool>> cancelled;
    const void *owner = nullptr;
    QThreadPool *pool = nullptr;
};

State &state() {
    static State s;
    return s;
}

QThreadPool *pool() {
    State &s = state();
    if (s.pool == nullptr) {
        // Owned by the application, whose end waits for the running reads
        // (a file each; cancelled ones stop at the next). Half the cores at
        // most 3: the shapes' loaders and the main thread go on.
        s.pool = new QThreadPool(QCoreApplication::instance());
        s.pool->setMaxThreadCount(qBound(1, QThread::idealThreadCount() / 2, 3));
    }
    return s.pool;
}

QString keyOf(const QString &folder, const QString &name, bool openRails) {
    return ContentPath::key(folder + "/" + name) + (openRails ? "|or" : "");
}

// A consist file's engine and wagon files (folder, file name), named as
// Consist::load names them, read without loading anything.
QVector<QPair<QString, QString>> vehiclesOf(const QString &consistFile, const QString &trainsetDirectory) {
    QVector<QPair<QString, QString>> out;
    QFile file(ContentPath::normalize(consistFile));
    if (!file.open(QIODevice::ReadOnly))
        return out;
    std::unique_ptr<FileBuffer> data(ReadFile::read(&file));
    file.close();
    if (data == nullptr)
        return out;
    data->off = 0;
    ParserX::NextLine(data.get());
    QString sh;
    while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
        if (sh == "train") {
            while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                if (sh == "traincfg") {
                    ParserX::GetString(data.get());
                    while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                        if (sh == "engine" || sh == "wagon") {
                            const QString wanted = sh == "engine" ? "enginedata" : "wagondata";
                            const QString extension = sh == "engine" ? ".eng" : ".wag";
                            while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                                if (sh == wanted) {
                                    const QString name = ParserX::GetString(data.get());
                                    const QString folder = ParserX::GetString(data.get());
                                    out.push_back({trainsetDirectory + "/" + folder, name + extension});
                                }
                                ParserX::SkipToken(data.get());
                            }
                        }
                        ParserX::SkipToken(data.get());
                    }
                }
                ParserX::SkipToken(data.get());
            }
        }
        ParserX::SkipToken(data.get());
    }
    return out;
}

}

void VehiclePreload::start(const void *owner, const QStringList &consistFiles, const QString &trainsetDirectory,
                           const QString &game, bool openRails, const QSet<QString> &loaded) {
    State &s = state();
    QMutexLocker lock(&s.mutex);
    if (owner != nullptr && owner == s.owner && s.cancelled != nullptr && !s.cancelled->load())
        return;
    if (s.cancelled != nullptr)
        s.cancelled->store(true);
    s.held.clear();
    s.owner = owner;
    const auto cancelled = std::make_shared<std::atomic<bool>>(false);
    s.cancelled = cancelled;
    lock.unlock();
    QThreadPool *threads = pool();
    // The consist files read on a worker too, each vehicle's file a task of
    // its own.
    threads->start([=]() {
        QElapsedTimer timer;
        timer.start();
        QSet<QString> seen = loaded;
        QVector<QPair<QString, QString>> files;
        for (const QString &consist : consistFiles) {
            if (cancelled->load())
                return;
            for (const QPair<QString, QString> &vehicle : vehiclesOf(consist, trainsetDirectory)) {
                const QString key = ContentPath::key(vehicle.first + "/" + vehicle.second);
                if (seen.contains(key))
                    continue;
                seen.insert(key);
                files.push_back(vehicle);
            }
        }
        const int count = files.size();
        const auto remaining = std::make_shared<std::atomic<int>>(count);
        for (const QPair<QString, QString> &vehicle : files) {
            threads->start([=]() {
                if (cancelled->load())
                    return;
                const std::shared_ptr<const VehicleDefinition> definition
                        = VehicleDefinition::read(vehicle.first, vehicle.second, game, openRails);
                const QDateTime modified = QFileInfo(definition->path).lastModified();
                State &st = state();
                QMutexLocker held(&st.mutex);
                if (st.cancelled == cancelled && !cancelled->load())
                    st.held.insert(keyOf(vehicle.first, vehicle.second, openRails), Held{definition, modified});
                if (--*remaining == 0)
                    qInfo().noquote() << QString("Vehicles read ahead: %1 files of %2 consists in %3 ms")
                                                 .arg(count).arg(consistFiles.size()).arg(timer.elapsed());
            });
        }
    });
}

void VehiclePreload::cancel() {
    State &s = state();
    QMutexLocker lock(&s.mutex);
    if (s.cancelled != nullptr)
        s.cancelled->store(true);
    s.cancelled.reset();
    s.owner = nullptr;
    s.held.clear();
}

std::shared_ptr<const VehicleDefinition> VehiclePreload::take(const QString &folder, const QString &name,
                                                              bool openRails) {
    State &s = state();
    QMutexLocker lock(&s.mutex);
    if (s.held.isEmpty())
        return nullptr;
    const auto found = s.held.find(keyOf(folder, name, openRails));
    if (found == s.held.end())
        return nullptr;
    const Held held = found.value();
    s.held.erase(found);
    lock.unlock();
    // Changed since it was read (edited in the meantime): read again.
    if (QFileInfo(held.definition->path).lastModified() != held.modified)
        return nullptr;
    return held.definition;
}

bool VehiclePreload::waitForDone(int ms) {
    return pool()->waitForDone(ms);
}

int VehiclePreload::held() {
    State &s = state();
    QMutexLocker lock(&s.mutex);
    return s.held.size();
}
