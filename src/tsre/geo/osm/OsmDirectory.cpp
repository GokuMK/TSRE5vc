/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/geo/osm/OsmDirectory.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace Osm {

namespace {
const QString PbfSuffix = QStringLiteral(".osm.pbf");
}

Sorted::SourceIdentity DirectoryEntry::identity() const {
    Sorted::SourceIdentity id;
    id.name = QFileInfo(path).fileName();
    id.size = size;
    id.timestamp = header.replicationTimestamp;
    return id;
}

QString OsmDirectory::convertedPathFor(const QString &downloadPath) {
    const QFileInfo info(downloadPath);
    QString base = info.fileName();
    if (base.endsWith(PbfSuffix, Qt::CaseInsensitive)) base.chop(PbfSuffix.size());
    return info.dir().filePath(base + Sorted::ConvertedSuffix);
}

bool OsmDirectory::scan(const QString &dir, QString &error) {
    dir_ = dir;
    entries_.clear();
    const QDir d(dir);
    if (dir.trimmed().isEmpty() || !d.exists()) {
        error = QStringLiteral("OSM data directory does not exist: %1").arg(dir);
        return false;
    }
    const QFileInfoList files = d.entryInfoList({QStringLiteral("*.osm.pbf")}, QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : files) {
        DirectoryEntry e;
        e.path = info.absoluteFilePath();
        e.size = info.size();
        QString headerError;
        if (!PbfFile::readHeader(e.path, e.header, headerError)) {
            e.error = headerError;
        } else if (e.header.hasFeature(Sorted::FeatureMarker)) {
            e.converted = true;
            if (!Sorted::SourceIdentity::fromString(e.header.source, e.source)) e.source = Sorted::SourceIdentity();
        }
        entries_.push_back(std::move(e));
    }
    for (DirectoryEntry &e : entries_) {
        if (!e.usable() || e.converted) continue;
        const Sorted::SourceIdentity id = e.identity();
        for (const DirectoryEntry &c : entries_)
            if (c.usable() && c.converted && c.source == id) { e.convertedPath = c.path; break; }
    }
    return true;
}

std::vector<const DirectoryEntry *> OsmDirectory::convertedFiles() const {
    std::vector<const DirectoryEntry *> v;
    for (const DirectoryEntry &e : entries_) if (e.usable() && e.converted) v.push_back(&e);
    return v;
}

std::vector<const DirectoryEntry *> OsmDirectory::pendingConversions(const Box *area) const {
    std::vector<const DirectoryEntry *> v;
    for (const DirectoryEntry &e : entries_) {
        if (!e.usable() || e.converted || !e.convertedPath.isEmpty()) continue;
        if (area && e.header.bbox.valid() && !e.header.bbox.intersects(*area)) continue;
        v.push_back(&e);
    }
    return v;
}

std::vector<const DirectoryEntry *> OsmDirectory::pendingOverviews(const OverviewConfig &config, const Box *area) const {
    std::vector<const DirectoryEntry *> v;
    for (const DirectoryEntry *e : convertedFiles()) {
        if (area && e->header.bbox.valid() && !e->header.bbox.intersects(*area)) continue;
        for (size_t l = 0; l < config.levels.size(); ++l)
            if (!overviewUpToDate(e->path, config, l)) { v.push_back(e); break; }
    }
    return v;
}

bool OsmDirectory::convert(const DirectoryEntry &download, bool deleteOriginal, const ConvertOptions &options,
                           ConvertStats &stats, QString &error, const ConvertProgress &progress, const std::atomic_bool *cancel) {
    if (download.converted) { error = QStringLiteral("%1 is already converted").arg(download.path); return false; }
    const QString converted = convertedPathFor(download.path);
    if (!convertPbf(download.path, converted, options, stats, error, progress, cancel)) return false;
    if (progress) progress(ConvertPhase::Overview, 0);
    std::vector<OverviewStats> overview;
    if (!buildOverviews(converted, OverviewConfig::standard(), overview, error, options.threads, cancel)) {
        error = QStringLiteral("Converted, but cannot build the overview maps: %1").arg(error);
        return false;
    }
    if (progress) progress(ConvertPhase::Overview, 1);
    if (deleteOriginal && !QFile::remove(download.path)) {
        error = QStringLiteral("Converted, but cannot delete %1").arg(download.path);
        return false;
    }
    return true;
}

}
