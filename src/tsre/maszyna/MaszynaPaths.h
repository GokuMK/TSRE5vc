/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MASZYNAPATHS_H
#define MASZYNAPATHS_H

#include <QString>
#include <QStringList>

// Where MaSzyna's content finds its files in a TSRE game root (task maszyna
// 02): dynamic/ is the trainset, textures/ is TEXTURES/MASZYNA. Names are
// lower-cased because the format says so; no folder is searched for them.
// That is for a .mmd vehicle's models only: any other model finds its files
// by TSRE's rules, names as written in one folder (task shapes 18).
namespace MaszynaPaths {

// How a model's names are spelt and where they are looked for. Maszyna: a
// .mmd vehicle's, lower case, its folder, then the shared folders. Tsre:
// every other model's, as written, in one folder (its texture path), as an
// S file's textures; a dynamic/ name is found nowhere.
struct Lookup {
    enum class Rules : unsigned char { Maszyna, Tsre };
    Rules rules = Rules::Maszyna;
    QString folder;

    static Lookup maszyna(const QString &vehicleFolder) { return {Rules::Maszyna, vehicleFolder}; }
    static Lookup tsre(const QString &folder) { return {Rules::Tsre, folder}; }
    // Tells lookups apart in a cache.
    QString key() const;
};

// A name as content writes it, as MaSzyna reads it: lower case (MaSzyna's
// rules only), with /, no leading slash, no known extension (in any case),
// its :traits apart.
struct Name {
    QString text;
    bool clampS = false, clampT = false;
};
Name clean(const QString &name);
Name clean(const QString &name, Lookup::Rules rules);

// The places a clean name may be, in the order they are tried, without an
// extension: inside the vehicle's folder, then the shared folder.
QStringList places(const QString &name, const QString &vehicleFolder);
// The same by the lookup's rules: TSRE's give one place, the folder and the
// name without a leading textures/, and none for a dynamic/ name.
QStringList places(const QString &name, const Lookup &lookup);

// Where a name found nowhere belongs: in the vehicle's folder when that has
// its first folder (skins/...), else the shared folder.
QString expectedPlace(const QString &name, const QString &vehicleFolder);
QString expectedPlace(const QString &name, const Lookup &lookup);

// The first of the places holding the name as a material, then with one of
// the texture extensions; empty for none.
QString findMaterial(const QStringList &places);
QString findTexture(const QStringList &places);

// The vehicle's folder for a model: the texture path it was opened with when
// that holds an .mmd, else the nearest parent of the file holding one, at
// most two levels up, else the file's own folder.
QString vehicleFolder(const QString &modelPath, const QString &texPath);
// Whether the model's folder, or one of two parents, holds an .mmd: a model
// opened on its own takes MaSzyna's rules there (the Shape Viewer). It
// lists those folders.
bool besideVehicle(const QString &modelPath);

}

#endif // MASZYNAPATHS_H
