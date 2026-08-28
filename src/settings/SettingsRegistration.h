#ifndef TSRE_SETTINGSREGISTRATION_H
#define TSRE_SETTINGSREGISTRATION_H

#include <QString>
#include <functional>

class SettingsRegistry;

namespace SettingsRegistration {
using Provider = std::function<bool(SettingsRegistry &, QString *)>;

bool addProvider(const QString &id, const Provider &provider,
                 QString *error = nullptr);
bool registerAll(SettingsRegistry &registry, QString *error = nullptr);
}

#endif
