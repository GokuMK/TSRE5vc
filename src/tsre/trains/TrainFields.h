/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TRAINFIELDS_H
#define TRAINFIELDS_H

#include <QString>

// TSRE's own fields of a train, the train as its passengers see it (task
// scripting 02, step 10): its category, number, proper name, operator,
// line, and where it comes from and goes to beyond the route; each
// optional. A timetable's #tsre.<field> rows; documents' `row.<field>` and
// `train.<field>`.
struct TrainFields {
    enum Field { Category, Number, Name, Operator, Line, Origin, Destination, Count };

    // The field's key: its row's word after "#tsre.", its name in documents.
    static const char *key(int field) {
        static const char *const keys[Count] = {"category", "number", "name", "operator",
                                                "line", "origin", "destination"};
        return field >= 0 && field < Count ? keys[field] : "";
    }
    // The field of a key, -1 none.
    static int field(const QString &key) {
        for (int f = 0; f < Count; ++f)
            if (key == QLatin1String(TrainFields::key(f)))
                return f;
        return -1;
    }

    QString values[Count];
    QString &operator[](int field) { return values[field]; }
    const QString &operator[](int field) const { return values[field]; }
    bool isEmpty() const {
        for (const QString &value : values)
            if (!value.isEmpty())
                return false;
        return true;
    }
    bool operator==(const TrainFields &other) const {
        for (int f = 0; f < Count; ++f)
            if (values[f] != other.values[f])
                return false;
        return true;
    }
    bool operator!=(const TrainFields &other) const { return !(*this == other); }
};

#endif // TRAINFIELDS_H
