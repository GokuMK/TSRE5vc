#ifndef TSRE_SETTINGSTYPES_H
#define TSRE_SETTINGSTYPES_H

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <functional>

enum class SettingType {
    Bool,
    Int,
    Float,
    String,
    MultilineString,
    Color,
    Enum,
    Path,
    Directory,
    KeySequence,
    StringList,
    Secret
};

QString settingTypeName(SettingType type);
bool settingTypeFromName(const QString &name, SettingType *type);
bool settingTypeAcceptsJson(SettingType type, const QJsonValue &value);
QJsonValue settingJsonFromVariant(const QVariant &value, SettingType type);
QVariant settingVariantFromJson(const QJsonValue &value, SettingType type);

struct SettingOption {
    QVariant value;
    QString nameId;
    QString label; // Plain display name for runtime catalogue entries.
    QString displayName() const;
};

struct SettingsSubgroupDefinition {
    QString id;
    QString nameId;
    QString descriptionId;
    int order = 0;

    QJsonObject toJson() const;
};

struct SettingsGroupDefinition {
    QString id;
    QString nameId;
    QString descriptionId;
    int order = 0;
    QVector<SettingsSubgroupDefinition> subgroups;

    QJsonObject toJson() const;
};

struct SettingsDefinition {
    QString key;
    SettingType type = SettingType::String;
    QVariant defaultValue;
    QString nameId;
    QString descriptionId;
    QString group;
    QString subgroup;
    QString unit;
    int order = 0;
    QString apply = "applicationRestart";
    bool advanced = false;
    bool nullable = false;
    bool hasRange = false;
    double minimum = 0.0;
    double maximum = 0.0;
    double step = 0.0;
    QVector<SettingOption> options;
    // Runtime metadata only: neither callbacks nor their choices go into profiles.
    std::function<QVector<SettingOption>()> optionsProvider;
    bool allowUnknownOptions = false; // String references may outlive catalogue entries.
    QVector<SettingOption> resolvedOptions() const;
    bool acceptsOption(const QVariant &value) const;

    // Transitional audit/integration metadata used by Phase 0 and Part 2.
    QStringList legacyFileKeys;
    QStringList legacyCodeSymbols;
    QString implementationOwner;
    bool requiresRuntimeCache = false;
    QString implementationAccess = "review";

    static SettingsDefinition boolean(const QString &key, bool defaultValue);
    static SettingsDefinition integer(const QString &key, int defaultValue);
    static SettingsDefinition floating(const QString &key, double defaultValue);
    static SettingsDefinition string(const QString &key, const QString &defaultValue,
                                     SettingType type = SettingType::String);
    static SettingsDefinition stringList(const QString &key,
                                         const QStringList &defaultValue = QStringList());
    static SettingsDefinition enumeration(const QString &key, const QVariant &defaultValue);

    SettingsDefinition &withNameId(const QString &value);
    SettingsDefinition &withDescriptionId(const QString &value);
    SettingsDefinition &inGroup(const QString &value, int valueOrder = 0);
    SettingsDefinition &inSubgroup(const QString &value);
    SettingsDefinition &withUnit(const QString &value);
    SettingsDefinition &withRange(double min, double max, double valueStep = 0.0);
    SettingsDefinition &withOptions(const QVector<SettingOption> &value);
    SettingsDefinition &withOptionsProvider(const std::function<QVector<SettingOption>()> &provider);
    SettingsDefinition &asReference();
    SettingsDefinition &applies(const QString &value);
    SettingsDefinition &asAdvanced(bool value = true);
    SettingsDefinition &withNullDefault();
    SettingsDefinition &legacyFileKey(const QString &value);
    SettingsDefinition &legacyCodeSymbol(const QString &value);
    SettingsDefinition &implementedBy(const QString &owner, bool runtimeCache,
                                      const QString &access = "review");

    QJsonObject toJson() const;
};

#endif
