#pragma once
#include <QObject>
#include <QString>

// The world's clock: the time of day and date that the sun, sky, lights,
// weather and fog show (Game::timeOfDayHours and Game::timeOfDayDate).
// One clock, whoever runs it: while a source provides it (a simulation, on
// its own thread) the clock is the source's; otherwise the main thread runs
// it on, at its speed, while core.rendering.timeOfDay.running is on. The
// environment window and the activity panel drive it whichever runs it
// (task simulation 01).
class WorldClock : public QObject {
    Q_OBJECT
public:
    // A clock running elsewhere.
    class Source {
    public:
        virtual ~Source() = default;
        // Its time of day (hours), date ("yyyy-MM-dd") and how fast it runs
        // now (0 stopped).
        virtual void clockNow(double &hours, QString &date, double &rate) const = 0;
        virtual bool clockRunning() const = 0;
        // Seconds of the day a second asked for; 0 as fast as possible.
        virtual double clockSpeed() const = 0;
        virtual void setClockRunning(bool on) = 0;
        virtual void setClockSpeed(double secondsPerSecond) = 0;
        // The time of day on the date shown.
        virtual void setClockTime(double hours) = 0;
        virtual void setClockDate(const QString &date) = 0;
    };

    static WorldClock *instance();

    // The source while one exists. When it goes, the clock goes on from its
    // time (the session's time and date set to it).
    void setSource(Source *source);
    Source *source() const { return provider; }

    double hours() const;
    QString date() const;
    bool running() const;
    double speed() const;
    // How fast it runs now: 0 stopped.
    double rate() const;
    void setRunning(bool on);
    void setSpeed(double secondsPerSecond);
    void setTime(double hours);
    void setDate(const QString &date);

    // The view's frame loop, `seconds` of wall time on (0 holds the main
    // thread's clock): Game's time and date as the clock has them.
    void tick(double seconds);

    // The main thread's clock: `seconds` of wall time (at most 1, after a
    // stall it goes on from where it was) at `speed`; true when it passed
    // midnight (the date moved on a day).
    static bool advance(double seconds, double speed, double &hours, QString &date);

public slots:
    // The source's state changed (running, speed, a time set).
    void sourceChanged();

signals:
    // Running, speed, source or a time set changed (not each tick).
    void changed();

private:
    WorldClock();
    Source *provider = nullptr;
    bool runs = true;
    double pace = 1.0;
    double clockHours = 0.0;
    // Game's time and date from the source.
    void applySource();
};
