/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <routeEditor/TerrainTileCreationDialog.h>
#include <routeEditor/TerrainProfileSelector.h>

#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QStringList>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/world/Route.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/world/QuadTree.h>
#include <tsre/world/TerrainInfo.h>
#include <tsre/world/TFile.h>

TerrainTileCreationDialog::TerrainTileCreationDialog(QWidget *parent)
    : QDialog(parent),
      profileSelector(new TerrainProfileSelector(this, true)) {
    setWindowTitle("Create or replace detailed terrain");

    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *statusTitle = new QLabel("<b>Current status</b>", this);
    statusTitle->setStyleSheet(QString("QLabel { color: %1; }").arg(Game::StyleMainLabel));
    layout->addWidget(statusTitle);
    terrainStatus = new QLabel(this);
    terrainStatus->setTextFormat(Qt::PlainText);
    terrainStatus->setWordWrap(true);
    quadTreeStatus = new QLabel(this);
    layout->addWidget(terrainStatus);
    layout->addWidget(quadTreeStatus);
    layout->addSpacing(8);
    QLabel *title = new QLabel("<b>Detailed terrain tile</b>", this);
    title->setStyleSheet(QString("QLabel { color: %1; }").arg(Game::StyleMainLabel));
    QLabel *warning = new QLabel(
            "Choose the detailed-terrain heightmap resolution and patch grid for "
            "this location. This B-key tool creates missing terrain or explicitly "
            "replaces existing terrain. Non-standard profiles are experimental; "
            "compatibility for the selected layout is shown below. The independent "
            "2048 m World tile is created only when it is missing.", this);
    warning->setWordWrap(true);
    profileSelector->setSelection(Game::defaultTerrainHeightProfile,
                                  Game::defaultTerrainPatchCount);

    QDialogButtonBox *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    layout->addWidget(title);
    layout->addWidget(warning);
    layout->addWidget(profileSelector);
    layout->addWidget(buttons);
    setFixedWidth(TerrainProfileSelector::SelectionDialogWidth);
}

TerrainHeightProfile TerrainTileCreationDialog::selectedProfile() const {
    return profileSelector->selectedProfile();
}

int TerrainTileCreationDialog::selectedPatchCount() const {
    return profileSelector->selectedPatchCount();
}

void TerrainTileCreationDialog::showStatus(int worldX, int worldZ) {
    // World Z and the QuadTree/file naming Z have opposite signs.
    const int terrainZ = -worldZ;
    TerrainInfo info;
    QuadTree *tree = Game::terrainLib->getQuadTreeDetailed();
    if (tree) tree->fillTerrainInfo(worldX, terrainZ, &info);
    quadTreeStatus->setText(!tree ? "QuadTree: unavailable (simple terrain mode)"
            : info.name.isEmpty() ? "QuadTree: not populated"
            : QString("QuadTree: populated at size %1 m").arg(info.level * 2048));

    QStringList names;
    if (!info.name.isEmpty()) names << info.name;
    // A missing populated node does not imply a missing descriptor. Probe each
    // enclosing MSTS grid size using the same naming logic as the real tree.
    // These temporary nodes are never attached to, or saved in, the route tree.
    for (int level = 1, prefix = 1; level <= 256; level *= 2, prefix = 1 - prefix) {
        const int span = level * 2;
        const int x = int(std::floor(double(worldX) / span)) * span;
        const int z = int(std::floor(double(terrainZ) / span)) * span;
        QuadTree::QuadTile node(level, prefix, x, z);
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) node.populated[i][j] = true;
        const QString name = node.getMyName(worldX, terrainZ);
        if (!names.contains(name)) names << name;
    }
    const QString directory = Game::root + "/routes/" + Game::route + "/tiles/";
    QStringList descriptions, paths;
    for (const QString &name : names) {
        const QString path = directory + name + ".t";
        if (!QFileInfo(path).isFile()) continue;
        TFile::LayoutInfo layout;
        if (TFile::readLayoutInfo(path, layout)) {
            descriptions << QString("exists, size %1 m, type: %2 samples / %3 patches (%4)")
                    .arg(double(layout.samples) * layout.spacing)
                    .arg(layout.samples).arg(layout.patches).arg(name);
        } else {
            descriptions << QString("exists, unreadable layout (%1)").arg(name);
        }
        paths << path;
    }
    terrainStatus->setText("Terrain tile: " + (descriptions.isEmpty()
            ? QString("does not exist") : descriptions.join("\nTerrain tile: ")));
    terrainStatus->setToolTip(paths.join('\n'));
}

void TerrainTileCreationDialog::showForTile(
        QWidget *parent, Route *route, int worldX, int worldZ) {
    if (!Game::writeEnabled) {
        QMessageBox::information(
                parent, "Terrain creation disabled",
                "Route writing is disabled. Enable route writing before creating or replacing terrain.");
        return;
    }

    TerrainTileCreationDialog dialog(parent);
    dialog.showStatus(worldX, worldZ);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const int terrainZ = -worldZ;
    const TerrainHeightProfile profile = dialog.selectedProfile();
    const int patchCount = dialog.selectedPatchCount();
    const bool overwrite = Game::terrainLib->hasDetailedTerrain(worldX, terrainZ);
    if (overwrite) {
        const QMessageBox::StandardButton answer = QMessageBox::warning(
                parent, "Replace detailed terrain?",
                "Detailed terrain already exists here. Replace its descriptor and "
                "heightmap with the selected profile? The existing World file will be preserved.",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    if (!Game::terrainLib->saveEmpty(
            worldX, terrainZ, profile, patchCount, overwrite)) {
        QMessageBox::critical(parent, "Terrain creation failed",
                              "The selected terrain profile could not be created. "
                              "See the log for the unsupported-layout or file error.");
        return;
    }

    route->ensureWorldTile(worldX, worldZ);
    Game::terrainLib->setDetailedAsCurrent();
    if (!Game::terrainLib->reload(worldX, worldZ)) {
        QMessageBox::critical(parent, "Terrain reload failed",
                              "The terrain files were created but could not be reloaded.");
        return;
    }

    if (Game::autoGeoTerrain) {
        float pos[3];
        Vec3::set(pos, 0, 0, 0);
        Game::terrainLib->setHeightFromGeo(worldX, worldZ, pos);
    }
}
