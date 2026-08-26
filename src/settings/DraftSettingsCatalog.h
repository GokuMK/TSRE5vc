#ifndef TSRE_DRAFTSETTINGSCATALOG_H
#define TSRE_DRAFTSETTINGSCATALOG_H

#include <QString>

class SettingsRegistry;

namespace DraftSettingsCatalog {
bool registerDefinitions(SettingsRegistry &registry, QString *error = nullptr);
}

#endif
