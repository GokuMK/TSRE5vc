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

// One-time conversion of a downloaded .osm.pbf into the TSRE spatially sorted PBF
// (OsmSortedFormat.h). Keeps every tagged node, every tagged or relation-member way
// with its coordinates (LocationsOnWays) and every relation; drops untagged nodes
// and metadata.

#include <QString>
#include <atomic>
#include <cstdint>
#include <functional>

namespace Osm {

class PbfFile;

struct ConvertOptions {
    int threads = 0;            // 0: all hardware threads
    // zlib level of the output blocks. Poland on 12 threads: level 1 25 s / 2.39 GB,
    // level 6 37 s / 2.28 GB; the first conversion's time matters more than 5 % of disk.
    int compressionLevel = 1;
    QString tempDirectory;      // empty: a directory next to the output
};

enum class ConvertPhase { Scan, Relations, Nodes, Ways, Write, Overview };  // Overview: OsmDirectory, after converting

struct ConvertStats {
    uint64_t sourceNodes = 0, taggedNodes = 0;
    uint64_t sourceWays = 0, keptWays = 0, droppedWays = 0;
    uint64_t relations = 0, unplacedRelations = 0;
    uint64_t wayRefs = 0, missingRefs = 0, tags = 0;
    uint64_t outputBlocks = 0, outputBytes = 0, tempBytes = 0;
    uint64_t nodeTableBytes = 0;
    double scanSeconds = 0, relationSeconds = 0, nodeSeconds = 0, waySeconds = 0, writeSeconds = 0, totalSeconds = 0;
};

// Resources a conversion is expected to need, for checks before starting.
struct ConvertEstimate { int64_t memoryBytes = 0, tempBytes = 0, outputBytes = 0; };
ConvertEstimate estimateConversion(int64_t sourceBytes, int threads = 0);

// Called on the converting thread, roughly every 100 ms, with progress 0..1 within the phase.
using ConvertProgress = std::function<void(ConvertPhase, double)>;

// Writes output atomically (output + ".part", renamed when complete; an existing output is replaced).
// Returns false with error on failure or cancellation; temporary files are removed either way.
bool convertPbf(const QString &sourcePath, const QString &outputPath, const ConvertOptions &options,
                ConvertStats &stats, QString &error, const ConvertProgress &progress = {},
                const std::atomic_bool *cancel = nullptr);

}
