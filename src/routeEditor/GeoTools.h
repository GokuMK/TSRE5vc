/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef GEOTOOLS_H
#define	GEOTOOLS_H

#include <QtWidgets>
#include <tsre/world/Route.h>
#include <QMap>
#include <QPair>

class Coords;

class ToolRegistry;

class GeoTools : public QWidget{
    Q_OBJECT
public:
    // The tools, to enable only the buttons of tools the view mode supports.
    void setToolRegistry(const ToolRegistry *registry) { toolRegistry = registry; }
    GeoTools(QString name);
    virtual ~GeoTools();
    
public slots:
    void mapTileShowToolEnabled(bool val);
    void mapTileLoadToolEnabled(bool val);
    void imageryTileLoadToolEnabled(bool val);
    void heightTileLoadToolEnabled(bool val);
    void makeTileTextureToolEnabled(bool val);
    void removeTileTextureToolEnabled(bool val);
    void msg(QString text, QString val);
    void chAutoCreateTileEnabled(int state);
    void chAutoGeoTerrainEnabled(int state);
    void selectTerrainProfileEnabled();
    void mkrList(QMap<QString, Coords*> list);
    void checkGeodataFilesEnabled();
    void generateTilesEnabled();
    void generateLoTilesEnabled();
    void generateLoTilesFromTDBEnabled();
    
signals:
    void enableTool(QString name);
    void createNewTiles(QMap<int, QPair<int, int>*> list);
    void createNewLoTiles(QMap<int, QPair<int, int>*> list);
    
private:
    QMap<QString, QPushButton*> buttonTools;
    const ToolRegistry *toolRegistry = nullptr;
    QMap<QString, Coords*> mkrFiles;
    QComboBox markerFiles;
    QSpinBox eRadius;
    QLabel *defaultTerrainProfileName;
};

#endif	/* GEOTOOLS_H */

