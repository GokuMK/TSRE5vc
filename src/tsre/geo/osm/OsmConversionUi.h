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

#include <tsre/geo/osm/OsmTypes.h>
#include <QStringList>

class QWidget;

namespace Osm {

struct EnsureResult {
    bool hasDirectory = false;  // core.paths.osmData is set and exists
    int converted = 0;          // downloads converted now
    int declined = 0;           // downloads the user chose not to convert
    QStringList errors;
};

enum class EnsureMode { Ask, AcceptAll };

// First use of OSM data for an area: downloads in the OSM directory that cover the area and
// have no up-to-date converted copy are converted after asking the user (once per file and
// session; a declined file is not offered again until restart). Conversion runs on a worker
// thread behind a cancellable progress dialog. AcceptAll skips the question (tests).
EnsureResult ensureConverted(QWidget *parent, const Box &area, EnsureMode mode = EnsureMode::Ask);
// The same with an explicit directory and keep/delete choice instead of the settings.
EnsureResult ensureConverted(QWidget *parent, const QString &osmDirectory, bool deleteOriginal, const Box &area,
                             EnsureMode mode = EnsureMode::Ask);

// Available physical memory in bytes, 0 when unknown.
int64_t availableMemoryBytes();

}
