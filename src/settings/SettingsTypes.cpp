#include <settings/SettingsTypes.h>

#include <QJsonArray>

QString settingTypeName(SettingType type) {
    switch (type) {
    case SettingType::Bool: return "bool";
    case SettingType::Int: return "int";
    case SettingType::Float: return "float";
    case SettingType::String: return "string";
    case SettingType::MultilineString: return "multilineString";
    case SettingType::Color: return "color";
    case SettingType::Enum: return "enum";
    case SettingType::Path: return "path";
    case SettingType::Directory: return "directory";
    case SettingType::KeySequence: return "keySequence";
    case SettingType::StringList: return "stringList";
    case SettingType::Secret: return "secret";
    }
    return "string";
}

bool settingTypeFromName(const QString &name, SettingType *type) {
    static const QHash<QString, SettingType> types = {
        {"bool", SettingType::Bool}, {"int", SettingType::Int},
        {"float", SettingType::Float}, {"string", SettingType::String},
        {"multilineString", SettingType::MultilineString},
        {"color", SettingType::Color}, {"enum", SettingType::Enum},
        {"path", SettingType::Path}, {"directory", SettingType::Directory},
        {"keySequence", SettingType::KeySequence},
        {"stringList", SettingType::StringList}, {"secret", SettingType::Secret}
    };
    const auto it = types.constFind(name);
    if (it == types.constEnd())
        return false;
    if (type)
        *type = it.value();
    return true;
}

bool settingTypeAcceptsJson(SettingType type, const QJsonValue &value) {
    switch (type) {
    case SettingType::Bool:
        return value.isBool();
    case SettingType::Int:
        return value.isDouble() && qFuzzyCompare(value.toDouble() + 1.0,
                                                  qRound64(value.toDouble()) + 1.0);
    case SettingType::Float:
        return value.isDouble();
    case SettingType::StringList:
        if (!value.isArray())
            return false;
        for (const QJsonValue &entry : value.toArray()) {
            if (!entry.isString())
                return false;
        }
        return true;
    case SettingType::Enum:
        return value.isString() || value.isDouble() || value.isBool();
    default:
        return value.isString();
    }
}

QJsonValue settingJsonFromVariant(const QVariant &value, SettingType type) {
    if (!value.isValid() || value.isNull())
        return QJsonValue::Null;
    switch (type) {
    case SettingType::Bool:
        return value.toBool();
    case SettingType::Int:
        return value.toInt();
    case SettingType::Float:
        return value.toDouble();
    case SettingType::StringList: {
        QJsonArray result;
        for (const QString &entry : value.toStringList())
            result.append(entry);
        return result;
    }
    case SettingType::Enum:
        return QJsonValue::fromVariant(value);
    default:
        return value.toString();
    }
}

QVariant settingVariantFromJson(const QJsonValue &value, SettingType type) {
    if (value.isNull() || value.isUndefined())
        return QVariant();
    switch (type) {
    case SettingType::Bool:
        return value.toBool();
    case SettingType::Int:
        return value.toInt();
    case SettingType::Float:
        return value.toDouble();
    case SettingType::StringList: {
        QStringList result;
        for (const QJsonValue &entry : value.toArray())
            result.append(entry.toString());
        return result;
    }
    case SettingType::Enum:
        return value.toVariant();
    default:
        return value.toString();
    }
}

QJsonObject SettingsSubgroupDefinition::toJson() const {
    return QJsonObject{{"id", id}, {"name", name},
                       {"description", description}, {"order", order}};
}

QJsonObject SettingsGroupDefinition::toJson() const {
    QJsonObject object;
    object["id"] = id;
    object["name"] = name;
    object["description"] = description;
    object["order"] = order;
    if (!subgroups.isEmpty()) {
        QJsonArray array;
        for (const SettingsSubgroupDefinition &subgroup : subgroups)
            array.append(subgroup.toJson());
        object["subgroups"] = array;
    }
    return object;
}

SettingsDefinition SettingsDefinition::boolean(const QString &key, bool defaultValue) {
    SettingsDefinition result;
    result.key = key;
    result.type = SettingType::Bool;
    result.defaultValue = defaultValue;
    return result;
}

SettingsDefinition SettingsDefinition::integer(const QString &key, int defaultValue) {
    SettingsDefinition result;
    result.key = key;
    result.type = SettingType::Int;
    result.defaultValue = defaultValue;
    return result;
}

SettingsDefinition SettingsDefinition::floating(const QString &key, double defaultValue) {
    SettingsDefinition result;
    result.key = key;
    result.type = SettingType::Float;
    result.defaultValue = defaultValue;
    return result;
}

SettingsDefinition SettingsDefinition::string(const QString &key,
                                               const QString &defaultValue,
                                               SettingType type) {
    SettingsDefinition result;
    result.key = key;
    result.type = type;
    result.defaultValue = defaultValue;
    return result;
}

SettingsDefinition SettingsDefinition::enumeration(const QString &key,
                                                    const QVariant &defaultValue) {
    SettingsDefinition result;
    result.key = key;
    result.type = SettingType::Enum;
    result.defaultValue = defaultValue;
    return result;
}

SettingsDefinition &SettingsDefinition::withName(const QString &value) { name = value; return *this; }
SettingsDefinition &SettingsDefinition::withDescription(const QString &value) { description = value; return *this; }
SettingsDefinition &SettingsDefinition::inGroup(const QString &value, int valueOrder) { group = value; order = valueOrder; return *this; }
SettingsDefinition &SettingsDefinition::inSubgroup(const QString &value) { subgroup = value; return *this; }
SettingsDefinition &SettingsDefinition::withUnit(const QString &value) { unit = value; return *this; }
SettingsDefinition &SettingsDefinition::withRange(double min, double max, double valueStep) { hasRange = true; minimum = min; maximum = max; step = valueStep; return *this; }
SettingsDefinition &SettingsDefinition::withOptions(const QVector<SettingOption> &value) { options = value; return *this; }
SettingsDefinition &SettingsDefinition::applies(const QString &value) { apply = value; return *this; }
SettingsDefinition &SettingsDefinition::asAdvanced(bool value) { advanced = value; return *this; }
SettingsDefinition &SettingsDefinition::withNullDefault() { nullable = true; defaultValue = QVariant(); return *this; }
SettingsDefinition &SettingsDefinition::legacyFileKey(const QString &value) { legacyFileKeys.append(value); return *this; }
SettingsDefinition &SettingsDefinition::legacyCodeSymbol(const QString &value) { legacyCodeSymbols.append(value); return *this; }
SettingsDefinition &SettingsDefinition::implementedBy(const QString &owner, bool runtimeCache, const QString &access) {
    implementationOwner = owner;
    requiresRuntimeCache = runtimeCache;
    implementationAccess = access;
    return *this;
}

QJsonObject SettingsDefinition::toJson() const {
    QJsonObject object;
    object["key"] = key;
    object["name"] = name;
    object["value"] = settingJsonFromVariant(defaultValue, type);
    object["default"] = settingJsonFromVariant(defaultValue, type);
    object["type"] = settingTypeName(type);
    object["group"] = group;
    if (!subgroup.isEmpty())
        object["subgroup"] = subgroup;
    object["description"] = description;
    object["order"] = order;
    object["apply"] = apply;
    if (!unit.isEmpty())
        object["unit"] = unit;
    if (advanced)
        object["advanced"] = true;
    if (nullable)
        object["nullable"] = true;
    if (hasRange) {
        QJsonObject range;
        range["minimum"] = minimum;
        range["maximum"] = maximum;
        if (step > 0.0)
            range["step"] = step;
        object["range"] = range;
    }
    if (!options.isEmpty()) {
        QJsonArray optionArray;
        for (const SettingOption &option : options) {
            QJsonObject optionObject;
            optionObject["value"] = QJsonValue::fromVariant(option.value);
            optionObject["name"] = option.name;
            optionArray.append(optionObject);
        }
        object["options"] = optionArray;
    }
    if (!legacyFileKeys.isEmpty() || !legacyCodeSymbols.isEmpty()) {
        QJsonObject legacy;
        legacy["fileKeys"] = QJsonArray::fromStringList(legacyFileKeys);
        legacy["codeSymbols"] = QJsonArray::fromStringList(legacyCodeSymbols);
        object["legacy"] = legacy;
    }
    if (!implementationOwner.isEmpty()) {
        QJsonObject implementation;
        implementation["owner"] = implementationOwner;
        implementation["requiresRuntimeCache"] = requiresRuntimeCache;
        implementation["access"] = implementationAccess;
        object["implementation"] = implementation;
    }
    return object;
}
