#include <tsre/trains/VehicleKeys.h>
#include <QHash>
#include <QStringList>

namespace VehicleKeys {
namespace {

struct Entry {
    const char *path;
    const char *read;
    const char *values;
    const char *source;
};

const Entry Table[] = {
#include "VehicleKeysTable.inc"
};

// Open Rails' units (STFReader.UNITS) as TSRE's quantities and the unit of
// a number written without one: Open Rails scales such a number as its
// own unit, which is SI but for these.
QStringList unmapped;

Value unitValue(const QString &unit) {
    using Q = Units::Quantity;
    static const QHash<QString, std::pair<Q, QString>> units{
            {"None", {Q::Any, {}}},
            {"Mass", {Q::Mass, {}}},
            {"Distance", {Q::Length, {}}},
            {"AreaDefaultFT2", {Q::Area, "ft^2"}},
            {"Volume", {Q::Volume, "l"}},
            {"VolumeDefaultFT3", {Q::Volume, "ft^3"}},
            {"Time", {Q::Time, {}}},
            {"TimeDefaultM", {Q::Time, "min"}},
            {"TimeDefaultH", {Q::Time, "h"}},
            {"Current", {Q::Current, {}}},
            {"Voltage", {Q::Voltage, {}}},
            {"MassRateDefaultLBpH", {Q::MassRate, "lb/h"}},
            {"Speed", {Q::Speed, {}}},
            {"SpeedDefaultMPH", {Q::Speed, "mph"}},
            {"Frequency", {Q::Frequency, {}}},
            {"Force", {Q::Force, {}}},
            {"Power", {Q::Power, {}}},
            {"Stiffness", {Q::Stiffness, {}}},
            {"Resistance", {Q::Resistance, {}}},
            {"PressureDefaultPSI", {Q::Pressure, "psi"}},
            {"PressureDefaultInHg", {Q::Pressure, "inhg"}},
            {"PressureRateDefaultPSIpS", {Q::PressureRate, "psi/s"}},
            {"PressureRateDefaultInHgpS", {Q::PressureRate, "inhg/s"}},
            {"EnergyDensity", {Q::EnergyDensity, "kj/kg"}},
            {"ResistanceDavisC", {Q::ResistanceSquared, {}}},
            {"Temperature", {Q::Temperature, {}}},
            {"Angle", {Q::Angle, {}}},
            {"Energy", {Q::Energy, {}}},
            {"ForceRate", {Q::ForceRate, {}}},
            {"PowerRate", {Q::PowerRate, {}}},
    };
    Value value;
    value.kind = Value::Kind::Number;
    const auto at = units.constFind(unit);
    if (at != units.cend()) {
        value.quantity = at->first;
        value.defaultUnit = at->second;
    } else if (!unmapped.contains(unit)) {
        unmapped << unit;
    }
    return value;
}

struct Catalogue {
    QVector<Key> keys;
    QHash<QString, int> byPath;
    Catalogue() {
        for (const Entry &e : Table) {
            Key key;
            key.path = QString::fromLatin1(e.path);
            const QString read = QString::fromLatin1(e.read);
            key.read = read == "block" ? Read::Block : read == "present" ? Read::Present : Read::Values;
            key.source = QString::fromLatin1(e.source);
            for (const QString &v : QString::fromLatin1(e.values).split(' ', Qt::SkipEmptyParts)) {
                if (v.startsWith("number:")) {
                    key.values << unitValue(v.mid(7));
                } else {
                    Value value;
                    value.kind = v == "integer" ? Value::Kind::Integer
                                 : v == "bool"  ? Value::Kind::Bool
                                                : Value::Kind::Text;
                    key.values << value;
                }
            }
            byPath.insert(key.path, int(keys.size()));
            keys << key;
        }
    }
};

const Catalogue &catalogue() {
    static const Catalogue made;
    return made;
}

} // namespace

const Key *find(QAnyStringView path) {
    const Catalogue &c = catalogue();
    const auto at = c.byPath.constFind(path.toString().toLower());
    return at == c.byPath.cend() ? nullptr : &c.keys[*at];
}

const QVector<Key> &all() { return catalogue().keys; }

QStringList unmappedUnits() {
    catalogue();
    return unmapped;
}

Value value(QAnyStringView path, int index) {
    const Key *key = find(path);
    if (key == nullptr || key->values.isEmpty() || index < 0)
        return {};
    return key->values[qMin(index, int(key->values.size()) - 1)];
}

}
