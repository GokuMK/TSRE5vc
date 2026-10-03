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
// Renders route views and writes images, picking IDs and counters to the
// capture named `label`. Requires route content and GL.
int runRendererCaptureSuite(const QString &casesFile, const QString &label, bool verbose);

// Compares two captures of the same cases file and route, for example one
// made before a renderer change and one after. Needs no GL context.
int runRendererCompareSuite(const QString &casesFile, const QString &baselineLabel,
                            const QString &label, bool verbose);

// Shape Viewer counterparts: render listed shapes, engines and consists, then
// compare two captures.
int runShapeViewerCaptureSuite(const QString &casesFile, const QString &label, bool verbose);
int runShapeViewerCompareSuite(const QString &casesFile, const QString &baselineLabel,
                               const QString &label, bool verbose);
}

#endif // TSRE_TESTS_RENDERERPARITYTESTSUITE_H
