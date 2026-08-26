#include <settings/SettingsProfile.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

namespace {
QString safeProfileName(QString name) {
    name = name.trimmed();
    if (name.isEmpty() || name == "." || name == "..")
        return "default";
    for (qsizetype i = 0; i < name.size(); ++i) {
        const QChar character = name.at(i);
        if (!character.isLetterOrNumber() && character != '-' && character != '_'
                && character != '.')
            name[i] = '_';
    }
    return name;
}

bool copyDirectoryContents(const QString &sourcePath, const QString &targetPath,
                           QString *error) {
    if (!QDir().mkpath(targetPath)) {
        if (error) *error = QString("Cannot create profile directory: %1").arg(targetPath);
        return false;
    }
    const QDir source(sourcePath);
    const QFileInfoList entries = source.entryInfoList(
                QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
    for (const QFileInfo &entry : entries) {
        if (entry.isSymLink()) {
            if (error) *error = QString("Profile contains unsupported symbolic link: %1")
                    .arg(entry.absoluteFilePath());
            return false;
        }
        const QString target = QDir(targetPath).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectoryContents(entry.absoluteFilePath(), target, error))
                return false;
        } else if (!QFile::copy(entry.absoluteFilePath(), target)) {
            if (error) *error = QString("Cannot copy profile file: %1").arg(entry.fileName());
            return false;
        }
    }
    return true;
}
}

QString SettingsProfile::portableProfilesRoot() {
    // main.cpp establishes TSRE's effective data root before initializing the
    // settings manager (and strips a development /build suffix when present).
    return QDir(QDir::currentPath()).filePath("profiles");
}

QString SettingsProfile::appDataProfileRoot() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(base).filePath("TSRE");
}

QStringList SettingsProfile::portableProfileNames() {
    QStringList result;
    const QDir root(portableProfilesRoot());
    for (const QFileInfo &directory : root.entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        if (QFileInfo::exists(QDir(directory.absoluteFilePath()).filePath("settings.json")))
            result.append(directory.fileName());
    }
    return result;
}

bool SettingsProfile::isPortableProfileFile(const QString &settingsFile,
                                            QString *profileName) {
    const QFileInfo file(settingsFile);
    if (file.fileName().compare("settings.json", Qt::CaseInsensitive) != 0)
        return false;
    const QDir root(portableProfilesRoot());
    const QString relativeDirectory = QDir::cleanPath(
                root.relativeFilePath(file.absolutePath()));
    if (relativeDirectory.isEmpty() || relativeDirectory == "."
            || relativeDirectory == ".." || relativeDirectory.startsWith("../")
            || relativeDirectory.startsWith("..\\")
            || relativeDirectory.contains('/') || relativeDirectory.contains('\\'))
        return false;
    if (profileName)
        *profileName = relativeDirectory;
    return true;
}

bool SettingsProfile::duplicatePortableProfile(const QString &sourceSettingsFile,
                                               const QString &newProfileName,
                                               QString *newSettingsFile,
                                               QString *error) {
    QString sourceName;
    if (!isPortableProfileFile(sourceSettingsFile, &sourceName)) {
        if (error) *error = "Only a managed portable profile can be duplicated.";
        return false;
    }
    const QString name = newProfileName.trimmed();
    static const QRegularExpression validName(QStringLiteral("^[\\p{L}\\p{N}._-]+$"));
    if (name.isEmpty() || name == "." || name == ".." || !validName.match(name).hasMatch()) {
        if (error) *error = "Profile name may contain only letters, numbers, '.', '_' and '-'.";
        return false;
    }
    for (const QString &existing : portableProfileNames()) {
        if (existing.compare(name, Qt::CaseInsensitive) == 0) {
            if (error) *error = QString("Profile already exists: %1").arg(existing);
            return false;
        }
    }

    const QString rootPath = portableProfilesRoot();
    if (!QDir().mkpath(rootPath)) {
        if (error) *error = QString("Cannot create profiles root: %1").arg(rootPath);
        return false;
    }
    for (const QFileInfo &entry : QDir(rootPath).entryInfoList(
             QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System)) {
        if (entry.fileName().compare(name, Qt::CaseInsensitive) == 0) {
            if (error) *error = QString("A file or directory already uses profile name: %1")
                    .arg(entry.fileName());
            return false;
        }
    }
    QTemporaryDir staging(QDir(rootPath).filePath(".duplicate-XXXXXX"));
    if (!staging.isValid()) {
        if (error) *error = "Cannot create temporary profile directory.";
        return false;
    }
    const QString sourceDirectory = QFileInfo(sourceSettingsFile).absolutePath();
    if (!copyDirectoryContents(sourceDirectory, staging.path(), error))
        return false;

    const QString stagedSettings = QDir(staging.path()).filePath("settings.json");
    QFile input(stagedSettings);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error) *error = QString("Cannot read cloned settings.json: %1").arg(input.errorString());
        return false;
    }
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &parseError);
    input.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QString("Cannot update cloned profile metadata: %1")
                .arg(parseError.errorString());
        return false;
    }
    QJsonObject root = document.object();
    QJsonObject profile = root.value("profile").toObject();
    profile["id"] = name;
    profile["name"] = name;
    root["profile"] = profile;
    QSaveFile output(stagedSettings);
    if (!output.open(QIODevice::WriteOnly)
            || output.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
            || !output.commit()) {
        if (error) *error = QString("Cannot write cloned settings.json: %1")
                .arg(output.errorString());
        return false;
    }

    const QString destination = QDir(rootPath).filePath(name);
    if (!QDir().rename(staging.path(), destination)) {
        if (error) *error = QString("Cannot finalize duplicated profile: %1").arg(destination);
        return false;
    }
    staging.setAutoRemove(false);
    if (newSettingsFile)
        *newSettingsFile = QDir(destination).filePath("settings.json");
    return true;
}

QString SettingsProfile::resolveSettingsFile(const SettingsProfileSelection &selection) {
    if (!selection.settingsFile.trimmed().isEmpty()) {
        QFileInfo info(selection.settingsFile);
        return info.isAbsolute()
                ? QDir::cleanPath(info.absoluteFilePath())
                : QDir::cleanPath(QDir(QDir::currentPath())
                                  .absoluteFilePath(selection.settingsFile));
    }
    if (selection.useAppDataProfile)
        return QDir(appDataProfileRoot()).filePath("settings.json");
    const QString safeName = safeProfileName(selection.profileName);
    return QDir(portableProfilesRoot()).filePath(safeName + "/settings.json");
}

bool SettingsProfile::ensureStartupArgsFile(const QString &filePath, QString *error) {
    if (QFileInfo::exists(filePath))
        return true;
    const QFileInfo info(filePath);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) *error = QString("Cannot create startup arguments directory: %1")
                .arg(info.absolutePath());
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QString("Cannot create startup arguments file: %1")
                .arg(file.errorString());
        return false;
    }
    const QByteArray contents = QByteArrayLiteral(
        "# TSRE startup arguments\n"
        "#\n"
        "# Remove the leading # from an example to enable it. Use one argument\n"
        "# per line. Command-line arguments override values from this file.\n"
        "\n"
        "# Select one settings profile source:\n"
        "# --profile=default\n"
        "# --settings=profiles/custom/settings.json\n"
        "# --appdata-profile\n"
        "\n"
        "# Override a profile value for this launch without saving it:\n"
        "# --set=core.rendering.tileRadius=4\n"
        "\n"
        "# Development content and startup selection:\n"
        "# --game-root=C:/TrainSimulator\n"
        "# --route=ROUTE_DIRECTORY\n"
        "# --geo-path=C:/Geodata\n"
        "\n"
        "# Application mode:\n"
        "# --conedit\n"
        "# --shapeview\n"
        "# --file=C:/Shapes/example.s\n"
        "# --play\n"
        "# --aceconv\n"
        "# --server\n"
        "# --ip=127.0.0.1\n"
        "# --port=65535\n"
        "\n"
        "# One-shot route operation:\n"
        "# --route-merge=SOURCE_ROUTE:0:0:0\n"
        "\n"
        "# Disabled renderer diagnostic retained for compatibility:\n"
        "# --gather-legacy-overlays\n"
        "\n"
        "# Developer tests and diagnostics:\n"
        "# --test-list\n"
        "# --test\n"
        "# --test-suite=settings\n"
        "# --test-cases=C:/Tests/cases.jsonl\n"
        "# --test-verbose\n"
        "# --flex-log\n"
        "# --flex-log-file=C:/Tests/flex.jsonl\n"
        "# --flex-log-candidates\n");
    if (file.write(contents) != contents.size() || !file.commit()) {
        if (error) *error = QString("Cannot write startup arguments file: %1")
                .arg(file.errorString());
        return false;
    }
    return true;
}

QStringList SettingsProfile::readStartupArguments(const QString &filePath,
                                                  QStringList *warnings) {
    QStringList result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (!line.startsWith('-')) {
            if (warnings)
                warnings->append(QString("Startup argument must begin with '-': %1")
                                 .arg(line));
            continue;
        }
        result.append(line);
    }
    file.close();
    return result;
}
