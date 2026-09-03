#include <tsre/tests/SettingsTestSuite.h>

#include <settings/SettingsRegistration.h>
#include <settings/SettingsAccess.h>
#include <settings/SettingsManager.h>
#include <settings/SettingsProfile.h>
#include <settings/ui/SettingsDialog.h>
#include <tsre/Game.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
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
    SettingsRegistration::registerAll(manager.registry());
    check(manager.registry().definitions().size() == 77,
          "phase2b-catalog-removes-inactive-and-one-shot-settings");
    check(manager.registry().definition("core.system.useWorkingDirectory") == nullptr
          && manager.registry().definition("core.system.warningBox") == nullptr,
          "inactive-legacy-settings-are-not-generated");
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
    const SettingsDefinition *terrainMesh =
            manager.registry().definition("core.rendering.terrainMesh");
    check(terrainMesh && terrainMesh->type == SettingType::Enum
          && terrainMesh->defaultValue.toString() == "paged"
          && terrainMesh->options.size() == 2
          && terrainMesh->options[0].value.toString() == "legacy"
          && terrainMesh->options[0].name == "Precomputed / Legacy"
          && terrainMesh->options[1].value.toString() == "paged"
          && terrainMesh->options[1].name == "On GPU / Experimental"
          && terrainMesh->apply == "routeReload",
          "paged-terrain-backend-is-default-and-experimental");
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
            manager.registry().definition("core.route.saving.trackDatabase");
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
    const SettingsDefinition *accent =
            manager.registry().definition("core.interface.accentColor");
    const SettingsDefinition *systemTheme =
            manager.registry().definition("core.system.systemTheme");
    const SettingsDefinition *procedural =
            manager.registry().definition("core.track.proceduralMode");
    const SettingsDefinition *snapRotationOnly =
            manager.registry().definition("core.track.snapRotationOnly");
    const SettingsDefinition *snapRadius =
            manager.registry().definition("core.track.snapRadius");
    check(accent && accent->type == SettingType::Color
          && accent->group == "interface" && accent->subgroup == "appearance"
          && accent->defaultValue.toString() == "#770000"
          && accent->description.contains("only", Qt::CaseInsensitive)
          && systemTheme && systemTheme->group == "interface"
          && systemTheme->subgroup == "appearance"
          && procedural && procedural->defaultValue.toString() == "Enabled"
          && procedural->group == "editing" && procedural->subgroup == "track"
          && snapRotationOnly && snapRotationOnly->group == "editing"
          && snapRotationOnly->subgroup == "objects"
          && snapRotationOnly->apply == "routeReload"
          && snapRadius && snapRadius->group == "editing"
          && snapRadius->subgroup == "objects"
          && snapRadius->defaultValue.toDouble() == 20.0
          && snapRadius->minimum == 1.0 && snapRadius->maximum == 999.0
          && snapRadius->apply == "routeReload",
          "category-review-adds-accent-and-enables-procedural-track-default");
    check(manager.registry().definition("core.editing.createMissingRoute") == nullptr
          && manager.registry().definition("core.terrain.loadAllWorldFiles") == nullptr
          && manager.registry().definition("core.consist.preferOpenRailsEng") == nullptr
          && manager.registry().definition("core.startup.createMissingRoute")
          && manager.registry().definition("core.route.loading.preloadAllWorldFiles")
          && manager.registry().definition("core.content.loading.preferOpenRailsEng"),
          "category-review-replaces-provisional-keys");
    const SettingsDefinition *fpsLimit =
            manager.registry().definition("core.system.fpsLimit");
    const SettingsDefinition *geoData =
            manager.registry().definition("core.paths.geoData");
    const SettingsDefinition *playerMode =
            manager.registry().definition("core.network.playerMode");
    const SettingsDefinition *objectsToRemove =
            manager.registry().definition("core.editing.objectsToRemove");
    const SettingsDefinition *externalLocomotive =
            manager.registry().definition("core.network.useNetworkEngine");
    const SettingsDefinition *writeEnabled =
            manager.registry().definition("core.route.saving.enabled");
    const SettingsDefinition *deleteTrackWatermarks =
            manager.registry().definition("core.editing.deleteTrackWatermarks");
    const SettingsDefinition *deleteViewDbSpheres =
            manager.registry().definition("core.editing.deleteViewDbSpheres");
    const SettingsDefinition *sortTileObjects =
            manager.registry().definition("core.editing.sortTileObjects");
    check(fpsLimit && fpsLimit->group == "rendering"
          && fpsLimit->subgroup == "performance"
          && geoData && geoData->group == "maps" && geoData->subgroup == "geodata"
          && playerMode && playerMode->group == "advanced"
          && playerMode->subgroup == "simulation" && playerMode->advanced
          && externalLocomotive && externalLocomotive->group == "advanced"
          && externalLocomotive->subgroup == "simulation"
          && objectsToRemove && objectsToRemove->group == "advanced"
          && objectsToRemove->subgroup == "safetyRecovery"
          && objectsToRemove->advanced
          && writeEnabled && writeEnabled->group == "advanced"
          && writeTrackDatabase->group == "advanced"
          && deleteTrackWatermarks && deleteTrackWatermarks->group == "advanced"
          && deleteTrackWatermarks->subgroup == "safetyRecovery"
          && deleteViewDbSpheres && deleteViewDbSpheres->group == "advanced"
          && deleteViewDbSpheres->subgroup == "safetyRecovery"
          && sortTileObjects && sortTileObjects->group == "advanced"
          && sortTileObjects->subgroup == "safetyRecovery",
          "category-review-places-performance-geodata-and-dangerous-settings");
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
    check(QFile::exists(settingsFile) && manager.settingsArray().size() == 77,
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
    check(manager.settingObject("core.editing.objectsToRemove")
              .value("value").isArray()
          && manager.settingObject("core.editing.objectsToRemove")
              .value("value").toArray().isEmpty()
          && manager.runtimeStringList("core.editing.objectsToRemove").isEmpty(),
          "empty-string-list-default-is-a-genuinely-empty-array");
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
          && manager.settingObject("core.route.saving.trackDatabase")
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

    bool allRuntimeSettingsClaimed = true;
    SettingsManager &applicationSettings = SettingsManager::instance();
    for (const SettingsDefinition &definition : applicationSettings.registry().definitions()) {
        if (applicationSettings.supportState(definition.key)
                != SettingsManager::Supported) {
            allRuntimeSettingsClaimed = false;
            break;
        }
    }
    check(allRuntimeSettingsClaimed,
          "phase2b-all-built-in-settings-have-runtime-support-claims");
    check(Settings::variant("core.rendering.shadow.primaryMapSize",
                            SettingType::Enum).toInt() > 0
          && Settings::variant("core.rendering.shadow.distantMapSize",
                               SettingType::Enum).toInt() > 0,
          "renderer-reads-shadow-map-sizes-as-numeric-enums");
    check(applicationSettings.supportState("core.rendering.shadow.primaryMapSize")
              == SettingsManager::Supported
          && applicationSettings.supportState("core.rendering.shadow.distantMapSize")
              == SettingsManager::Supported,
          "shadow-map-size-enums-have-runtime-support-claims");
    SettingsDialog supportDialog(&applicationSettings);
    QCheckBox *nearShadowSupport = supportDialog.findChild<QCheckBox *>(
                "setting-support:core.rendering.shadow.primaryMapSize");
    QCheckBox *distantShadowSupport = supportDialog.findChild<QCheckBox *>(
                "setting-support:core.rendering.shadow.distantMapSize");
    check(nearShadowSupport && nearShadowSupport->isChecked()
          && distantShadowSupport && distantShadowSupport->isChecked(),
          "settings-dialog-shows-shadow-map-sizes-as-supported");
    QAction *copyShadowKeyValue = supportDialog.findChild<QAction *>(
                "copy-setting-key-value:core.rendering.shadow.primaryMapSize");
    QAction *pasteSettingKeyValue = supportDialog.findChild<QAction *>(
                "paste-setting-key-value");
    if (copyShadowKeyValue)
        copyShadowKeyValue->trigger();
    const QString copiedKeyValue = QApplication::clipboard()->text();
    const QString copiedPrefix =
            "core.rendering.shadow.primaryMapSize : ";
    QJsonParseError clipboardParseError;
    const QJsonDocument clipboardValue = QJsonDocument::fromJson(
                QString("[%1]").arg(copiedKeyValue.mid(copiedPrefix.size())).toUtf8(),
                &clipboardParseError);
    check(copyShadowKeyValue && clipboardParseError.error == QJsonParseError::NoError
          && copiedKeyValue.startsWith(copiedPrefix)
          && clipboardValue.isArray()
          && clipboardValue.array().size() == 1
          && clipboardValue.array().first().toInt() > 0,
          "settings-row-menu-copies-human-readable-key-and-json-value");
    QApplication::clipboard()->setText(
                "core.rendering.shadow.primaryMapSize : 4096");
    if (pasteSettingKeyValue)
        pasteSettingKeyValue->trigger();
    SettingsManager *dialogProfile = supportDialog.findChild<SettingsManager *>();
    check(pasteSettingKeyValue && dialogProfile
          && dialogProfile->value("core.rendering.shadow.primaryMapSize").toInt() == 4096,
          "edit-menu-pastes-matching-setting-value");

    QJsonObject runtimeApplyDocument = applicationSettings.document();
    QJsonArray runtimeApplySettings = runtimeApplyDocument.value("settings").toArray();
    const float originalCameraSpeed = Game::cameraSpeedStd;
    for (int i = 0; i < runtimeApplySettings.size(); ++i) {
        QJsonObject setting = runtimeApplySettings.at(i).toObject();
        if (setting.value("key").toString() == "core.camera.speed.standard") {
            setting["value"] = 7.5;
            runtimeApplySettings.replace(i, setting);
            break;
        }
    }
    runtimeApplyDocument["settings"] = runtimeApplySettings;
    check(applicationSettings.applyProfileToRuntime(runtimeApplyDocument, nullptr, &error)
          && qFuzzyCompare(Game::cameraSpeedStd, 7.5f),
          "editor-runtime-apply-updates-dynamic-cache-without-saving");
    check(applicationSettings.applyProfileToRuntime(
              applicationSettings.document(), nullptr, &error)
          && qFuzzyCompare(Game::cameraSpeedStd, originalCameraSpeed),
          "runtime-apply-can-restore-loaded-profile-value");

    QJsonObject shadowApplyDocument = applicationSettings.document();
    QJsonArray shadowApplySettings = shadowApplyDocument.value("settings").toArray();
    for (int i = 0; i < shadowApplySettings.size(); ++i) {
        QJsonObject setting = shadowApplySettings.at(i).toObject();
        if (setting.value("key").toString() == "core.rendering.shadows.enabled") {
            setting["value"] = false;
            shadowApplySettings.replace(i, setting);
            break;
        }
    }
    shadowApplyDocument["settings"] = shadowApplySettings;
    const int originalShadowsEnabled = Game::shadowsEnabled;
    const bool originalMstsShadows = Game::mstsShadows;
    Game::mstsShadows = true;
    check(applicationSettings.applyProfileToRuntime(shadowApplyDocument, nullptr, &error)
          && Game::shadowsEnabled == 0 && Game::mstsShadows,
          "master-shadow-setting-does-not-change-msts-shadow-filter");
    check(applicationSettings.applyProfileToRuntime(
              applicationSettings.document(), nullptr, &error)
          && Game::shadowsEnabled == originalShadowsEnabled,
          "master-shadow-setting-restores-loaded-profile-value");
    Game::mstsShadows = originalMstsShadows;

    QJsonObject custom{{"key", "fork.weather.enabled"}, {"name", "Fork weather"},
        {"description", "A setting understood by another fork."}, {"group", "advanced"},
        {"type", "bool"}, {"value", true}, {"default", false}, {"apply", "restart"}};
    check(manager.addSettingObject(custom, &error), "custom-setting-add");
    check(manager.supportState("fork.weather.enabled") == SettingsManager::Unsupported,
          "custom-setting-unsupported");
    check(manager.save(&error), "custom-setting-save");

    SettingsManager reloaded;
    SettingsRegistration::registerAll(reloaded.registry());
    check(reloaded.loadFile(settingsFile, &error), "profile-reload");
    check(reloaded.value("fork.weather.enabled").toBool(), "custom-setting-preserved");

    QJsonObject customized = reloaded.settingObject(threadedKey);
    customized["description"] = "Profile-owned description";
    check(reloaded.replaceSettingObject(threadedKey, customized, &error)
          && reloaded.save(&error), "stored-metadata-customization");
    SettingsManager preserved;
    SettingsRegistration::registerAll(preserved.registry());
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
    SettingsRegistration::registerAll(recoverable.registry());
    check(recoverable.loadFile(invalidFile.fileName(), &error),
          "per-setting-error-remains-loadable");
    const SettingsDefinition *invalidDefinition =
            recoverable.registry().definition(invalidKey);
    check(invalidDefinition
          && recoverable.value(invalidKey) == invalidDefinition->defaultValue,
          "invalid-known-value-uses-registry-default");
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

    SettingsManager runtimeManager;
    SettingsRegistration::registerAll(runtimeManager.registry());
    SettingsProfileSelection runtimeSelection;
    runtimeSelection.settingsFile = settingsFile;
    runtimeSelection.startupOverrides.insert(threadedKey, "false");
    runtimeSelection.commandLineOverrides.insert(threadedKey, "true");
    check(runtimeManager.initialize(runtimeSelection, &error),
          "runtime-layers-initialize");
    check(runtimeManager.runtimeBool(threadedKey)
          && runtimeManager.runtimeValueSource(threadedKey)
             == SettingsManager::CommandLine,
          "terminal-override-wins-runtime-precedence");
    check(runtimeManager.settingObject(threadedKey).value("value").toBool(),
          "launch-overrides-do-not-mutate-profile-json");
    QJsonObject applyDocument = runtimeManager.document();
    QJsonArray applySettings = applyDocument.value("settings").toArray();
    for (int i = 0; i < applySettings.size(); ++i) {
        QJsonObject setting = applySettings.at(i).toObject();
        if (setting.value("key").toString() == threadedKey) {
            setting["value"] = false;
            applySettings.replace(i, setting);
            break;
        }
    }
    applyDocument["settings"] = applySettings;
    QStringList appliedKeys;
    check(runtimeManager.applyProfileToRuntime(applyDocument, &appliedKeys, &error)
          && appliedKeys.contains(threadedKey)
          && runtimeManager.runtimeBool(threadedKey),
          "profile-apply-preserves-terminal-override");
    check(runtimeManager.setSessionValue(threadedKey, false, &error)
          && !runtimeManager.runtimeBool(threadedKey)
          && runtimeManager.runtimeValueSource(threadedKey)
             == SettingsManager::ForcedSession,
          "forced-session-value-is-separate-runtime-layer");
    runtimeManager.clearSessionValue(threadedKey);
    check(runtimeManager.runtimeBool(threadedKey)
          && runtimeManager.runtimeValueSource(threadedKey)
             == SettingsManager::CommandLine,
          "clearing-session-value-restores-launch-override");
    const QString createMissingRouteKey =
            "core.startup.createMissingRoute";
    check(runtimeManager.setSessionValue(createMissingRouteKey, true, &error)
          && runtimeManager.runtimeBool(createMissingRouteKey)
          && runtimeManager.runtimeValueSource(createMissingRouteKey)
             == SettingsManager::ForcedSession,
          "new-route-action-can-override-startup-creation-policy-for-session");
    runtimeManager.clearSessionValue(createMissingRouteKey);
    SettingsManager invalidOverrideManager;
    SettingsRegistration::registerAll(invalidOverrideManager.registry());
    SettingsProfileSelection invalidOverrideSelection;
    invalidOverrideSelection.settingsFile = settingsFile;
    invalidOverrideSelection.commandLineOverrides.insert(
                "core.rendering.tileRadius", "not-an-integer");
    check(!invalidOverrideManager.initialize(invalidOverrideSelection, &error)
          && error.contains("tileRadius"),
          "invalid-launch-override-fails-clearly");

    QTemporaryDir duplicateWorkspace;
    const QString previousWorkingDirectory = QDir::currentPath();
    check(duplicateWorkspace.isValid()
          && QDir::setCurrent(duplicateWorkspace.path()), "duplicate-profile-workspace");
    SettingsManager duplicateSource;
    SettingsRegistration::registerAll(duplicateSource.registry());
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

    check(SettingsRegistration::addProvider(
              "test.fork", [](SettingsRegistry &registry, QString *providerError) {
        if (!registry.defineGroup(
                    {"forkTest", "Fork test", "Test extension settings.", 500, {}},
                    providerError))
            return false;
        return registry.define(
                    SettingsDefinition::boolean("fork.test.enabled", true)
                        .withName("Fork test").withDescription("Extension setting")
                        .inGroup("forkTest"), providerError);
    }, &error), "extension-provider-registers");
    SettingsManager extensionManager;
    check(SettingsRegistration::registerAll(extensionManager.registry(), &error)
          && extensionManager.registry().definition("fork.test.enabled"),
          "extension-provider-contributes-definition");

    qInfo() << "[tests:settings] cases=" << (passed + failed)
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
