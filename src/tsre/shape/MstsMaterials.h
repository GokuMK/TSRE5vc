/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MSTSMATERIALS_H
#define MSTSMATERIALS_H

#include <QHash>
#include <QString>
#include <QVector>
#include <cmath>
#include <functional>
#include <vector>

class RenderItem;

// PBR materials of MSTS shapes (task shapes 13): primitive states drawn with
// the shader names PbrTexDiff (opaque, or alpha-tested as the state says) and
// BlendAPbrTexDiff (blended glass) through the glTF shading. Their textures
// (tex_idxs): the base colour, ORM(C) (occlusion, roughness, metalness, a
// clearcoat mask in alpha) and a tangent-space normal map (OpenGL: green
// up). Their factors come from the shape's .sd, one line a material keyed by
// its base colour texture:
//   ESD_TSREMaterial ( body.ace 1 1 1 1 0 0.4 1 1 1 0.05 )
// tint (rgba), metalness, roughness, normal strength, occlusion strength,
// clearcoat, clearcoat roughness, transmission; values left out keep their
// defaults. Open Rails and MSTS skip the token.
namespace MstsMaterials {

enum class Shader { Other, Pbr, PbrBlended };
// PbrTexDiff or BlendAPbrTexDiff (any case).
Shader shader(const QString &name);
inline bool pbr(Shader s) { return s != Shader::Other; }

// The .sd token.
inline constexpr const char Token[] = "ESD_TSREMaterial";

struct Factors {
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    // Multiply the ORM's metalness (B) and roughness (G); without an ORM
    // texture they are the values. Unset (NaN): 1 with an ORM, else a
    // dielectric (0) of roughness 0.5.
    float metalness = NAN;
    float roughness = NAN;
    // The normal map's strength and the occlusion's (R).
    float normal = 1.0f;
    float occlusion = 1.0f;
    // The varnish layer (times the ORM's alpha) and its roughness.
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.0f;
    // Light passing through (glass), drawn in the transmission pass.
    float transmission = 0.0f;

    static constexpr int Count = 11;
    // The values of a line after its key, in order; missing ones (or
    // fewer) keep their defaults.
    static Factors read(const QVector<float> &values);
    // The values to write: up to the last that differs from its default
    // (an unset metalness or roughness written as the default it stands
    // for, given whether there is an ORM texture).
    QVector<float> values(bool orm) const;
    float metalnessFor(bool orm) const { return std::isnan(metalness) ? (orm ? 1.0f : 0.0f) : metalness; }
    float roughnessFor(bool orm) const { return std::isnan(roughness) ? (orm ? 1.0f : 0.5f) : roughness; }
    bool operator==(const Factors &o) const;
};

// A material's key in the .sd: its base colour texture's file name, lower
// case (without the directory).
QString key(const QString &imageName);

// MSTS light materials (vtx_state), as Open Rails reads them: -6 Specular
// 25, -7 Specular 750; the specular power, 0 for none.
float specularPower(int lightMaterial);
// A packet's lighting for a light material, as the shapes draw it: lit down
// to -7, half bright at -12, vegetation for the cruciform -9 and -10, and
// the specular power.
void applyLight(RenderItem &item, int lightMaterial);

// A packet's PBR state for such a material (its base colour is the
// packet's texture): the factors, the ORM and normal map (TexLib ids, -1
// none), the surface (blended for PbrBlended, else alpha-tested at 0.51 or
// opaque) and lighting.
void fill(RenderItem &item, const Factors &factors, int ormTexture, int normalTexture, Shader shader,
          bool alphaTest);

// Per-vertex tangents (x, y, z, handedness) from the triangles' texture
// coordinate gradients, accumulated per vertex and made perpendicular to its
// normal (as glTF shapes without TANGENT get them), the bitangent up the
// image (texture coordinates grow down it, as MSTS and glTF have them). `vertex` gives a
// vertex's position, normal and texture coordinates; `triangles` holds
// vertex index triples. Out: 4 floats a vertex.
using VertexReader = std::function<bool(int vertex, float *position, float *normal, float *uv)>;
void tangents(int vertexCount, const VertexReader &vertex, const std::vector<int> &triangles,
              std::vector<float> &out);

// An ORM(C) texture packed from grayscale maps (their luminance), one a
// channel: occlusion (R), roughness (G), metalness (B), clearcoat (A); a
// missing file (empty path) gives its channel the constant (0 to 1). The
// maps are scaled to the largest; written as PNG (`out`).
struct OrmSources {
    QString files[4];
    float constants[4] = {1.0f, 0.5f, 0.0f, 1.0f};
};
bool packOrm(const OrmSources &sources, const QString &out, QString *error = nullptr);

} // namespace MstsMaterials

#endif // MSTSMATERIALS_H
