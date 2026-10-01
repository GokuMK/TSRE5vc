/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TSRE_TESTS_RENDERERPARITYTESTSUITE_H
#define TSRE_TESTS_RENDERERPARITYTESTSUITE_H

#include <QString>

namespace TsreTests {
// Renders route views with the pipeline selected at startup
// (core.rendering.pipeline) and writes images, picking IDs and counters.
// The pipeline is never switched at runtime. Requires route content and GL.
int runRendererCaptureSuite(const QString &casesFile, bool verbose);

// Compares a legacy and a gather capture of the same cases file and route.
// Needs no GL context.
int runRendererCompareSuite(const QString &casesFile, bool verbose);
}

#endif // TSRE_TESTS_RENDERERPARITYTESTSUITE_H
