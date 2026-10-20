#include <tsre/sim/SimulationTexts.h>

#include <QTime>
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

}
