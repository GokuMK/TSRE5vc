#include "EnvironmentWindowTestSuite.h"
#include "TokenTestSupport.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QFile>
#include <QTemporaryDir>
#include <QToolButton>
#include <cmath>
#include <routeEditor/EnvironmentWindow.h>
#include <routeEditor/RouteEditorGLWidget.h>
#include <settings/SettingsManager.h>
#include <tsre/Game.h>

namespace {
QByteArray contents(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

int TsreTests::runEnvironmentWindowSuite(const QString &imagePath, bool verbose) {
    TokenTest::Suite test{"[tests:environment-window]", verbose};
    SettingsManager &settings = SettingsManager::instance();
    const QString fog = "core.rendering.fogDensity", bloom = "core.rendering.bloom";
    const QString date = "core.rendering.timeOfDay.date", sunSize = "core.rendering.sky.sunSize";
    for (const QString &key : EnvironmentWindow::keys())
        settings.clearSessionValue(key);
    // Saving goes to a copy of the profile, never to the user's.
    QTemporaryDir temp;
    const QString profile = temp.filePath("settings.json");
    QString error;
    test.check(temp.isValid() && settings.saveAs(profile, &error), "profile copied to a temporary file " + error);
    if (settings.settingsFilePath() != QFileInfo(profile).absoluteFilePath())
        return test.finish();
    const QByteArray before = contents(profile);
    const double profileFog = settings.value(fog).toDouble();
    const double newFog = profileFog > 0.5 ? 0.2 : 0.8;

    EnvironmentWindow window(nullptr);
    if (!imagePath.isEmpty()) {
        window.adjustSize();
        test.check(window.grab().save(imagePath), "window image saved to " + imagePath);
    }
    auto *fogSpin = window.findChild<QDoubleSpinBox *>(fog);
    auto *bloomSpin = window.findChild<QDoubleSpinBox *>(bloom);
    auto *fogReset = window.findChild<QToolButton *>("reset:" + fog);
    test.check(fogSpin && bloomSpin && fogReset && window.findChild<QDateEdit *>(date)
                   && window.findChild<QCheckBox *>("core.rendering.sky.moon"),
               "controls named after their settings");
    if (!fogSpin || !bloomSpin || !fogReset)
        return test.finish();
    test.check(std::abs(fogSpin->value() - settings.runtimeFloat(fog)) < 1e-6 && !fogReset->isEnabled(),
               "shows the running values, nothing to reset");

    fogSpin->setValue(newFog);
    test.check(std::abs(settings.runtimeFloat(fog) - newFog) < 1e-6
                   && settings.runtimeValueSource(fog) == SettingsManager::ForcedSession,
               "a change is a session value");
    test.check(std::abs(Game::fogDensity - newFog) < 1e-6, "fog density applies at once");
    test.check(std::abs(settings.value(fog).toDouble() - profileFog) < 1e-9 && contents(profile) == before,
               "the profile is left alone");
    test.check(fogReset->isEnabled(), "a changed value can be reset");

    settings.setSessionValue(bloom, 1.5);
    test.check(std::abs(bloomSpin->value() - 1.5) < 1e-6, "changes made elsewhere show in the window");
    settings.clearSessionValue(bloom);

    fogReset->click();
    test.check(std::abs(settings.runtimeFloat(fog) - profileFog) < 1e-6
                   && settings.runtimeValueSource(fog) != SettingsManager::ForcedSession
                   && std::abs(fogSpin->value() - profileFog) < 1e-6,
               "Reset gives back the profile's value");

    fogSpin->setValue(newFog);
    window.findChild<QDateEdit *>(date)->setDate(QDate(2026, 12, 21));
    window.findChild<QDoubleSpinBox *>(sunSize)->setValue(1.7);
    test.check(window.saveToProfile(&error), "Save to profile " + error);
    test.check(std::abs(settings.value(fog).toDouble() - newFog) < 1e-6
                   && settings.value(date).toString() == "2026-12-21"
                   && std::abs(settings.value(sunSize).toDouble() - 1.7) < 1e-6,
               "the profile holds the saved values");
    test.check(contents(profile) != before && contents(profile).contains("2026-12-21"),
               "the profile file is written");
    test.check(settings.runtimeValueSource(fog) == SettingsManager::ProfileValue
                   && std::abs(settings.runtimeFloat(fog) - newFog) < 1e-6,
               "after saving the values come from the profile");

    // The clock: its speed from real time to 4 min a second, Stop keeping the
    // time; the editor's step runs the time on, the date past midnight.
    const QString time = "core.rendering.timeOfDay.time", running = "core.rendering.timeOfDay.running";
    auto *speeds = window.findChild<QComboBox *>("clockSpeed");
    auto *clockButton = window.findChild<QToolButton *>(running);
    test.check(speeds && clockButton && speeds->count() == 8 && std::abs(speeds->itemData(0).toDouble() - 1.0) < 1e-9
                   && std::abs(speeds->itemData(7).toDouble() - 240.0) < 1e-9
                   && speeds->styleSheet().contains("combobox-popup: 0"),
               "the clock's speeds, real time first, in TSRE's combo box style");
    if (!speeds || !clockButton)
        return test.finish();
    double told = 0.0;
    QObject::connect(&window, &EnvironmentWindow::clockSpeedChanged, [&told](double value) { told = value; });
    emit speeds->activated(7);
    test.check(std::abs(told - 240.0) < 1e-9 && settings.registry().definition("core.rendering.timeOfDay.speed") == nullptr,
               "a speed goes to the editor for the session, not into the settings");
    settings.setSessionValue(running, true);
    Game::timeOfDayHours = 15.5f;
    clockButton->click();
    test.check(!settings.runtimeBool(running) && std::abs(settings.runtimeFloat(time) - 15.5) < 1e-6
                   && clockButton->text() != QString(),
               "Stop keeps the time where it is");
    const QString stopped = clockButton->text();
    clockButton->click();
    test.check(settings.runtimeBool(running) && clockButton->text() != stopped, "Resume runs it on");
    double hours = 23.96;
    QString day = "2026-06-21";
    const bool nextDay = RouteEditorGLWidget::advanceClock(0.75, 240.0, hours, day);
    test.check(nextDay && std::abs(hours - 0.01) < 1e-9 && day == "2026-06-22",
               QString("4 minutes a second run into the next day (%1 h, %2)").arg(hours).arg(day));
    RouteEditorGLWidget::advanceClock(30.0, 240.0, hours, day);
    test.check(std::abs(hours - (0.01 + 4.0 / 60.0)) < 1e-9, "after a stall the clock goes on from where it was");

    window.resetAll();
    bool sessionLeft = false;
    for (const QString &key : EnvironmentWindow::keys())
        sessionLeft = sessionLeft || settings.runtimeValueSource(key) == SettingsManager::ForcedSession;
    test.check(!sessionLeft && std::abs(settings.runtimeFloat(fog) - newFog) < 1e-6,
               "Reset all leaves the saved values");
    return test.finish();
}
