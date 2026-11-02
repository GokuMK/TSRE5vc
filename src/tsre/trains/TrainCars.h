/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TRAINCARS_H
#define TRAINCARS_H

#include <QString>
#include <QStringList>
#include <QVector>

// A car of a train as its passengers see it (task scripting 03): its number
// and its tags. A timetable's #tsre.cars row has a bracket a vehicle, in the
// train's order from its front: "[-] [272 A] [271 WR] [270 B wheelchair]".
// The first word is the number ("-" none, "*" the consist's UiD), the rest
// tags: words TSRE passes on to documents as they are.
struct TrainCar {
    QString number;
    // Words separated by single spaces.
    QString tags;
    // "*": its consist's number kept.
    bool consistNumber = false;

    bool operator==(const TrainCar &other) const {
        return number == other.number && tags == other.tags && consistNumber == other.consistNumber;
    }
    bool operator!=(const TrainCar &other) const { return !(*this == other); }

    // A number or a tag as a cell can hold it: no spaces, no brackets.
    static QString Word(const QString &typed) {
        QString word = typed;
        word.remove('[').remove(']');
        return word.simplified().remove(' ');
    }
    // Tags as a cell can hold them: words, no brackets.
    static QString Words(const QString &typed) {
        QString words = typed;
        return words.remove('[').remove(']').simplified();
    }

    // The cars of a #tsre.cars cell (an empty one: none). False, none, for
    // text outside the brackets, a bracket not closed or an empty one.
    static bool Parse(const QString &cell, QVector<TrainCar> &cars) {
        cars.clear();
        int k = 0;
        while (k < cell.size()) {
            if (cell[k].isSpace()) {
                ++k;
                continue;
            }
            const int end = cell.indexOf(']', k);
            if (cell[k] != '[' || end < 0 || cell.mid(k + 1, end - k - 1).contains('[')) {
                cars.clear();
                return false;
            }
            const QStringList words = cell.mid(k + 1, end - k - 1).simplified().split(' ', Qt::SkipEmptyParts);
            if (words.isEmpty()) {
                cars.clear();
                return false;
            }
            TrainCar car;
            car.consistNumber = words.first() == "*";
            if (!car.consistNumber && words.first() != "-")
                car.number = words.first();
            car.tags = words.mid(1).join(' ');
            cars << car;
            k = end + 1;
        }
        return true;
    }
    // Their cell.
    static QString Text(const QVector<TrainCar> &cars) {
        QStringList brackets;
        for (const TrainCar &car : cars) {
            const QString number = Word(car.number);
            const QString tags = Words(car.tags);
            const QString first = car.consistNumber ? QString("*") : number.isEmpty() ? QString("-") : number;
            brackets << "[" + first + (tags.isEmpty() ? QString() : " " + tags) + "]";
        }
        return brackets.join(' ');
    }
};

#endif // TRAINCARS_H
