#include "TerrainLibQt.h"
#include "QuadTree.h"
#include "TerrainInfo.h"
#include "Terrain.h"
#include <tsre/Game.h>
#include <tsre/ErrorMessage.h>
#include <tsre/ErrorMessagesLib.h>
#include <settings/SettingsAccess.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <QDir>
#include <QFileInfo>

void TerrainLibQt::loadRecoveryTree(bool low) {
    auto *tree = new QuadTree(low);
    auto *&slot = low ? quadTreeLo : quadTree;
    // Existing caches are invalidated by loadQuadTree; do not retain stale repair actions.
    delete slot;
    slot = tree;
    recoveryMessages[low] = nullptr;
    recoveryNeedsScan[low] = false;
    const bool useSaved = Settings::boolean("core.advanced.useQuadTree");
    QString error;
    const QString directory = recoveryRoutePath + (low ? "/LO_TILES" : "/TILES");
    const QString index = ContentPath::normalize(recoveryRoutePath + (low ? "/TD/lo_td_idx.dat" : "/TD/td_idx.dat"));
    const auto status = useSaved ? tree->loadChecked(recoveryRoutePath + "/TD", error)
                                : (QFileInfo::exists(index) ? QuadTree::LoadStatus::Loaded : QuadTree::LoadStatus::Missing);
    if (useSaved && status == QuadTree::LoadStatus::Loaded) return;
    QStringList issues;
    int count = 0;
    if (!useSaved || status == QuadTree::LoadStatus::Missing)
        tree->reconstruct(directory, issues, count);
    else {
        tree->makeTemporary(); // B/create must not overwrite rejected metadata either.
        recoveryNeedsScan[low] = true;
    }
    // Missing distant metadata is normal if the route contains no distant terrain.
    if (low && status == QuadTree::LoadStatus::Missing && count == 0 && issues.isEmpty()) {
        if (useSaved) {
            // A route without distant terrain is healthy. Its first distant tile
            // can use ordinary creation rather than an unadoptable silent recovery.
            delete tree;
            slot = new QuadTree(true);
        }
        return;
    }
    QString domain;
    if (low) {
        //% "Distant terrain"
        domain = qtTrId("route.errors.qt.distant");
    } else {
        //% "Detailed terrain"
        domain = qtTrId("route.errors.qt.detailed");
    }
    //% "%1: QuadTree recovery"
    auto *message = new ErrorMessage(status == QuadTree::LoadStatus::Loaded
            ? ErrorMessage::Type_Warning : ErrorMessage::Type_Error,
        ErrorMessage::Source_World, qtTrId("route.errors.qt.recovery").arg(domain));
    if (!recoveryNeedsScan[low]) {
        //% "Using a temporary QuadTree reconstructed from %1 terrain descriptors. Fix adopts it as modified route content; the ordinary Save dialog is still required to write it."
        message->action = qtTrId("route.errors.qt.temporary").arg(count);
        if (!useSaved && status == QuadTree::LoadStatus::Loaded) {
            //% "The saved QuadTree was deliberately ignored, not validated."
            message->action += "\n" + qtTrId("route.errors.qt.ignored");
        }
    } else {
        //% "Existing QuadTree metadata could not be loaded. Fix reconstructs it from terrain descriptors. Rendering is not switched to reconstructed data until Fix is chosen."
        message->action = qtTrId("route.errors.qt.invalid") + "\n" + error;
    }
    //% "Reconstruction cannot recover populated entries without terrain files. Existing metadata will be backed up when the adopted tree is saved."
    message->action += "\n" + qtTrId("route.errors.qt.limitations");
    if (!issues.isEmpty()) message->action += "\n" + issues.join('\n');
    const std::weak_ptr<int> session = recoverySession;
    const QString routePath = recoveryRoutePath;
    message->canFix = [this, session, routePath] {
        return !session.expired() && Game::terrainLib == this && Game::writeEnabled
            && routePath == Game::root + "/ROUTES/" + Game::route;
    };
    message->fix = [this, low](QString &result) { return adoptRecoveredTree(low, result); };
    recoveryMessages[low] = message;
    // Do not auto-replace an existing invalid or deliberately ignored tree.
    if (status == QuadTree::LoadStatus::Missing && issues.isEmpty() &&
            Settings::boolean("core.route.validation.autoFix") && message->canFix()) {
        QString result;
        message->applyFix(result);
    }
    ErrorMessagesLib::PushErrorMessage(message);
    if (status != QuadTree::LoadStatus::Loaded) {
        ErrorMessagesLib::RequestShowMessage(message, [this, session, routePath] {
            return !session.expired() && Game::terrainLib == this
                && routePath == Game::root + "/ROUTES/" + Game::route;
        });
    }
}

bool TerrainLibQt::adoptRecoveredTree(bool low, QString &result) {
    auto *&tree = low ? quadTreeLo : quadTree;
    if (!tree || !Game::writeEnabled) {
        //% "Route writing is disabled."
        result = qtTrId("route.errors.qt.write.disabled"); return false;
    }
    if (recoveryNeedsScan[low]) {
        // Corrupt saved tree: only build/activate on explicit Fix, not startup.
        auto replacement = std::make_unique<QuadTree>(low);
        QStringList issues; int count;
        replacement->reconstruct(recoveryRoutePath + (low ? "/LO_TILES" : "/TILES"), issues, count);
        if (count == 0) {
            //% "No valid terrain descriptors found. %1"
            result = qtTrId("route.errors.qt.no.descriptors").arg(issues.join('\n')); return false;
        }
        const bool current = currentQuadTree == tree;
        auto &cache = low ? terrainQtLo : terrainQt;
        for (auto info : cache) if (info && info->t) terrainAvailabilityChanged(info->t);
        cache.clear(); // Existing Terrain lifetime follows the terrain cache's current policy.
        delete tree;
        tree = replacement.release();
        recoveryNeedsScan[low] = false;
        if (current) currentQuadTree = tree;
        if (!issues.isEmpty()) result = issues.join('\n') + "\n";
    }
    tree->adoptRecovery();
    //% "QuadTree adopted in memory and marked modified. Confirm the ordinary Save dialog to write it; no metadata has been written yet."
    result += qtTrId("route.errors.qt.adopted");
    return true;
}

void TerrainLibQt::saveRecoveredTrees() {
    if (!Game::writeEnabled) return;
    for (int low = 0; low < 2; ++low) {
        auto *tree = low ? quadTreeLo : quadTree;
        if (!tree || !tree->isRecovery() || !tree->isModified()) continue;
        QString error;
        const bool ok = tree->saveChecked(recoveryRoutePath + "/TD", error);
        QString description;
        if (ok) {
            //% "Reconstructed QuadTree saved."
            description = qtTrId("route.errors.qt.saved");
        } else {
            //% "Could not save reconstructed QuadTree. It remains modified for retry."
            description = qtTrId("route.errors.qt.save.failed");
        }
        ErrorMessagesLib::PushErrorMessage(new ErrorMessage(
            ok ? ErrorMessage::Type_Info : ErrorMessage::Type_Error,
            ErrorMessage::Source_World, description, error));
    }
}
