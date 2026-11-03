/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef UIDISPLAYDOCUMENT_H
#define UIDISPLAYDOCUMENT_H

#include <QDateTime>
#include <QElapsedTimer>
#include <QMap>
#include <QSize>
#include <QString>
#include <QVector>
#include <algorithm>
#include <memory>

#include <tsre/shape/ShapeDisplays.h>
#include <tsre/trains/TrainFields.h>

class Renderer;

namespace Ui {

class Display;

// A message for a display's author (TSRE's Errors and Messages, World), each
// text once; GUI thread only.
void displayMessage(bool error, const QString &text, const QString &action = QString());

// A display's document drawn into a texture (task scripting 02), shared by
// the displays that show the same document with the same inputs at the same
// size (RouteDisplays). It binds the data model `display`: `time` and `date`
// (the clock's), `clock` (seconds after midnight), `hours`, `minutes`,
// `seconds`, `year`, `month` and `day` (numbers), `station`, `platform`, and `departures` and `arrivals`, lists
// of rows (`time`, `id`: the train's timetable name, `platform`, `delay`:
// the minutes late, empty on time; the train's fields, TrainFields'
// `category` … `destination`, `origin` and `destination` its own where it
// has them, else its first and last stops' stations on the route (none
// where it comes onto the route or leaves it here); `extra`: the train's
// data by key, as `row.extra.kind`, a key it has not empty text); for a
// car's display `train`: its train's `id`, its fields (`origin` and
// `destination` its own, else its first and last stops' stations), its
// `state` (`waiting`, `standing` at a station, `held` elsewhere, `running`,
// `approaching` its next stop, `finished`; empty without a simulation),
// `at` (the station it stands at), `next` (the next stop it reaches),
// `previous` (the station it left last), `platform` (at's, else next's),
// `departs` (booked, from at, or from its first stop while waiting),
// `arrives` (booked, at next), `distance` (m to next, where known),
// `delay`, `stops` (its stops on the route: `station`, `platform`,
// `arrives`, `departs`, `passed`: left already) and `extra`. TSRE's
// functions for its expressions: UiDocumentFunctions. GUI thread only. On
// each draw its document gets a `displayupdate` event (its Lua:
// `ondisplayupdate`), `time` the clock's seconds after midnight. Drawn
// before its pictures loaded (TexLib loads them in the background), it is
// drawn again soon.
class DisplayDocument {
public:
    // A car of a train (task scripting 03): its place from the train's
    // front (from 0), its number and tags, its kind (engine, carriage,
    // freight, tender), its file's name.
    struct Car {
        int index = 0;
        QString number, tags, kind, name;
    };
    // A train at a station or a platform: its time there (a list's: a
    // departure's, an arrival's; a platform's train its next), its booked
    // arrival and departure there (empty where it starts or ends), its
    // cars (a platform's next two trains only).
    struct Row {
        QString time, id, platform, delay, arrives, departs;
        TrainFields fields;
        QMap<QString, QString> extra;
        QVector<Car> cars;
    };
    struct Stop {
        QString station, platform, arrives, departs;
        bool passed = false;
    };
    struct Train {
        QString id, state, at, next, previous, platform, departs, arrives, distance, delay;
        TrainFields fields;
        QVector<Stop> stops;
        QMap<QString, QString> extra;
        QVector<Car> cars;
    };
    // What a display shows besides the clock: a station's departures and
    // arrivals, a platform's trains in the order they come (task scripting
    // 03: each once).
    struct Data {
        QString station, platform;
        QVector<Row> departures, arrivals, trains;
        Train train;
        // The car the display is on (Car).
        Car car;
    };

    // The document's file, its size in pixels, its redraws a second (0
    // once) and what it takes besides the clock (the trains for Platform,
    // the departures and arrivals for Station, the train for Train and Car,
    // the car for Car).
    DisplayDocument(const QString &path, QSize size, float refreshRate, ::ShapeDisplays::Display::Input input);
    // The texture stays: release() it with its renderer first.
    ~DisplayDocument();
    DisplayDocument(const DisplayDocument &) = delete;
    DisplayDocument &operator=(const DisplayDocument &) = delete;

    ::ShapeDisplays::Display::Input input() const { return takes; }
    // It did not load (RmlUi, the file): never drawn.
    bool failed() const { return broken; }
    // To be drawn: never yet, its refresh time passed (wall time, the same
    // at any simulation speed), or its pictures were not all there.
    bool due() const;
    // How long it is overdue (ms; 0 when not due); one never drawn longest.
    qint64 waiting() const;
    // Its next draw loads it (its file read and laid out: the costly one).
    bool unloaded() const { return !broken && !display; }
    // Its last draw left out pictures not yet loaded.
    bool waitsForPictures() const { return picturesPending; }
    // Loaded on the first draw (its data model filled first), then its data
    // set to `now` and `data` and drawn into its texture.
    void draw(Renderer &renderer, const QDateTime &now, const Data &data);
    // 0 until drawn.
    unsigned int texture() const;
    // The texture freed by the renderer that made it; the document stays
    // loaded and is due at once (drawn again when seen).
    void release(Renderer &renderer);

    // The frame it was last seen in (RouteDisplays: drawn while seen) and
    // its nearest display then (m from the camera).
    unsigned int seen = 0;
    float distance = 0.0f;
    // Seen in `frame` by a display `metres` from the camera.
    void see(unsigned int frame, float metres) {
        distance = seen == frame ? std::min(distance, metres) : metres;
        seen = frame;
        sighted.start();
    }
    // Not seen for `ms` or longer (wall time); false before it is seen.
    bool unseenFor(qint64 ms) const { return sighted.isValid() && sighted.elapsed() >= ms; }

private:
    struct Model;
    QString file;
    QSize pixels;
    float rate = 0.0f;
    ::ShapeDisplays::Display::Input takes = ::ShapeDisplays::Display::Input::None;
    bool broken = false;
    bool drawn = false;
    bool picturesPending = false;
    QElapsedTimer last;
    // Since it was last seen.
    QElapsedTimer sighted;
    std::unique_ptr<Display> display;
    std::unique_ptr<Model> model;
    void fill(const QDateTime &now, const Data &data);
};

}

#endif // UIDISPLAYDOCUMENT_H
