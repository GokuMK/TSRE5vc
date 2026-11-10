/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MASZYNAFIZ_H
#define MASZYNAFIZ_H

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

// A vehicle's .fiz as MaSzyna's parser reads it (task maszyna 03,
// LoadFIZ_Parse): its sections by name, each with its keys' values and its
// table's rows. A line is the unit; a section is a case-sensitive header at
// its start; a key is `Key=value` after a space, case-sensitive, its first
// in a line counting; a table's rows run to its END marker (Cntrl.'s to the
// next section); lines with `#` are dropped; includes with their (pN). A
// repeated section is read again, its keys overriding.
//
// Values in SI where the key catalogue knows the key, from the unit
// MaSzyna's parser takes it in (km/h, kN, kN/m, bar, kW, litres, rev/s…);
// as written where it does not, numbers as numbers, the rest as text.
// Rotational speeds are rpm, as TSRE's engines take them. MotorParamTable
// rows stay as written: their columns differ by engine type.
struct MaszynaFiz {
    struct Value {
        bool isNumber = false;
        double number = 0.0;
        QString text;
    };
    struct Section {
        // The header without its . or : ("Param", "Engine", "RList").
        QString name;
        // In the order first given.
        QVector<QPair<QString, Value>> keys;
        QVector<QVector<Value>> rows;
        const Value *value(QStringView key) const;
        double number(QStringView key, double otherwise = 0.0) const;
        QString text(QStringView key) const;
    };
    QVector<Section> sections;
    // Every file read, includes not found.
    QStringList files, problems;

    const Section *section(QStringView name) const;
    double number(QStringView section, QStringView key, double otherwise = 0.0) const;
    QString text(QStringView section, QStringView key) const;

    // The file `name` in the vehicle's `folder`, where its includes are
    // looked for too; empty (no sections) when it is not there.
    static MaszynaFiz read(const QString &folder, const QString &name);
};

#endif // MASZYNAFIZ_H
