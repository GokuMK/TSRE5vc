#include <settings/SettingsManager.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>

SettingsManager::SettingsManager(QObject *parent) : QObject(parent) {}

SettingsManager &SettingsManager::instance() {
    static SettingsManager manager;
    return manager;
}

SettingsRegistry &SettingsManager::registry() { return m_registry; }
const SettingsRegistry &SettingsManager::registry() const { return m_registry; }

bool SettingsManager::initialize(const SettingsProfileSelection &selection, QString *error) {
    const QString file = SettingsProfile::resolveSettingsFile(selection);
    if (!loadFile(file, error))
        return false;
    applyOverrides(selection.overrides);
    return true;
}

bool SettingsManager::loadFile(const QString &settingsFile, QString *error) {
    m_settingsFile = QDir::cleanPath(QFileInfo(settingsFile).absoluteFilePath());
    m_secretsFile = QDir(QFileInfo(m_settingsFile).absolutePath()).filePath("secrets.json");
    m_created = false;
    m_modified = false;
    m_secretsModified = false;

    QFile file(m_settingsFile);
    if (!file.exists()) {
        QJsonObject profile;
        const QString directoryName = QFileInfo(m_settingsFile).dir().dirName();
        profile["id"] = directoryName.isEmpty() ? QString("default") : directoryName;
        profile["name"] = directoryName.isEmpty() ? QString("Default") : directoryName;
        profile["description"] = "Registry-generated TSRE settings profile";
        m_document = QJsonObject{
            {"format", "tsre-settings"},
            {"schemaVersion", 1},
            {"profile", profile},
            {"groups", QJsonArray()},
            {"settings", QJsonArray()}
        };
        seedMissingDefinitions();
        rebuildIndex();
        m_issues = SettingsValidator::validateDocument(m_document, m_registry);
        if (SettingsValidator::hasErrors(m_issues)) {
            if (error) *error = "Generated settings definitions failed validation.";
            return false;
        }
        m_created = true;
        m_modified = true;
        if (!save(error))
            return false;
    } else {
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = QString("Cannot open settings file: %1").arg(file.errorString());
            return false;
        }
        QJsonParseError parseError;
        const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();
        if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
            if (error) *error = QString("Settings JSON is invalid: %1").arg(parseError.errorString());
            return false;
        }
        const QJsonObject candidate = parsed.object();
        if (candidate.value("format").toString() != "tsre-settings") {
            if (error) *error = "The selected file is not a self-describing tsre-settings profile.";
            return false;
        }
        if (!isStructurallyLoadable(candidate, error))
            return false;
        m_document = candidate;
        rebuildIndex();
        m_issues = SettingsValidator::validateDocument(m_document, m_registry);
        // Per-setting errors remain loadable so the editor can expose and repair them.
        // Only structural errors above prevent indexing the document safely.
        seedMissingDefinitions();
        rebuildIndex();
        m_issues = SettingsValidator::validateDocument(m_document, m_registry);
    }

    if (!loadSecrets(error))
        return false;
    m_loadedHash = currentFileHash();
    emit profileChanged(m_settingsFile);
    emit settingsChanged();
    return true;
}

bool SettingsManager::reload(QString *error) {
    if (m_settingsFile.isEmpty()) {
        if (error) *error = "No settings profile is selected.";
        return false;
    }
    return loadFile(m_settingsFile, error);
}

bool SettingsManager::save(QString *error, bool forceExternalOverwrite) {
    if (m_settingsFile.isEmpty()) {
        if (error) *error = "No settings profile is selected.";
        return false;
    }
    if (!forceExternalOverwrite && hasExternalChange()) {
        if (error) *error = "Settings file changed outside TSRE after it was loaded.";
        return false;
    }
    m_issues = SettingsValidator::validateDocument(m_document, m_registry);
    if (SettingsValidator::hasErrors(m_issues)) {
        if (error) *error = "Settings profile contains validation errors.";
        return false;
    }
    if (QFileInfo::exists(m_settingsFile) && !backupFile(m_settingsFile, error))
        return false;
    if (!writeJsonAtomically(m_settingsFile, m_document, error))
        return false;
    if (m_secretsModified && !saveSecrets(error))
        return false;
    m_loadedHash = currentFileHash();
    m_modified = false;
    m_secretsModified = false;
    emit settingsChanged();
    return true;
}

bool SettingsManager::saveAs(const QString &settingsFile, QString *error) {
    const QString oldFile = m_settingsFile;
    const QString oldSecrets = m_secretsFile;
    const QByteArray oldHash = m_loadedHash;
    m_settingsFile = QDir::cleanPath(QFileInfo(settingsFile).absoluteFilePath());
    m_secretsFile = QDir(QFileInfo(m_settingsFile).absolutePath()).filePath("secrets.json");
    m_loadedHash.clear();
    if (!save(error, true)) {
        m_settingsFile = oldFile;
        m_secretsFile = oldSecrets;
        m_loadedHash = oldHash;
        return false;
    }
    if (!saveSecrets(error))
        return false;
    emit profileChanged(m_settingsFile);
    return true;
}

QString SettingsManager::settingsFilePath() const { return m_settingsFile; }
QString SettingsManager::profileDirectory() const { return QFileInfo(m_settingsFile).absolutePath(); }
QString SettingsManager::profileName() const {
    return m_document.value("profile").toObject().value("name").toString();
}
bool SettingsManager::isModified() const { return m_modified; }
bool SettingsManager::wasCreated() const { return m_created; }
bool SettingsManager::hasExternalChange() const {
    if (m_settingsFile.isEmpty() || m_loadedHash.isEmpty() || !QFileInfo::exists(m_settingsFile))
        return false;
    return currentFileHash() != m_loadedHash;
}

QJsonObject SettingsManager::document() const { return m_document; }
QJsonArray SettingsManager::settingsArray() const { return m_document.value("settings").toArray(); }
QJsonArray SettingsManager::groupsArray() const { return m_document.value("groups").toArray(); }
QVector<SettingsIssue> SettingsManager::issues() const { return m_issues; }

QStringList SettingsManager::keys() const {
    QStringList result = m_index.keys();
    result.sort(Qt::CaseSensitive);
    return result;
}

bool SettingsManager::contains(const QString &key) const { return m_index.contains(key); }

QJsonObject SettingsManager::settingObject(const QString &key) const {
    const auto it = m_index.constFind(key);
    if (it == m_index.constEnd())
        return QJsonObject();
    return m_document.value("settings").toArray().at(it.value()).toObject();
}

QVariant SettingsManager::value(const QString &key, const QVariant &fallback) const {
    const QJsonObject object = settingObject(key);
    if (object.isEmpty())
        return fallback;
    SettingType type;
    if (!settingTypeFromName(object.value("type").toString(), &type)
            || (!settingTypeAcceptsJson(type, object.value("value"))
                && !(object.value("nullable").toBool()
                     && object.value("value").isNull())))
        return fallback;
    return settingVariantFromJson(object.value("value"), type);
}

SettingsManager::SupportState SettingsManager::supportState(const QString &key) const {
    SettingType supportedType;
    if (!m_registry.supportedType(key, &supportedType))
        return Unsupported;
    const QJsonObject object = settingObject(key);
    SettingType storedType;
    if (!settingTypeFromName(object.value("type").toString(), &storedType)
            || storedType != supportedType)
        return TypeMismatch;
    return Supported;
}

void SettingsManager::setSupported(const QString &key, SettingType type) {
    m_registry.setSupported(key, type);
    emit settingsChanged();
}

bool SettingsManager::setValue(const QString &key, const QVariant &value, QString *error) {
    QJsonObject object = settingObject(key);
    if (object.isEmpty()) {
        if (error) *error = QString("Unknown setting key: %1").arg(key);
        return false;
    }
    SettingType type;
    if (!settingTypeFromName(object.value("type").toString(), &type)) {
        if (error) *error = QString("Cannot edit unknown setting type for %1.").arg(key);
        return false;
    }
    if ((!value.isValid() || value.isNull()) && !object.value("nullable").toBool()) {
        if (error) *error = QString("Setting is not nullable: %1").arg(key);
        return false;
    }
    object["value"] = settingJsonFromVariant(value, type);
    return replaceSettingObject(key, object, error);
}

bool SettingsManager::replaceSettingObject(const QString &oldKey,
                                           const QJsonObject &object,
                                           QString *error) {
    const QString newKey = object.value("key").toString().trimmed();
    if (newKey.isEmpty()) {
        if (error) *error = "Setting key cannot be empty.";
        return false;
    }
    if (newKey != oldKey && m_index.contains(newKey)) {
        if (error) *error = QString("Setting key already exists: %1").arg(newKey);
        return false;
    }
    const QVector<SettingsIssue> objectIssues = SettingsValidator::validateSetting(object, m_registry);
    if (SettingsValidator::hasErrors(objectIssues)) {
        if (error) *error = objectIssues.first().message;
        return false;
    }
    const auto it = m_index.constFind(oldKey);
    if (it == m_index.constEnd()) {
        if (error) *error = QString("Unknown setting key: %1").arg(oldKey);
        return false;
    }
    QJsonArray array = m_document.value("settings").toArray();
    array.replace(it.value(), object);
    m_document["settings"] = array;
    rebuildIndex();
    m_modified = true;
    m_issues = SettingsValidator::validateDocument(m_document, m_registry);
    emit settingsChanged();
    return true;
}

bool SettingsManager::addSettingObject(const QJsonObject &object, QString *error) {
    const QString key = object.value("key").toString().trimmed();
    if (key.isEmpty() || m_index.contains(key)) {
        if (error) *error = key.isEmpty() ? "Setting key cannot be empty."
                                          : QString("Setting key already exists: %1").arg(key);
        return false;
    }
    const QVector<SettingsIssue> objectIssues = SettingsValidator::validateSetting(object, m_registry);
    if (SettingsValidator::hasErrors(objectIssues)) {
        if (error) *error = objectIssues.first().message;
        return false;
    }
    QJsonArray array = m_document.value("settings").toArray();
    array.append(object);
    m_document["settings"] = array;
    rebuildIndex();
    m_modified = true;
    emit settingsChanged();
    return true;
}

bool SettingsManager::removeSetting(const QString &key, QString *error) {
    const auto it = m_index.constFind(key);
    if (it == m_index.constEnd()) {
        if (error) *error = QString("Unknown setting key: %1").arg(key);
        return false;
    }
    QJsonArray array = m_document.value("settings").toArray();
    array.removeAt(it.value());
    m_document["settings"] = array;
    rebuildIndex();
    m_modified = true;
    emit settingsChanged();
    return true;
}

bool SettingsManager::replaceDocument(const QJsonObject &document, QString *error) {
    const QVector<SettingsIssue> candidateIssues = SettingsValidator::validateDocument(document, m_registry);
    if (SettingsValidator::hasErrors(candidateIssues)) {
        if (error) {
            for (const SettingsIssue &issue : candidateIssues) {
                if (issue.severity == SettingsIssue::Error) { *error = issue.message; break; }
            }
        }
        return false;
    }
    m_document = document;
    m_issues = candidateIssues;
    rebuildIndex();
    m_modified = true;
    emit settingsChanged();
    return true;
}

QString SettingsManager::secretValue(const QString &reference) const {
    return m_secretsDocument.value("secrets").toObject().value(reference).toString();
}

QString SettingsManager::resolveSecretPlaceholders(const QString &text,
                                                    QString *error) const {
    if (error) error->clear();
    static const QRegularExpression placeholder(
                QStringLiteral("\\{secret:([A-Za-z0-9._-]+)\\}"));
    const QJsonObject secrets = m_secretsDocument.value("secrets").toObject();
    QString result = text;
    QVector<QPair<qsizetype, QPair<qsizetype, QString> > > replacements;
    QRegularExpressionMatchIterator matches = placeholder.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString reference = match.captured(1);
        if (!secrets.contains(reference) || !secrets.value(reference).isString()) {
            if (error)
                *error = QString("Missing secret reference: %1").arg(reference);
            return QString();
        }
        replacements.append({match.capturedStart(0),
                             {match.capturedLength(0),
                              secrets.value(reference).toString()}});
    }
    QString unmatched = text;
    for (auto it = replacements.crbegin(); it != replacements.crend(); ++it)
        unmatched.remove(it->first, it->second.first);
    if (unmatched.contains("{secret:")) {
        if (error) *error = "Invalid secret placeholder syntax.";
        return QString();
    }
    for (auto it = replacements.crbegin(); it != replacements.crend(); ++it)
        result.replace(it->first, it->second.first, it->second.second);
    return result;
}

bool SettingsManager::setSecretValue(const QString &reference, const QString &value,
                                     QString *error) {
    static const QRegularExpression validReference(
                QStringLiteral("^[A-Za-z0-9._-]+$"));
    if (!validReference.match(reference).hasMatch()) {
        if (error)
            *error = "Secret reference may contain only letters, numbers, '.', '_', and '-'.";
        return false;
    }
    QJsonObject secrets = m_secretsDocument.value("secrets").toObject();
    secrets[reference] = value;
    m_secretsDocument["secrets"] = secrets;
    m_secretsModified = true;
    m_modified = true;
    Q_UNUSED(error);
    return true;
}

void SettingsManager::rebuildIndex() {
    m_index.clear();
    const QJsonArray array = m_document.value("settings").toArray();
    for (int i = 0; i < array.size(); ++i) {
        const QString key = array.at(i).toObject().value("key").toString();
        if (!key.isEmpty() && !m_index.contains(key))
            m_index.insert(key, i);
    }
}

void SettingsManager::seedMissingDefinitions() {
    QJsonArray groups = m_document.value("groups").toArray();
    QSet<QString> groupIds;
    for (const QJsonValue &entry : groups)
        groupIds.insert(entry.toObject().value("id").toString());
    for (const SettingsGroupDefinition &group : m_registry.groups()) {
        if (!groupIds.contains(group.id)) {
            groups.append(group.toJson());
            groupIds.insert(group.id);
            m_modified = true;
        } else if (!group.subgroups.isEmpty()) {
            // Preserve profile-owned group metadata, but seed newly registered
            // subgroup definitions so an older profile can display new sections.
            for (int i = 0; i < groups.size(); ++i) {
                QJsonObject stored = groups.at(i).toObject();
                if (stored.value("id").toString() != group.id)
                    continue;
                QJsonArray subgroups = stored.value("subgroups").toArray();
                QSet<QString> subgroupIds;
                for (const QJsonValue &entry : subgroups)
                    subgroupIds.insert(entry.toObject().value("id").toString());
                for (const SettingsSubgroupDefinition &subgroup : group.subgroups) {
                    if (!subgroupIds.contains(subgroup.id)) {
                        subgroups.append(subgroup.toJson());
                        subgroupIds.insert(subgroup.id);
                        m_modified = true;
                    }
                }
                stored["subgroups"] = subgroups;
                groups.replace(i, stored);
                break;
            }
        }
    }
    m_document["groups"] = groups;

    rebuildIndex();
    QJsonArray settings = m_document.value("settings").toArray();
    for (const SettingsDefinition &definition : m_registry.definitions()) {
        if (!m_index.contains(definition.key)) {
            settings.append(definition.toJson());
            m_modified = true;
        }
    }
    m_document["settings"] = settings;
}

void SettingsManager::applyOverrides(const QHash<QString, QString> &overrides) {
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        QJsonObject object = settingObject(it.key());
        SettingType type;
        if (object.isEmpty() || !settingTypeFromName(object.value("type").toString(), &type))
            continue;
        QVariant parsed;
        switch (type) {
        case SettingType::Bool:
            parsed = (it.value().compare("true", Qt::CaseInsensitive) == 0
                      || it.value() == "1" || it.value().compare("on", Qt::CaseInsensitive) == 0);
            break;
        case SettingType::Int: parsed = it.value().toInt(); break;
        case SettingType::Float: parsed = it.value().toDouble(); break;
        case SettingType::Enum: {
            const QJsonArray options = object.value("options").toArray();
            for (const QJsonValue &entry : options) {
                const QJsonValue option = entry.toObject().value("value");
                if ((option.isString()
                     && option.toString().compare(it.value(), Qt::CaseInsensitive) == 0)
                        || (option.isDouble()
                            && QString::number(option.toDouble(), 'g', 16) == it.value())
                        || (option.isBool()
                            && option.toBool() == (it.value().compare("true", Qt::CaseInsensitive) == 0))) {
                    parsed = option.toVariant();
                    break;
                }
            }
            if (!parsed.isValid())
                parsed = it.value();
            break;
        }
        case SettingType::StringList: parsed = it.value().split(':', Qt::SkipEmptyParts); break;
        default: parsed = it.value(); break;
        }
        object["value"] = settingJsonFromVariant(parsed, type);
        const int index = m_index.value(it.key());
        QJsonArray settings = m_document.value("settings").toArray();
        settings.replace(index, object);
        m_document["settings"] = settings;
        // Launch overrides are effective in memory and deliberately do not mark the profile dirty.
    }
    rebuildIndex();
}

bool SettingsManager::loadSecrets(QString *error) {
    QFile file(m_secretsFile);
    if (!file.exists()) {
        m_secretsDocument = QJsonObject{{"format", "tsre-secrets"},
                                        {"schemaVersion", 1},
                                        {"secrets", QJsonObject()}};
        m_secretsModified = false;
        return writeJsonAtomically(m_secretsFile, m_secretsDocument, error);
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QString("Cannot open secrets file: %1").arg(file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()
            || parsed.object().value("format").toString() != "tsre-secrets"
            || !parsed.object().value("secrets").isObject()) {
        if (error) *error = "Secrets file is invalid or has an unsupported format.";
        return false;
    }
    m_secretsDocument = parsed.object();
    m_secretsModified = false;
    return true;
}

bool SettingsManager::saveSecrets(QString *error) {
    return writeJsonAtomically(m_secretsFile, m_secretsDocument, error);
}

QByteArray SettingsManager::currentFileHash() const { return fileHash(m_settingsFile); }

QByteArray SettingsManager::fileHash(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

bool SettingsManager::writeJsonAtomically(const QString &path,
                                          const QJsonObject &document,
                                          QString *error) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) *error = QString("Cannot create profile directory: %1").arg(info.absolutePath());
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QString("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(QJsonDocument(document).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = QString("Cannot commit %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool SettingsManager::backupFile(const QString &path, QString *error) {
    const QFileInfo info(path);
    if (!info.exists())
        return true;
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
    const QString backup = info.dir().filePath(
                QString("%1-%2.%3").arg(info.completeBaseName(), stamp, info.suffix()));
    if (!QFile::copy(path, backup)) {
        if (error) *error = QString("Cannot create settings backup: %1").arg(backup);
        return false;
    }

    QDir directory = info.dir();
    const QString pattern = QString("%1-*.%2").arg(info.completeBaseName(), info.suffix());
    const QFileInfoList backups = directory.entryInfoList({pattern}, QDir::Files, QDir::Time);
    for (int i = 5; i < backups.size(); ++i)
        QFile::remove(backups.at(i).absoluteFilePath());
    return true;
}

bool SettingsManager::isStructurallyLoadable(const QJsonObject &document, QString *error) {
    if (document.value("format").toString() != "tsre-settings") {
        if (error) *error = "The selected file is not a self-describing tsre-settings profile.";
        return false;
    }
    if (document.value("schemaVersion").toInt(-1) != 1
            || !document.value("groups").isArray()
            || !document.value("settings").isArray()) {
        if (error) *error = "Settings profile has an unsupported or incomplete document structure.";
        return false;
    }
    QSet<QString> keys;
    for (const QJsonValue &entry : document.value("settings").toArray()) {
        const QString key = entry.isObject()
                ? entry.toObject().value("key").toString().trimmed() : QString();
        if (key.isEmpty() || keys.contains(key)) {
            if (error) *error = key.isEmpty()
                    ? "Settings profile contains an entry without a key."
                    : QString("Settings profile contains duplicate key: %1").arg(key);
            return false;
        }
        keys.insert(key);
    }
    return true;
}
