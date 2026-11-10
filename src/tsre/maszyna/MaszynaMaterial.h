/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MASZYNAMATERIAL_H
#define MASZYNAMATERIAL_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <cmath>
#include <memory>

// A MaSzyna material (task maszyna 01): a .mat, or a texture named without
// one. Only reads and finds its files; E3dShape draws it through
// MstsMaterials, with its shader family's preset (task maszyna 04).
// TSRE-only keys are tsre_metalness, tsre_roughness and tsre_orm, which
// MaSzyna skips.
struct MaszynaMaterial {
    // The files found, empty for none.
    QString base, normal, orm;
    // Its .mat, empty for a texture named without one.
    QString file;
    // As the content names them, for the messages.
    QString baseName, normalName, ormName;
    bool clampS = false, clampT = false;
    // Whether a .mat or a texture was found under the name.
    bool found = false;
    // Its base texture's file has an alpha channel, whatever its pixels: what
    // MaSzyna asks of a livery (DXT1 has none, DXT3 and DXT5 always do).
    bool alphaChannel = false;
    bool hasOpacity = false;
    float opacity = 0.5f;
    float metalness = NAN, roughness = NAN;
    // The light level below which it glows: 2 always, -1 never; NaN: the
    // submodel's.
    float selfIllum = NAN;
    QString shader;
    // Its other textures as named (task maszyna 04): texture2 (a reflection
    // map without a shader named), texture_specgloss, texture_reflmap; how
    // many texture slots it fills; its param_reflection and param_specular
    // (NaN: the shader's own).
    QString secondName, specGlossName, reflMapName;
    int textureCount = 0;
    float reflection = NAN, specular = NAN;
    // The map whose alpha masks its reflection (its family's: the reflmap,
    // texture2 or the normal map), as named and found.
    QString maskName, mask;
    // Its specgloss map found (texture_specgloss with a *_specgloss shader):
    // drawn in the metallic-roughness slot, read by its channels
    // (RenderItem::Pbr::specGlossInMetallicRoughness).
    QString specGlossMap;

    // MaSzyna's shader families (shaders/mat_*.frag at 66a940d4): Colored
    // (no texture), Diffuse (no reflection, no highlight: its default
    // material), Reflective (reflmap; texture2 without a shader: MaSzyna's
    // default_2), NormalMapped (normalmap, parallax, detail_*: reflection
    // masked by the normal map's alpha), MatteNormalMapped (sunless,
    // shadowless: no reflection), Other (water, windscreen rain, sky).
    enum class Family { Colored, Diffuse, Reflective, NormalMapped, MatteNormalMapped, Other };
    Family family() const;
    // Its shader names a specgloss map (R specular, G gloss, B metal).
    bool specGloss() const { return shader.contains(QLatin1String("specgloss")); }
    // Its PBR preset (task maszyna 04), as MaSzyna's apply_lights weighs
    // its reflection: the specular strength (KHR_materials_specular; its
    // mask's alpha times it), its param_reflection where given, else none
    // for the families without reflection (its default material: two
    // thirds of the pack's parts) and full for the reflective ones; their
    // roughness low (MaSzyna's reflection is a mirror's where no specgloss
    // map gives it), NaN for the others (no reflection to shape) and with a
    // specgloss map (its gloss gives it).
    float reflectionStrength() const;
    float presetRoughness() const;

    // The text of a .mat: its names and values, in the season given (lower
    // case, with its colon: "winter:"). No file is looked for.
    static MaszynaMaterial parse(const QByteArray &text, const QString &season);
    // The material a name means for this vehicle's folder, each read once;
    // an empty name, or a name found nowhere, gives one with found false.
    static std::shared_ptr<const MaszynaMaterial> find(const QString &name, const QString &vehicleFolder);
    // Whether a .mat or texture exists under the name.
    static bool exists(const QString &name, const QString &vehicleFolder);
    // Everything read is read again (a reload).
    static void forget();
};

#endif // MASZYNAMATERIAL_H
