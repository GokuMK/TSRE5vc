/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/ui/UiRouteDisplays.h>
#include <tsre/ui/UiShapeDisplays.h>
#include <tsre/environment/WorldClock.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLib.h>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>

#include <QDate>
#include <QDebug>
#include <QMatrix4x4>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Ui {

namespace {

// How far a display looks for its platform line (m); how far in front of a
// square its readers stand (m); the track looked at every so many metres.
constexpr double PlatformReach = 300.0;
constexpr float ReadersDistance = 3.0f;
constexpr double TrackStep = 5.0;
// A display's distance as a later due time (ms a metre).
constexpr double DistanceDelay = 10.0;

// A point's distance to a segment, across the ground.
double segmentDistance(double x, double z, const double *a, const double *b) {
    const double dx = b[0] - a[0], dz = b[1] - a[1];
    const double length = dx * dx + dz * dz;
    double t = length > 0.0 ? ((x - a[0]) * dx + (z - a[1]) * dz) / length : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(x - (a[0] + t * dx), z - (a[1] + t * dz));
}

QString clockText(int seconds) {
    seconds = ((seconds % 86400) + 86400) % 86400;
    return QString("%1:%2").arg(seconds / 3600, 2, 10, QLatin1Char('0')).arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'));
}

// Where a train is on its plan, by its state (none: not known): the stop
// it stands at or runs to (-1 none), standing there, and how late it is
// now (minutes: leaving late where it stands, past its next stop's
// arrival, past its start).
struct Progress {
    int at = -1;
    bool standing = false;
    int late = 0;
};

Progress progressOf(const Sim::TrainPlan &plan, const Sim::TrainState *state, double now) {
    Progress p;
    if (state == nullptr)
        return p;
    const std::vector<Sim::StopPlan> &stops = plan.stops;
    p.standing = state->status == Sim::TrainStatus::Stopped && state->stoppedAt == Sim::StopKind::Station;
    p.at = p.standing ? state->nextStop - 1 : state->nextStop;
    const bool known = p.at >= 0 && size_t(p.at) < stops.size();
    double late = 0.0;
    if (state->status == Sim::TrainStatus::Waiting)
        late = now - plan.startTime;
    else if (p.standing && known && stops[size_t(p.at)].departure >= 0)
        late = now - stops[size_t(p.at)].departure;
    else if (!p.standing && known && stops[size_t(p.at)].arrival >= 0)
        late = now - stops[size_t(p.at)].arrival;
    if (late < 0.0 || late > 12 * 3600.0)
        late = 0.0;
    p.late = int(late / 60.0);
    return p;
}

}

PlacedDisplays::PlacedDisplays() = default;
PlacedDisplays::~PlacedDisplays() = default;

void PlacedDisplays::release() {
    if (displays && shape != nullptr)
        displays->release(*shape, state);
    displays.reset();
    shape = nullptr;
}

RouteDisplays &RouteDisplays::instance() {
    static RouteDisplays displays;
    return displays;
}

void RouteDisplays::push(std::unique_ptr<PlacedDisplays> &placed, RenderQueue &queue, ComplexShape &shape,
                         unsigned int stateId, int tileX, int tileY, const float *matrix, quint32 selectionId,
                         float distance) {
    // Most objects have none (a shape whose blocks were all left out: told).
    if (!placed && shape.displays().isEmpty()) {
        if (!shape.displayProblems().isEmpty())
            Ui::ShapeDisplays::report(shape);
        return;
    }
    if (!shape.isLoaded())
        return;
    if (!placed)
        placed = std::make_unique<PlacedDisplays>();
    PlacedDisplays &p = *placed;
    const bool same = p.shape == &shape && p.state == stateId && p.tile[0] == tileX && p.tile[1] == tileY
            && std::memcmp(p.matrix, matrix, sizeof(p.matrix)) == 0;
    if (!same) {
        std::unique_ptr<ShapeDisplays> bound = bind(shape, stateId, tileX, tileY, matrix, -1);
        // Bound to the same documents: kept as they are.
        const bool kept = p.displays && bound && p.shape == &shape && p.state == stateId
                && p.displays->documents() == bound->documents();
        if (!kept) {
            if (p.displays && p.shape == &shape)
                p.displays->release(shape, p.state);
            p.displays = std::move(bound);
        }
        p.shape = &shape;
        p.state = stateId;
        p.tile[0] = tileX;
        p.tile[1] = tileY;
        std::memcpy(p.matrix, matrix, sizeof(p.matrix));
    }
    if (p.displays)
        p.displays->push(queue, shape, stateId, selectionId, serial, distance);
}

void RouteDisplays::overrideTextures(const std::unique_ptr<PlacedDisplays> &placed, ComplexShape &shape,
                                     unsigned int stateId) {
    if (placed && placed->displays && placed->shape == &shape && placed->state == stateId)
        placed->displays->overrideTextures(shape, stateId);
}

void RouteDisplays::pushCar(std::unique_ptr<PlacedDisplays> &placed, RenderQueue &queue, ComplexShape &shape,
                            unsigned int stateId, int train, int car, int standing, quint32 selectionId,
                            float distance) {
    if (!placed && shape.displays().isEmpty()) {
        if (!shape.displayProblems().isEmpty())
            Ui::ShapeDisplays::report(shape);
        return;
    }
    if (!shape.isLoaded())
        return;
    if (!placed)
        placed = std::make_unique<PlacedDisplays>();
    PlacedDisplays &p = *placed;
    if (p.shape != &shape || p.state != stateId || p.train != train || p.car != car || p.standing != standing) {
        std::unique_ptr<ShapeDisplays> bound = bind(shape, stateId, 0, 0, nullptr, train, car, standing);
        const bool sameState = p.shape == &shape && p.state == stateId;
        const bool kept = p.displays && bound && sameState && p.displays->documents() == bound->documents();
        if (!kept) {
            // Another state's overrides are its car's to clear (release())
            // before it lets the state go.
            if (p.displays && sameState)
                p.displays->release(shape, p.state);
            p.displays = std::move(bound);
        }
        p.shape = &shape;
        p.state = stateId;
        p.train = train;
        p.car = car;
        p.standing = standing;
    }
    if (p.displays)
        p.displays->push(queue, shape, stateId, selectionId, serial, distance);
}

std::unique_ptr<ShapeDisplays> RouteDisplays::bind(ComplexShape &shape, unsigned int stateId, int tileX, int tileY,
                                                   const float *matrix, int train, int car, int standing) {
    Ui::ShapeDisplays::report(shape);
    if (shape.displays().isEmpty())
        return nullptr;
    QMatrix4x4 object;
    if (matrix != nullptr)
        std::copy(matrix, matrix + 16, object.data());
    std::vector<std::shared_ptr<DisplayDocument>> bound;
    for (const ::ShapeDisplays::Display &d : shape.displays()) {
        const QString path = Ui::ShapeDisplays::documentPath(shape, d);
        if (path.isEmpty()) {
            bound.push_back(nullptr);
            continue;
        }
        const QSize size = Ui::ShapeDisplays::documentSize(shape, d);
        Platform platform;
        const bool lists = d.input == ::ShapeDisplays::Display::Input::Platform
                || d.input == ::ShapeDisplays::Display::Input::Station;
        if (lists && matrix != nullptr && Game::trackDB != nullptr) {
            // Where its readers stand: in front of a square, else at the
            // object; on its tile, then MSTS's x and z.
            QVector3D at = object.map(QVector3D(0.0f, 0.0f, 0.0f));
            if (d.square) {
                const int part = Ui::ShapeDisplays::squarePart(shape, d);
                at = object.map(Ui::ShapeDisplays::squareMatrix(shape, stateId, part, d)
                                        .map(QVector3D(0.0f, 0.0f, ReadersDistance)));
            }
            platform = nearestPlatform(*Game::trackDB, double(tileX) * 2048.0 + at.x(),
                                       -(double(tileY) * 2048.0 + at.z()));
        }
        QString key = path.toLower() + QString("|%1x%2|%3|%4").arg(size.width()).arg(size.height())
                .arg(double(d.refreshSeconds)).arg(int(d.input));
        if (d.input == ::ShapeDisplays::Display::Input::Platform)
            key += "|" + QString::number(platform.start);
        else if (d.input == ::ShapeDisplays::Display::Input::Station)
            key += "|" + platform.station.toLower();
        else if (d.input == ::ShapeDisplays::Display::Input::Train)
            key += "|" + QString::number(train);
        // A car's own (task scripting 03).
        const bool ownCar = d.input == ::ShapeDisplays::Display::Input::Car;
        if (ownCar)
            key += QString("|%1|%2|%3").arg(train).arg(car).arg(standing);
        Entry &entry = documents[key];
        if (!entry.document) {
            entry.document = std::make_shared<DisplayDocument>(path, size, d.refreshSeconds, d.input);
            entry.input = d.input;
            entry.platform = platform;
            entry.train = d.input == ::ShapeDisplays::Display::Input::Train || ownCar ? train : -1;
            entry.car = ownCar ? car : -1;
            entry.standing = ownCar ? standing : -1;
            qDebug() << "Display" << d.document << size << "at" << platform.station << platform.name
                     << platform.start << "for" << shape.getPathId().section('/', -1);
        }
        bound.push_back(entry.document);
    }
    return std::make_unique<Ui::ShapeDisplays>(shape, std::move(bound));
}

void RouteDisplays::frame(Renderer &renderer) {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const std::shared_ptr<const Sim::SimulationInput> in = simulation->input();
    const std::shared_ptr<const Sim::SimulationFrame> now = in ? simulation->viewFrame() : nullptr;
    WorldClock *clock = WorldClock::instance();
    // The plans' clock: the simulation's, else the world clock's.
    const double seconds = now ? now->time : clock->hours() * 3600.0;
    QDate date = QDate::fromString(clock->date().trimmed(), "yyyy-MM-dd");
    if (!date.isValid())
        date = QDate::currentDate();
    const double day = std::fmod(std::max(seconds, 0.0), 86400.0);
    const QDateTime time = QDateTime(date, QTime(0, 0)).addMSecs(qint64(day * 1000.0));
    const qint64 keep = qint64(Settings::integer("core.rendering.displayKeepUnseen")) * 1000;
    for (auto it = documents.begin(); it != documents.end();) {
        DisplayDocument &document = *it.value().document;
        // No display holds it any more.
        if (it.value().document.use_count() == 1) {
            document.release(renderer);
            it = documents.erase(it);
            continue;
        }
        // Out of sight a while: its texture's memory back (the shapes take
        // the new one before they are drawn again, overrideTextures).
        if (keep > 0 && document.texture() != 0 && document.unseenFor(keep)) {
            qDebug() << "Display texture freed, unseen" << keep / 1000 << "s:" << it.key();
            document.release(renderer);
        }
        ++it;
    }
    // Those due, once none is erased (an erase may move the others).
    std::vector<Entry *> waiting;
    std::vector<Due> due;
    for (Entry &e : documents)
        if (e.document->seen == serial && e.document->due()) {
            waiting.push_back(&e);
            due.push_back({e.document->waiting(), e.document->distance, e.document->unloaded()});
        }
    QHash<QString, DisplayDocument::Data> made;
    for (int index : redraws(due, std::max(1, Settings::integer("core.rendering.displayRedraws")))) {
        Entry &e = *waiting[size_t(index)];
        // A Car display's train made once a frame as a Train display's, its
        // car taken from it.
        // A standing train's car: its car only.
        const bool ownCar = e.input == ::ShapeDisplays::Display::Input::Car;
        if (ownCar && e.standing >= 0) {
            DisplayDocument::Data own;
            own.car = standingCar(now.get(), e.standing, e.car);
            e.document->draw(renderer, time, own);
            continue;
        }
        const ::ShapeDisplays::Display::Input takes = ownCar ? ::ShapeDisplays::Display::Input::Train : e.input;
        const QString inputs = QString::number(int(takes)) + "|" + QString::number(e.platform.start) + "|"
                + QString::number(e.train);
        auto found = made.find(inputs);
        if (found == made.end())
            found = made.insert(inputs, Game::trackDB == nullptr ? DisplayDocument::Data()
                    : takes == ::ShapeDisplays::Display::Input::Train
                    ? trainData(e.train, *Game::trackDB, in.get(), now.get(), seconds)
                    : data(e.input, e.platform, *Game::trackDB, in.get(), now.get(), seconds));
        if (ownCar) {
            DisplayDocument::Data own = found.value();
            if (e.car >= 0 && e.car < own.train.cars.size())
                own.car = own.train.cars[e.car];
            e.document->draw(renderer, time, own);
        } else {
            e.document->draw(renderer, time, found.value());
        }
    }
    ++serial;
}

DisplayDocument::Car RouteDisplays::carOf(const Sim::Vehicle &vehicle, int index) {
    DisplayDocument::Car car;
    car.index = index;
    car.number = vehicle.number;
    car.tags = vehicle.tags;
    const int type = vehicle.type();
    car.kind = type >= 4 ? "engine" : type == 3 ? "tender" : type == 2 ? "freight" : type == 1 ? "carriage" : "";
    // Its file's name, as its consist names it; else its Wagon block's.
    car.name = vehicle.definition->engName;
    if (Game::currentEngLib != nullptr) {
        const auto found = Game::currentEngLib->eng.find(vehicle.eng);
        if (found != Game::currentEngLib->eng.end() && found->second != nullptr)
            car.name = found->second->name.section('.', 0, -2);
    }
    return car;
}

QVector<DisplayDocument::Car> RouteDisplays::carsOf(int train, const Sim::SimulationInput *in,
                                                   const Sim::SimulationFrame *frame) {
    QVector<DisplayDocument::Car> cars;
    if (in == nullptr || train < 0 || size_t(train) >= in->trains.size())
        return cars;
    // As they are now (coupled and cut off), else as planned.
    const std::vector<Sim::Vehicle> *vehicles = &in->trains[size_t(train)].vehicles;
    if (frame != nullptr && size_t(train) < frame->trains.size() && frame->trains[size_t(train)].vehicles)
        vehicles = frame->trains[size_t(train)].vehicles.get();
    for (size_t k = 0; k < vehicles->size(); ++k)
        cars << carOf((*vehicles)[k], int(k));
    return cars;
}

DisplayDocument::Car RouteDisplays::standingCar(const Sim::SimulationFrame *frame, int standing, int car) {
    if (frame == nullptr || !frame->standing || standing < 0 || size_t(standing) >= frame->standing->size())
        return DisplayDocument::Car();
    const Sim::StandingFrame &st = (*frame->standing)[size_t(standing)];
    if (car < 0 || size_t(car) >= st.vehicles.size())
        return DisplayDocument::Car();
    return carOf(st.vehicles[size_t(car)], car);
}

DisplayDocument::Data RouteDisplays::carData(int train, int car, int standing, const TDB &tdb,
                                             const Sim::SimulationInput *in, const Sim::SimulationFrame *frame,
                                             double now) {
    DisplayDocument::Data shown;
    if (standing >= 0) {
        shown.car = standingCar(frame, standing, car);
        return shown;
    }
    shown = trainData(train, tdb, in, frame, now);
    if (car >= 0 && car < shown.train.cars.size())
        shown.car = shown.train.cars[car];
    return shown;
}

std::vector<int> RouteDisplays::redraws(const std::vector<Due> &due, int budget) {
    std::vector<int> order(due.size());
    for (size_t i = 0; i < due.size(); ++i)
        order[i] = int(i);
    auto priority = [&due](int i) {
        return double(due[size_t(i)].waiting) - double(due[size_t(i)].distance) * DistanceDelay;
    };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return priority(a) > priority(b); });
    std::vector<int> drawn;
    bool loaded = false;
    for (int i : order) {
        if (int(drawn.size()) >= budget)
            break;
        if (due[size_t(i)].loads) {
            if (loaded)
                continue;
            loaded = true;
        }
        drawn.push_back(i);
    }
    return drawn;
}

void RouteDisplays::release(Renderer &renderer) {
    for (Entry &e : documents)
        e.document->release(renderer);
}

void RouteDisplays::readLines(TDB &tdb) {
    if (linesOf == &tdb && linesItems == tdb.iTRitems)
        return;
    linesOf = &tdb;
    linesItems = tdb.iTRitems;
    lines.clear();
    for (const StationPlatforms::Platform &platform : StationPlatforms::read(tdb)) {
        Line line;
        line.platform.start = platform.start;
        line.platform.end = platform.end;
        line.platform.station = platform.station;
        line.platform.name = platform.name;
        line.ends[0][0] = platform.ends[0];
        line.ends[0][1] = platform.ends[2];
        line.ends[1][0] = platform.ends[3];
        line.ends[1][1] = platform.ends[5];
        line.length = platform.to - platform.from;
        line.source = platform;
        lines.push_back(line);
    }
}

void RouteDisplays::sample(TDB &tdb, Line &line) {
    line.sampled = true;
    StationPlatforms::sample(tdb, line.source, TrackStep);
    for (size_t i = 0; i + 2 < line.source.points.size(); i += 3)
        line.points << line.source.points[i] << line.source.points[i + 2];
}

RouteDisplays::Platform RouteDisplays::nearestPlatform(TDB &tdb, double x, double z) {
    readLines(tdb);
    Platform nearest;
    double best = PlatformReach;
    for (Line &line : lines) {
        // No point of its track is farther from its ends' chord than half
        // its length.
        if (segmentDistance(x, z, line.ends[0], line.ends[1]) - line.length * 0.5 >= best)
            continue;
        if (!line.sampled)
            sample(tdb, line);
        for (int i = 0; i + 3 < line.points.size(); i += 2) {
            const double distance = segmentDistance(x, z, &line.points[i], &line.points[i + 2]);
            if (distance < best) {
                best = distance;
                nearest = line.platform;
            }
        }
    }
    return nearest;
}

DisplayDocument::Data RouteDisplays::data(::ShapeDisplays::Display::Input input, const Platform &platform,
                                          const TDB &tdb, const Sim::SimulationInput *in,
                                          const Sim::SimulationFrame *frame, double now) {
    DisplayDocument::Data shown;
    shown.station = platform.station;
    // A station's display shows no one platform.
    if (input != ::ShapeDisplays::Display::Input::Station)
        shown.platform = platform.name;
    const bool lists = input == ::ShapeDisplays::Display::Input::Platform
            || input == ::ShapeDisplays::Display::Input::Station;
    if (in == nullptr || platform.start < 0 || !lists)
        return shown;
    auto item = [&tdb](int id) -> const TRitem * {
        const auto found = tdb.trackItems.find(id);
        return found != tdb.trackItems.end() ? found->second : nullptr;
    };
    auto stationOf = [&item](int id) {
        const TRitem *found = item(id);
        return found != nullptr ? found->stationName.trimmed() : QString();
    };
    auto here = [&](const Sim::StopPlan &stop) {
        if (input == ::ShapeDisplays::Display::Input::Platform)
            return stop.platformItem >= 0 && (stop.platformItem == platform.start || stop.platformItem == platform.end);
        return stop.platformItem >= 0 && stationOf(stop.platformItem).compare(platform.station, Qt::CaseInsensitive) == 0;
    };
    struct Timed {
        int time = 0;
        DisplayDocument::Row row;
        size_t train = 0;
    };
    // A station's departures and arrivals; a platform's trains, each once,
    // by their next time there (task scripting 03).
    const bool atPlatform = input == ::ShapeDisplays::Display::Input::Platform;
    std::vector<Timed> departures, arrivals, trains;
    for (size_t t = 0; t < in->trains.size(); ++t) {
        const Sim::TrainPlan &plan = in->trains[t];
        const std::vector<Sim::StopPlan> &stops = plan.stops;
        if (stops.empty())
            continue;
        const Sim::TrainState *state = frame != nullptr && t < frame->trains.size() ? &frame->trains[t].state : nullptr;
        if (state != nullptr && (state->status == Sim::TrainStatus::Finished || state->joinedTo >= 0))
            continue;
        // The stop it stands at or runs to.
        const Progress progress = progressOf(plan, state, now);
        const bool standing = progress.standing;
        const int at = progress.at;
        const int minutes = progress.late;
        const QString origin = stationOf(stops.front().platformItem);
        const QString destination = stationOf(stops.back().platformItem);
        for (size_t k = 0; k < stops.size(); ++k) {
            const Sim::StopPlan &stop = stops[k];
            if (!here(stop))
                continue;
            DisplayDocument::Row row;
            row.id = plan.service;
            row.fields = plan.fields;
            row.extra = plan.extra;
            // Its own origin and destination, else from its first stop, to
            // its last; none where this is one (it comes onto the route or
            // leaves it here).
            QString &from = row.fields[TrainFields::Origin];
            QString &to = row.fields[TrainFields::Destination];
            if (from.isEmpty())
                from = k > 0 ? origin : QString();
            if (to.isEmpty())
                to = k + 1 < stops.size() ? destination : QString();
            const TRitem *stopItem = item(stop.platformItem);
            row.platform = stopItem != nullptr ? stopItem->platformName.trimmed() : stop.platform;
            row.delay = minutes > 0 ? QString::number(minutes) : QString();
            const int index = int(k);
            // It leaves here (its last stop too where a timetable books it
            // to leave: two times, it runs on beyond the route); it arrives
            // here (not where it starts).
            const bool leaves = k + 1 < stops.size()
                    || (plan.timetableTrain && stop.arrival >= 0 && stop.departure > stop.arrival);
            const bool departs = stop.departure >= 0 && leaves;
            const bool arrives = stop.arrival >= 0 && (k > 0 || stop.arrival > plan.startTime);
            row.departs = departs ? clockText(stop.departure) : QString();
            row.arrives = arrives ? clockText(stop.arrival) : QString();
            // Departures until it leaves, arrivals until it arrives.
            const bool toLeave = departs && (state != nullptr ? at <= index : stop.departure + 60 >= now);
            const bool toArrive = arrives
                    && (state != nullptr ? at < index || (at == index && !standing) : stop.arrival + 60 >= now);
            if (atPlatform) {
                // Until it leaves (or arrives, where it ends here): by its
                // arrival until then, then by its departure.
                if (toArrive || toLeave) {
                    const int next = toArrive ? stop.arrival : stop.departure;
                    row.time = clockText(next);
                    trains.push_back({next, row, t});
                }
                continue;
            }
            if (toLeave) {
                row.time = row.departs;
                departures.push_back({stop.departure, row, t});
            }
            if (toArrive) {
                row.time = row.arrives;
                arrivals.push_back({stop.arrival, row, t});
            }
        }
    }
    auto byTime = [](const Timed &a, const Timed &b) { return a.time < b.time; };
    std::stable_sort(departures.begin(), departures.end(), byTime);
    std::stable_sort(arrivals.begin(), arrivals.end(), byTime);
    std::stable_sort(trains.begin(), trains.end(), byTime);
    for (const Timed &t : departures)
        shown.departures.push_back(t.row);
    for (const Timed &t : arrivals)
        shown.arrivals.push_back(t.row);
    // A platform's next two trains with their cars (the user: a platform
    // screen shows two at most).
    for (const Timed &t : trains) {
        shown.trains.push_back(t.row);
        if (shown.trains.size() <= PlatformTrainsWithCars)
            shown.trains.back().cars = carsOf(int(t.train), in, frame);
    }
    return shown;
}

DisplayDocument::Data RouteDisplays::trainData(int train, const TDB &tdb, const Sim::SimulationInput *in,
                                               const Sim::SimulationFrame *frame, double now) {
    DisplayDocument::Data shown;
    if (in == nullptr || train < 0 || size_t(train) >= in->trains.size())
        return shown;
    const Sim::TrainPlan &plan = in->trains[size_t(train)];
    const Sim::TrainState *state =
            frame != nullptr && size_t(train) < frame->trains.size() ? &frame->trains[size_t(train)].state : nullptr;
    auto item = [&tdb](int id) -> const TRitem * {
        const auto found = tdb.trackItems.find(id);
        return found != tdb.trackItems.end() ? found->second : nullptr;
    };
    DisplayDocument::Train &shows = shown.train;
    shows.id = plan.service;
    shows.fields = plan.fields;
    shows.extra = plan.extra;
    shows.cars = carsOf(train, in, frame);
    const std::vector<Sim::StopPlan> &stops = plan.stops;
    if (stops.empty())
        return shown;
    // Its own origin and destination, else its first and last stops'.
    const TRitem *first = item(stops.front().platformItem);
    const TRitem *last = item(stops.back().platformItem);
    if (shows.fields[TrainFields::Origin].isEmpty() && first != nullptr)
        shows.fields[TrainFields::Origin] = first->stationName.trimmed();
    if (shows.fields[TrainFields::Destination].isEmpty() && last != nullptr)
        shows.fields[TrainFields::Destination] = last->stationName.trimmed();
    const Progress progress = progressOf(plan, state, now);
    auto stationAt = [&](int k) {
        const TRitem *found = k >= 0 && size_t(k) < stops.size() ? item(stops[size_t(k)].platformItem) : nullptr;
        return found != nullptr ? found->stationName.trimmed() : QString();
    };
    auto platformAt = [&](int k) {
        if (k < 0 || size_t(k) >= stops.size())
            return QString();
        const TRitem *found = item(stops[size_t(k)].platformItem);
        return found != nullptr ? found->platformName.trimmed() : stops[size_t(k)].platform;
    };
    auto booked = [](int seconds) { return seconds >= 0 ? clockText(seconds) : QString(); };
    // The stop it stands at (-1 none), the next it reaches, the stops it
    // left.
    int standingAt = -1, next = 0, left = 0;
    if (state == nullptr) {
        shows.departs = booked(stops.front().departure);
    } else if (state->status == Sim::TrainStatus::Finished || state->joinedTo >= 0
               || (state->status == Sim::TrainStatus::Stopped && state->stoppedAt == Sim::StopKind::End)) {
        shows.state = "finished";
        next = -1;
        left = int(stops.size());
    } else if (state->status == Sim::TrainStatus::Waiting
               || (state->status == Sim::TrainStatus::Stopped && state->stoppedAt == Sim::StopKind::Created)) {
        shows.state = "waiting";
        next = state->nextStop;
        shows.departs = booked(stops.front().departure);
    } else if (progress.standing) {
        shows.state = "standing";
        standingAt = progress.at;
        next = state->nextStop;
        left = std::max(standingAt, 0);
        if (standingAt >= 0 && size_t(standingAt) < stops.size())
            shows.departs = booked(stops[size_t(standingAt)].departure);
    } else {
        next = state->nextStop;
        left = std::max(next, 0);
        shows.state = state->status == Sim::TrainStatus::Stopped ? "held" : "running";
        if (next >= 0 && size_t(next) < stops.size() && stops[size_t(next)].part == state->part) {
            const double metres = stops[size_t(next)].front - state->front;
            if (metres >= 0.0) {
                shows.distance = QString::number(qRound64(metres));
                if (metres <= ApproachDistance && shows.state == "running")
                    shows.state = "approaching";
            }
        }
    }
    shows.at = stationAt(standingAt);
    shows.next = stationAt(next);
    shows.previous = stationAt(standingAt >= 0 ? standingAt - 1 : (state != nullptr && next > 0 ? next - 1 : -1));
    shows.platform = standingAt >= 0 ? platformAt(standingAt) : platformAt(next);
    if (next >= 0 && size_t(next) < stops.size())
        shows.arrives = booked(stops[size_t(next)].arrival);
    shows.delay = progress.late > 0 ? QString::number(progress.late) : QString();
    for (int k = 0; k < int(stops.size()); ++k)
        shows.stops.push_back({stationAt(k), platformAt(k), booked(stops[size_t(k)].arrival),
                               booked(stops[size_t(k)].departure), k < left});
    return shown;
}

}
