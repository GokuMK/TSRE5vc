#pragma once
#include <QString>
#include <QStringView>
#include <tsre/units/Quantity.h>

// Task core si-units: values between SI, the units of MSTS's files and the
// units the editors show, for a system given (no settings: Units.h has the
// user's). Qt Core only, so every reader of files can have it.
namespace Units {

// The units the editors show (the setting core.interface.units, Units.h).
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

// The unit a quantity is shown in, in `system`.
QString unit(Quantity quantity, System system);
// SI to the unit shown in `system` and back.
double toShown(double si, Quantity quantity, System system);
double fromShown(double shown, Quantity quantity, System system);
// "72 km/h": the value in the shown unit, with `decimals` after the point
// (-1: the quantity's usual number), trailing zeros dropped.
QString format(double si, Quantity quantity, System system, int decimals = -1);
// The number alone, as format gives it.
QString formatNumber(double si, Quantity quantity, System system, int decimals = -1);
// Text typed into a field: a number, with a unit or without one (then in
// `system`'s unit). False for text that is no number or a unit the
// quantity does not take.
bool parse(const QString &text, Quantity quantity, System system, double &si);

}
