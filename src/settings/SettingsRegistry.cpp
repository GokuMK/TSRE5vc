#include <settings/SettingsRegistry.h>

#include <QSet>

bool SettingsRegistry::defineGroup(const SettingsGroupDefinition &definition,
                                   QString *error) {
    if (definition.id.trimmed().isEmpty()) {
        if (error) *error = "Settings group ID cannot be empty.";
        return false;
    }
    if (m_groupIndex.contains(definition.id)) {
        if (error) *error = QString("Duplicate settings group: %1").arg(definition.id);
        return false;
    }
    QSet<QString> subgroupIds;
    for (const SettingsSubgroupDefinition &subgroup : definition.subgroups) {
        if (subgroup.id.trimmed().isEmpty()) {
            if (error) *error = QString("Settings group '%1' has an empty subgroup ID.")
                    .arg(definition.id);
            return false;
        }
        if (subgroupIds.contains(subgroup.id)) {
            if (error) *error = QString("Duplicate subgroup '%1' in settings group '%2'.")
                    .arg(subgroup.id, definition.id);
            return false;
        }
        subgroupIds.insert(subgroup.id);
    }
    m_groupIndex.insert(definition.id, m_groups.size());
    m_groups.append(definition);
    return true;
}

bool SettingsRegistry::define(const SettingsDefinition &definition, QString *error) {
    if (definition.key.trimmed().isEmpty()) {
        if (error) *error = "Setting key cannot be empty.";
        return false;
    }
    if (m_definitionIndex.contains(definition.key)) {
        if (error) *error = QString("Duplicate setting definition: %1").arg(definition.key);
        return false;
    }
    const auto groupIt = m_groupIndex.constFind(definition.group);
    if (groupIt == m_groupIndex.constEnd()) {
        if (error) *error = QString("Unknown group '%1' for setting '%2'.")
                .arg(definition.group, definition.key);
        return false;
    }
    if (!definition.subgroup.isEmpty()) {
        bool found = false;
        for (const SettingsSubgroupDefinition &subgroup : m_groups.at(groupIt.value()).subgroups) {
            if (subgroup.id == definition.subgroup) {
                found = true;
                break;
            }
        }
        if (!found) {
            if (error) *error = QString("Unknown subgroup '%1' for setting '%2'.")
                    .arg(definition.subgroup, definition.key);
            return false;
        }
    }
    m_definitionIndex.insert(definition.key, m_definitions.size());
    m_definitions.append(definition);
    return true;
}

void SettingsRegistry::setSupported(const QString &key, SettingType type) {
    if (!key.trimmed().isEmpty())
        m_supportedTypes.insert(key, type);
}

bool SettingsRegistry::supportedType(const QString &key, SettingType *type) const {
    const auto it = m_supportedTypes.constFind(key);
    if (it == m_supportedTypes.constEnd())
        return false;
    if (type) *type = it.value();
    return true;
}

bool SettingsRegistry::contains(const QString &key) const {
    return m_definitionIndex.contains(key);
}

const SettingsDefinition *SettingsRegistry::definition(const QString &key) const {
    const auto it = m_definitionIndex.constFind(key);
    return it == m_definitionIndex.constEnd() ? nullptr : &m_definitions[it.value()];
}

QVector<SettingsDefinition> SettingsRegistry::definitions() const { return m_definitions; }
QVector<SettingsGroupDefinition> SettingsRegistry::groups() const { return m_groups; }
bool SettingsRegistry::isEmpty() const { return m_definitions.isEmpty(); }
