/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/ui/UiDocumentFunctions.h>

#include <QDate>
#include <QLocale>
#include <QTime>
#include <RmlUi/Core.h>
#include <cmath>
#include <string>

namespace Ui {

namespace {

constexpr int Day = 86400;

QString text(const Rml::Variant &value) {
    return QString::fromStdString(value.Get<Rml::String>()).trimmed();
}

Rml::Variant textVariant(const QString &value) {
    return Rml::Variant(value.toStdString());
}

// No value, or empty text: a field the train has not.
bool isEmpty(const Rml::Variant &value) {
    return value.GetType() == Rml::Variant::NONE || (value.GetType() == Rml::Variant::STRING && text(value).isEmpty());
}

// Empty text and a warning, the document's message for its author.
Rml::Variant wrong(const char *function, const QString &why) {
    Rml::Log::Message(Rml::Log::LT_WARNING, "%s: %s", function, why.toStdString().c_str());
    return Rml::Variant(Rml::String());
}

// A time: "H:mm" or "H:mm:ss" text, or seconds after midnight; -1 none.
// `withSeconds`: its text had them.
int secondsOf(const Rml::Variant &value, bool *withSeconds = nullptr) {
    if (withSeconds != nullptr)
        *withSeconds = false;
    if (value.GetType() == Rml::Variant::STRING) {
        const QString t = text(value);
        QTime time = QTime::fromString(t, "H:mm:ss");
        if (time.isValid()) {
            if (withSeconds != nullptr)
                *withSeconds = true;
        } else {
            time = QTime::fromString(t, "H:mm");
        }
        return time.isValid() ? time.msecsSinceStartOfDay() / 1000 : -1;
    }
    double number = 0.0;
    if (value.GetType() == Rml::Variant::NONE || value.GetType() == Rml::Variant::BOOL || !value.GetInto(number))
        return -1;
    return ((int(std::floor(number)) % Day) + Day) % Day;
}

QString timeText(int seconds, bool withSeconds) {
    return QTime::fromMSecsSinceStartOfDay(seconds * 1000).toString(withSeconds ? "HH:mm:ss" : "HH:mm");
}

}

void registerDocumentFunctions(Rml::DataModelConstructor &constructor) {
    // A time in a Qt time pattern: {{ row.time | format_time('h:mm AP') }}.
    constructor.RegisterTransformFunc("format_time", [](const Rml::VariantList &a) -> Rml::Variant {
        if (a.size() != 2)
            return wrong("format_time", "a time and a pattern, as row.time | format_time('h:mm AP')");
        if (isEmpty(a[0]))
            return Rml::Variant(Rml::String());
        const int seconds = secondsOf(a[0]);
        if (seconds < 0)
            return wrong("format_time", "not a time: " + text(a[0]));
        return textVariant(QTime::fromMSecsSinceStartOfDay(seconds * 1000).toString(text(a[1])));
    });
    // The date (yyyy-MM-dd) in a Qt date pattern, names of days and months
    // in a language (English without one): {{ date | format_date('dddd', 'pl') }}.
    constructor.RegisterTransformFunc("format_date", [](const Rml::VariantList &a) -> Rml::Variant {
        if (a.size() != 2 && a.size() != 3)
            return wrong("format_date", "a date, a pattern and perhaps a language, as date | format_date('dd.MM.yyyy')");
        if (isEmpty(a[0]))
            return Rml::Variant(Rml::String());
        const QDate date = QDate::fromString(text(a[0]), Qt::ISODate);
        if (!date.isValid())
            return wrong("format_date", "not a date (yyyy-MM-dd): " + text(a[0]));
        const QLocale locale = a.size() == 3 ? QLocale(text(a[2])) : QLocale::c();
        return textVariant(locale.toString(date, text(a[1])));
    });
    // Filled on the left to a width ('0' by default): {{ day | pad(2) }}.
    constructor.RegisterTransformFunc("pad", [](const Rml::VariantList &a) -> Rml::Variant {
        int width = 0;
        if ((a.size() != 2 && a.size() != 3) || !a[1].GetInto(width) || width < 0)
            return wrong("pad", "a value and a width, as day | pad(2)");
        if (isEmpty(a[0]))
            return Rml::Variant(Rml::String());
        const QString fill = a.size() == 3 ? text(a[2]) : QString("0");
        return textVariant(text(a[0]).rightJustified(width, fill.isEmpty() ? QChar(' ') : fill.front()));
    });
    // Its own text where it has one, else the default: {{ row.platform | default('-') }}.
    constructor.RegisterTransformFunc("default", [](const Rml::VariantList &a) -> Rml::Variant {
        if (a.size() != 2)
            return wrong("default", "a value and its default, as row.platform | default('-')");
        return isEmpty(a[0]) ? a[1] : a[0];
    });
    // Words separated by spaces hold a word (task scripting 03: a car's
    // tags): {{ car.tags | has('bike') }}, a data-if's condition.
    constructor.RegisterTransformFunc("has", [](const Rml::VariantList &a) -> Rml::Variant {
        if (a.size() != 2)
            return wrong("has", "words and a word, as car.tags | has('bike')");
        return Rml::Variant(text(a[0]).split(' ', Qt::SkipEmptyParts).contains(text(a[1])));
    });
    // The time n minutes later (an empty n none), past midnight too:
    // {{ row.time | add_minutes(row.delay) }}.
    constructor.RegisterTransformFunc("add_minutes", [](const Rml::VariantList &a) -> Rml::Variant {
        if (a.size() != 2)
            return wrong("add_minutes", "a time and minutes, as row.time | add_minutes(row.delay)");
        if (isEmpty(a[0]))
            return Rml::Variant(Rml::String());
        bool withSeconds = false;
        const int seconds = secondsOf(a[0], &withSeconds);
        if (seconds < 0)
            return wrong("add_minutes", "not a time: " + text(a[0]));
        double minutes = 0.0;
        if (!isEmpty(a[1]) && !a[1].GetInto(minutes))
            return wrong("add_minutes", "not minutes: " + text(a[1]));
        const int later = seconds + int(std::lround(minutes * 60.0));
        return textVariant(timeText(((later % Day) + Day) % Day, withSeconds));
    });
    // Whole minutes from the clock (seconds after midnight) to a time
    // (negative once it passed; the nearer way round midnight):
    // {{ row.time | minutes_until(clock) }}.
    constructor.RegisterTransformFunc("minutes_until", [](const Rml::VariantList &a) -> Rml::Variant {
        if (a.size() != 2)
            return wrong("minutes_until", "a time and the clock, as row.time | minutes_until(clock)");
        if (isEmpty(a[0]))
            return Rml::Variant(Rml::String());
        const int seconds = secondsOf(a[0]);
        const int now = secondsOf(a[1]);
        if (seconds < 0 || now < 0)
            return wrong("minutes_until", "not a time: " + text(seconds < 0 ? a[0] : a[1]));
        int until = seconds - now;
        if (until > Day / 2)
            until -= Day;
        else if (until <= -Day / 2)
            until += Day;
        return Rml::Variant(int(std::floor(until / 60.0)));
    });
    // One of the values in turn by a number (the clock: each `period`
    // seconds): {{ clock | cycle(4, 'Odjazdy', 'Departures') }},
    // data-if="clock | cycle(4, true, false)".
    constructor.RegisterTransformFunc("cycle", [](const Rml::VariantList &a) -> Rml::Variant {
        double now = 0.0, period = 0.0;
        if (a.size() < 4 || a[0].GetType() == Rml::Variant::STRING || !a[0].GetInto(now) || !a[1].GetInto(period)
                || period <= 0.0)
            return wrong("cycle", "the clock, seconds each, then two values or more, as "
                                  "clock | cycle(4, 'Odjazdy', 'Departures')");
        const long long turns = (long long)(a.size() - 2);
        const long long turn = (long long)std::floor(now / period);
        return a[2 + size_t(((turn % turns) + turns) % turns)];
    });
}

}
