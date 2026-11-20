/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/E3dShape.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/shape/SFileComplex.h>
#include <QFileInfo>

// By the file's extension, as ShapeLib makes shapes (task characters 02).
bool ComplexShape::readTriangleParts(const QString &path, const QString &texPath, std::vector<TrianglePart> &parts) {
    const QString extension = QFileInfo(path).suffix().toLower();
    if (extension == QLatin1String("gltf") || extension == QLatin1String("glb"))
        return GltfShape::cpuTriangleParts(path, texPath, parts);
    if (extension == QLatin1String("e3d"))
        return E3dShape::cpuTriangleParts(path, parts);
    return SFileComplex::cpuTriangleParts(path, texPath, parts);
}
