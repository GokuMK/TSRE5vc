#include <settings/SettingsValidator.h>

#include <settings/SettingsRegistry.h>

#include <QColor>
#include <QJsonArray>
#include <QSet>

namespace {
void addIssue(QVector<SettingsIssue> &issues, SettingsIssue::Severity severity,
              const QString &key, const QString &message) {
    SettingsIssue issue;
    issue.severity = severity;
    issue.key = key;
    issue.message = message;
    issues.append(issue);
}

bool enumContains(const QJsonObject &setting, const QJsonValue &value) {
    const QJsonArray options = setting.value("options").toArray();
    for (const QJsonValue &entry : options) {
        if (entry.toObject().value("value") == value)
            return true;
    }
    return false;
}
}

QVector<SettingsIssue> SettingsValidator::validateSetting(
        const QJsonObject &setting, const SettingsRegistry &registry) {
    QVector<SettingsIssue> issues;
    const QString key = setting.value("key").toString().trimmed();
    if (key.isEmpty()) {
        addIssue(issues, SettingsIssue::Error, QString(), "Setting key is missing or empty.");
        return issues;
    }
    const QString typeName = setting.value("type").toString();
    SettingType type;
    if (!settingTypeFromName(typeName, &type)) {
        addIssue(issues, SettingsIssue::Warning, key,
                 QString("Unknown setting type '%1'; value is preserved for raw editing.").arg(typeName));
        return issues;
    }
    const bool nullable = setting.value("nullable").toBool(false);
    const bool valueIsNull = setting.value("value").isNull();
    const bool defaultIsNull = setting.value("default").isNull();
    if (!setting.contains("value")
            || (!settingTypeAcceptsJson(type, setting.value("value"))
                && !(nullable && valueIsNull)))
        addIssue(issues, SettingsIssue::Error, key,
                 QString("Value does not match declared type '%1'.").arg(typeName));
    if (setting.contains("default")
            && !settingTypeAcceptsJson(type, setting.value("default"))
            && !(nullable && defaultIsNull))
        addIssue(issues, SettingsIssue::Error, key,
                 QString("Default does not match declared type '%1'.").arg(typeName));

    static const QSet<QString> applyModes{
        "dynamic", "routeReload", "rendererRestart", "applicationRestart",
        "nextAction", "restart" // accepted only as legacy Part 1 aliases
    };
    const QString apply = setting.value("apply").toString();
    if (!apply.isEmpty() && !applyModes.contains(apply))
        addIssue(issues, SettingsIssue::Warning, key,
                 QString("Unknown apply lifecycle '%1'.").arg(apply));

    const SettingsDefinition *registered = registry.definition(key);
    if (registered && registered->type != type) {
        addIssue(issues, SettingsIssue::Error, key,
                 QString("Running build expects type '%1'.")
                 .arg(settingTypeName(registered->type)));
    } else if (registered) {
        if (valueIsNull && !registered->nullable)
            addIssue(issues, SettingsIssue::Error, key,
                     "Running build does not allow a null value.");
        if (registered->hasRange && setting.value("value").isDouble()) {
            const double value = setting.value("value").toDouble();
            if (value < registered->minimum || value > registered->maximum)
                addIssue(issues, SettingsIssue::Error, key,
                         "Value is outside the range supported by this build.");
        }
        if (registered->type == SettingType::Enum && !valueIsNull) {
            bool found = false;
            const QVariant candidate = setting.value("value").toVariant();
            for (const SettingOption &option : registered->options) {
                if (option.value == candidate) { found = true; break; }
            }
            if (!found)
                addIssue(issues, SettingsIssue::Error, key,
                         "Enum value is not supported by this build.");
        }
    }

    if ((type == SettingType::Int || type == SettingType::Float)
            && setting.value("value").isDouble() && setting.value("range").isObject()) {
        const QJsonObject range = setting.value("range").toObject();
        const double value = setting.value("value").toDouble();
        if (range.contains("minimum") && value < range.value("minimum").toDouble())
            addIssue(issues, SettingsIssue::Error, key, "Value is below the declared minimum.");
        if (range.contains("maximum") && value > range.value("maximum").toDouble())
            addIssue(issues, SettingsIssue::Error, key, "Value is above the declared maximum.");
        if (range.contains("minimum") && range.contains("maximum")
                && range.value("minimum").toDouble() > range.value("maximum").toDouble())
            addIssue(issues, SettingsIssue::Error, key, "Range minimum exceeds maximum.");
    }
    if (type == SettingType::Enum) {
        if (!setting.value("options").isArray() || setting.value("options").toArray().isEmpty())
            addIssue(issues, SettingsIssue::Error, key, "Enum has no options.");
        else if (!enumContains(setting, setting.value("value")))
            addIssue(issues, SettingsIssue::Error, key, "Enum value is not present in options.");
    }
    if (type == SettingType::Color && !valueIsNull && setting.value("value").isString()
            && !QColor(setting.value("value").toString()).isValid())
        addIssue(issues, SettingsIssue::Error, key, "Colour value is invalid.");

    return issues;
}

QVector<SettingsIssue> SettingsValidator::validateDocument(
        const QJsonObject &document, const SettingsRegistry &registry) {
    QVector<SettingsIssue> issues;
    if (document.value("format").toString() != "tsre-settings")
        addIssue(issues, SettingsIssue::Error, QString(),
                 "Document format must be 'tsre-settings'.");
    if (!document.value("schemaVersion").isDouble()
            || document.value("schemaVersion").toInt() != 1)
        addIssue(issues, SettingsIssue::Error, QString(),
                 "Unsupported or missing settings schemaVersion.");
    if (!document.value("groups").isArray())
        addIssue(issues, SettingsIssue::Error, QString(), "Document groups must be an array.");
    if (!document.value("settings").isArray()) {
        addIssue(issues, SettingsIssue::Error, QString(), "Document settings must be an array.");
        return issues;
    }

    QHash<QString, QSet<QString> > subgroupIds;
    QSet<QString> groupIds;
    for (const QJsonValue &entry : document.value("groups").toArray()) {
        if (!entry.isObject()) {
            addIssue(issues, SettingsIssue::Error, QString(),
                     "Groups array contains a non-object entry.");
            continue;
        }
        const QJsonObject group = entry.toObject();
        const QString groupId = group.value("id").toString().trimmed();
        if (groupId.isEmpty()) {
            addIssue(issues, SettingsIssue::Error, QString(), "Settings group ID is empty.");
            continue;
        }
        if (groupIds.contains(groupId))
            addIssue(issues, SettingsIssue::Error, QString(),
                     QString("Duplicate settings group '%1'.").arg(groupId));
        groupIds.insert(groupId);
        for (const QJsonValue &subgroupEntry : group.value("subgroups").toArray()) {
            const QString subgroupId = subgroupEntry.toObject().value("id").toString().trimmed();
            if (subgroupId.isEmpty()) {
                addIssue(issues, SettingsIssue::Error, QString(),
                         QString("Group '%1' contains an empty subgroup ID.").arg(groupId));
            } else if (subgroupIds[groupId].contains(subgroupId)) {
                addIssue(issues, SettingsIssue::Error, QString(),
                         QString("Group '%1' contains duplicate subgroup '%2'.")
                         .arg(groupId, subgroupId));
            } else {
                subgroupIds[groupId].insert(subgroupId);
            }
        }
    }

    QSet<QString> keys;
    for (const QJsonValue &entry : document.value("settings").toArray()) {
        if (!entry.isObject()) {
            addIssue(issues, SettingsIssue::Error, QString(), "Settings array contains a non-object entry.");
            continue;
        }
        const QString key = entry.toObject().value("key").toString();
        if (!key.isEmpty() && keys.contains(key))
            addIssue(issues, SettingsIssue::Error, key, "Duplicate setting key.");
        keys.insert(key);
        const QJsonObject setting = entry.toObject();
        issues += validateSetting(setting, registry);
        const QString group = setting.value("group").toString();
        if (!groupIds.contains(group))
            addIssue(issues, SettingsIssue::Warning, key,
                     QString("Unknown settings group '%1'.").arg(group));
        const QString subgroup = setting.value("subgroup").toString();
        if (!subgroup.isEmpty()
                && !subgroupIds.value(group).contains(subgroup))
            addIssue(issues, SettingsIssue::Warning, key,
                     QString("Unknown subgroup '%1' in group '%2'.")
                     .arg(subgroup, group));
    }
    return issues;
}

bool SettingsValidator::hasErrors(const QVector<SettingsIssue> &issues) {
    for (const SettingsIssue &issue : issues) {
        if (issue.severity == SettingsIssue::Error)
            return true;
    }
    return false;
}
