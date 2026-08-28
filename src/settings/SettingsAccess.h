#ifndef TSRE_SETTINGSACCESS_H
#define TSRE_SETTINGSACCESS_H

#include <settings/SettingsManager.h>

namespace Settings {
inline bool boolean(const char *key) {
    SettingsManager &manager = SettingsManager::instance();
    manager.setSupported(QString::fromLatin1(key), SettingType::Bool);
    return manager.runtimeBool(QString::fromLatin1(key));
}

inline int integer(const char *key) {
    SettingsManager &manager = SettingsManager::instance();
    manager.setSupported(QString::fromLatin1(key), SettingType::Int);
    return manager.runtimeInt(QString::fromLatin1(key));
}

inline double floating(const char *key) {
    SettingsManager &manager = SettingsManager::instance();
    manager.setSupported(QString::fromLatin1(key), SettingType::Float);
    return manager.runtimeFloat(QString::fromLatin1(key));
}

inline QString string(const char *key, SettingType type = SettingType::String) {
    SettingsManager &manager = SettingsManager::instance();
    manager.setSupported(QString::fromLatin1(key), type);
    return manager.runtimeString(QString::fromLatin1(key));
}

inline QVariant variant(const char *key, SettingType type) {
    SettingsManager &manager = SettingsManager::instance();
    const QString settingKey = QString::fromLatin1(key);
    manager.setSupported(settingKey, type);
    return manager.runtimeValue(settingKey);
}

inline QStringList stringList(const char *key) {
    SettingsManager &manager = SettingsManager::instance();
    manager.setSupported(QString::fromLatin1(key), SettingType::StringList);
    return manager.runtimeStringList(QString::fromLatin1(key));
}

inline int enumIndex(const char *key) {
    SettingsManager &manager = SettingsManager::instance();
    const QString settingKey = QString::fromLatin1(key);
    manager.setSupported(settingKey, SettingType::Enum);
    const SettingsDefinition *definition = manager.registry().definition(settingKey);
    if (!definition)
        return 0;
    const QVariant value = manager.runtimeValue(settingKey);
    for (int i = 0; i < definition->options.size(); ++i) {
        if (definition->options.at(i).value == value)
            return i;
    }
    return 0;
}
}

#endif
