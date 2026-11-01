/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef UISHAPEDISPLAYS_H
#define UISHAPEDISPLAYS_H

#include <QDateTime>
#include <QString>
#include <QVector>
#include <memory>
#include <vector>

class ComplexShape;
class RenderQueue;
class Renderer;

namespace Ui {

// The displays of a shape's state (task scripting 02): its .sd's blocks
// (ShapeDisplays::read), each a document drawn into a texture when due (its
// refresh rate), shown through the state's texture override (a texture
// replaced: the whole texture is the document) or on a square of its own
// hung on one of the shape's parts. Documents bind the data model
// `display`: `time` and `date` (the clock's), `hours`, `minutes` and
// `seconds` (numbers, for clock hands), `station`, `platform`, and
// `departures` and `arrivals`, lists of rows (`time`, `train`,
// `destination`, `origin`, `platform`, `delay`). GUI thread only.
class ShapeDisplays {
public:
    struct Row {
        QString time, train, destination, origin, platform, delay;
    };
    // What the displays show besides the clock.
    struct Data {
        QString station, platform;
        QVector<Row> departures, arrivals;
    };

    explicit ShapeDisplays(const QString &shapePath);
    ~ShapeDisplays();
    // Whether the shape has displays.
    bool empty() const;
    // Each display drawn when due, at the clock's time `now` with `data`,
    // its texture given to the shape's state.
    void update(Renderer &renderer, ComplexShape &shape, unsigned int stateId, const QDateTime &now,
                const Data &data);
    // The squares, after the shape's own submission (the same transform):
    // each on its part as the state poses it.
    void pushSquares(RenderQueue &queue, ComplexShape &shape, unsigned int stateId, quint32 selectionId);
    // The textures freed and the state's overrides cleared.
    void release(Renderer &renderer, ComplexShape *shape, unsigned int stateId);

private:
    struct Entry;
    static void fill(Entry &entry, const QDateTime &now, const Data &data);
    QString folder;
    std::vector<std::unique_ptr<Entry>> entries;
};

}

#endif // UISHAPEDISPLAYS_H
