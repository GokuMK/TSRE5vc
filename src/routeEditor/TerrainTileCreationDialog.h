/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TERRAINTILECREATIONDIALOG_H
#define TERRAINTILECREATIONDIALOG_H

#include <QDialog>
#include <tsre/world/TerrainGridLayout.h>

class Route;
class TerrainProfileSelector;

class TerrainTileCreationDialog : public QDialog {
public:
    explicit TerrainTileCreationDialog(QWidget *parent = nullptr);

    static void showForTile(QWidget *parent, Route *route, int worldX, int worldZ);

private:
    TerrainHeightProfile selectedProfile() const;
    int selectedPatchCount() const;

    TerrainProfileSelector *profileSelector;
};

#endif /* TERRAINTILECREATIONDIALOG_H */
