/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef UIROUTEDISPLAYS_H
#define UIROUTEDISPLAYS_H

#include <QHash>
#include <QString>
#include <QVector>
#include <memory>
#include <vector>

#include <tsre/shape/ShapeDisplays.h>
#include <tsre/ui/UiDisplayDocument.h>

class ComplexShape;
class RenderQueue;
class Renderer;
class TDB;

namespace Sim {
struct Vehicle;
struct SimulationInput;
struct SimulationFrame;
}

namespace Ui {

class ShapeDisplays;
struct PlacedDisplays;

// The route's displays (task scripting 02): the placed objects' displays,
// each bound on its first drawn frame to the platform line nearest it (a
// square: a few metres in front of it, where its readers stand), nothing
// stored; and the trains' cars' displays, bound to the train each car's
// train gives it. Their documents are shared by all the displays showing
// the same document with the same inputs at the same size (a train's cars
// one), and drawn when due while at least one of them is in view, a few a
// frame (redraws()), at the world clock's time (the simulation's), with the
// simulation's trains' departures and arrivals, or a train's data. GUI
// thread only.
class RouteDisplays {
public:
    static RouteDisplays &instance();

    // A placed object drawn, after its shape's own submission (the same
    // transform; `matrix` its own, on its tile), `distance` metres from the
    // camera: its displays made once its shape has loaded, bound again when
    // it moves, changes its shape or its state, then pushed. The object
    // keeps `placed` (null for one without displays). Its tile as world
    // objects keep it (y: minus MSTS's tile z).
    void push(std::unique_ptr<PlacedDisplays> &placed, RenderQueue &queue, ComplexShape &shape,
              unsigned int stateId, int tileX, int tileY, const float *matrix, quint32 selectionId,
              float distance);
    // A placed object or a car about to be drawn, before its shape's own
    // submission: the state's textures as its documents' are now
    // (ShapeDisplays::overrideTextures; nothing before its displays are
    // made or after it changes its shape or state: push binds them again).
    void overrideTextures(const std::unique_ptr<PlacedDisplays> &placed, ComplexShape &shape,
                          unsigned int stateId);
    // A car drawn, after its shape's own submission in its state: the same
    // for its displays, bound again when its train (the simulation's, as its
    // train gives it; -1 none), its place in it (task scripting 03: `car`
    // from its front, in the train or in standing train `standing`), shape
    // or state change.
    void pushCar(std::unique_ptr<PlacedDisplays> &placed, RenderQueue &queue, ComplexShape &shape,
                 unsigned int stateId, int train, int car, int standing, quint32 selectionId, float distance);

    // A frame of the view, before its scene is gathered: the documents seen
    // in the frame before drawn when due, at most the frame's budget
    // (core.rendering.displayRedraws); those no display holds any more
    // freed; the textures of those not seen for a while freed
    // (core.rendering.displayKeepUnseen, seconds), their documents kept
    // loaded and drawn again first when seen.
    void frame(Renderer &renderer);
    // A document due: how long it is overdue (ms; DisplayDocument::waiting),
    // how far its nearest display is (m) and whether its draw loads it.
    struct Due {
        qint64 waiting = 0;
        float distance = 0.0f;
        bool loads = false;
    };
    // Which of them a frame draws (their indices): at most `budget`, one of
    // them loaded (a load costs tens of times a redraw), the longest overdue
    // first, one never drawn before any, a farther one as if it became due
    // later (10 ms a metre). None is left out for good: one waiting grows
    // past those drawn again.
    static std::vector<int> redraws(const std::vector<Due> &due, int budget);
    // Every document's texture freed (its renderer goes); drawn again when
    // seen.
    void release(Renderer &renderer);

    // A platform line: the TDB's two platform items, its station and its
    // name (trimmed); start -1 for none.
    struct Platform {
        int start = -1;
        int end = -1;
        QString station;
        QString name;
    };
    // The platform line of `tdb` nearest a point within 300 m (x and z,
    // MSTS's: a tile × 2048 + its position), along its track.
    Platform nearestPlatform(TDB &tdb, double x, double z);
    // What a display bound to `platform` shows at `now` (seconds after
    // midnight, the plans' clock): its station's name, its platform's (not
    // for Station); for Platform, the stops at its line; for Station, at its
    // station's. Departures (not a train's last stop, unless a timetable
    // books it to leave there: two times, it runs on beyond the route) and
    // arrivals (not where it starts), booked times, by time; a stop passed is left out (by
    // the train's state in `frame`; without one, by its booked time), and so
    // is a train finished. A row's origin: the station of the train's first
    // stop, its destination: of its last (none where that is this stop: it
    // comes onto the route or leaves it here). The delay: minutes, the
    // train's now (leaving late where it stands, past its next stop's
    // arrival, past its start). Its extras: the train's (a timetable's
    // #tsre.extra.<key> rows).
    static DisplayDocument::Data data(::ShapeDisplays::Display::Input input, const Platform &platform,
                                      const TDB &tdb, const Sim::SimulationInput *in,
                                      const Sim::SimulationFrame *frame, double now);
    // What a car's display shows of its train (Train): its name, its first
    // and last stops' stations, where it is on its way by its state in
    // `frame` (DisplayDocument's `train`: its state, the stop it stands at,
    // its next and its previous, their booked times, the metres to the next
    // where its route has them; approaching within ApproachDistance; without
    // a state, no state and its first stop next), its delay as the boards
    // have it, its extras, its cars as they are in `frame` (else its plan's;
    // task scripting 03). Nothing for no train.
    static DisplayDocument::Data trainData(int train, const TDB &tdb, const Sim::SimulationInput *in,
                                           const Sim::SimulationFrame *frame, double now);
    // A Car display's: its train's (trainData) and its car, `car` from the
    // train's front; a standing train's car (`standing` its place in the
    // frame's, `train` -1): its car only.
    static DisplayDocument::Data carData(int train, int car, int standing, const TDB &tdb,
                                         const Sim::SimulationInput *in, const Sim::SimulationFrame *frame,
                                         double now);
    // A vehicle as documents have it (`index` its place from the train's
    // front); a standing train's car in `frame` (none when not there).
    static DisplayDocument::Car carOf(const Sim::Vehicle &vehicle, int index);
    static DisplayDocument::Car standingCar(const Sim::SimulationFrame *frame, int standing, int car);
    static constexpr double ApproachDistance = 500.0;

private:
    RouteDisplays() = default;
    // A placed object's displays (`matrix` its own) or a car's (`matrix`
    // null, `train`, `car` and `standing` its, as pushCar has them).
    std::unique_ptr<ShapeDisplays> bind(ComplexShape &shape, unsigned int stateId, int tileX, int tileY,
                                        const float *matrix, int train, int car = -1, int standing = -1);
    // A document and what it shows.
    struct Entry {
        std::shared_ptr<DisplayDocument> document;
        ::ShapeDisplays::Display::Input input = ::ShapeDisplays::Display::Input::None;
        Platform platform;
        int train = -1;
        // A Car display's car (task scripting 03), in its train or in a
        // standing train.
        int car = -1;
        int standing = -1;
    };
    QHash<QString, Entry> documents;
    // The frame counted (DisplayDocument::seen).
    unsigned int serial = 1;
    // The TDB's platform lines: their two ends (x, z), their track's
    // length and, once looked at, their track every few metres.
    struct Line {
        Platform platform;
        double ends[2][2] = {{0, 0}, {0, 0}};
        double length = 0.0;
        bool sampled = false;
        QVector<double> points;
    };
    const TDB *linesOf = nullptr;
    int linesItems = -1;
    std::vector<Line> lines;
    void readLines(TDB &tdb);
    static void sample(TDB &tdb, Line &line);
};

// A placed object's or a car's displays, as it keeps them
// (RouteDisplays::push, pushCar): what they were bound for.
struct PlacedDisplays {
    PlacedDisplays();
    ~PlacedDisplays();
    std::unique_ptr<ShapeDisplays> displays;
    ComplexShape *shape = nullptr;
    unsigned int state = 0;
    int tile[2] = {0, 0};
    float matrix[16] = {};
    int train = -1;
    int car = -1;
    int standing = -1;
    // The state's overrides cleared, the squares retired: before the state
    // goes (a car's posed state released).
    void release();
};

}

#endif // UIROUTEDISPLAYS_H
