#ifndef TSRE_SETTINGSPROFILE_H
#define TSRE_SETTINGSPROFILE_H

#include <QHash>
#include <QString>
#include <QStringList>

struct SettingsProfileSelection {
    QString settingsFile;
    QString profileName = "default";
    bool useAppDataProfile = false;
    QHash<QString, QString> overrides;
};

class SettingsProfile {
public:
    static QString portableProfilesRoot();
    static QString appDataProfileRoot();
    static QStringList portableProfileNames();
    static bool isPortableProfileFile(const QString &settingsFile,
                                      QString *profileName = nullptr);
    static bool duplicatePortableProfile(const QString &sourceSettingsFile,
                                         const QString &newProfileName,
                                         QString *newSettingsFile = nullptr,
                                         QString *error = nullptr);
    static QString resolveSettingsFile(const SettingsProfileSelection &selection);
    static bool ensureStartupArgsFile(const QString &filePath,
                                      QString *error = nullptr);
    static QStringList readStartupArguments(const QString &filePath,
                                            QStringList *warnings = nullptr);
};

#endif
