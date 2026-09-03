/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TERRAINPROFILESELECTOR_H
#define TERRAINPROFILESELECTOR_H

#include <QWidget>
#include <tsre/world/TerrainGridLayout.h>

class QComboBox;
class QLabel;

class TerrainProfileSelector : public QWidget {
public:
    static constexpr int SelectionDialogWidth = 720;

    explicit TerrainProfileSelector(QWidget *parent = nullptr);

    TerrainHeightProfile selectedProfile() const;
    int selectedPatchCount() const;
    void setSelection(TerrainHeightProfile profile, int patches);

    static bool choose(QWidget *parent, const QString &title,
                       TerrainHeightProfile &profile, int &patches);

private:
    void updatePatchAvailability();
    void updateDescription();
    void setCompatibility(QLabel *name, QLabel *status, const QString &text);
    QString tsreCompatibility(const TerrainGridLayout &layout) const;
    QString msts18Compatibility(const TerrainGridLayout &layout) const;
    QString msts19Compatibility(const TerrainGridLayout &layout) const;
    QString ortsMasterCompatibility(const TerrainGridLayout &layout) const;
    QString ortsUnstableCompatibility(const TerrainGridLayout &layout) const;

    QComboBox *profiles;
    QComboBox *patches;
    QLabel *profileDescription;
    QLabel *tsreName;
    QLabel *msts18Name;
    QLabel *msts19Name;
    QLabel *ortsMasterName;
    QLabel *ortsUnstableName;
    QLabel *tsreStatus;
    QLabel *msts18Status;
    QLabel *msts19Status;
    QLabel *ortsMasterStatus;
    QLabel *ortsUnstableStatus;
};

#endif /* TERRAINPROFILESELECTOR_H */
