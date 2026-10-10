#include "EnvironmentWindowTestSuite.h"
#include "TokenTestSupport.h"
#include <QCheckBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QFile>
#include <QTemporaryDir>
#include <QToolButton>
#include <cmath>
#include <routeEditor/EnvironmentWindow.h>
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

    window.resetAll();
    bool sessionLeft = false;
    for (const QString &key : EnvironmentWindow::keys())
        sessionLeft = sessionLeft || settings.runtimeValueSource(key) == SettingsManager::ForcedSession;
    test.check(!sessionLeft && std::abs(settings.runtimeFloat(fog) - newFog) < 1e-6,
               "Reset all leaves the saved values");
    return test.finish();
}
