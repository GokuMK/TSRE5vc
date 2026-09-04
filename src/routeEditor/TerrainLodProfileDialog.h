/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef TERRAINLODPROFILEDIALOG_H
#define TERRAINLODPROFILEDIALOG_H

#include <QDialog>
#include <QVector>

#include <tsre/world/TerrainLod.h>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

class TerrainLodProfileDialog : public QDialog {
public:
    explicit TerrainLodProfileDialog(QWidget *parent = nullptr);

    void setLevels(const QVector<TerrainLodLevel> &levels);
    QVector<TerrainLodLevel> levels() const;
    static QString summary(const QVector<TerrainLodLevel> &levels);

private:
    QCheckBox *enabled = nullptr;
    QTableWidget *table = nullptr;
    QLabel *validation = nullptr;
    QPushButton *okButton = nullptr;
    QPushButton *addButton = nullptr;
    QPushButton *removeButton = nullptr;

    void addLevelRow(const TerrainLodLevel &level);
    void addSuggestedLevel();
    void removeSelectedLevel();
    void synchronizeSpacings();
    void updateEnabledState();
    void validateInput();
};

#endif // TERRAINLODPROFILEDIALOG_H
