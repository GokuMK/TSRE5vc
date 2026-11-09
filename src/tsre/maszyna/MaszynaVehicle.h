/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MASZYNAVEHICLE_H
#define MASZYNAVEHICLE_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

struct VehicleDefinition;

// What one MaSzyna vehicle type says (task maszyna 02): its .fiz and .mmd,
// read once per type and shared by its livery rows and every car made from
// them.
struct MaszynaVehicle {
    QString folder, type;
    QString fizPath, mmdPath;

    // From the .fiz; the first value of a key counts.
    QString category, fizType, couplerType, engineType, loadAccepted, doorOpenMethod, stepOpenMethod, pantType;
    double massKg = 0.0, maxSpeedKmh = 0.0, powerKw = 0.0;
    float length = 0.0f, height = 0.0f, width = 0.0f, wheelDiameter = 0.0f;
    float doorShiftLeft = 0.0f, doorShiftRight = 0.0f, doorPlugShift = 0.0f, stepShift = 0.0f, mirrorShift = 0.0f;
    float wiperAngle = 0.0f;

    // From the .mmd, names as written.
    QString model, lowpolyInterior;
    QStringList attachments;
    // In the order wheels, doors, levers, buffers, bogies, pantographs,
    // pistons, door steps, mirrors, wipers; -1 not given.
    static constexpr int AnimationKinds = 10;
    int animations[AnimationKinds] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    // "animwheelprefix" and the like, lower-case key without its colon.
    QHash<QString, QString> prefixes;
    float pantFactors[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bool hasPantFactors = false;
    QVector<float> bogieOffsets;
    QStringList cabModels;
    // TSRE's own keys, tsre_...: without their colon.
    QHash<QString, QString> tsre;

    // Includes not found and the like, for the messages.
    QStringList problems;

    // The type in the folder, null when its .mmd or .fiz is not there; read
    // once, and again when one of them changes.
    static std::shared_ptr<const MaszynaVehicle> read(const QString &folder, const QString &type);
    // Everything read is read again.
    static void forget();
};

// A vehicle folder's catalogue, textures.txt.
struct MaszynaCatalogue {
    struct Livery {
        QString skin, type;         // lower case
        QString name, keywords;     // from its metadata
    };
    QVector<Livery> liveries;
    // A type's kind letter (its category line): e, s, p... An upper-case
    // letter is a carriage.
    QHash<QString, QChar> kinds;
    // Read once per folder, and again when it changes.
    static std::shared_ptr<const MaszynaCatalogue> read(const QString &folder);
};

namespace MaszynaVehicles {

// The rows a vehicle folder gives: file names as Eng takes them,
// <type>#<skin>.mmd, one per livery in its catalogue, <type>#.mmd for a type
// with none. Only railway vehicles (Category=train). Liveries not in the
// catalogue are counted in `unlisted`.
QStringList rows(const QString &folder, int *unlisted = nullptr);
// Whether a folder holds a vehicle: an .mmd with its .fiz.
bool holdsVehicle(const QString &folder);
// The first row of a type (what the Shape Viewer opens for an .mmd).
QString firstRow(const QString &folder, const QString &type);
// Whether a file name (or a consist's vehicle name) is a MaSzyna row.
inline bool isRow(const QString &name) { return name.contains(QLatin1Char('#')); }
// The file Eng takes for a consist's vehicle name: .mmd for a MaSzyna row,
// else with the extension given.
inline QString fileOf(const QString &name, const QString &extension) {
    return name + (isRow(name) ? QStringLiteral(".mmd") : extension);
}

// The definition of a row in a folder; ok() false when it is not there.
std::shared_ptr<const VehicleDefinition> definition(const QString &folder, const QString &row);

}

#endif // MASZYNAVEHICLE_H
