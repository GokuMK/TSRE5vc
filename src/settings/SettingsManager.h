#ifndef TSRE_SETTINGSMANAGER_H
#define TSRE_SETTINGSMANAGER_H

#include <settings/SettingsProfile.h>
#include <settings/SettingsRegistry.h>
#include <settings/SettingsValidator.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

class SettingsManager : public QObject {
    Q_OBJECT
public:
    enum SupportState { Unsupported, Supported, TypeMismatch };

    explicit SettingsManager(QObject *parent = nullptr);
    static SettingsManager &instance();

    SettingsRegistry &registry();
    const SettingsRegistry &registry() const;

    bool initialize(const SettingsProfileSelection &selection, QString *error = nullptr);
    bool loadFile(const QString &settingsFile, QString *error = nullptr);
    bool save(QString *error = nullptr, bool forceExternalOverwrite = false);
    bool saveAs(const QString &settingsFile, QString *error = nullptr);
    bool reload(QString *error = nullptr);

    QString settingsFilePath() const;
    QString profileDirectory() const;
    QString profileName() const;
    bool isModified() const;
    bool wasCreated() const;
    bool hasExternalChange() const;

    QJsonObject document() const;
    QJsonArray settingsArray() const;
    QJsonArray groupsArray() const;
    QVector<SettingsIssue> issues() const;
    QStringList keys() const;

    bool contains(const QString &key) const;
    QJsonObject settingObject(const QString &key) const;
    QVariant value(const QString &key, const QVariant &fallback = QVariant()) const;
    SupportState supportState(const QString &key) const;
    void setSupported(const QString &key, SettingType type);

    bool setValue(const QString &key, const QVariant &value, QString *error = nullptr);
    bool replaceSettingObject(const QString &oldKey, const QJsonObject &object,
                              QString *error = nullptr);
    bool addSettingObject(const QJsonObject &object, QString *error = nullptr);
    bool removeSetting(const QString &key, QString *error = nullptr);
    bool replaceDocument(const QJsonObject &document, QString *error = nullptr);

    QString secretValue(const QString &reference) const;
    QString resolveSecretPlaceholders(const QString &text,
                                      QString *error = nullptr) const;
    bool setSecretValue(const QString &reference, const QString &value,
                        QString *error = nullptr);

signals:
    void settingsChanged();
    void profileChanged(const QString &path);

private:
    SettingsRegistry m_registry;
    QJsonObject m_document;
    QHash<QString, int> m_index;
    QVector<SettingsIssue> m_issues;
    QString m_settingsFile;
    QByteArray m_loadedHash;
    bool m_modified = false;
    bool m_created = false;

    QJsonObject m_secretsDocument;
    QString m_secretsFile;
    bool m_secretsModified = false;

    void rebuildIndex();
    void seedMissingDefinitions();
    void applyOverrides(const QHash<QString, QString> &overrides);
    bool loadSecrets(QString *error);
    bool saveSecrets(QString *error);
    QByteArray currentFileHash() const;
    static QByteArray fileHash(const QString &path);
    static bool writeJsonAtomically(const QString &path, const QJsonObject &document,
                                    QString *error);
    static bool backupFile(const QString &path, QString *error);
    static bool isStructurallyLoadable(const QJsonObject &document, QString *error);
};

#endif
