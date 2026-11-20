#include <tsre/sim/SimulationWire.h>

#include <QDataStream>
#include <QIODevice>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <tsre/fileFunctions/NetworkToken.h>
#include <tsre/fileFunctions/TS.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/trains/CabControls.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/VehicleDefinition.h>

namespace Sim {
namespace Wire {

namespace {

constexpr quint32 FrameMark = 0x46535254; // "TRSF"
constexpr quint32 EventsMark = 0x45535254; // "TRSE"
constexpr quint32 MessagesMark = 0x4D535254; // "TRSM"
constexpr quint8 Version = 1;

// Little-endian bytes out.
class Out {
public:
    QByteArray bytes;
    template <typename T>
    void raw(T value) {
        const T le = qToLittleEndian(value);
        bytes.append(reinterpret_cast<const char *>(&le), int(sizeof(T)));
    }
    void u8(quint8 v) { bytes.append(char(v)); }
    void u32(quint32 v) { raw(v); }
    void i32(qint32 v) { raw(v); }
    void u64(quint64 v) { raw(v); }
    void i64(qint64 v) { raw(v); }
    void f32(float v) {
        quint32 bits;
        std::memcpy(&bits, &v, 4);
        raw(bits);
    }
    void f64(double v) {
        quint64 bits;
        std::memcpy(&bits, &v, 8);
        raw(bits);
    }
    void flag(bool v) { u8(v ? 1 : 0); }
    void str(const QString &text) {
        const QByteArray utf8 = text.toUtf8();
        u32(quint32(utf8.size()));
        bytes.append(utf8);
    }
};

// And in: every read checked; once short, everything after reads zero.
class In {
public:
    explicit In(const QByteArray &data) : data(data) {}
    bool ok = true;
    template <typename T>
    T raw() {
        if (!ok || pos + int(sizeof(T)) > data.size()) {
            ok = false;
            return T();
        }
        T le;
        std::memcpy(&le, data.constData() + pos, sizeof(T));
        pos += int(sizeof(T));
        return qFromLittleEndian(le);
    }
    quint8 u8() { return raw<quint8>(); }
    quint32 u32() { return raw<quint32>(); }
    qint32 i32() { return raw<qint32>(); }
    quint64 u64() { return raw<quint64>(); }
    qint64 i64() { return raw<qint64>(); }
    float f32() {
        const quint32 bits = raw<quint32>();
        float v;
        std::memcpy(&v, &bits, 4);
        return v;
    }
    double f64() {
        const quint64 bits = raw<quint64>();
        double v;
        std::memcpy(&v, &bits, 8);
        return v;
    }
    bool flag() { return u8() != 0; }
    QString str() {
        const quint32 n = u32();
        if (!ok || pos + qint64(n) > data.size()) {
            ok = false;
            return QString();
        }
        const QString text = QString::fromUtf8(data.constData() + pos, int(n));
        pos += int(n);
        return text;
    }
    // A count of items at least `least` bytes each: false (and short) when
    // the bytes left cannot hold them.
    quint32 count(int least) {
        const quint32 n = u32();
        if (!ok || qint64(n) * least > data.size() - pos) {
            ok = false;
            return 0;
        }
        return n;
    }
    bool done() const { return ok && pos == data.size(); }

private:
    const QByteArray &data;
    int pos = 0;
};

void putValue(Out &out, const MessageValue &value) {
    out.u8(quint8(value.index()));
    if (std::holds_alternative<bool>(value))
        out.flag(std::get<bool>(value));
    else if (std::holds_alternative<double>(value))
        out.f64(std::get<double>(value));
    else
        out.str(std::get<QString>(value));
}

MessageValue getValue(In &in) {
    switch (in.u8()) {
    case 0: return MessageValue(in.flag());
    case 1: return MessageValue(in.f64());
    case 2: return MessageValue(in.str());
    default:
        in.ok = false;
        return MessageValue(0.0);
    }
}

// A vehicle's values against what the client has (`before`; none: all):
// those that changed or are new, and the names gone.
void putValueChanges(Out &out, const QHash<QString, MessageValue> *before, const QHash<QString, MessageValue> &now) {
    QStringList changed, gone;
    for (auto it = now.cbegin(); it != now.cend(); ++it)
        if (before == nullptr || !before->contains(it.key()) || !(before->value(it.key()) == it.value()))
            changed << it.key();
    if (before != nullptr)
        for (auto it = before->cbegin(); it != before->cend(); ++it)
            if (!now.contains(it.key()))
                gone << it.key();
    changed.sort();
    gone.sort();
    out.u32(quint32(changed.size()));
    for (const QString &name : changed) {
        out.str(name);
        putValue(out, now.value(name));
    }
    out.u32(quint32(gone.size()));
    for (const QString &name : gone)
        out.str(name);
}

void getValueChanges(In &in, QHash<QString, MessageValue> &values) {
    const quint32 changed = in.count(6);
    for (quint32 i = 0; i < changed && in.ok; ++i) {
        const QString name = in.str();
        values.insert(name, getValue(in));
    }
    const quint32 gone = in.count(4);
    for (quint32 i = 0; i < gone && in.ok; ++i)
        values.remove(in.str());
}

void putState(Out &out, const TrainState &s) {
    out.u8(quint8(s.status));
    out.i32(s.part);
    out.f64(s.front);
    out.f32(s.speed);
    out.f32(s.acceleration);
    out.flag(s.reversed);
    out.u8(quint8(s.stoppedAt));
    out.f64(s.until);
    out.i32(s.nextStop);
    out.i32(s.nextWait);
    out.f64(s.travelled);
    out.f64(s.authority);
    out.i32(s.blockedBy);
    out.i32(s.waitSection);
    out.u8(quint8(s.waitFor));
    out.i32(s.atSignal);
    out.f32(s.signalSpeed);
    out.flag(s.deadlocked);
    out.i32(s.eventStop);
    out.i32(s.joinedTo);
    out.i32(s.atStation);
    const TrainState::Passengers &p = s.passengers;
    out.i32(p.stop);
    out.i32(p.waiting);
    out.i32(p.alighting);
    out.i32(p.boarded);
    out.i32(p.alighted);
    out.i32(p.missed);
    out.flag(p.left);
    out.f64(p.arrived);
    out.i32(p.doors);
    out.f64(p.doorsOpenSeconds);
    out.i32(p.boardedTotal);
    out.i32(p.alightedTotal);
    out.i32(p.missedTotal);
}

TrainState getState(In &in) {
    TrainState s;
    s.status = TrainStatus(in.u8());
    s.part = in.i32();
    s.front = in.f64();
    s.speed = in.f32();
    s.acceleration = in.f32();
    s.reversed = in.flag();
    s.stoppedAt = StopKind(in.u8());
    s.until = in.f64();
    s.nextStop = in.i32();
    s.nextWait = in.i32();
    s.travelled = in.f64();
    s.authority = in.f64();
    s.blockedBy = in.i32();
    s.waitSection = in.i32();
    s.waitFor = WaitFor(in.u8());
    s.atSignal = in.i32();
    s.signalSpeed = in.f32();
    s.deadlocked = in.flag();
    s.eventStop = in.i32();
    s.joinedTo = in.i32();
    s.atStation = in.i32();
    TrainState::Passengers &p = s.passengers;
    p.stop = in.i32();
    p.waiting = in.i32();
    p.alighting = in.i32();
    p.boarded = in.i32();
    p.alighted = in.i32();
    p.missed = in.i32();
    p.left = in.flag();
    p.arrived = in.f64();
    p.doors = in.i32();
    p.doorsOpenSeconds = in.f64();
    p.boardedTotal = in.i32();
    p.alightedTotal = in.i32();
    p.missedTotal = in.i32();
    return s;
}

// A vehicle's end by its place on the track: the node, the distance along
// it, its way, and whether its roll is the track's turned (the core's
// rule: a train's when it runs against the node's way, the other way when
// reversed; a standing train's when along it). The rest the client works
// out from its own geometry, as the core's pose does.
enum EndFlag : quint8 { Forward = 1, RollTurned = 2 };

void putEnds(Out &out, const std::vector<TrackPose> &ends, bool standing, bool reversed) {
    out.u32(quint32(ends.size()));
    for (const TrackPose &p : ends) {
        out.i32(p.node);
        out.f32(p.distance);
        const bool turned = standing ? p.forward : (!p.forward) != reversed;
        out.u8(quint8((p.forward ? Forward : 0) | (turned ? RollTurned : 0)));
    }
}

void getEnds(In &in, const TrackGeometry *geometry, std::vector<TrackPose> &ends) {
    const quint32 n = in.count(9);
    ends.resize(n);
    for (quint32 i = 0; i < n && in.ok; ++i) {
        const int node = in.i32();
        const float distance = in.f32();
        const quint8 flags = in.u8();
        TrackPose p;
        if (node >= 0 && geometry != nullptr && geometry->pose(node, distance, p)) {
            p.node = node;
            p.distance = distance;
            p.forward = (flags & Forward) != 0;
            if (flags & RollTurned)
                p.roll = -p.roll;
        }
        ends[i] = p;
    }
}

void putVehicles(Out &out, const std::vector<Vehicle> &vehicles) {
    out.u32(quint32(vehicles.size()));
    for (const Vehicle &v : vehicles) {
        out.str(v.file);
        out.flag(v.flip);
        out.u32(v.id);
        out.str(v.number);
        out.str(v.tags);
        out.i32(v.lines);
        out.flag(v.controlOff);
    }
}

void getVehicles(In &in, std::vector<Vehicle> &vehicles) {
    const quint32 n = in.count(18);
    vehicles.resize(n);
    for (quint32 i = 0; i < n && in.ok; ++i) {
        Vehicle &v = vehicles[i];
        v.file = in.str();
        v.flip = in.flag();
        v.id = in.u32();
        v.number = in.str();
        v.tags = in.str();
        v.lines = in.i32();
        v.controlOff = in.flag();
        // Its definition in this process: its Eng's, by its file.
        if (const Eng *eng = EngOfFile(v.file))
            if (eng->definition != nullptr)
                v.definition = eng->definition;
    }
}

// A train's state, its few values and its ends: what tells it changed.
QByteArray trainPayload(const TrainFrame &t) {
    Out out;
    putState(out, t.state);
    out.u32(t.carsVersion);
    out.f64(t.drawnTravelled);
    out.i32(t.pathPart);
    out.flag(t.keepsToAuthority);
    putEnds(out, t.fronts, false, t.state.reversed);
    putEnds(out, t.rears, false, t.state.reversed);
    return out.bytes;
}

void putEventState(Out &out, const EventState &s) {
    out.i32(s.level);
    out.i32(s.fired);
    out.flag(s.disabled);
    out.f64(s.firedAt);
    out.i32(s.firedBy);
    out.flag(s.standIn);
    out.f64(s.activeSince);
    out.f64(s.passedAt);
    out.flag(s.passedActive);
    out.f32(s.bestSpeed);
    out.flag(s.reachedSiding);
}

EventState getEventState(In &in) {
    EventState s;
    s.level = in.i32();
    s.fired = in.i32();
    s.disabled = in.flag();
    s.firedAt = in.f64();
    s.firedBy = in.i32();
    s.standIn = in.flag();
    s.activeSince = in.f64();
    s.passedAt = in.f64();
    s.passedActive = in.flag();
    s.bestSpeed = in.f32();
    s.reachedSiding = in.flag();
    return s;
}

// How a shared part comes: as in the frame before, whole, its changed
// entries, or none.
enum Part : quint8 { Same = 0, Whole = 1, Entries = 2, None = 3 };

// The driven cab's vehicle's controls' end: the rear's (a MaSzyna vehicle
// has a cab at each end).
bool cabRear(const DrivenFrame &d, const std::vector<Vehicle> *vehicles) {
    if (d.cabControlsFor == nullptr || vehicles == nullptr || d.cab < 0 || size_t(d.cab) >= vehicles->size())
        return false;
    const std::shared_ptr<const VehicleDefinition> &definition = (*vehicles)[size_t(d.cab)].definition;
    return definition != nullptr && definition->cab3DControlsAt(true) == d.cabControlsFor
           && definition->cab3DControlsAt(false) != d.cabControlsFor;
}

}

void FrameEncoder::next(std::shared_ptr<const SimulationFrame> frame) {
    before = now;
    trainsBefore.swap(trainsNow);
    now = std::move(frame);
    trainsNow.clear();
    if (now != nullptr) {
        trainsNow.reserve(now->trains.size());
        for (const TrainFrame &t : now->trains)
            trainsNow.push_back(trainPayload(t));
    }
    // A frame of another run follows on from nothing.
    if (before != nullptr && now != nullptr && before->runId != now->runId) {
        before.reset();
        trainsBefore.clear();
    }
    wholeMade = changesMade = false;
    changedCount = 0;
}

QByteArray FrameEncoder::whole() {
    if (!wholeMade) {
        wholeBytes = encode(true);
        wholeMade = true;
    }
    return wholeBytes;
}

QByteArray FrameEncoder::changes() {
    if (before == nullptr)
        return whole();
    if (!changesMade) {
        changesBytes = encode(false);
        changesMade = true;
    }
    return changesBytes;
}

QByteArray FrameEncoder::encode(bool all) {
    if (now == nullptr)
        return QByteArray();
    const SimulationFrame &f = *now;
    const SimulationFrame *b = all ? nullptr : before.get();
    Out out;
    out.u32(FrameMark);
    out.u8(Version);
    out.flag(b == nullptr);
    out.u32(f.runId);
    out.u64(f.tick);
    out.u64(b != nullptr ? b->tick : 0);
    out.i64(f.publishedNs);
    out.f64(f.step);
    out.u64(f.acknowledged);
    out.u64(f.steps);
    out.f64(f.time);
    out.f64(f.askedTime);
    out.u8(quint8(f.mode));
    out.f64(f.speed);
    out.f64(f.rate);
    out.f64(f.target);
    out.flag(f.finished);
    // The trains driven (task server 06: several).
    out.u32(quint32(f.driven.size()));
    for (int train : f.driven)
        out.i32(train);
    out.u8(quint8(f.activityEnd));
    out.f64(f.activityEndedAt);
    out.f64(f.collision.time);
    out.i32(f.collision.train);
    out.i32(f.collision.other);
    out.flag(f.collision.standing);
    out.f64(f.collision.speed);

    // The trains: every one, or those that changed.
    out.u32(quint32(f.trains.size()));
    std::vector<size_t> sent;
    for (size_t t = 0; t < f.trains.size(); ++t)
        if (b == nullptr || t >= b->trains.size() || t >= trainsBefore.size() || trainsBefore[t] != trainsNow[t]
                || b->trains[t].vehicles != f.trains[t].vehicles || b->trains[t].pathNodes != f.trains[t].pathNodes)
            sent.push_back(t);
    if (!all)
        changedCount = int(sent.size());
    out.u32(quint32(sent.size()));
    for (size_t t : sent) {
        const TrainFrame &train = f.trains[t];
        const bool vehicles = train.vehicles != nullptr
                && (b == nullptr || t >= b->trains.size() || b->trains[t].carsVersion != train.carsVersion
                    || b->trains[t].vehicles == nullptr);
        const bool nodes = train.pathNodes != nullptr
                && (b == nullptr || t >= b->trains.size() || b->trains[t].pathNodes != train.pathNodes);
        out.u32(quint32(t));
        out.u8(quint8((vehicles ? 1 : 0) | (nodes ? 2 : 0) | (train.pathNodes == nullptr ? 4 : 0)
                      | (train.vehicles == nullptr ? 8 : 0)));
        out.bytes.append(trainsNow[t]);
        if (vehicles)
            putVehicles(out, *train.vehicles);
        if (nodes) {
            out.u32(quint32(train.pathNodes->size()));
            for (int node : *train.pathNodes)
                out.i32(node);
        }
    }
    // Which sections each train stands on.
    const bool standsOn = b == nullptr || b->standsOn != f.standsOn;
    out.flag(standsOn);
    if (standsOn) {
        out.u32(quint32(f.standsOn.size()));
        for (size_t on : f.standsOn)
            out.u32(quint32(on));
    }

    // The aspects and draw states.
    out.u64(f.drawSerial);
    if (f.aspects == nullptr || f.drawStates == nullptr) {
        out.u8(None);
    } else if (b != nullptr && b->drawSerial == f.drawSerial && b->aspects != nullptr) {
        out.u8(Same);
    } else if (b != nullptr && b->aspects != nullptr && b->drawStates != nullptr && b->aspects->size() == f.aspects->size()
               && b->drawStates->size() == f.drawStates->size() && f.aspects->size() == f.drawStates->size()) {
        out.u8(Entries);
        std::vector<quint32> changed;
        for (size_t i = 0; i < f.aspects->size(); ++i)
            if ((*b->aspects)[i] != (*f.aspects)[i] || (*b->drawStates)[i] != (*f.drawStates)[i])
                changed.push_back(quint32(i));
        out.u32(quint32(changed.size()));
        for (quint32 i : changed) {
            out.u32(i);
            out.i32((*f.aspects)[i]);
            out.i32((*f.drawStates)[i]);
        }
    } else {
        out.u8(Whole);
        out.u32(quint32(f.aspects->size()));
        for (int v : *f.aspects)
            out.i32(v);
        out.u32(quint32(f.drawStates->size()));
        for (int v : *f.drawStates)
            out.i32(v);
    }

    // The section uses.
    out.u64(f.sectionSerial);
    out.u64(f.holdSerial);
    if (f.sectionUses == nullptr) {
        out.u8(None);
    } else if (b != nullptr && b->sectionUses == f.sectionUses) {
        out.u8(Same);
    } else if (b != nullptr && b->sectionUses != nullptr && b->sectionUses->size() == f.sectionUses->size()) {
        out.u8(Entries);
        std::vector<quint32> changed;
        for (size_t i = 0; i < f.sectionUses->size(); ++i)
            if ((*b->sectionUses)[i].holder != (*f.sectionUses)[i].holder
                    || (*b->sectionUses)[i].occupied != (*f.sectionUses)[i].occupied)
                changed.push_back(quint32(i));
        out.u32(quint32(changed.size()));
        for (quint32 i : changed) {
            out.u32(i);
            out.i32((*f.sectionUses)[i].holder);
            out.flag((*f.sectionUses)[i].occupied);
        }
    } else {
        out.u8(Whole);
        out.u32(quint32(f.sectionUses->size()));
        for (const SimulationCore::SectionUse &use : *f.sectionUses) {
            out.i32(use.holder);
            out.flag(use.occupied);
        }
    }

    // The level crossings.
    out.u64(f.levelCrossingSerial);
    if (f.levelCrossings == nullptr) {
        out.u8(None);
    } else if (b != nullptr && b->levelCrossings == f.levelCrossings) {
        out.u8(Same);
    } else {
        out.u8(Whole);
        out.u32(quint32(f.levelCrossings->size()));
        for (unsigned char closed : *f.levelCrossings)
            out.u8(closed);
    }

    // The activity's events' states.
    out.u64(f.eventSerial);
    if (f.eventStates == nullptr) {
        out.u8(None);
    } else if (b != nullptr && b->eventStates == f.eventStates) {
        out.u8(Same);
    } else {
        out.u8(Whole);
        out.u32(quint32(f.eventStates->size()));
        for (const EventState &s : *f.eventStates)
            putEventState(out, s);
    }

    // The standing trains.
    out.u64(f.standingSerial);
    if (f.standing == nullptr) {
        out.u8(None);
    } else if (b != nullptr && b->standing == f.standing) {
        out.u8(Same);
    } else {
        out.u8(Whole);
        out.u32(quint32(f.standing->size()));
        for (const StandingFrame &st : *f.standing) {
            out.str(st.name);
            putVehicles(out, st.vehicles);
            putEnds(out, st.fronts, true, false);
            putEnds(out, st.rears, true, false);
            out.flag(st.gone);
            out.flag(st.loose);
        }
    }

    // Each driven train's values: the vehicles' that changed since its
    // values before (task server 06: several).
    out.u32(quint32(f.drivenValues.size()));
    for (const std::shared_ptr<const DrivenFrame> &values : f.drivenValues) {
        const DrivenFrame &d = *values;
        const DrivenFrame *bd = b != nullptr ? b->drivenOf(d.train) : nullptr;
        out.u8(bd != nullptr ? Entries : Whole);
        out.i32(d.train);
        out.i32(d.cab);
        // Each vehicle's values that changed.
        out.u32(quint32(d.values.size()));
        for (size_t v = 0; v < d.values.size(); ++v)
            putValueChanges(out, bd != nullptr && v < bd->values.size() ? &bd->values[v] : nullptr, d.values[v]);
        out.u32(quint32(d.cabControls.size()));
        for (double value : d.cabControls)
            out.f64(value);
        const std::vector<Vehicle> *vehicles = d.train >= 0 && size_t(d.train) < f.trains.size()
                ? f.trains[size_t(d.train)].vehicles.get() : nullptr;
        out.flag(d.cabControlsFor != nullptr);
        out.flag(cabRear(d, vehicles));
        out.flag(d.cabStops != nullptr);
        if (d.cabStops != nullptr) {
            out.u32(quint32(d.cabStops->size()));
            for (const std::vector<double> &stops : *d.cabStops) {
                out.u32(quint32(stops.size()));
                for (double stop : stops)
                    out.f64(stop);
            }
        }
    }

    // The heard trains: their values when their version changed.
    out.u32(quint32(f.heard.size()));
    for (const HeardFrame &h : f.heard) {
        const HeardFrame *bh = nullptr;
        if (b != nullptr)
            for (const HeardFrame &other : b->heard)
                if (other.train == h.train)
                    bh = &other;
        out.i32(h.train);
        out.i32(h.cab);
        out.u64(h.version);
        const bool values = bh == nullptr || bh->version != h.version;
        out.flag(values);
        if (values) {
            out.u32(quint32(h.values.size()));
            for (size_t v = 0; v < h.values.size(); ++v)
                putValueChanges(out, bh != nullptr && v < bh->values.size() ? &bh->values[v] : nullptr, h.values[v]);
        }
    }
    return out.bytes;
}

void FrameDecoder::setInput(std::shared_ptr<const SimulationInput> input) {
    in = std::move(input);
}

void FrameDecoder::reset() {
    last.reset();
}

double FrameDecoder::drawnAgeMost() const {
    return std::clamp(2.0 * interval + spread, 0.05, 1.0);
}

std::shared_ptr<const SimulationFrame> FrameDecoder::decode(const QByteArray &bytes, long long arrivalNs,
                                                            QString *error) {
    const auto fail = [&](const QString &why) -> std::shared_ptr<const SimulationFrame> {
        if (error != nullptr)
            *error = why;
        return nullptr;
    };
    In r(bytes);
    if (r.u32() != FrameMark || r.u8() != Version)
        return fail("not a frame");
    const bool whole = r.flag();
    auto f = std::make_shared<SimulationFrame>();
    f->runId = r.u32();
    f->tick = r.u64();
    const quint64 baseTick = r.u64();
    const qint64 serverNs = r.i64();
    // Older than the frame this client has, or not following on from it.
    const SimulationFrame *b = nullptr;
    if (last != nullptr && last->runId == f->runId) {
        if (f->tick <= last->tick)
            return fail("an older frame");
        b = last.get();
    }
    if (!whole && (b == nullptr || baseTick != b->tick))
        return fail("a frame that does not follow on from this client's");
    if (whole)
        b = nullptr;
    f->step = r.f64();
    f->acknowledged = r.u64();
    f->steps = r.u64();
    f->time = r.f64();
    f->askedTime = r.f64();
    f->mode = RunMode(r.u8());
    f->speed = r.f64();
    f->rate = r.f64();
    f->target = r.f64();
    f->finished = r.flag();
    f->driven.resize(r.count(4));
    for (int &train : f->driven)
        train = r.i32();
    if (!std::is_sorted(f->driven.begin(), f->driven.end()))
        return fail("driven trains out of order");
    f->activityEnd = ActivityEnd(r.u8());
    f->activityEndedAt = r.f64();
    f->collision.time = r.f64();
    f->collision.train = r.i32();
    f->collision.other = r.i32();
    f->collision.standing = r.flag();
    f->collision.speed = r.f64();

    const TrackGeometry *geometry = in != nullptr ? in->geometry.get() : nullptr;
    const quint32 trains = r.count(0);
    if (!r.ok || trains > 100000)
        return fail("a frame cut short");
    f->trains.resize(trains);
    // Unchanged trains: as in the frame before.
    if (b != nullptr)
        for (size_t t = 0; t < trains && t < b->trains.size(); ++t)
            f->trains[t] = b->trains[t];
    const quint32 sent = r.count(5);
    for (quint32 k = 0; k < sent && r.ok; ++k) {
        const quint32 t = r.u32();
        const quint8 flags = r.u8();
        if (t >= trains)
            return fail("a train out of the frame");
        TrainFrame &train = f->trains[t];
        train.state = getState(r);
        train.carsVersion = r.u32();
        train.drawnTravelled = r.f64();
        train.pathPart = r.i32();
        train.keepsToAuthority = r.flag();
        getEnds(r, geometry, train.fronts);
        getEnds(r, geometry, train.rears);
        if (flags & 1) {
            auto vehicles = std::make_shared<std::vector<Vehicle>>();
            getVehicles(r, *vehicles);
            train.vehicles = std::move(vehicles);
        } else if (flags & 8) {
            train.vehicles.reset();
        }
        if (flags & 2) {
            auto nodes = std::make_shared<std::vector<int>>(r.count(4));
            for (int &node : *nodes)
                node = r.i32();
            train.pathNodes = std::move(nodes);
        } else if (flags & 4) {
            train.pathNodes.reset();
        }
    }
    if (r.flag()) {
        f->standsOn.resize(r.count(4));
        for (size_t &on : f->standsOn)
            on = r.u32();
    } else if (b != nullptr) {
        f->standsOn = b->standsOn;
    }

    // Aspects and draw states.
    f->drawSerial = r.u64();
    switch (r.u8()) {
    case Same:
        if (b == nullptr)
            return fail("aspects as before, none before");
        f->aspects = b->aspects;
        f->drawStates = b->drawStates;
        break;
    case Entries: {
        if (b == nullptr || b->aspects == nullptr || b->drawStates == nullptr)
            return fail("aspects' changes, none before");
        auto aspects = std::make_shared<std::vector<int>>(*b->aspects);
        auto draw = std::make_shared<std::vector<int>>(*b->drawStates);
        const quint32 n = r.count(12);
        for (quint32 k = 0; k < n && r.ok; ++k) {
            const quint32 i = r.u32();
            const int aspect = r.i32(), state = r.i32();
            if (i >= aspects->size() || i >= draw->size())
                return fail("an aspect out of the frame");
            (*aspects)[i] = aspect;
            (*draw)[i] = state;
        }
        f->aspects = std::move(aspects);
        f->drawStates = std::move(draw);
        break;
    }
    case Whole: {
        auto aspects = std::make_shared<std::vector<int>>(r.count(4));
        for (int &v : *aspects)
            v = r.i32();
        auto draw = std::make_shared<std::vector<int>>(r.count(4));
        for (int &v : *draw)
            v = r.i32();
        f->aspects = std::move(aspects);
        f->drawStates = std::move(draw);
        break;
    }
    default: break;
    }

    // Section uses.
    f->sectionSerial = r.u64();
    f->holdSerial = r.u64();
    switch (r.u8()) {
    case Same:
        if (b == nullptr)
            return fail("sections as before, none before");
        f->sectionUses = b->sectionUses;
        break;
    case Entries: {
        if (b == nullptr || b->sectionUses == nullptr)
            return fail("sections' changes, none before");
        auto uses = std::make_shared<std::vector<SimulationCore::SectionUse>>(*b->sectionUses);
        const quint32 n = r.count(9);
        for (quint32 k = 0; k < n && r.ok; ++k) {
            const quint32 i = r.u32();
            const int holder = r.i32();
            const bool occupied = r.flag();
            if (i >= uses->size())
                return fail("a section out of the frame");
            (*uses)[i].holder = holder;
            (*uses)[i].occupied = occupied;
        }
        f->sectionUses = std::move(uses);
        break;
    }
    case Whole: {
        auto uses = std::make_shared<std::vector<SimulationCore::SectionUse>>(r.count(5));
        for (SimulationCore::SectionUse &use : *uses) {
            use.holder = r.i32();
            use.occupied = r.flag();
        }
        f->sectionUses = std::move(uses);
        break;
    }
    default: break;
    }

    // Level crossings.
    f->levelCrossingSerial = r.u64();
    switch (r.u8()) {
    case Same:
        if (b == nullptr)
            return fail("crossings as before, none before");
        f->levelCrossings = b->levelCrossings;
        break;
    case Whole: {
        auto crossings = std::make_shared<std::vector<unsigned char>>(r.count(1));
        for (unsigned char &closed : *crossings)
            closed = r.u8();
        f->levelCrossings = std::move(crossings);
        break;
    }
    default: break;
    }

    // Events' states.
    f->eventSerial = r.u64();
    switch (r.u8()) {
    case Same:
        if (b == nullptr)
            return fail("events as before, none before");
        f->eventStates = b->eventStates;
        break;
    case Whole: {
        auto states = std::make_shared<std::vector<EventState>>(r.count(40));
        for (EventState &s : *states)
            s = getEventState(r);
        f->eventStates = std::move(states);
        break;
    }
    default: break;
    }

    // Standing trains.
    f->standingSerial = r.u64();
    switch (r.u8()) {
    case Same:
        if (b == nullptr)
            return fail("standing trains as before, none before");
        f->standing = b->standing;
        break;
    case Whole: {
        auto standing = std::make_shared<std::vector<StandingFrame>>(r.count(10));
        for (StandingFrame &st : *standing) {
            st.name = r.str();
            getVehicles(r, st.vehicles);
            getEnds(r, geometry, st.fronts);
            getEnds(r, geometry, st.rears);
            st.gone = r.flag();
            st.loose = r.flag();
        }
        f->standing = std::move(standing);
        break;
    }
    default: break;
    }

    // Each driven train's values.
    const quint32 drivenValues = r.count(9);
    for (quint32 k = 0; k < drivenValues && r.ok; ++k) {
        const quint8 drivenPart = r.u8();
        if (drivenPart != Whole && drivenPart != Entries)
            return fail("a driven train's values of no kind");
        auto d = std::make_shared<DrivenFrame>();
        d->train = r.i32();
        d->cab = r.i32();
        const quint32 vehicles = r.count(0);
        if (vehicles > 10000)
            return fail("a driven train cut short");
        d->values.resize(vehicles);
        if (drivenPart == Entries) {
            const DrivenFrame *bd = b != nullptr ? b->drivenOf(d->train) : nullptr;
            if (bd == nullptr)
                return fail("driven values' changes, none before");
            for (size_t v = 0; v < vehicles && v < bd->values.size(); ++v)
                d->values[v] = bd->values[v];
        }
        for (QHash<QString, MessageValue> &values : d->values)
            getValueChanges(r, values);
        d->cabControls.resize(r.count(8));
        for (double &value : d->cabControls)
            value = r.f64();
        const bool controls = r.flag();
        const bool rear = r.flag();
        if (controls && d->train >= 0 && size_t(d->train) < f->trains.size()) {
            const auto &list = f->trains[size_t(d->train)].vehicles;
            if (list != nullptr && d->cab >= 0 && size_t(d->cab) < list->size()
                    && (*list)[size_t(d->cab)].definition != nullptr)
                d->cabControlsFor = (*list)[size_t(d->cab)].definition->cab3DControlsAt(rear);
        }
        if (r.flag()) {
            auto stops = std::make_shared<CabStops>(r.count(4));
            for (std::vector<double> &control : *stops) {
                control.resize(r.count(8));
                for (double &stop : control)
                    stop = r.f64();
            }
            d->cabStops = std::move(stops);
        }
        f->drivenValues.push_back(std::move(d));
    }

    // The heard trains.
    const quint32 heard = r.count(17);
    f->heard.resize(heard);
    for (quint32 k = 0; k < heard && r.ok; ++k) {
        HeardFrame &h = f->heard[k];
        h.train = r.i32();
        h.cab = r.i32();
        h.version = r.u64();
        const HeardFrame *bh = nullptr;
        if (b != nullptr)
            for (const HeardFrame &other : b->heard)
                if (other.train == h.train)
                    bh = &other;
        if (r.flag()) {
            h.values.resize(r.count(8));
            for (size_t v = 0; v < h.values.size(); ++v) {
                if (bh != nullptr && v < bh->values.size())
                    h.values[v] = bh->values[v];
                getValueChanges(r, h.values[v]);
            }
        } else {
            if (bh == nullptr)
                return fail("a heard train's values as before, none before");
            h.values = bh->values;
        }
    }
    if (!r.done())
        return fail(r.ok ? "a frame with bytes left over" : "a frame cut short");

    // Its time on this clock: the server's stamp moved by the smallest
    // delay seen; the delays' spread above it, and the frames' interval,
    // say how long its trains may be moved on.
    const long long delay = arrivalNs - serverNs;
    // The smallest delay lately: it may rise 1 ms a second of the server's
    // time, so that a clock running faster than the server's is followed
    // (the review: the offset only fell, and its error grew all session).
    if (offsetKnown && lastServerNs != 0 && serverNs > lastServerNs)
        offsetNs += (serverNs - lastServerNs) / 1000;
    if (!offsetKnown || delay < offsetNs) {
        offsetNs = delay;
        offsetKnown = true;
    }
    spread = 0.9 * spread + 0.1 * double(delay - offsetNs) * 1e-9;
    if (lastServerNs != 0 && serverNs > lastServerNs) {
        const double gap = double(serverNs - lastServerNs) * 1e-9;
        if (gap > 1e-3 && gap < 2.0)
            interval = 0.9 * interval + 0.1 * gap;
    }
    lastServerNs = serverNs;
    f->publishedNs = serverNs + offsetNs;
    f->drawnAgeMost = drawnAgeMost();
    last = std::move(f);
    return last;
}

QByteArray EncodeEvents(const std::vector<TrainEvent> &events) {
    Out out;
    out.u32(EventsMark);
    out.u8(Version);
    out.u32(quint32(events.size()));
    for (const TrainEvent &e : events) {
        out.f64(e.time);
        out.i32(e.train);
        out.i32(int(e.kind));
        out.i32(e.index);
        out.u8(quint8(e.waitFor));
        out.i32(e.signal);
        out.f64(e.travelled);
        out.i32(e.count);
    }
    return out.bytes;
}

bool DecodeEvents(const QByteArray &bytes, std::vector<TrainEvent> &out) {
    In r(bytes);
    if (r.u32() != EventsMark || r.u8() != Version)
        return false;
    out.resize(r.count(37));
    for (TrainEvent &e : out) {
        e.time = r.f64();
        e.train = r.i32();
        e.kind = TrainEvent::Kind(r.i32());
        e.index = r.i32();
        e.waitFor = WaitFor(r.u8());
        e.signal = r.i32();
        e.travelled = r.f64();
        e.count = r.i32();
    }
    return r.done();
}

QByteArray EncodeMessages(const std::vector<TrainMessage> &messages) {
    Out out;
    out.u32(MessagesMark);
    out.u8(Version);
    out.u32(quint32(messages.size()));
    for (const TrainMessage &m : messages) {
        out.i32(m.train);
        out.i32(m.vehicle);
        out.str(m.message.name);
        putValue(out, m.message.value);
    }
    return out.bytes;
}

bool DecodeMessages(const QByteArray &bytes, std::vector<TrainMessage> &out) {
    In r(bytes);
    if (r.u32() != MessagesMark || r.u8() != Version)
        return false;
    out.resize(r.count(14));
    for (TrainMessage &m : out) {
        m.train = r.i32();
        m.vehicle = r.i32();
        m.message.name = r.str();
        m.message.value = getValue(r);
    }
    return r.done();
}

QByteArray Record(Kind kind, const QByteArray &body) {
    QByteArray bytes;
    {
        QDataStream out(&bytes, QIODevice::WriteOnly);
        NetworkToken::write(out, TS::TSRE_Simulation);
        // The envelope's historical fields, as every binary record has them.
        out << qint32(0) << qint8(0) << qint32(0) << qint32(0) << quint8(kind);
    }
    bytes.append(body);
    return bytes;
}

bool ParseRecord(const QByteArray &payload, Kind &kind, QByteArray &body) {
    // 'B', the token, the envelope's 13 bytes, the kind.
    constexpr int Head = 1 + 4 + 13 + 1;
    if (payload.size() < Head || payload[0] != 'B')
        return false;
    quint32 token;
    std::memcpy(&token, payload.constData() + 1, 4);
    if (qFromLittleEndian(token) != quint32(TS::TSRE_Simulation))
        return false;
    kind = Kind(quint8(payload[Head - 1]));
    body = payload.mid(Head);
    return true;
}

QByteArray EncodeAnnouncement(const Announcement &run) {
    Out out;
    out.u32(run.runId);
    out.str(run.options.date);
    out.flag(run.options.boardingHolds);
    out.flag(run.options.driving);
    out.u32(quint32(run.digest.size()));
    out.bytes.append(run.digest);
    out.u32(quint32(run.trainDigests.size()));
    for (const QByteArray &digest : run.trainDigests) {
        out.u32(quint32(digest.size()));
        out.bytes.append(digest);
    }
    const QByteArray data = run.data.encode();
    out.u32(quint32(data.size()));
    out.bytes.append(data);
    return out.bytes;
}

namespace {
QByteArray getBytes(In &in) {
    const quint32 n = in.count(1);
    QByteArray bytes;
    bytes.resize(int(n));
    for (quint32 i = 0; i < n && in.ok; ++i)
        bytes[int(i)] = char(in.u8());
    return bytes;
}
}

bool DecodeAnnouncement(const QByteArray &bytes, Announcement &out, QString *error) {
    In r(bytes);
    out = Announcement();
    out.runId = r.u32();
    out.options.date = r.str();
    out.options.boardingHolds = r.flag();
    out.options.driving = r.flag();
    out.digest = getBytes(r);
    const quint32 trains = r.count(4);
    for (quint32 t = 0; t < trains && r.ok; ++t)
        out.trainDigests.push_back(getBytes(r));
    const QByteArray data = getBytes(r);
    if (!r.done()) {
        if (error != nullptr)
            *error = "a run's announcement cut short";
        return false;
    }
    return RunData::Decode(data, out.data, error);
}

QByteArray EncodeReady(unsigned int runId, bool same, const QString &why) {
    Out out;
    out.u32(runId);
    out.flag(same);
    out.str(why);
    return out.bytes;
}

bool DecodeReady(const QByteArray &bytes, unsigned int &runId, bool &same, QString &why) {
    In r(bytes);
    runId = r.u32();
    same = r.flag();
    why = r.str();
    return r.done();
}

QByteArray EncodeTrains(unsigned int runId, const std::vector<int> &trains) {
    Out out;
    out.u32(runId);
    out.u32(quint32(trains.size()));
    for (int train : trains)
        out.i32(train);
    return out.bytes;
}

bool DecodeTrains(const QByteArray &bytes, unsigned int &runId, std::vector<int> &trains) {
    In r(bytes);
    runId = r.u32();
    trains.resize(r.count(4));
    for (int &train : trains)
        train = r.i32();
    return r.done();
}

QByteArray EncodeCommand(unsigned int runId, int train, int vehicle, const Message &message) {
    Out out;
    out.u32(runId);
    out.i32(train);
    out.i32(vehicle);
    out.str(message.name);
    putValue(out, message.value);
    return out.bytes;
}

bool DecodeCommand(const QByteArray &bytes, unsigned int &runId, int &train, int &vehicle, Message &message) {
    In r(bytes);
    runId = r.u32();
    train = r.i32();
    vehicle = r.i32();
    message.name = r.str();
    message.value = getValue(r);
    return r.done();
}

QByteArray EncodeEnd(unsigned int runId, const QString &why) {
    Out out;
    out.u32(runId);
    out.str(why);
    return out.bytes;
}

bool DecodeEnd(const QByteArray &bytes, unsigned int &runId, QString &why) {
    In r(bytes);
    runId = r.u32();
    why = r.str();
    return r.done();
}

QByteArray EncodeTaken(unsigned long long sequence) {
    Out out;
    out.u64(sequence);
    return out.bytes;
}

bool DecodeTaken(const QByteArray &bytes, unsigned long long &sequence) {
    In r(bytes);
    sequence = r.u64();
    return r.done();
}

QByteArray EncodeValue(const MessageValue &value) {
    Out out;
    putValue(out, value);
    return out.bytes;
}

}
}
