#include <tsre/units/Units.h>

#include <QLocale>
#include <atomic>
#include <settings/SettingsAccess.h>
#include <settings/SettingsManager.h>

namespace Units {

namespace {

std::atomic<int> chosen{int(Choice::Automatic)};

const char *const SettingKey = "core.interface.units";

Choice readChoice() {
    const int value = Settings::variant(SettingKey, SettingType::Enum).toInt();
    return value >= int(Choice::Automatic) && value <= int(Choice::ImperialUK) ? Choice(value) : Choice::Automatic;
}

}

System localeSystem() {
    switch (QLocale::system().measurementSystem()) {
    case QLocale::ImperialUSSystem: return System::ImperialUS;
    case QLocale::ImperialUKSystem: return System::ImperialUK;
    default: return System::Metric;
    }
}

System system() {
    switch (choice()) {
    case Choice::Metric: return System::Metric;
    case Choice::ImperialUS: return System::ImperialUS;
    case Choice::ImperialUK: return System::ImperialUK;
    case Choice::Automatic: break;
    }
    return localeSystem();
}

void setChoice(Choice value) {
    Notifier *notifier = Notifier::instance();
    if (chosen.exchange(int(value)) != int(value))
        emit notifier->changed();
}

Choice choice() {
    // The setting is read once and again when it changes (Notifier).
    Notifier::instance();
    return Choice(chosen.load(std::memory_order_relaxed));
}

QString unit(Quantity quantity) {
    return unit(quantity, system());
}

double toShown(double si, Quantity quantity) {
    return toShown(si, quantity, system());
}

double fromShown(double shown, Quantity quantity) {
    return fromShown(shown, quantity, system());
}

QString format(double si, Quantity quantity, int decimals) {
    return format(si, quantity, system(), decimals);
}

QString formatNumber(double si, Quantity quantity, int decimals) {
    return formatNumber(si, quantity, system(), decimals);
}

bool parse(const QString &text, Quantity quantity, double &si) {
    return parse(text, quantity, system(), si);
}

Notifier::Notifier() {
    QObject::connect(&SettingsManager::instance(), &SettingsManager::runtimeSettingsChanged, this,
                     [](const QStringList &keys) {
                         if (keys.isEmpty() || keys.contains(QLatin1String(SettingKey)))
                             setChoice(readChoice());
                     });
}

Notifier *Notifier::instance() {
    static Notifier *notifier = [] {
        Notifier *made = new Notifier();
        chosen.store(int(readChoice()));
        return made;
    }();
    return notifier;
}

}
