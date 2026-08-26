#include <tsre/tests/SettingsTestSuite.h>

#include <settings/DraftSettingsCatalog.h>
#include <settings/SettingsManager.h>
#include <settings/SettingsProfile.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>

int TsreTests::runSettingsSuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    auto check = [&](bool condition, const char *name) {
        if (condition) {
            ++passed;
            if (verbose) qInfo() << "[tests:settings] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:settings] FAIL" << name;
        }
    };

    SettingsManager manager;
    DraftSettingsCatalog::registerDefinitions(manager.registry());
    check(manager.registry().definitions().size() == 76,
          "phase2a-catalog-replaces-two-commands-with-two-native-settings");
    const SettingsDefinition *tileRadius =
            manager.registry().definition("core.rendering.tileRadius");
    check(tileRadius && tileRadius->legacyFileKeys.contains("tileLod")
          && tileRadius->subgroup == "visibility"
          && tileRadius->maximum == 128
          && tileRadius->description.contains("radius", Qt::CaseInsensitive),
          "phase2a-corrects-tile-radius-semantics");
    const SettingsDefinition *objectDistance =
            manager.registry().definition("core.rendering.objectLodDistance");
    check(objectDistance && objectDistance->maximum == 200000.0,
          "phase2a-object-distance-allows-debug-range");
    const SettingsDefinition *gradeFormat =
            manager.registry().definition("core.track.defaultGradeFormat");
    check(gradeFormat && gradeFormat->type == SettingType::Enum
          && gradeFormat->defaultValue.toString() == "permille"
          && gradeFormat->options.size() == 4,
          "phase2a-corrects-grade-format-semantics");
    const SettingsDefinition *startupPosition =
            manager.registry().definition("core.startup.useTilePosition");
    check(startupPosition && startupPosition->type == SettingType::Bool
          && !startupPosition->defaultValue.toBool(),
          "phase2a-startup-tile-position-is-explicit");
    check(manager.registry().definition("core.editing.routeMergeMode") == nullptr,
          "phase2a-route-merge-is-not-profile-preference");
    const SettingsDefinition *writeTrackDatabase =
            manager.registry().definition("core.editing.writeTrackDatabase");
    check(writeTrackDatabase && writeTrackDatabase->defaultValue.toBool()
          && writeTrackDatabase->description.contains("Disabling"),
          "phase2a-track-database-writes-default-enabled");
    const SettingsDefinition *serverAuthentication =
            manager.registry().definition("core.network.serverAuthenticationMode");
    check(serverAuthentication && serverAuthentication->type == SettingType::Enum,
          "phase2a-server-auth-is-mode-not-secret");
    const SettingsDefinition *antiAliasing =
            manager.registry().definition("core.rendering.antiAliasingSamples");
    const SettingsDefinition *shadowMap =
            manager.registry().definition("core.rendering.shadow.primaryMapSize");
    check(antiAliasing && antiAliasing->type == SettingType::Enum
          && antiAliasing->options.size() == 5
          && shadowMap && shadowMap->type == SettingType::Enum
          && shadowMap->options.size() == 7,
          "phase2a-render-buffer-sizes-use-numeric-enums");
    const SettingsDefinition *clientLogin =
            manager.registry().definition("core.network.clientLogin");
    check(clientLogin && clientLogin->type == SettingType::String
          && clientLogin->description.contains("{secret:ID}"),
          "phase2a-client-login-supports-inline-secret-reference");
    const SettingsDefinition *mapApiKey =
            manager.registry().definition("core.maps.imageryApiKey");
    check(mapApiKey && mapApiKey->type == SettingType::Secret
          && mapApiKey->legacyFileKeys.isEmpty(),
          "phase2a-map-api-key-is-native-secret");
    bool foundSubgroups = false;
    for (const SettingsGroupDefinition &group : manager.registry().groups()) {
        if (group.id == "rendering" && !group.subgroups.isEmpty())
            foundSubgroups = true;
    }
    check(foundSubgroups, "phase2a-catalogue-defines-one-level-subgroups");
    check(QDir::cleanPath(SettingsProfile::portableProfilesRoot())
          == QDir::cleanPath(QDir(QDir::currentPath()).filePath("profiles")),
          "portable-profiles-use-tsre-working-root");
    SettingsProfileSelection relativeSelection;
    relativeSelection.settingsFile = "profiles/test/settings.json";
    check(SettingsProfile::resolveSettingsFile(relativeSelection)
          == QDir::cleanPath(QDir(QDir::currentPath())
                             .absoluteFilePath(relativeSelection.settingsFile)),
          "relative-settings-file-uses-tsre-working-root");

    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary-profile-directory");
    const QString settingsFile = QDir(temporary.path()).filePath("profile/settings.json");
    QString error;
    check(manager.loadFile(settingsFile, &error), "registry-generates-profile");
    check(QFile::exists(settingsFile) && manager.settingsArray().size() == 76,
          "generated-profile-has-catalogue");
    check(manager.value("core.paths.gameRoot").toString().isEmpty()
          && manager.value("core.paths.geoData").toString().isEmpty()
          && manager.value("core.startup.route").toString().isEmpty(),
          "generated-profile-has-portable-empty-content-defaults");
    check(manager.settingObject("core.interface.consistBackground")
              .value("value").isNull()
          && manager.settingObject("core.interface.consistBackground")
              .value("nullable").toBool(),
          "generated-profile-supports-default-colour");
    check(manager.settingObject("core.camera.speed.standard")
              .value("apply").toString() == "dynamic"
          && manager.settingObject("core.editing.sortTileObjects")
              .value("apply").toString() == "dynamic"
          && manager.settingObject("core.maps.imageResolution")
              .value("apply").toString() == "dynamic"
          && manager.settingObject("core.rendering.shadow.primaryMapSize")
              .value("apply").toString() == "rendererRestart",
          "generated-profile-uses-controlled-apply-lifecycle");
    bool generatedNextAction = false;
    for (const QJsonValue &entry : manager.settingsArray()) {
        if (entry.toObject().value("apply").toString() == "nextAction") {
            generatedNextAction = true;
            break;
        }
    }
    check(!generatedNextAction, "generated-profile-does-not-use-next-action");
    check(manager.settingObject("core.camera.speed.standard")
              .value("implementation").toObject()
              .contains("requiresRuntimeCache")
          && !manager.settingObject("core.camera.speed.standard")
              .value("implementation").toObject()
              .contains("requiresGameMember"),
          "generated-profile-uses-runtime-cache-metadata");
    check(manager.settingObject("core.camera.speed.standard")
              .value("implementation").toObject()
              .value("requiresRuntimeCache").toBool()
          && !manager.settingObject("core.system.fpsLimit")
              .value("implementation").toObject()
              .value("requiresRuntimeCache").toBool()
          && manager.settingObject("core.editing.writeTrackDatabase")
              .value("implementation").toObject()
              .value("requiresRuntimeCache").toBool(),
          "runtime-cache-metadata-distinguishes-hot-cold-and-safety-values");
    check(manager.settingObject("core.rendering.tileRadius").value("subgroup").toString()
          == "visibility", "generated-settings-store-subgroup");
    check(QFile::exists(QDir(QFileInfo(settingsFile).absolutePath()).filePath("secrets.json")),
          "generated-profile-has-editable-secrets-file");
    const QString threadedKey = "core.rendering.threadedTextureLoading";
    check(manager.supportState(threadedKey) == SettingsManager::Unsupported,
          "definition-is-not-support-claim");
    manager.setSupported(threadedKey, SettingType::Bool);
    check(manager.supportState(threadedKey) == SettingsManager::Supported,
          "explicit-key-type-support-claim");

    QJsonObject custom{{"key", "fork.weather.enabled"}, {"name", "Fork weather"},
        {"description", "A setting understood by another fork."}, {"group", "advanced"},
        {"type", "bool"}, {"value", true}, {"default", false}, {"apply", "restart"}};
    check(manager.addSettingObject(custom, &error), "custom-setting-add");
    check(manager.supportState("fork.weather.enabled") == SettingsManager::Unsupported,
          "custom-setting-unsupported");
    check(manager.save(&error), "custom-setting-save");

    SettingsManager reloaded;
    DraftSettingsCatalog::registerDefinitions(reloaded.registry());
    check(reloaded.loadFile(settingsFile, &error), "profile-reload");
    check(reloaded.value("fork.weather.enabled").toBool(), "custom-setting-preserved");

    QJsonObject customized = reloaded.settingObject(threadedKey);
    customized["description"] = "Profile-owned description";
    check(reloaded.replaceSettingObject(threadedKey, customized, &error)
          && reloaded.save(&error), "stored-metadata-customization");
    SettingsManager preserved;
    DraftSettingsCatalog::registerDefinitions(preserved.registry());
    check(preserved.loadFile(settingsFile, &error)
          && preserved.settingObject(threadedKey).value("description").toString()
             == "Profile-owned description", "registry-does-not-overwrite-stored-metadata");

    QJsonObject invalidDocument = preserved.document();
    QJsonArray invalidSettings = invalidDocument.value("settings").toArray();
    QJsonObject invalid = invalidSettings.first().toObject();
    const QString invalidKey = invalid.value("key").toString();
    invalid["value"] = QJsonArray{1, 2};
    invalidSettings.replace(0, invalid);
    invalidDocument["settings"] = invalidSettings;
    QFile invalidFile(QDir(temporary.path()).filePath("invalid.json"));
    check(invalidFile.open(QIODevice::WriteOnly)
          && invalidFile.write(QJsonDocument(invalidDocument).toJson()) > 0,
          "write-recoverable-invalid-profile");
    invalidFile.close();
    SettingsManager recoverable;
    DraftSettingsCatalog::registerDefinitions(recoverable.registry());
    check(recoverable.loadFile(invalidFile.fileName(), &error),
          "per-setting-error-remains-loadable");
    check(!recoverable.value(invalidKey, "fallback").isValid()
          || recoverable.value(invalidKey, "fallback").toString() == "fallback",
          "invalid-value-safe-fallback");
    check(!recoverable.save(&error), "validation-error-blocks-save");

    const QString launchFile = QDir(temporary.path()).filePath("startup-args.txt");
    const QString generatedLaunchFile =
            QDir(temporary.path()).filePath("generated/startup-args.txt");
    check(SettingsProfile::ensureStartupArgsFile(generatedLaunchFile, &error),
          "startup-arguments-template-generated");
    QFile generatedLaunch(generatedLaunchFile);
    check(generatedLaunch.open(QIODevice::ReadOnly | QIODevice::Text),
          "open-generated-startup-arguments-template");
    const QByteArray generatedLaunchContents = generatedLaunch.readAll();
    check(generatedLaunchContents.contains("# --profile=default")
          && generatedLaunchContents.contains("# --route-merge=SOURCE_ROUTE:0:0:0"),
          "startup-arguments-template-lists-commented-examples");
    generatedLaunch.close();
    QFile launch(launchFile);
    check(launch.open(QIODevice::WriteOnly | QIODevice::Text), "open-startup-arguments-file");
    launch.write("useThreads = true\n");
    launch.close();
    QStringList startupWarnings;
    check(SettingsProfile::readStartupArguments(launchFile, &startupWarnings).isEmpty()
          && !startupWarnings.isEmpty(), "invalid-startup-argument-ignored");
    check(launch.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text),
          "open-populated-startup-arguments-file");
    launch.write("# TSRE startup arguments\n--profile=server\n"
                 "--gather-legacy-overlays\n"
                 "--game-root=C:/development/root\n"
                 "--route=debug-route\n"
                 "--geo-path=C:/development/geo\n"
                 "--route-merge=source:1:2:3\n"
                 "--set=core.rendering.threadedTextureLoading=false\n");
    launch.close();
    const QStringList startupArgs = SettingsProfile::readStartupArguments(launchFile);
    check(startupArgs.size() == 7
          && startupArgs.contains("--profile=server")
          && startupArgs.contains("--gather-legacy-overlays")
          && startupArgs.contains("--game-root=C:/development/root")
          && startupArgs.contains("--route=debug-route")
          && startupArgs.contains("--geo-path=C:/development/geo")
          && startupArgs.contains("--route-merge=source:1:2:3")
          && startupArgs.contains("--set=core.rendering.threadedTextureLoading=false"),
          "startup-arguments-forwarded-to-command-line-parser");

    check(preserved.setSecretValue("network.serverAuth", "not-in-settings", &error),
          "profile-local-secret-set");
    check(preserved.resolveSecretPlaceholders(
              "user:{secret:network.serverAuth}@localhost", &error)
          == "user:not-in-settings@localhost",
          "inline-secret-placeholder-resolves-at-use-boundary");
    check(preserved.resolveSecretPlaceholders(
              "{secret:missing.reference}", &error).isEmpty()
          && error.contains("missing.reference"),
          "missing-inline-secret-is-reported-by-reference");
    check(preserved.save(&error), "profile-local-secret-save");
    QFile settingsJson(settingsFile);
    check(settingsJson.open(QIODevice::ReadOnly), "open-settings-for-secret-check");
    check(!settingsJson.readAll().contains("not-in-settings"), "secret-not-written-to-settings-json");

    QTemporaryDir duplicateWorkspace;
    const QString previousWorkingDirectory = QDir::currentPath();
    check(duplicateWorkspace.isValid()
          && QDir::setCurrent(duplicateWorkspace.path()), "duplicate-profile-workspace");
    SettingsManager duplicateSource;
    DraftSettingsCatalog::registerDefinitions(duplicateSource.registry());
    SettingsProfileSelection duplicateSelection;
    duplicateSelection.profileName = "source";
    check(duplicateSource.initialize(duplicateSelection, &error), "duplicate-source-profile");
    QFile forkFile(QDir(duplicateSource.profileDirectory()).filePath("fork-data.txt"));
    check(forkFile.open(QIODevice::WriteOnly) && forkFile.write("preserve me") > 0,
          "duplicate-source-extra-file");
    forkFile.close();
    QString duplicatedSettings;
    check(SettingsProfile::duplicatePortableProfile(
              duplicateSource.settingsFilePath(), "clone", &duplicatedSettings, &error),
          "duplicate-whole-profile-directory");
    QFile clonedFile(QDir(QFileInfo(duplicatedSettings).absolutePath()).filePath("fork-data.txt"));
    check(clonedFile.open(QIODevice::ReadOnly) && clonedFile.readAll() == "preserve me",
          "duplicate-preserves-extra-files");
    clonedFile.close();
    QFile clonedSettings(duplicatedSettings);
    check(clonedSettings.open(QIODevice::ReadOnly), "open-duplicated-settings");
    const QJsonObject clonedDocument = QJsonDocument::fromJson(clonedSettings.readAll()).object();
    check(clonedDocument.value("profile").toObject().value("id").toString() == "clone"
          && clonedDocument.value("profile").toObject().value("name").toString() == "clone",
          "duplicate-updates-profile-identity");
    check(!SettingsProfile::duplicatePortableProfile(
              duplicateSource.settingsFilePath(), "CLONE", nullptr, &error),
          "duplicate-rejects-case-insensitive-collision");
    check(QDir::setCurrent(previousWorkingDirectory), "restore-working-directory");

    qInfo() << "[tests:settings] cases=" << (passed + failed)
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
