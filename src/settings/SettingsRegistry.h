#ifndef TSRE_SETTINGSREGISTRY_H
#define TSRE_SETTINGSREGISTRY_H

#include <settings/SettingsTypes.h>

#include <QHash>

class SettingsRegistry {
public:
    bool defineGroup(const SettingsGroupDefinition &definition, QString *error = nullptr);
    bool define(const SettingsDefinition &definition, QString *error = nullptr);
    void setSupported(const QString &key, SettingType type);
    bool supportedType(const QString &key, SettingType *type = nullptr) const;

    bool contains(const QString &key) const;
    const SettingsDefinition *definition(const QString &key) const;
    QVector<SettingsDefinition> definitions() const;
    QVector<SettingsGroupDefinition> groups() const;
    bool isEmpty() const;

private:
    QVector<SettingsDefinition> m_definitions;
    QVector<SettingsGroupDefinition> m_groups;
    QHash<QString, int> m_definitionIndex;
    QHash<QString, int> m_groupIndex;
    QHash<QString, SettingType> m_supportedTypes;
};

#endif
