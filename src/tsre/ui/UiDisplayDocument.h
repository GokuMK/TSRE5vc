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

class Renderer;

namespace Ui {

class Display;

// A message for a display's author (TSRE's Errors and Messages, World), each
// text once; GUI thread only.
void displayMessage(bool error, const QString &text, const QString &action = QString());

// A display's document drawn into a texture (task scripting 02), shared by
// the displays that show the same document with the same inputs at the same
// size (RouteDisplays). It binds the data model `display`: `time` and `date`
// (the clock's), `hours`, `minutes` and `seconds` (numbers, for clock
// hands), `station`, `platform`, and `departures` and `arrivals`, lists of
// rows (`time`, `train`, `destination`, `origin`, `platform`, `delay`: the
// minutes late, empty on time; `extra`: the train's data by key, as
// `row.extra.number`, a key it has not empty text); for a car's display
// `train`: its train's `name`, `origin` and `destination` (its first and
// last stops' stations), its `state` (`waiting`, `standing` at a station,
// `held` elsewhere, `running`, `approaching` its next stop, `finished`;
// empty without a simulation), `at` (the station it stands at), `next`
// (the next stop it reaches), `previous` (the station it left last),
// `platform` (at's, else next's), `departs` (booked, from at, or from its
// first stop while waiting), `arrives` (booked, at next), `distance` (m to
// next, where known), `delay` and `extra`. GUI thread only. On each draw
// its document gets a `displayupdate` event (its Lua: `ondisplayupdate`),
// `time` the clock's seconds after midnight. Drawn before its pictures
// loaded (TexLib loads them in the background), it is drawn again soon.
class DisplayDocument {
public:
    struct Row {
        QString time, train, destination, origin, platform, delay;
        QMap<QString, QString> extra;
    };
    struct Train {
        QString name, origin, destination, state, at, next, previous, platform, departs, arrives, distance, delay;
        QMap<QString, QString> extra;
    };
    // What a display shows besides the clock.
    struct Data {
        QString station, platform;
        QVector<Row> departures, arrivals;
        Train train;
    };

    // The document's file, its size in pixels, its redraws a second (0
    // once) and what it takes besides the clock (the lists for Platform and
    // Station only, the train for Train).
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
    // The texture freed by the renderer that made it; drawn again when due.
    void release(Renderer &renderer);

    // The frame it was last seen in (RouteDisplays: drawn while seen) and
    // its nearest display then (m from the camera).
    unsigned int seen = 0;
    float distance = 0.0f;
    // Seen in `frame` by a display `metres` from the camera.
    void see(unsigned int frame, float metres) {
        distance = seen == frame ? std::min(distance, metres) : metres;
        seen = frame;
    }

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
    std::unique_ptr<Display> display;
    std::unique_ptr<Model> model;
    void fill(const QDateTime &now, const Data &data);
};

}

#endif // UIDISPLAYDOCUMENT_H
