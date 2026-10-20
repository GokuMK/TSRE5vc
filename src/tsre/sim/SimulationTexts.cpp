#include <tsre/sim/SimulationTexts.h>

#include <QTime>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <tsre/sim/TrackNetwork.h>
#include <tsre/units/Units.h>

namespace Sim {

namespace {

QString clock(double seconds) {
    const int s = (int(std::floor(seconds)) % 86400 + 86400) % 86400;
    return QTime::fromMSecsSinceStartOfDay(s * 1000).toString("HH:mm:ss");
}

}

QString StateText(const SimulationInput &in, const TrainState &s, double time, int train) {
    if (train < 0 || size_t(train) >= in.trains.size())
        return QString();
    const TrainPlan &plan = in.trains[size_t(train)];
    switch (s.status) {
    case TrainStatus::Waiting:
        if (s.blockedBy != NoHolder && time >= plan.startTime) {
            if (s.waitFor == WaitFor::SingleTrack)
                //% "to start: the single track is claimed by %1"
                return qtTrId("simulation.state.waiting.single").arg(HolderName(in, s.blockedBy));
            //% "to start: its track is held by %1"
            return qtTrId("simulation.state.waiting.track").arg(HolderName(in, s.blockedBy));
        }
        //% "starts at %1"
        return qtTrId("simulation.state.waiting").arg(clock(plan.startTime));
    case TrainStatus::Running:
        //% "running %1"
        return qtTrId("simulation.state.running.speed").arg(Units::format(s.speed, Units::Quantity::Speed, 0));
    case TrainStatus::Finished:
        //% "finished"
        return qtTrId("simulation.state.finished");
    case TrainStatus::Stopped:
        break;
    }
    switch (s.stoppedAt) {
    case StopKind::Station: {
        const int stop = s.nextStop - 1;
        const QString platform = stop >= 0 && size_t(stop) < plan.stops.size() ? plan.stops[size_t(stop)].platform
                                                                              : QString();
        //% "at %1 until %2"
        return qtTrId("simulation.state.station").arg(platform, clock(s.until));
    }
    case StopKind::Wait:
        //% "at a waiting point until %1"
        return qtTrId("simulation.state.wait").arg(clock(s.until));
    case StopKind::Reversal:
        //% "reversing"
        return qtTrId("simulation.state.reversal");
    case StopKind::End:
        //% "at its path's end"
        return qtTrId("simulation.state.end");
    case StopKind::Blocked:
        if (s.deadlocked)
            //% "deadlocked: waiting for %1"
            return qtTrId("simulation.state.deadlock").arg(HolderName(in, s.blockedBy));
        if (s.blockedBy == NoHolder) {
            if (s.atSignal < 0)
                break;
            //% "at signal %1, not cleared"
            return qtTrId("simulation.state.signal").arg(SignalName(in, s.atSignal));
        }
        return WaitText(in, s.waitFor, s.atSignal, s.blockedBy);
    case StopKind::Event: {
        const QString name = s.eventStop >= 0 && size_t(s.eventStop) < in.events.size()
                ? in.events[size_t(s.eventStop)].name : QString();
        //% "stopped for event %1"
        return qtTrId("simulation.state.event").arg(name);
    }
    case StopKind::None:
        break;
    }
    return QString();
}

QString TrainName(const SimulationInput &in, int train) {
    if (train < 0 || size_t(train) >= in.trains.size())
        return QString();
    const TrainPlan &plan = in.trains[size_t(train)];
    return plan.player ?
        //% "%1 (player)"
        qtTrId("simulation.train.player").arg(plan.service) :
        //% "%1 %2"
        qtTrId("simulation.train.ai").arg(plan.service, clock(plan.serviceTime));
}

QString HolderName(const SimulationInput &in, int holder) {
    if (holder == StandingConsist)
        //% "a standing consist"
        return qtTrId("simulation.holder.consist");
    return TrainName(in, holder);
}

QString SignalName(const SimulationInput &in, int signal) {
    if (in.network == nullptr || signal < 0 || size_t(signal) >= in.network->signalGroups().size())
        return QString();
    const SignalGroup &group = in.network->signalGroups()[size_t(signal)];
    // Its first NORMAL head's TDB item, as the editor shows signals.
    int head = group.heads.empty() ? -1 : group.heads.front();
    for (int h : group.heads)
        if (in.network->isNormal(in.network->heads()[size_t(h)])) {
            head = h;
            break;
        }
    return head < 0 ? QString() : QString::number(in.network->heads()[size_t(head)].item);
}

QString WaitText(const SimulationInput &in, WaitFor waitFor, int signal, int holder) {
    const QString who = HolderName(in, holder);
    const QString at = SignalName(in, signal);
    switch (waitFor) {
    case WaitFor::SingleTrack:
        return at.isEmpty() ?
            //% "waiting for the single track ahead, claimed by %1"
            qtTrId("simulation.wait.single").arg(who) :
            //% "at signal %1: the single track beyond is claimed by %2"
            qtTrId("simulation.wait.single.signal").arg(at, who);
    case WaitFor::PassingTrack:
        return at.isEmpty() ?
            //% "waiting for the passing track, held by %1"
            qtTrId("simulation.wait.passing").arg(who) :
            //% "at signal %1: the passing track is held by %2"
            qtTrId("simulation.wait.passing.signal").arg(at, who);
    case WaitFor::Track:
        break;
    }
    return at.isEmpty() ?
        //% "waiting for track held by %1"
        qtTrId("simulation.state.blocked").arg(who) :
        //% "at signal %1: the track beyond is held by %2"
        qtTrId("simulation.wait.track.signal").arg(at, who);
}

namespace {

// Where a car of a wagon list is: the player's train, a loose consist, no
// train.
QString carPlace(const SimulationInput &in, unsigned int car) {
    const QString name = QString("%1-%2").arg(car / 65536u).arg(car % 65536u);
    for (const TrainPlan &plan : in.trains)
        for (const Vehicle &vehicle : plan.vehicles)
            if (vehicle.id == car)
                //% "car %1 in %2"
                return qtTrId("simulation.why.car.train").arg(name, TrainName(in, plan.id));
    for (const StaticConsist &consist : in.staticConsists)
        if (std::find(consist.cars.begin(), consist.cars.end(), car) != consist.cars.end())
            //% "car %1 in the loose consist %2"
            return qtTrId("simulation.why.car.loose").arg(name, consist.name);
    //% "car %1 in no train"
    return qtTrId("simulation.why.car.none").arg(name);
}

// Whether a train's path passes one of the event's places.
bool passes(const TrainPlan &plan, const EventPlan &event) {
    for (const RouteTrack &part : plan.route.parts)
        for (const RouteInterval &interval : part.intervals())
            for (const TrackPlace &place : event.places)
                if (interval.node == place.node
                        && place.distance >= std::min(interval.from, interval.to) - 1e-3f
                        && place.distance <= std::max(interval.from, interval.to) + 1e-3f)
                    return true;
    return false;
}

}

QString WhyNotFired(const SimulationInput &in, int event, const EventState &state, ActivityEnd end, double endedAt) {
    if (event < 0 || size_t(event) >= in.events.size())
        return QString();
    const EventPlan &plan = in.events[size_t(event)];
    int player = -1;
    for (const TrainPlan &train : in.trains)
        if (train.player && player < 0)
            player = train.id;
    QString why;
    if (plan.kind == EventPlan::PickUpPassengers) {
        //% "PickUpPassengers never fires (as in Open Rails)."
        why = qtTrId("simulation.why.passengers");
    } else if (state.activeSince < 0.0) {
        //% "Never active (its level stayed at %1)."
        why = qtTrId("simulation.why.inactive").arg(state.level);
    } else {
        switch (plan.kind) {
        case EventPlan::Time:
            //% "Its time, %1, is after the run's end."
            why = qtTrId("simulation.why.time").arg(clock(in.startTime + plan.time));
            break;
        case EventPlan::Location: {
            const int looked = plan.trigger >= 0 ? plan.trigger : player;
            const QString train = TrainName(in, looked);
            if (!plan.triggerMissing.isEmpty())
                //% "Its triggering train %1 is not in the activity: the player's train is looked at."
                why = qtTrId("simulation.why.trigger.missing").arg(plan.triggerMissing) + " ";
            if (plan.places.empty()) {
                if (plan.offTrack >= 0.0f)
                    //% "Its point is %1 m from the track: within 2.5 m it would be on it (Open Rails)."
                    why += qtTrId("simulation.why.off.track").arg(plan.offTrack, 0, 'f', 1);
                else
                    //% "Its point is not near any track."
                    why += qtTrId("simulation.why.no.track");
            } else if (looked < 0 || !passes(in.trains[size_t(looked)], plan)) {
                if (plan.offPath >= 0.0f)
                    //% "Its point is on another track, %1 m from the path of %2: within 2.5 m of the track a train runs on it would fire (Open Rails)."
                    why += qtTrId("simulation.why.other.track").arg(plan.offPath, 0, 'f', 1).arg(train);
                else
                    //% "The path of %1 does not pass its point."
                    why += qtTrId("simulation.why.not.on.path").arg(train);
            } else if (state.passedAt >= 0.0 && !state.passedActive) {
                //% "%1 passed it at %2, before it was active (from %3)."
                why += qtTrId("simulation.why.passed.inactive").arg(train, clock(state.passedAt), clock(state.activeSince));
            } else if (state.passedAt >= 0.0 && plan.onStop) {
                //% "%1 passed it at %2 without stopping: active too late to stop."
                why += qtTrId("simulation.why.passed.no.stop").arg(train, clock(state.passedAt));
            } else if (state.passedAt >= 0.0) {
                //% "%1 passed it at %2."
                why += qtTrId("simulation.why.passed").arg(train, clock(state.passedAt));
            } else {
                //% "%1 did not get there."
                why += qtTrId("simulation.why.not.there").arg(train);
            }
            break;
        }
        case EventPlan::AllStops:
            if (player < 0 || in.trains[size_t(player)].stops.empty())
                //% "The player's train has no station stops."
                why = qtTrId("simulation.why.no.stops");
            else
                //% "The player's train did not leave its last station stop."
                why = qtTrId("simulation.why.last.stop");
            break;
        case EventPlan::ReachSpeed:
            //% "The player's train reached %1 at most, %2 asked."
            why = qtTrId("simulation.why.speed").arg(Units::format(state.bestSpeed, Units::Quantity::Speed, 0),
                                                     Units::format(plan.speed, Units::Quantity::Speed, 0));
            break;
        case EventPlan::AssembleTrain:
        case EventPlan::AssembleTrainAtLocation:
        case EventPlan::DropOffWagonsAtLocation:
        case EventPlan::PickUpWagons: {
            QStringList places;
            for (unsigned int car : plan.cars)
                places << carPlace(in, car);
            if (plan.kind == EventPlan::AssembleTrainAtLocation && !state.reachedSiding)
                //% "The player's train did not reach the siding."
                why = qtTrId("simulation.why.siding") + " ";
            //% "The cars: %1. The autopilot does not shunt yet."
            why += qtTrId("simulation.why.cars").arg(places.join(", "));
            break;
        }
        default:
            break;
        }
    }
    if (end != ActivityEnd::None && endedAt >= 0.0)
        //% "The activity ended at %1."
        why += " " + qtTrId("simulation.why.ended").arg(clock(endedAt));
    return why.trimmed();
}

}
