#include <tsre/sim/TrainLines.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/trains/VehicleDefinition.h>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace Sim {

namespace {

struct Word {
    const char *name;
    int bit;
};
constexpr Word Words[] = {
    {"brakehose", Lines::BrakeHose},     {"control", Lines::Control},     {"highvoltage", Lines::HighVoltage},
    {"gangway", Lines::Gangway},         {"mainhose", Lines::MainHose},   {"heating", Lines::Heating},
    {"permanent", Lines::Permanent},     {"24v", Lines::LowVoltage24},    {"110v", Lines::LowVoltage110},
    {"400v", Lines::ThreePhase},
};

// The coupler end's section (BuffCoupl1 the front, BuffCoupl2 the rear).
const MaszynaFiz::Section *endSection(const MaszynaFiz &fiz, int end) {
    return fiz.section(end == 0 ? u"BuffCoupl1" : u"BuffCoupl2");
}

}

int ParseLines(const QString &words, QString *unknown) {
    int lines = Lines::Coupler;
    QStringList other;
    for (const QString &word : words.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        bool known = false;
        for (const Word &w : Words)
            if (word.compare(QLatin1String(w.name), Qt::CaseInsensitive) == 0) {
                lines |= w.bit;
                known = true;
            }
        if (!known)
            other << word;
    }
    if (unknown != nullptr)
        *unknown = other.join(QLatin1Char(' '));
    return lines;
}

QString LinesText(int lines) {
    QStringList out;
    for (const Word &w : Words)
        if ((lines & w.bit) != 0)
            out << QLatin1String(w.name);
    return out.join(QLatin1Char(' '));
}

int AllowedLines(const VehicleDefinition &vehicle, int end) {
    if (vehicle.maszyna == nullptr)
        return Lines::Coupler | Lines::BrakeHose;
    const MaszynaFiz &fiz = vehicle.maszyna->physics();
    double allowed = 3.0;
    if (const MaszynaFiz::Section *both = fiz.section(u"BuffCoupl"))
        allowed = both->number(u"AllowedFlag", allowed);
    if (const MaszynaFiz::Section *own = endSection(fiz, end))
        allowed = own->number(u"AllowedFlag", allowed);
    const int flag = int(std::lround(allowed));
    return flag < 0 ? (-flag | Lines::Permanent) : flag;
}

QString ControlType(const VehicleDefinition &vehicle, int end) {
    if (vehicle.maszyna == nullptr)
        return QString();
    const MaszynaFiz &fiz = vehicle.maszyna->physics();
    QString type;
    if (const MaszynaFiz::Section *both = fiz.section(u"BuffCoupl"); both && both->value(u"ControlType"))
        type = both->text(u"ControlType");
    if (const MaszynaFiz::Section *own = endSection(fiz, end); own && own->value(u"ControlType"))
        type = own->text(u"ControlType");
    return type;
}

namespace {

// A coupler key's value: BuffCoupl. for both ends, BuffCoupl1./2. its own.
int couplerNumber(const VehicleDefinition &vehicle, int end, const char16_t *key, int otherwise) {
    if (vehicle.maszyna == nullptr)
        return otherwise;
    const MaszynaFiz &fiz = vehicle.maszyna->physics();
    double value = otherwise;
    if (const MaszynaFiz::Section *both = fiz.section(u"BuffCoupl"))
        value = both->number(key, value);
    if (const MaszynaFiz::Section *own = endSection(fiz, end))
        value = own->number(key, value);
    return int(std::lround(value));
}

constexpr int LowVoltages = Lines::LowVoltage24 | Lines::LowVoltage110 | Lines::ThreePhase;

}

int PowerFlag(const VehicleDefinition &vehicle, int end) {
    if (vehicle.maszyna == nullptr)
        return 0;
    return couplerNumber(vehicle, end, u"PowerFlag", Lines::LowVoltage24 | Lines::LowVoltage110) & LowVoltages;
}

int PowerCoupling(const VehicleDefinition &vehicle, int end) {
    return couplerNumber(vehicle, end, u"PowerCoupling", Lines::Permanent);
}

int DefaultLines(const VehicleDefinition &front, int frontEnd, const VehicleDefinition &rear, int rearEnd, bool twin) {
    const bool frontFiz = front.maszyna != nullptr, rearFiz = rear.maszyna != nullptr;
    if (frontFiz && rearFiz) {
        int lines = AllowedLines(front, frontEnd) & AllowedLines(rear, rearEnd);
        if ((lines & Lines::Control) != 0 && ControlType(front, frontEnd) != ControlType(rear, rearEnd))
            lines &= ~Lines::Control;
        // The low voltages only where the joint carries what both ends'
        // PowerCoupling asks, those both ends' PowerFlag give (MaSzyna
        // passes them so, PowerCouplersCheck, whatever AllowedFlag has).
        const auto carries = [lines](int coupling) { return (lines & coupling) == coupling; };
        const int frontPower = carries(PowerCoupling(front, frontEnd)) ? PowerFlag(front, frontEnd) : 0;
        const int rearPower = carries(PowerCoupling(rear, rearEnd)) ? PowerFlag(rear, rearEnd) : 0;
        return (lines & ~LowVoltages) | (frontPower & rearPower) | Lines::Coupler;
    }
    int lines = Lines::Coupler | Lines::BrakeHose | (twin ? Lines::MainHose : 0);
    if (!frontFiz && !rearFiz)
        lines |= Lines::Control;
    return lines;
}

std::vector<LineVoltages> TrainVoltages(const std::vector<int> &joints, const std::vector<LineSupply> &supplies) {
    const size_t n = supplies.size();
    std::vector<LineVoltages> at(n);
    // Each line along the train both ways: what arrives at each vehicle over
    // the joint on its one side, from the vehicles beyond (MaSzyna: the
    // voltage a coupler passes is the vehicle's own or what reaches it
    // from beyond, live where the joint carries the line and it has one).
    const auto joint = [&](size_t i) { return i < joints.size() ? joints[i] : 0; };
    struct Arrived {
        double voltage = 0.0;
        bool live = false;
    };
    // The high voltage and heating cable together (MaSzyna's power_high).
    const auto high = [&](bool forward) {
        std::vector<Arrived> arrived(n);
        Arrived carried;
        for (size_t k = 0; k < n; ++k) {
            const size_t v = forward ? k : n - 1 - k;
            arrived[v] = carried;
            if (k + 1 == n)
                break;
            const int lines = joint(forward ? v : v - 1);
            const bool present = (lines & (Lines::HighVoltage | Lines::Heating)) != 0;
            const LineSupply &own = supplies[v];
            const bool exports = (lines & Lines::HighVoltage) != 0
                    || ((lines & Lines::Heating) != 0 && own.heatingCable && own.highVoltage > 1.0);
            Arrived next;
            next.voltage = std::max(own.highVoltage, carried.live ? carried.voltage : 0.0);
            next.live = (carried.live && carried.voltage > 0.1 && present) || (own.highVoltage > 0.1 && exports);
            carried = next;
        }
        return arrived;
    };
    const auto low = [&](bool forward, int bit, double LineSupply::*source) {
        std::vector<Arrived> arrived(n);
        Arrived carried;
        for (size_t k = 0; k < n; ++k) {
            const size_t v = forward ? k : n - 1 - k;
            arrived[v] = carried;
            if (k + 1 == n)
                break;
            const bool present = (joint(forward ? v : v - 1) & bit) != 0;
            const double own = supplies[v].*source;
            Arrived next;
            next.voltage = std::max(own, carried.live ? carried.voltage : 0.0);
            next.live = present && ((carried.live && carried.voltage > 0.1) || own > 0.1);
            carried = next;
        }
        return arrived;
    };
    // What a vehicle reads over the joint on each side: a joint carrying the
    // line.
    const auto read = [&](const std::vector<Arrived> &fromFront, const std::vector<Arrived> &fromRear, size_t v,
                          int bit) {
        double voltage = 0.0;
        if (v > 0 && (joint(v - 1) & bit) != 0 && fromFront[v].live)
            voltage = std::max(voltage, fromFront[v].voltage);
        if (v + 1 < n && (joint(v) & bit) != 0 && fromRear[v].live)
            voltage = std::max(voltage, fromRear[v].voltage);
        return voltage;
    };
    const std::vector<Arrived> highFront = high(true), highRear = high(false);
    const std::vector<Arrived> f110 = low(true, Lines::LowVoltage110, &LineSupply::v110),
                               r110 = low(false, Lines::LowVoltage110, &LineSupply::v110);
    const std::vector<Arrived> f24 = low(true, Lines::LowVoltage24, &LineSupply::v24),
                               r24 = low(false, Lines::LowVoltage24, &LineSupply::v24);
    for (size_t v = 0; v < n; ++v) {
        at[v].highVoltage = read(highFront, highRear, v, Lines::HighVoltage);
        at[v].heating = read(highFront, highRear, v, Lines::Heating);
        at[v].v110 = read(f110, r110, v, Lines::LowVoltage110);
        at[v].v24 = read(f24, r24, v, Lines::LowVoltage24);
    }
    return at;
}

}
