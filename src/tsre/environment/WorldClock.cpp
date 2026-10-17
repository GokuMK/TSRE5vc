#include <tsre/environment/WorldClock.h>

#include <QDate>
#include <QDebug>
#include <algorithm>
#include <settings/SettingsAccess.h>
#include <settings/SettingsManager.h>
#include <tsre/Game.h>

namespace {

const char *const TimeKey = "core.rendering.timeOfDay.time";
const char *const DateKey = "core.rendering.timeOfDay.date";
const char *const RunningKey = "core.rendering.timeOfDay.running";

void setSession(const char *key, const QVariant &value) {
    QString error;
    if (!SettingsManager::instance().setSessionValue(key, value, &error))
        qWarning() << "World clock:" << error;
}

}

WorldClock *WorldClock::instance() {
    static WorldClock *clock = new WorldClock();
    return clock;
}

WorldClock::WorldClock() {
    runs = Settings::boolean(RunningKey);
    clockHours = Game::timeOfDayHours;
    connect(&SettingsManager::instance(), &SettingsManager::runtimeSettingsChanged, this,
            [this](const QStringList &keys) {
        if (!keys.contains(QString::fromLatin1(RunningKey)))
            return;
        const bool now = Settings::boolean(RunningKey);
        if (now == runs)
            return;
        runs = now;
        if (provider == nullptr)
            emit changed();
    });
}

void WorldClock::setSource(Source *source) {
    if (source == provider)
        return;
    if (provider != nullptr && source == nullptr) {
        // On from the source's time, for the panel and the profile too, at
        // its speed (not as fast as possible).
        applySource();
        if (provider->clockSpeed() > 0.0)
            pace = provider->clockSpeed();
        clockHours = Game::timeOfDayHours;
        if (!Game::timeOfDayDate.isEmpty())
            setSession(DateKey, Game::timeOfDayDate);
        setSession(TimeKey, clockHours);
    }
    provider = source;
    if (provider != nullptr)
        applySource();
    emit changed();
}

void WorldClock::applySource() {
    double hours = 0.0, rate = 0.0;
    QString date;
    provider->clockNow(hours, date, rate);
    clockHours = hours;
    Game::timeOfDayHours = float(hours);
    if (!date.isEmpty() && date != Game::timeOfDayDate) {
        Game::timeOfDayDate = date;
        setSession(DateKey, date);
    }
}

double WorldClock::hours() const {
    if (provider != nullptr) {
        double hours = 0.0, rate = 0.0;
        QString date;
        provider->clockNow(hours, date, rate);
        return hours;
    }
    return Game::timeOfDayHours;
}

QString WorldClock::date() const {
    if (provider != nullptr) {
        double hours = 0.0, rate = 0.0;
        QString date;
        provider->clockNow(hours, date, rate);
        return date;
    }
    return Game::timeOfDayDate;
}

bool WorldClock::running() const {
    return provider != nullptr ? provider->clockRunning() : runs;
}

double WorldClock::speed() const {
    return provider != nullptr ? provider->clockSpeed() : pace;
}

double WorldClock::rate() const {
    if (provider != nullptr) {
        double hours = 0.0, rate = 0.0;
        QString date;
        provider->clockNow(hours, date, rate);
        return rate;
    }
    return runs ? pace : 0.0;
}

void WorldClock::setRunning(bool on) {
    if (provider != nullptr) {
        provider->setClockRunning(on);
        return;
    }
    // Stopped, the time stays where it is, also for the panel and the profile.
    if (!on)
        setSession(TimeKey, double(Game::timeOfDayHours));
    setSession(RunningKey, on);
    runs = on;
    emit changed();
}

void WorldClock::setSpeed(double secondsPerSecond) {
    if (provider != nullptr) {
        provider->setClockSpeed(secondsPerSecond);
        return;
    }
    pace = std::max(0.0, secondsPerSecond);
    emit changed();
}

void WorldClock::setTime(double hours) {
    if (provider != nullptr) {
        provider->setClockTime(hours);
        return;
    }
    clockHours = hours;
    Game::timeOfDayHours = float(hours);
    setSession(TimeKey, hours);
}

void WorldClock::setDate(const QString &date) {
    if (provider != nullptr) {
        provider->setClockDate(date);
        return;
    }
    Game::timeOfDayDate = date;
    setSession(DateKey, date);
}

void WorldClock::tick(double seconds) {
    if (provider != nullptr) {
        applySource();
        return;
    }
    if (!runs || seconds <= 0.0)
        return;
    // Set elsewhere (a panel, the settings), it goes on from there; the
    // settings hear of it only past midnight.
    if (float(clockHours) != Game::timeOfDayHours)
        clockHours = Game::timeOfDayHours;
    QString date = Game::timeOfDayDate;
    if (advance(seconds, pace, clockHours, date)) {
        setSession(DateKey, date);
        setSession(TimeKey, clockHours);
    }
    Game::timeOfDayHours = float(clockHours);
}

bool WorldClock::advance(double seconds, double speed, double &hours, QString &date) {
    hours += std::clamp(seconds, 0.0, 1.0) * speed / 3600.0;
    if (hours < 24.0)
        return false;
    hours -= 24.0;
    QDate day = QDate::fromString(date.trimmed(), "yyyy-MM-dd");
    if (!day.isValid())
        day = QDate(2026, 6, 21);
    date = day.addDays(1).toString("yyyy-MM-dd");
    return true;
}

void WorldClock::sourceChanged() {
    if (provider != nullptr)
        emit changed();
}
