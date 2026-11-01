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
#include <QSize>
#include <QString>
#include <QVector>
#include <memory>

#include <tsre/shape/ShapeDisplays.h>

class Renderer;

namespace Ui {

class Display;

// A display's document drawn into a texture (task scripting 02), shared by
// the displays that show the same document with the same inputs at the same
// size (RouteDisplays). It binds the data model `display`: `time` and `date`
// (the clock's), `hours`, `minutes` and `seconds` (numbers, for clock
// hands), `station`, `platform`, and `departures` and `arrivals`, lists of
// rows (`time`, `train`, `destination`, `origin`, `platform`, `delay`: the
// minutes late, empty on time). GUI thread only.
class DisplayDocument {
public:
    struct Row {
        QString time, train, destination, origin, platform, delay;
    };
    // What a display shows besides the clock.
    struct Data {
        QString station, platform;
        QVector<Row> departures, arrivals;
    };

    // The document's file, its size in pixels, its redraws a second (0
    // once) and what it takes besides the clock (the lists for Platform and
    // Station only).
    DisplayDocument(const QString &path, QSize size, float refreshRate, ::ShapeDisplays::Display::Input input);
    // The texture stays: release() it with its renderer first.
    ~DisplayDocument();
    DisplayDocument(const DisplayDocument &) = delete;
    DisplayDocument &operator=(const DisplayDocument &) = delete;

    ::ShapeDisplays::Display::Input input() const { return takes; }
    // It did not load (RmlUi, the file): never drawn.
    bool failed() const { return broken; }
    // To be drawn: never yet, or its refresh time passed (wall time, the
    // same at any simulation speed).
    bool due() const;
    // Loaded on the first draw (its data model filled first), then its data
    // set to `now` and `data` and drawn into its texture.
    void draw(Renderer &renderer, const QDateTime &now, const Data &data);
    // 0 until drawn.
    unsigned int texture() const;
    // The texture freed by the renderer that made it; drawn again when due.
    void release(Renderer &renderer);

    // The frame it was last seen in (RouteDisplays: drawn while seen).
    unsigned int seen = 0;

private:
    struct Model;
    QString file;
    QSize pixels;
    float rate = 0.0f;
    ::ShapeDisplays::Display::Input takes = ::ShapeDisplays::Display::Input::None;
    bool broken = false;
    bool drawn = false;
    QElapsedTimer last;
    std::unique_ptr<Display> display;
    std::unique_ptr<Model> model;
    void fill(const QDateTime &now, const Data &data);
};

}

#endif // UIDISPLAYDOCUMENT_H
