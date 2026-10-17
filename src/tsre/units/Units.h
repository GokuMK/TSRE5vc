#pragma once
#include <QObject>
#include <QString>
#include <QStringView>

// Physical values (task core si-units): SI inside TSRE, the units of MSTS's
// files when reading them, the user's units in the editors' fields. Units
// that are part of the route's data (a speed post's km/h or mph, the
// milepost units) are the designer's and are not converted for showing.
namespace Units {

// What a value measures, and its unit inside TSRE.
enum class Quantity {
    // A file's value its reader does not name: its unit tells (fromFile).
    Any,
    Length,             // m
    Distance,           // m, shown in km or miles
    Speed,              // m/s
    Mass,               // kg
    Force,              // N
    Power,              // W
    Pressure,           // Pa
    Temperature,        // °C
    Angle,              // rad, shown in degrees
    Time,               // s
    Current,            // A
    Voltage,            // V
    Frequency,          // Hz
    Area,               // m²
    Volume,             // m³
    Resistance,         // N/(m/s), Davis B
    ResistanceSquared,  // N/(m/s)², Davis C
    Stiffness,          // N/m
    Energy,             // J
    EnergyDensity,      // J/kg
    MassRate,           // kg/s
    PressureRate,       // Pa/s
    ForceRate,          // N/s
    PowerRate,          // W/s
};

// The units the editors show (the setting core.interface.units).
enum class System { Metric, ImperialUS, ImperialUK };

// A value read from a file: `number` written with `unit` (as in the file,
// any case; empty for none) to SI. A value without a unit is in
// `defaultUnit`, the parameter's own (MSTS's default units are mostly SI;
// some are not, as psi or mph): empty for SI. With Quantity::Any the unit
// alone decides, as MSTS's units are read without a parameter: m is metres
// (not minutes), lb pounds of mass (not force), in inches. False for a unit
// the quantity does not take; `si` is then the number unchanged.
bool fromFile(double number, QStringView unit, Quantity quantity, double &si,
              QStringView defaultUnit = {});
// The other way, for writing a file: SI to `unit` (one fromFile takes for
// the quantity; empty for SI). False for a unit the quantity does not take.
bool toFile(double si, QStringView unit, Quantity quantity, double &number);
// The quantity a unit belongs to as Quantity::Any reads it; Any for none.
Quantity quantityOf(QStringView unit);

// The setting's choices: the system locale's units, or one system.
enum class Choice { Automatic = 0, Metric = 1, ImperialUS = 2, ImperialUK = 3 };
// The units shown now (the setting, cached).
System system();
// For tests, and when the setting changes.
void setChoice(Choice choice);
Choice choice();
// The system the locale measures in (QLocale::measurementSystem()).
System localeSystem();

// The unit a quantity is shown in, in `system` (the current one by default).
QString unit(Quantity quantity);
QString unit(Quantity quantity, System system);
// SI to the shown unit and back.
double toShown(double si, Quantity quantity);
double fromShown(double shown, Quantity quantity);
// "72 km/h": the value in the shown unit, with `decimals` after the point
// (-1: the quantity's usual number), trailing zeros dropped.
QString format(double si, Quantity quantity, int decimals = -1);
// The number alone, as format gives it.
QString formatNumber(double si, Quantity quantity, int decimals = -1);
// Text typed into a field: a number, with a unit or without one (then in
// the shown unit). False for text that is no number or a unit the quantity
// does not take.
bool parse(const QString &text, Quantity quantity, double &si);

// Tells when the units shown change, so fields show their values again.
// Made on first use, on the main thread (the editors' fields).
class Notifier : public QObject {
    Q_OBJECT
public:
    static Notifier *instance();
signals:
    void changed();
private:
    Notifier();
};

}
