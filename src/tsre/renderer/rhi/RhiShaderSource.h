/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RHISHADERSOURCE_H
#define RHISHADERSOURCE_H

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

// Turns the GLSL 3.30 programs of shaders330 into the Vulkan-style GLSL 4.40
// QShaderBaker takes, so both renderers draw with one set of shader sources:
// variant #ifdefs are evaluated, loose uniforms of both stages become one
// std140 block, samplers keep their OpenGL texture units as bindings, and
// attributes and varyings get locations.
namespace RhiShaderSource {

// Binding of the block holding the loose uniforms, and of named blocks.
constexpr int UniformBlockBinding = 20;
constexpr int TerrainPatchBlockBinding = 21;

// Vertex attribute locations (as GLUU binds them), and the per-instance
// matrix columns the QRhi renderer adds.
const QHash<QString, int> &attributeLocations();
// Sampler bindings: the texture unit each sampler uses in OpenGL.
const QHash<QString, int> &samplerBindings();

// Keeps the lines of the branches whose #ifdef / #ifndef / #if defined /
// #elif defined conditions hold for the defines.
QByteArray preprocess(const QByteArray &source, const QSet<QString> &defines);

struct Program {
    QByteArray vertex;
    QByteArray fragment;
    // Problems found while converting; empty when it worked.
    QString error;
};
// Converts a program from its include-expanded GLSL 3.30 stage sources.
Program convert(const QByteArray &vertex, const QByteArray &fragment,
                const QStringList &defines);

}

#endif
