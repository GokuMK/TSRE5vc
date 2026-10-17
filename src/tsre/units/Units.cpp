#include <tsre/units/Units.h>

#include <QLocale>
#include <QRegularExpression>
#include <atomic>
#include <cmath>
#include <initializer_list>
#include <settings/SettingsAccess.h>
#include <settings/SettingsManager.h>

namespace Units {

namespace {

constexpr double Pi = 3.14159265358979323846;
constexpr double Foot = 0.3048;
constexpr double Inch = 0.0254;
constexpr double Mile = 1609.344;
constexpr double Pound = 0.45359237;
constexpr double PoundForce = 4.4482216152605;
constexpr double Horsepower = 745.69987158227022;
constexpr double Psi = 6894.757293168361;
constexpr double InchOfMercury = 3386.389;
constexpr double ShortTon = 907.18474;
constexpr double LongTon = 1016.0469088;
constexpr double MilePerHour = Mile / 3600.0;
constexpr double KilometrePerHour = 1.0 / 3.6;
constexpr double UsGallon = 3.785411784e-3;
constexpr double UkGallon = 4.54609e-3;

// A unit: its name as files write it (lower case) and SI = number × factor
// + offset (only temperatures have an offset).
struct Unit {
    const char *name;
    double factor;
    double offset = 0.0;
};

struct Table {
    Quantity quantity;
    std::initializer_list<Unit> units;
};

// The units each quantity takes in files, after MSTS's and Open Rails'
// readers (Open Rails' STFReader as a reference for the units in use).
const Table Tables[] = {
    {Quantity::Length, {{"m", 1.0}, {"cm", 0.01}, {"mm", 0.001}, {"km", 1000.0}, {"ft", Foot},
                        {"in", Inch}, {"in/2", Inch / 2.0}, {"yd", 3.0 * Foot}, {"mi", Mile}}},
    {Quantity::Mass, {{"kg", 1.0}, {"g", 0.001}, {"t", 1000.0}, {"t-uk", LongTon}, {"t-us", ShortTon},
                      {"lb", Pound}, {"lbs", Pound}}},
    {Quantity::Speed, {{"m/s", 1.0}, {"mps", 1.0}, {"cm/s", 0.01}, {"mm/s", 0.001}, {"km/h", KilometrePerHour},
                       {"kph", KilometrePerHour}, {"kmh", KilometrePerHour}, {"kmph", KilometrePerHour},
                       {"mph", MilePerHour}, {"ft/s", Foot}, {"in/s", Inch}}},
    {Quantity::Force, {{"n", 1.0}, {"kn", 1000.0}, {"lbf", PoundForce}, {"klbf", 1000.0 * PoundForce},
                       {"lb", PoundForce}}},
    {Quantity::Power, {{"w", 1.0}, {"kw", 1000.0}, {"mw", 1.0e6}, {"hp", Horsepower}}},
    {Quantity::Pressure, {{"pa", 1.0}, {"kpa", 1000.0}, {"bar", 1.0e5}, {"psi", Psi},
                          {"inhg", InchOfMercury}, {"cmhg", InchOfMercury / 2.54},
                          {"kgf/cm^2", 98066.5}}},
    {Quantity::Temperature, {{"degc", 1.0}, {"degf", 5.0 / 9.0, -32.0 * 5.0 / 9.0}}},
    {Quantity::Angle, {{"rad", 1.0}, {"deg", Pi / 180.0}}},
    {Quantity::Time, {{"s", 1.0}, {"min", 60.0}, {"m", 60.0}, {"h", 3600.0}}},
    {Quantity::Current, {{"a", 1.0}, {"amps", 1.0}}},
    {Quantity::Voltage, {{"v", 1.0}, {"kv", 1000.0}}},
    {Quantity::Frequency, {{"hz", 1.0}, {"rps", 1.0}, {"rpm", 1.0 / 60.0}}},
    {Quantity::Area, {{"m^2", 1.0}, {"*(m^2)", 1.0}, {"ft^2", Foot * Foot}, {"*(ft^2)", Foot * Foot}}},
    {Quantity::Volume, {{"m^3", 1.0}, {"*(m^3)", 1.0}, {"l", 0.001}, {"ft^3", Foot * Foot * Foot},
                        {"*(ft^3)", Foot * Foot * Foot}, {"in^3", Inch * Inch * Inch},
                        {"*(in^3)", Inch * Inch * Inch}, {"g-uk", UkGallon}, {"g-us", UsGallon},
                        {"gal", UsGallon}, {"gals", UsGallon}}},
    {Quantity::Resistance, {{"n/m/s", 1.0}, {"n/(m/s)", 1.0}, {"ns/m", 1.0},
                            {"lbf/mph", PoundForce / MilePerHour}}},
    {Quantity::ResistanceSquared, {{"n/(m/s)^2", 1.0}, {"n/m/s^2", 1.0}, {"nm/s^2", 1.0}, {"ns^2/m^2", 1.0},
                                   {"n/(m/s)2", 1.0}, {"lbf/mph^2", PoundForce / (MilePerHour * MilePerHour)},
                                   {"lbf/mph2", PoundForce / (MilePerHour * MilePerHour)}}},
    {Quantity::Stiffness, {{"n/m", 1.0}}},
    {Quantity::Energy, {{"j", 1.0}, {"kj", 1000.0}, {"mj", 1.0e6}, {"wh", 3600.0}, {"kwh", 3.6e6}}},
    {Quantity::EnergyDensity, {{"j/kg", 1.0}, {"kj/kg", 1000.0}, {"j/g", 1000.0},
                               {"btu/lb", 1055.05585262 / Pound}}},
    {Quantity::MassRate, {{"kg/s", 1.0}, {"kg/h", 1.0 / 3600.0}, {"g/h", 0.001 / 3600.0},
                          {"lb/h", Pound / 3600.0}}},
    {Quantity::PressureRate, {{"pa/s", 1.0}, {"psi/s", Psi}, {"psi/min", Psi / 60.0}, {"bar/s", 1.0e5},
                              {"bar/min", 1.0e5 / 60.0}, {"kpa/s", 1000.0}, {"inhg/s", InchOfMercury},
                              {"cmhg/s", InchOfMercury / 2.54}}},
    {Quantity::ForceRate, {{"n/s", 1.0}, {"kn/s", 1000.0}, {"lbf/s", PoundForce}, {"lb/s", PoundForce}}},
    {Quantity::PowerRate, {{"w/s", 1.0}, {"kw/s", 1000.0}, {"hp/s", Horsepower}}},
};

// Quantity::Any: the order units are looked for in, so a unit two
// quantities take reads as files mean it (m metres, lb mass).
const Quantity AnyOrder[] = {
    Quantity::Length, Quantity::Mass, Quantity::Speed, Quantity::Force, Quantity::Power,
    Quantity::Pressure, Quantity::Temperature, Quantity::Angle, Quantity::Time, Quantity::Current,
    Quantity::Voltage, Quantity::Frequency, Quantity::Area, Quantity::Volume, Quantity::Resistance,
    Quantity::ResistanceSquared, Quantity::Stiffness, Quantity::Energy, Quantity::EnergyDensity,
    Quantity::MassRate, Quantity::PressureRate, Quantity::ForceRate, Quantity::PowerRate,
};

const Table *tableOf(Quantity quantity) {
    if (quantity == Quantity::Distance)
        quantity = Quantity::Length;
    for (const Table &table : Tables)
        if (table.quantity == quantity)
            return &table;
    return nullptr;
}

const Unit *find(const Table *table, QStringView name) {
    if (table == nullptr)
        return nullptr;
    for (const Unit &unit : table->units)
        if (name.compare(QLatin1String(unit.name), Qt::CaseInsensitive) == 0)
            return &unit;
    return nullptr;
}

// The unit `name` in `quantity`'s table, or in the first table that has it
// for Any (`found` then tells which).
const Unit *lookup(QStringView name, Quantity quantity, Quantity *found = nullptr) {
    if (quantity != Quantity::Any) {
        if (found != nullptr)
            *found = quantity;
        return find(tableOf(quantity), name);
    }
    for (Quantity candidate : AnyOrder)
        if (const Unit *unit = find(tableOf(candidate), name)) {
            if (found != nullptr)
                *found = candidate;
            return unit;
        }
    return nullptr;
}

// A unit shown in the editors: its symbol and its factor and offset to SI.
struct Shown {
    const char *symbol;
    double factor;
    double offset = 0.0;
};

Shown shownUnit(Quantity quantity, System system) {
    const bool metric = system == System::Metric, us = system == System::ImperialUS;
    switch (quantity) {
    case Quantity::Length: return metric ? Shown{"m", 1.0} : Shown{"ft", Foot};
    case Quantity::Distance: return metric ? Shown{"km", 1000.0} : Shown{"mi", Mile};
    case Quantity::Speed: return metric ? Shown{"km/h", KilometrePerHour} : Shown{"mph", MilePerHour};
    case Quantity::Mass: return metric ? Shown{"t", 1000.0} : Shown{"ton", us ? ShortTon : LongTon};
    case Quantity::Force: return metric ? Shown{"kN", 1000.0} : Shown{"lbf", PoundForce};
    case Quantity::Power: return metric ? Shown{"kW", 1000.0} : Shown{"hp", Horsepower};
    case Quantity::Pressure: return metric ? Shown{"bar", 1.0e5} : Shown{"psi", Psi};
    case Quantity::Temperature:
        return us ? Shown{"°F", 5.0 / 9.0, -32.0 * 5.0 / 9.0} : Shown{"°C", 1.0};
    case Quantity::Angle: return Shown{"°", Pi / 180.0};
    case Quantity::Time: return Shown{"s", 1.0};
    case Quantity::Current: return Shown{"A", 1.0};
    case Quantity::Voltage: return Shown{"V", 1.0};
    case Quantity::Frequency: return Shown{"Hz", 1.0};
    case Quantity::Area: return Shown{"m²", 1.0};
    case Quantity::Volume: return Shown{"m³", 1.0};
    case Quantity::Resistance: return Shown{"N/(m/s)", 1.0};
    case Quantity::ResistanceSquared: return Shown{"N/(m/s)²", 1.0};
    case Quantity::Stiffness: return Shown{"N/m", 1.0};
    case Quantity::Energy: return Shown{"J", 1.0};
    case Quantity::EnergyDensity: return Shown{"J/kg", 1.0};
    case Quantity::MassRate: return Shown{"kg/s", 1.0};
    case Quantity::PressureRate: return Shown{"Pa/s", 1.0};
    case Quantity::ForceRate: return Shown{"N/s", 1.0};
    case Quantity::PowerRate: return Shown{"W/s", 1.0};
    case Quantity::Any: break;
    }
    return Shown{"", 1.0};
}

int usualDecimals(Quantity quantity) {
    switch (quantity) {
    case Quantity::Power: return 0;
    case Quantity::Speed:
    case Quantity::Mass:
    case Quantity::Force:
    case Quantity::Temperature:
    case Quantity::Angle: return 1;
    case Quantity::Length:
    case Quantity::Distance:
    case Quantity::Pressure: return 2;
    default: return 3;
    }
}

std::atomic<int> chosen{int(Choice::Automatic)};

const char *const SettingKey = "core.interface.units";

Choice readChoice() {
    const int value = Settings::variant(SettingKey, SettingType::Enum).toInt();
    return value >= int(Choice::Automatic) && value <= int(Choice::ImperialUK) ? Choice(value) : Choice::Automatic;
}

}

bool fromFile(double number, QStringView unit, Quantity quantity, double &si, QStringView defaultUnit) {
    si = number;
    const QStringView name = unit.trimmed();
    if (name.isEmpty()) {
        if (defaultUnit.isEmpty())
            return true;
        const Unit *found = lookup(defaultUnit, quantity);
        if (found == nullptr)
            return false;
        si = number * found->factor + found->offset;
        return true;
    }
    const Unit *found = lookup(name, quantity);
    if (found == nullptr)
        return false;
    si = number * found->factor + found->offset;
    return true;
}

bool toFile(double si, QStringView unit, Quantity quantity, double &number) {
    number = si;
    const QStringView name = unit.trimmed();
    if (name.isEmpty())
        return true;
    const Unit *found = lookup(name, quantity);
    if (found == nullptr)
        return false;
    number = (si - found->offset) / found->factor;
    return true;
}

Quantity quantityOf(QStringView unit) {
    Quantity found = Quantity::Any;
    return lookup(unit.trimmed(), Quantity::Any, &found) != nullptr ? found : Quantity::Any;
}

System localeSystem() {
    switch (QLocale::system().measurementSystem()) {
    case QLocale::ImperialUSSystem: return System::ImperialUS;
    case QLocale::ImperialUKSystem: return System::ImperialUK;
    default: return System::Metric;
    }
}

System system() {
    switch (choice()) {
    case Choice::Metric: return System::Metric;
    case Choice::ImperialUS: return System::ImperialUS;
    case Choice::ImperialUK: return System::ImperialUK;
    case Choice::Automatic: break;
    }
    return localeSystem();
}

void setChoice(Choice value) {
    Notifier *notifier = Notifier::instance();
    if (chosen.exchange(int(value)) != int(value))
        emit notifier->changed();
}

Choice choice() {
    // The setting is read once and again when it changes (Notifier).
    Notifier::instance();
    return Choice(chosen.load(std::memory_order_relaxed));
}

QString unit(Quantity quantity) {
    return unit(quantity, system());
}

QString unit(Quantity quantity, System inSystem) {
    return QString::fromUtf8(shownUnit(quantity, inSystem).symbol);
}

double toShown(double si, Quantity quantity) {
    const Shown shown = shownUnit(quantity, system());
    return (si - shown.offset) / shown.factor;
}

double fromShown(double value, Quantity quantity) {
    const Shown shown = shownUnit(quantity, system());
    return value * shown.factor + shown.offset;
}

QString formatNumber(double si, Quantity quantity, int decimals) {
    const double value = toShown(si, quantity);
    QString text = QString::number(value, 'f', decimals >= 0 ? decimals : usualDecimals(quantity));
    if (text.contains('.')) {
        while (text.endsWith('0'))
            text.chop(1);
        if (text.endsWith('.'))
            text.chop(1);
    }
    if (text == "-0")
        text = "0";
    return text;
}

QString format(double si, Quantity quantity, int decimals) {
    const QString symbol = unit(quantity);
    const QString number = formatNumber(si, quantity, decimals);
    // An angle's degrees stick to the number ("90°"); other units, degrees
    // Celsius and Fahrenheit too, stand apart ("20 °C").
    return symbol == QString(QChar(0x00b0)) ? number + symbol : number + ' ' + symbol;
}

bool parse(const QString &text, Quantity quantity, double &si) {
    static const QRegularExpression pattern(
        R"(^\s*([-+]?(?:\d+[.,]?\d*|[.,]\d+)(?:[eE][-+]?\d+)?)\s*(.*?)\s*$)");
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch())
        return false;
    QString digits = match.captured(1);
    digits.replace(',', '.');
    bool ok = false;
    const double number = digits.toDouble(&ok);
    if (!ok)
        return false;
    const QString written = match.captured(2);
    if (written.isEmpty()) {
        si = fromShown(number, quantity);
        return true;
    }
    // The units shown in any system, then the units files take.
    for (System candidate : {system(), System::Metric, System::ImperialUS, System::ImperialUK}) {
        const Shown shown = shownUnit(quantity, candidate);
        if (written.compare(QString::fromUtf8(shown.symbol), Qt::CaseInsensitive) == 0) {
            si = number * shown.factor + shown.offset;
            return true;
        }
    }
    QString name = written;
    if (quantity == Quantity::Angle && name == QString(QChar(0x00b0)))
        name = "deg";
    if (quantity == Quantity::Temperature && name.startsWith(QChar(0x00b0)))
        name = "deg" + name.mid(1);
    return quantity != Quantity::Any && fromFile(number, name, quantity, si);
}

Notifier::Notifier() {
    QObject::connect(&SettingsManager::instance(), &SettingsManager::runtimeSettingsChanged, this,
                     [](const QStringList &keys) {
                         if (keys.isEmpty() || keys.contains(QLatin1String(SettingKey)))
                             setChoice(readChoice());
                     });
}

Notifier *Notifier::instance() {
    static Notifier *notifier = [] {
        Notifier *made = new Notifier();
        chosen.store(int(readChoice()));
        return made;
    }();
    return notifier;
}

}
