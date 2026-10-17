#pragma once
#include <QObject>
#include <QString>
#include <tsre/units/UnitConversion.h>

// Physical values (task core si-units): SI inside TSRE, the units of MSTS's
// files when reading them (UnitConversion.h), the user's units in the
// editors' fields (here). Units that are part of the route's data (a speed
// post's km/h or mph, the milepost units) are the designer's and are not
// converted for showing.
namespace Units {

// The setting's choices: the system locale's units, or one system.
enum class Choice { Automatic = 0, Metric = 1, ImperialUS = 2, ImperialUK = 3 };
// The units shown now (the setting, cached).
System system();
// For tests, and when the setting changes.
void setChoice(Choice choice);
Choice choice();
// The system the locale measures in (QLocale::measurementSystem()).
System localeSystem();

// As UnitConversion.h's, in the units shown now.
QString unit(Quantity quantity);
double toShown(double si, Quantity quantity);
double fromShown(double shown, Quantity quantity);
QString format(double si, Quantity quantity, int decimals = -1);
QString formatNumber(double si, Quantity quantity, int decimals = -1);
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
