/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "MemoryLog.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QMap>
#include <QMutex>
#include <QStringList>
#include <cstdio>
#include <cstring>
#include <rhi/qrhi.h>
#include <tsre/Game.h>
#include <tsre/renderer/Mesh.h>
#include <tsre/renderer/rhi/RhiContext.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/texture/TexLib.h>
#include <tsre/world/Route.h>
#include <tsre/world/TerrainLibQt.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace MemoryLog {

namespace {

QMutex failuresMutex;
// The failures by kind, in the kinds' order.
QMap<QByteArray, int> failuresByKind;

QString megabytes(qint64 bytes) {
    return bytes < 0 ? QString("?") : QString::number((bytes + (1 << 19)) >> 20);
}

QString megabytes(double mb) {
    return mb < 0 ? QString("?") : QString::number(qRound64(mb));
}

#ifndef _WIN32
// The kB values of a /proc file's lines ("VmRSS:   1234 kB") as MB, -1
// for those not found.
void readProc(const char *path, std::initializer_list<std::pair<const char *, double *>> wanted) {
    FILE *file = std::fopen(path, "r");
    if (file == nullptr)
        return;
    char text[256];
    while (std::fgets(text, sizeof(text), file) != nullptr)
        for (const auto &[name, value] : wanted) {
            const size_t length = std::strlen(name);
            long long kb = 0;
            if (std::strncmp(text, name, length) == 0 && std::sscanf(text + length, " %lld", &kb) == 1)
                *value = double(kb) / 1024.0;
        }
    std::fclose(file);
}
#endif

}

Process process() {
    Process memory;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters {};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                                sizeof(counters))) {
        memory.privateMB = double(counters.PrivateUsage) / 1048576.0;
        memory.workingSetMB = double(counters.WorkingSetSize) / 1048576.0;
        memory.peakWorkingSetMB = double(counters.PeakWorkingSetSize) / 1048576.0;
    }
    MEMORYSTATUSEX status {};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        memory.systemTotalMB = double(status.ullTotalPhys) / 1048576.0;
        memory.systemUsedMB = double(status.ullTotalPhys - status.ullAvailPhys) / 1048576.0;
        memory.commitLimitMB = double(status.ullTotalPageFile) / 1048576.0;
        memory.commitMB = double(status.ullTotalPageFile - status.ullAvailPageFile) / 1048576.0;
    }
#else
    // Linux: the anonymous memory resident as the private memory; no commit
    // (its limit holds only with strict overcommit).
    readProc("/proc/self/status", {{"RssAnon:", &memory.privateMB},
                                   {"VmRSS:", &memory.workingSetMB},
                                   {"VmHWM:", &memory.peakWorkingSetMB}});
    double available = -1;
    readProc("/proc/meminfo", {{"MemTotal:", &memory.systemTotalMB}, {"MemAvailable:", &available}});
    if (memory.systemTotalMB >= 0 && available >= 0)
        memory.systemUsedMB = memory.systemTotalMB - available;
#endif
    return memory;
}

QString line() {
    QStringList parts;
    if (RhiContext *context = RhiContext::existing(); context != nullptr && context->rhi() != nullptr) {
        const RhiContext::MemoryUse use = context->memoryUse();
        // Used: TSRE's; allowed: what the system lets TSRE have before its
        // allocations fail or move out (lavapipe: the whole system's).
        const auto heaps = [](const char *name, qint64 used, qint64 allowed, qint64 size) {
            return used >= 0 ? QString("%1 %2 MB used, %3 MB allowed, %4 MB in all")
                                       .arg(QString::fromLatin1(name), megabytes(used), megabytes(allowed),
                                            megabytes(size))
                             : QString("%1 %2 MB in all").arg(QString::fromLatin1(name), megabytes(size));
        };
        if (use.ownSize > 0)
            parts << heaps("GPU memory", use.ownUsed, use.ownBudget, use.ownSize)
                             + (use.sharedSize > 0 ? "; " + heaps("shared", use.sharedUsed, use.sharedBudget,
                                                                  use.sharedSize)
                                                   : QString());
        // QRhi's allocator (Vulkan): what its allocations use, and what its
        // blocks hold unused.
        const QRhiStats stats = context->rhi()->statistics();
        parts << QString("QRhi used %1 unused %2 MB in %3 blocks")
                         .arg(megabytes(qint64(stats.usedBytes)), megabytes(qint64(stats.unusedBytes)))
                         .arg(stats.blockCount);
    }
    const Meshes::RhiMemory meshes = Meshes::rhiMemory();
    parts << QString("textures %1 of %2 MB budget, meshes %3 MB (%4 MB waiting)")
                     .arg(megabytes(TexLib::residentTotal()), megabytes(TexLib::residencyBudget()),
                          megabytes(meshes.gpu), megabytes(meshes.pending));
    const Process memory = process();
    parts << QString("process %1 MB private, %2 MB working set (peak %3)")
                     .arg(megabytes(memory.privateMB), megabytes(memory.workingSetMB),
                          megabytes(memory.peakWorkingSetMB));
    parts << QString("system %1 of %2 MB").arg(megabytes(memory.systemUsedMB), megabytes(memory.systemTotalMB))
                     + (memory.commitLimitMB >= 0 ? QString(", commit %1 of %2 MB")
                                                            .arg(megabytes(memory.commitMB), megabytes(memory.commitLimitMB))
                                                  : QString());
    const auto *terrain = dynamic_cast<const TerrainLibQt *>(Game::terrainLib);
    parts << QString("shapes %1, world tiles %2, terrain tiles %3")
                     .arg(Game::currentShapeLib != nullptr ? int(Game::currentShapeLib->shape.size()) : 0)
                     .arg(Game::currentRoute != nullptr ? int(Game::currentRoute->tile.size()) : 0)
                     .arg(terrain != nullptr ? terrain->loadedTileCount(false) : 0);
    const QString notCreated = failures();
    parts << QString("not created: %1").arg(notCreated.isEmpty() ? QString("none") : notCreated);
    return parts.join(" | ");
}

void frame() {
    static const int seconds = qEnvironmentVariableIsSet("TSRE_MEMORY_LOG")
            ? qEnvironmentVariableIntValue("TSRE_MEMORY_LOG") : 60;
    if (seconds <= 0)
        return;
    static QElapsedTimer clock;
    static qint64 next = 0;
    if (!clock.isValid())
        clock.start();
    const qint64 now = clock.elapsed();
    if (now < next)
        return;
    const qint64 period = qint64(seconds) * 1000;
    next = (now / period + 1) * period;
    const QString when = now < period ? QString("at the first frame")
            : seconds % 60 == 0 ? QString("after %1 min").arg(now / 60000)
                                : QString("after %1 s").arg(now / 1000);
    qInfo().noquote() << QString("Memory %1: %2").arg(when, line());
}

namespace {
void notCreated(const char *what, const QString &size) {
    int count = 0;
    {
        QMutexLocker lock(&failuresMutex);
        count = ++failuresByKind[QByteArray(what)];
    }
    if (count == 1)
        qWarning().noquote() << QString("GPU resource not created: %1 (%2); the memory lines count its failures")
                                        .arg(QString::fromLatin1(what), size);
}
}

bool created(QRhiBuffer *buffer, const char *what) {
    if (buffer->create())
        return true;
    notCreated(what, QString("%1 KB").arg(buffer->size() / 1024));
    return false;
}

bool created(QRhiTexture *texture, const char *what) {
    if (texture->create())
        return true;
    notCreated(what, QString("%1x%2").arg(texture->pixelSize().width()).arg(texture->pixelSize().height()));
    return false;
}

QString failures() {
    QMutexLocker lock(&failuresMutex);
    QStringList kinds;
    for (auto it = failuresByKind.cbegin(); it != failuresByKind.cend(); ++it)
        kinds << QString("%1 %2").arg(QString::fromLatin1(it.key())).arg(it.value());
    return kinds.join(", ");
}

}
