#include <tsre/tests/UnitsTestSuite.h>

#include <QObject>
#include <cmath>
#include <settings/SettingsManager.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/units/Units.h>

using Units::Quantity;
using Units::System;

namespace {

bool near(double a, double b, double tolerance = 1e-6) {
    return std::fabs(a - b) <= tolerance * std::max(1.0, std::fabs(b));
}

}

int TsreTests::runUnitsSuite(bool verbose) {
    TokenTest::Suite test{"[tests:units]", verbose};
    const Units::Choice before = Units::choice();
    SettingsManager &settings = SettingsManager::instance();
    const QVariant settingBefore = settings.value("core.interface.units");

    // Files' units to SI, the quantity named or not.
    struct Read {
        double number;
        const char *unit;
        Quantity quantity;
        double si;
    };
    const Read reads[] = {
        {100, "km/h", Quantity::Speed, 100 / 3.6},
        {100, "kmh", Quantity::Any, 100 / 3.6},
        {74, "mph", Quantity::Any, 74 * 0.44704},
        {74, "MPH", Quantity::Speed, 74 * 0.44704},
        {80, "t", Quantity::Any, 80000},
        {39.19, "t", Quantity::Mass, 39190},
        {50, "lb", Quantity::Any, 50 * 0.45359237},
        {50, "lb", Quantity::Force, 50 * 4.4482216152605},
        {300, "kN", Quantity::Any, 300000},
        {25000, "lbf", Quantity::Force, 25000 * 4.4482216152605},
        {4000, "hp", Quantity::Any, 4000 * 745.69987158227022},
        {2803.84, "kW", Quantity::Power, 2803840},
        {5, "psi", Quantity::Pressure, 5 * 6894.757293168361},
        {1, "bar", Quantity::Any, 100000},
        {68, "degF", Quantity::Temperature, 20},
        {30, "deg", Quantity::Angle, 30 * 3.14159265358979323846 / 180},
        {2, "m", Quantity::Time, 120},
        {2, "m", Quantity::Any, 2},
        {3, "in", Quantity::Any, 0.0762},
        {36, "in/2", Quantity::Length, 0.4572},
        {22, "N/m/s", Quantity::Resistance, 22},
        {0.5, "lbf/mph", Quantity::Resistance, 0.5 * 4.4482216152605 / 0.44704},
        {1, "lbf/mph^2", Quantity::ResistanceSquared, 4.4482216152605 / (0.44704 * 0.44704)},
        {2, "kV", Quantity::Voltage, 2000},
        {600, "rpm", Quantity::Frequency, 10},
        {10, "g-us", Quantity::Volume, 0.03785411784},
        {7, "", Quantity::Speed, 7},
    };
    bool allRead = true;
    QString wrong;
    for (const Read &read : reads) {
        double si = -1;
        const bool ok = Units::fromFile(read.number, QString::fromLatin1(read.unit), read.quantity, si);
        if (!ok || !near(si, read.si)) {
            allRead = false;
            wrong += QString(" %1%2=%3").arg(read.number).arg(read.unit).arg(si);
        }
    }
    test.check(allRead, "files' units to SI:" + wrong);

    double si = 0;
    test.check(Units::fromFile(45, QString(), Quantity::Speed, si, u"mph") && near(si, 45 * 0.44704)
                   && Units::fromFile(45, QString(), Quantity::Pressure, si, u"psi") && near(si, 45 * 6894.757293168361),
               "a value without a unit in its parameter's default unit");
    test.check(!Units::fromFile(12, u"xyz", Quantity::Speed, si) && si == 12
                   && !Units::fromFile(12, u"kg", Quantity::Speed, si) && si == 12,
               "a unit the quantity does not take: refused, the number kept");
    test.check(Units::quantityOf(u"mph") == Quantity::Speed && Units::quantityOf(u"m") == Quantity::Length
                   && Units::quantityOf(u"lb") == Quantity::Mass && Units::quantityOf(u"lbf") == Quantity::Force
                   && Units::quantityOf(u"degF") == Quantity::Temperature && Units::quantityOf(u"xyz") == Quantity::Any,
               "the quantity a unit reads as");

    // Back to the file's unit.
    bool back = true;
    for (const Read &read : reads) {
        double number = 0;
        if (!Units::toFile(read.si, QString::fromLatin1(read.unit), read.quantity, number) || !near(number, read.number))
            back = false;
    }
    test.check(back, "SI back to each file's unit");

    // The units shown, by system.
    Units::setChoice(Units::Choice::Metric);
    const bool metric = Units::system() == System::Metric && Units::format(100 / 3.6, Quantity::Speed) == "100 km/h"
            && Units::format(80000, Quantity::Mass) == "80 t" && Units::format(300000, Quantity::Force) == "300 kN"
            && Units::format(2803840, Quantity::Power) == "2804 kW" && Units::format(12.5, Quantity::Length) == "12.5 m"
            && Units::format(2500, Quantity::Distance) == "2.5 km" && Units::format(500000, Quantity::Pressure) == "5 bar"
            && Units::format(20, Quantity::Temperature) == QString::fromUtf8("20 °C")
            && Units::format(3.14159265358979323846 / 2, Quantity::Angle) == QString::fromUtf8("90°");
    test.check(metric, "metric: " + Units::format(2803840, Quantity::Power) + ", " + Units::format(12.5, Quantity::Length));
    Units::setChoice(Units::Choice::ImperialUS);
    const bool us = Units::system() == System::ImperialUS && Units::format(60 * 0.44704, Quantity::Speed) == "60 mph"
            && Units::format(907.18474 * 120, Quantity::Mass) == "120 ton"
            && Units::format(25000 * 4.4482216152605, Quantity::Force) == "25000 lbf"
            && Units::format(4000 * 745.69987158227022, Quantity::Power) == "4000 hp"
            && Units::format(0.3048 * 50, Quantity::Length) == "50 ft" && Units::format(1609.344 * 3, Quantity::Distance) == "3 mi"
            && Units::format(90 * 6894.757293168361, Quantity::Pressure) == "90 psi"
            && Units::format(20, Quantity::Temperature) == QString::fromUtf8("68 °F");
    test.check(us, "imperial US: " + Units::format(907.18474 * 120, Quantity::Mass) + ", "
                       + Units::format(20, Quantity::Temperature));
    Units::setChoice(Units::Choice::ImperialUK);
    test.check(Units::system() == System::ImperialUK && Units::format(1016.0469088 * 40, Quantity::Mass) == "40 ton"
                   && Units::format(20, Quantity::Temperature) == QString::fromUtf8("20 °C")
                   && Units::format(60 * 0.44704, Quantity::Speed) == "60 mph",
               "imperial UK: long tons, degrees C, mph");
    test.check(Units::unit(Quantity::Speed, System::Metric) == "km/h" && Units::unit(Quantity::Speed, System::ImperialUS) == "mph"
                   && Units::formatNumber(100 / 3.6, Quantity::Speed) == "62.1"
                   && Units::formatNumber(100 / 3.6, Quantity::Speed, 0) == "62",
               "a unit by system; the number alone, decimals asked for");

    // Typed values.
    Units::setChoice(Units::Choice::Metric);
    bool typed = Units::parse("72", Quantity::Speed, si) && near(si, 20);
    typed = typed && Units::parse(" 45 mph ", Quantity::Speed, si) && near(si, 45 * 0.44704);
    typed = typed && Units::parse("72,5", Quantity::Speed, si) && near(si, 72.5 / 3.6);
    typed = typed && Units::parse("90°", Quantity::Angle, si) && near(si, 3.14159265358979323846 / 2);
    typed = typed && Units::parse("68 °F", Quantity::Temperature, si) && near(si, 20);
    typed = typed && Units::parse("80t", Quantity::Mass, si) && near(si, 80000);
    Units::setChoice(Units::Choice::ImperialUS);
    typed = typed && Units::parse("1 ton", Quantity::Mass, si) && near(si, 907.18474);
    typed = typed && Units::parse("60", Quantity::Speed, si) && near(si, 60 * 0.44704);
    typed = typed && !Units::parse("abc", Quantity::Speed, si) && !Units::parse("10 kg", Quantity::Speed, si);
    test.check(typed, "typed values: the shown unit, other units, a comma, refused text");

    // Fields hear of a change; the setting chooses.
    int heard = 0;
    const QMetaObject::Connection hearing = QObject::connect(Units::Notifier::instance(), &Units::Notifier::changed,
                                                             [&heard] { ++heard; });
    Units::setChoice(Units::Choice::Metric);
    Units::setChoice(Units::Choice::Metric);
    Units::setChoice(Units::Choice::ImperialUK);
    QObject::disconnect(hearing);
    test.check(heard == 2, QString("a change told once (%1)").arg(heard));
    QString error;
    const bool set = settings.setSessionValue("core.interface.units", 2, &error);
    test.check(set && Units::choice() == Units::Choice::ImperialUS && Units::system() == System::ImperialUS,
               "the setting core.interface.units chooses the units " + error);
    settings.setSessionValue("core.interface.units", 0, &error);
    test.check(Units::choice() == Units::Choice::Automatic && Units::system() == Units::localeSystem(),
               "System / Automatic: the locale's units");

    if (settingBefore.isValid())
        settings.setSessionValue("core.interface.units", settingBefore, &error);
    Units::setChoice(before);
    return test.finish();
}
