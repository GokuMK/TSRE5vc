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
#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/world/Route.h>
#include <tsre/world/TerrainLib.h>

TerrainTileCreationDialog::TerrainTileCreationDialog(QWidget *parent)
    : QDialog(parent),
      profileSelector(new TerrainProfileSelector(this, true)) {
    setWindowTitle("Create or replace detailed terrain");

    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *title = new QLabel("<b>Detailed terrain tile</b>", this);
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

void TerrainTileCreationDialog::showForTile(
        QWidget *parent, Route *route, int worldX, int worldZ) {
    if (!Game::writeEnabled) {
        QMessageBox::information(
                parent, "Terrain creation disabled",
                "Route writing is disabled. Enable route writing before creating or replacing terrain.");
        return;
    }

    TerrainTileCreationDialog dialog(parent);
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
