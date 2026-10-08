/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/geo/osm/OsmConversionUi.h>
#include <tsre/geo/osm/OsmDirectory.h>
#include <tsre/geo/osm/OsmOverview.h>
#include <settings/SettingsAccess.h>
#include <QCheckBox>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSet>
#include <QStorageInfo>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <thread>
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <QFile>
#endif

namespace Osm {

namespace {

QSet<QString> &declinedFiles() {
    static QSet<QString> declined;
    return declined;
}
QString declineKey(const DirectoryEntry &e) { return e.path + QLatin1Char('|') + QString::number(e.size); }
QString size(int64_t bytes) { return QLocale().formattedDataSize(bytes); }

QString phaseText(ConvertPhase phase) {
    switch (phase) {
        //% "scanning blocks"
        case ConvertPhase::Scan: return qtTrId("geo.osm.conversion.phase.scan");
        //% "reading relations"
        case ConvertPhase::Relations: return qtTrId("geo.osm.conversion.phase.relations");
        //% "reading nodes"
        case ConvertPhase::Nodes: return qtTrId("geo.osm.conversion.phase.nodes");
        //% "reading ways"
        case ConvertPhase::Ways: return qtTrId("geo.osm.conversion.phase.ways");
        //% "writing the sorted file"
        case ConvertPhase::Write: return qtTrId("geo.osm.conversion.phase.write");
        //% "building overview maps"
        case ConvertPhase::Overview: return qtTrId("geo.osm.conversion.phase.overview");
    }
    return {};
}

// The question shown before converting; returns false when the user postpones.
bool askToConvert(QWidget *parent, const std::vector<const DirectoryEntry *> &files, bool &deleteOriginal) {
    int64_t memory = 0, temp = 0, output = 0;
    QStringList lines;
    for (const DirectoryEntry *e : files) {
        const ConvertEstimate est = estimateConversion(e->size, 0, writeGroupBytesFor(availableMemoryBytes()));
        memory = std::max(memory, est.memoryBytes);
        temp = std::max(temp, est.tempBytes);
        output += est.outputBytes;
        lines << QStringLiteral("%1 (%2)").arg(QFileInfo(e->path).fileName().toHtmlEscaped(), size(e->size));
    }
    const QString dir = QFileInfo(files.front()->path).absolutePath();
    const int64_t freeDisk = QStorageInfo(dir).bytesAvailable();
    const int64_t freeMemory = availableMemoryBytes();

    QDialog dialog(parent);
    //% "Convert OpenStreetMap data"
    dialog.setWindowTitle(qtTrId("geo.osm.conversion.title"));
    dialog.setMinimumWidth(460);
    auto *layout = new QVBoxLayout(&dialog);
    QString text = QStringLiteral("<p>%1</p><ul><li>%2</li></ul><p>%3</p>")
            //% "Downloaded OpenStreetMap files covering this area need a one-time conversion into a spatially sorted copy before TSRE can use them:"
            .arg(qtTrId("geo.osm.conversion.question"), lines.join(QStringLiteral("</li><li>")),
                 //% "This needs about %1 of memory (%2 available) and %3 of free disk space (%4 available) while it runs."
                 qtTrId("geo.osm.conversion.resources")
                         .arg(size(memory),
                              //% "unknown"
                              freeMemory > 0 ? size(freeMemory) : qtTrId("geo.osm.conversion.unknown"),
                              size(temp + output), size(freeDisk)));
    if (freeDisk >= 0 && freeDisk < temp + output)
        //% "There is not enough free disk space."
        text += QStringLiteral("<p><b>%1</b></p>").arg(qtTrId("geo.osm.conversion.low.disk"));
    if (freeMemory > 0 && freeMemory < memory)
        //% "Available memory may not be enough. Regional extracts of the same data (for example per province) need much less."
        text += QStringLiteral("<p><b>%1</b></p>").arg(qtTrId("geo.osm.conversion.low.memory"));
    auto *label = new QLabel(text, &dialog);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    layout->addWidget(label);
    //% "Delete the downloaded file after conversion"
    auto *deleteBox = new QCheckBox(qtTrId("geo.osm.conversion.delete.original"), &dialog);
    deleteBox->setChecked(deleteOriginal);
    layout->addWidget(deleteBox);
    auto *buttons = new QDialogButtonBox(&dialog);
    //% "Convert now"
    buttons->addButton(qtTrId("geo.osm.conversion.convert"), QDialogButtonBox::AcceptRole);
    //% "Not now"
    buttons->addButton(qtTrId("geo.osm.conversion.later"), QDialogButtonBox::RejectRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return false;
    if (deleteBox->isChecked() != deleteOriginal) {
        deleteOriginal = deleteBox->isChecked();
        QString error;
        SettingsManager &settings = SettingsManager::instance();
        if (!settings.setValue(QStringLiteral("geo.osm.originalAfterConversion"), deleteOriginal ? QStringLiteral("delete") : QStringLiteral("keep"), &error)
            || !settings.save(&error))
            qWarning().noquote() << "Cannot save geo.osm.originalAfterConversion:" << error;
    }
    return true;
}

}

int64_t availableMemoryBytes() {
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? int64_t(status.ullAvailPhys) : 0;
#elif defined(Q_OS_LINUX)
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    for (QByteArray line = f.readLine(); !line.isEmpty(); line = f.readLine())
        if (line.startsWith("MemAvailable:")) return line.mid(13).trimmed().split(' ').value(0).toLongLong() * 1024;
    return 0;
#else
    return 0;
#endif
}

EnsureResult ensureConverted(QWidget *parent, const Box &area, EnsureMode mode) {
    return ensureConverted(parent, Settings::string("core.paths.osmData", SettingType::Directory),
                           Settings::string("geo.osm.originalAfterConversion", SettingType::Enum) == QStringLiteral("delete"), area, mode);
}

EnsureResult ensureConverted(QWidget *parent, const QString &path, bool deleteOriginal, const Box &area, EnsureMode mode) {
    EnsureResult result;
    OsmDirectory dir;
    QString error;
    if (path.trimmed().isEmpty() || !dir.scan(path, error)) return result;
    result.hasDirectory = true;

    std::vector<const DirectoryEntry *> files;
    for (const DirectoryEntry *e : dir.pendingConversions(&area)) {
        if (mode == EnsureMode::Ask && declinedFiles().contains(declineKey(*e))) { ++result.declined; continue; }
        files.push_back(e);
    }
    // Overviews of converted files are rebuilt without asking: one pass, seconds even for a country.
    const OverviewConfig &overviewConfig = OverviewConfig::standard();
    const std::vector<const DirectoryEntry *> overviews = dir.pendingOverviews(overviewConfig, &area);
    if (files.empty() && overviews.empty()) return result;
    if (!files.empty() && mode == EnsureMode::Ask && !askToConvert(parent, files, deleteOriginal)) {
        for (const DirectoryEntry *e : files) declinedFiles().insert(declineKey(*e));
        result.declined += int(files.size());
        files.clear();
        if (overviews.empty()) return result;
    }

    // Worker thread converts the files one by one, then refreshes overviews; this thread shows progress.
    std::atomic_bool cancel{false}, done{false};
    std::atomic<int> current{0}, phase{0}, permille{0};
    QStringList errors;
    int converted = 0;
    const size_t steps = files.size() + overviews.size();
    // Write groups sized to the memory available now (the editor holds its own data too).
    ConvertOptions options;
    options.writeGroupBytes = writeGroupBytesFor(availableMemoryBytes());
    std::thread worker([&] {
        // Out of memory or another exception becomes an error message, not an end of the program.
        try {
            for (size_t i = 0; i < files.size() && !cancel; ++i) {
                current = int(i);
                ConvertStats stats;
                QString e;
                const ConvertEstimate est = estimateConversion(files[i]->size, options.threads, options.writeGroupBytes);
                qInfo().noquote() << QStringLiteral("OSM conversion: %1, %2 MB, estimated memory %3 MB, available %4 MB, write groups %5 MB")
                        .arg(files[i]->path).arg(files[i]->size >> 20).arg(est.memoryBytes >> 20)
                        .arg(availableMemoryBytes() >> 20).arg(options.writeGroupBytes >> 20);
                int logged = -1;
                const bool ok = OsmDirectory::convert(*files[i], deleteOriginal, options, stats, e,
                                                      [&](ConvertPhase p, double f) {
                                                          if (int(p) != logged) {
                                                              logged = int(p);
                                                              static const char *const names[] = {"scan", "relations", "nodes", "ways", "write", "overview maps"};
                                                              qInfo().noquote() << QStringLiteral("OSM conversion: %1, available memory %2 MB")
                                                                      .arg(QLatin1String(names[std::clamp(logged, 0, 5)])).arg(availableMemoryBytes() >> 20);
                                                          }
                                                          phase = int(p); permille = int(f * 1000);
                                                      }, &cancel);
                if (ok) {
                    ++converted;
                    qInfo().noquote() << QStringLiteral("OSM conversion: done in %1 s").arg(stats.totalSeconds, 0, 'f', 1);
                } else if (!cancel) {
                    errors << e;
                    qWarning().noquote() << "OSM conversion failed:" << e;
                }
            }
            for (size_t i = 0; i < overviews.size() && !cancel; ++i) {
                current = int(files.size() + i);
                phase = int(ConvertPhase::Overview);
                permille = 0;
                std::vector<OverviewStats> stats;
                QString e;
                qInfo().noquote() << "OSM overviews:" << overviews[i]->path;
                if (!buildOverviews(overviews[i]->path, overviewConfig, stats, e, 0, &cancel) && !cancel) {
                    errors << e;
                    qWarning().noquote() << "OSM overviews failed:" << e;
                }
            }
        } catch (const std::bad_alloc &) {
            errors << QStringLiteral("Not enough memory. A smaller regional extract needs less.");
            qWarning() << "OSM conversion: out of memory";
        } catch (const std::exception &ex) {
            errors << QString::fromLocal8Bit(ex.what());
            qWarning() << "OSM conversion:" << ex.what();
        }
        done = true;
    });
    if (mode == EnsureMode::Ask) {
        QProgressDialog progress(parent);
        //% "Convert OpenStreetMap data"
        progress.setWindowTitle(qtTrId("geo.osm.conversion.title"));
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setRange(0, int(steps) * 6000);
        QObject::connect(&progress, &QProgressDialog::canceled, [&] { cancel = true; });
        QEventLoop loop;
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, [&] {
            if (done) { loop.quit(); return; }
            const int i = current, p = phase;
            const QString name = QFileInfo(size_t(i) < files.size() ? files[size_t(i)]->path : overviews[size_t(i) - files.size()]->path).fileName();
            //% "Converting %1 (%2 of %3): %4"
            progress.setLabelText(qtTrId("geo.osm.conversion.progress").arg(name).arg(i + 1).arg(steps).arg(phaseText(ConvertPhase(p))));
            progress.setValue(i * 6000 + p * 1000 + permille);
        });
        timer.start(100);
        loop.exec();
        progress.reset();
    }
    worker.join();
    result.converted = converted;
    result.errors = errors;
    if (mode == EnsureMode::Ask && !errors.isEmpty())
        //% "OpenStreetMap conversion failed"
        QMessageBox::warning(parent, qtTrId("geo.osm.conversion.failed"), errors.join(QLatin1Char('\n')));
    return result;
}

}
