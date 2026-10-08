/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#pragma once

// The user's OSM data directory (setting core.paths.osmData): top-level *.osm.pbf
// files only. Downloaded files are paired with their converted <base>.tsre.osm.pbf
// copies by the source identity recorded in the converted header, not by name.

#include <tsre/geo/osm/OsmConverter.h>
#include <tsre/geo/osm/OsmPbf.h>
#include <tsre/geo/osm/OsmSortedFormat.h>
#include <QString>
#include <vector>

namespace Osm {

struct DirectoryEntry {
    QString path;
    int64_t size = 0;
    HeaderInfo header;
    bool converted = false;           // a TSRE sorted file
    Sorted::SourceIdentity source;    // converted files: the download they were made from
    QString convertedPath;            // downloads: an up-to-date converted copy, empty when conversion is needed
    QString error;                    // unreadable or not a PBF; such entries are otherwise ignored

    bool usable() const { return error.isEmpty(); }
    Sorted::SourceIdentity identity() const;  // identity of this file as a conversion source
};

class OsmDirectory {
public:
    // Reads the headers of every top-level *.osm.pbf in dir (subdirectories are not read).
    bool scan(const QString &dir, QString &error);
    const QString &path() const { return dir_; }
    const std::vector<DirectoryEntry> &entries() const { return entries_; }

    // Converted files to query; all of them, also those whose download has changed since.
    std::vector<const DirectoryEntry *> convertedFiles() const;
    // Downloads without an up-to-date converted copy. With an area, only files whose header bbox
    // intersects it (files without a bbox always match).
    std::vector<const DirectoryEntry *> pendingConversions(const Box *area = nullptr) const;

    // <dir>/<base>.tsre.osm.pbf for <dir>/<base>.osm.pbf.
    static QString convertedPathFor(const QString &downloadPath);

    // Converts one download next to itself; with deleteOriginal the download is removed after
    // the converted file is complete. Call scan() again afterwards.
    static bool convert(const DirectoryEntry &download, bool deleteOriginal, const ConvertOptions &options,
                        ConvertStats &stats, QString &error, const ConvertProgress &progress = {},
                        const std::atomic_bool *cancel = nullptr);

private:
    QString dir_;
    std::vector<DirectoryEntry> entries_;
};

}
