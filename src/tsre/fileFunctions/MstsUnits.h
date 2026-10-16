#pragma once
#include <QString>

// Values with MSTS units ("80t", "300kN", "4000hp", "100mph", "22N/m/s") in
// SI units: kg, N, W, m/s, N/(m/s), N/(m/s)²; a value without a unit is in
// those already; quotes round a value and a comment after it ("#...") are
// dropped (task simulation 03).
namespace MstsUnits {

enum class Kind { Mass, Force, Power, Speed, Resistance, ResistanceSquared, None };

// The value of a token; `ok` false when it is no number or its unit is not
// one of this kind's.
double value(const QString &token, Kind kind, bool *ok = nullptr);

}
