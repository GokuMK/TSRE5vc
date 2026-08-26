#ifndef TSRE_SETTINGSVALIDATOR_H
#define TSRE_SETTINGSVALIDATOR_H

#include <QJsonObject>
#include <QString>
#include <QVector>

class SettingsRegistry;

struct SettingsIssue {
    enum Severity { Warning, Error };
    Severity severity = Error;
    QString key;
    QString message;
};

class SettingsValidator {
public:
    static QVector<SettingsIssue> validateDocument(const QJsonObject &document,
                                                   const SettingsRegistry &registry);
    static QVector<SettingsIssue> validateSetting(const QJsonObject &setting,
                                                  const SettingsRegistry &registry);
    static bool hasErrors(const QVector<SettingsIssue> &issues);
};

#endif
