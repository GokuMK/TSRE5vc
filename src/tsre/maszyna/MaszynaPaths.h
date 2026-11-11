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
namespace MaszynaPaths {

// A name as content writes it, as MaSzyna reads it: lower case, with /, no
// leading slash, no known extension, its :traits apart.
struct Name {
    QString text;
    bool clampS = false, clampT = false;
};
Name clean(const QString &name);

// The places a clean name may be, in the order they are tried, without an
// extension: inside the vehicle's folder, then the shared folder.
QStringList places(const QString &name, const QString &vehicleFolder);

// Where a name found nowhere belongs: in the vehicle's folder when that has
// its first folder (skins/...), else the shared folder.
QString expectedPlace(const QString &name, const QString &vehicleFolder);

// The first of the places holding the name as a material, then with one of
// the texture extensions; empty for none.
QString findMaterial(const QStringList &places);
QString findTexture(const QStringList &places);

// MaSzyna's sounds/ in the game root (task maszyna 05): SOUND/MASZYNA, or
// sounds/ when the root is MaSzyna's own folder.
QString soundFolder();

// The vehicle's folder for a model: the texture path it was opened with when
// that holds an .mmd, else the nearest parent of the file holding one, at
// most two levels up, else the file's own folder.
QString vehicleFolder(const QString &modelPath, const QString &texPath);

}

#endif // MASZYNAPATHS_H
