/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TRKWINDOW_H
#define	TRKWINDOW_H

#include <QtWidgets>
#include <tsre/world/TerrainLod.h>
#include <tsre/gui/UnitSpinBox.h>

class Trk;

class TrkWindow : public QDialog {
    Q_OBJECT
public:
    enum class Mode {
        ExistingRoute,
        NewRouteTemplate
    };

    explicit TrkWindow(Mode mode = Mode::ExistingRoute);
    virtual ~TrkWindow();
    Trk *trk = NULL;
    
    int exec();
    
signals:    
    
public slots:
    void bokEnabled();
    void bcancelEnabled();
    void envNameEnabled(QString item);
    void terrainLodEnabled();
    
private:
    Mode mode;
    QLabel imageGraphic;
    QLabel imageLoad;
    
    QLineEdit idName;
    QLineEdit displayName;
    QPlainTextEdit description;
    QLineEdit routeName;
    QComboBox electrified;
    //d int mountains;
    // In the units the editors show (task core si-units).
    UnitSpinBox overheadWireHeight{Units::Quantity::Length};
    UnitSpinBox speedLimit{Units::Quantity::Speed};
    QDoubleSpinBox terrainErrorScale;
    QDoubleSpinBox enclosedAmbient;
    QLineEdit startTileX;
    QLineEdit startTileZ;
    QLineEdit startpX;
    QLineEdit startpZ;
    QComboBox milepostUnitsKilometers;
    QDoubleSpinBox maxLineVoltage;
    UnitSpinBox tempRestrictedSpeed{Units::Quantity::Speed};
    QComboBox envName;
    QLineEdit envValue;
    QComboBox iList;
    QPushButton iCopy;
    QPushButton iPaste;
    QLabel terrainLodSummary;
    QPushButton terrainLodEdit;
    QVector<TerrainLodLevel> pendingTerrainLodLevels;
};

#endif	/* TRKWINDOW_H */

