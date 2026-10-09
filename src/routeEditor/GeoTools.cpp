/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "GeoTools.h"
#include "tools/ToolButtons.h"
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Brush.h>
#include <tsre/texture/Texture.h>
#include <tsre/gui/GuiFunct.h>
#include <tsre/world/objects/TransferObj.h>
#include <tsre/Game.h>
#include <tsre/coords/Coords.h>
#include <tsre/geo/HeightWindow.h>
#include <routeEditor/TerrainProfileSelector.h>
#include <tsre/world/TerrainGridLayout.h>
#include <tsre/geo/TerrainOverlays.h>

GeoTools::GeoTools(QString name)
    : QWidget(),
      defaultTerrainProfileName(new QLabel(this)) {
    setFixedWidth(250);
    int row = 0;
    
    // One tool for the tile's overlay and its texture: each button chooses the
    // action a left click runs; the context menu lists them all.
    buttonTools["terrainOverlayTool:show"] = new QPushButton(
        //% "Show/Hide Loaded Overlay"
        qtTrId("route.editor.geo.tools.button.overlay.show"), this);
    buttonTools["terrainOverlayTool:osm"] = new QPushButton(
        //% "Create from OSM"
        qtTrId("route.editor.geo.tools.button.overlay.osm"), this);
    buttonTools["terrainOverlayTool:imagery"] = new QPushButton(
        //% "Create from Imagery"
        qtTrId("route.editor.geo.tools.button.overlay.imagery"), this);
    buttonTools["heightTileLoadTool"] = new QPushButton(
        //% "Load Height"
        qtTrId("route.editor.geo.tools.button.load.height"), this);
    buttonTools["terrainOverlayTool:makeTexture"] = new QPushButton(
        //% "Make from Overlay"
        qtTrId("route.editor.geo.tools.button.texture.make"), this);
    buttonTools["terrainOverlayTool:removeTexture"] = new QPushButton(
        //% "Remove"
        qtTrId("route.editor.geo.tools.button.texture.remove"), this);
    buttonTools["quadTreeTool"] = new QPushButton(
        //% "Edit Quad Tree"
        qtTrId("route.editor.geo.tools.button.edit.quad.tree"), this);
    buttonTools["quadTreeTool"]->setToolTip(
        //% "Right click a quad of the terrain tree being edited for its actions: split, populate, create or delete its tile."
        qtTrId("route.editor.geo.tools.tooltip.edit.quad.tree"));
    QMapIterator<QString, QPushButton*> i(buttonTools);
    while (i.hasNext()) {
        i.next();
        i.value()->setCheckable(true);
    }

    QLabel *label0;
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
        
    label0 = new QLabel(
        //% "Terrain Tile Overlay:"
        qtTrId("route.editor.geo.tools.label.overlay"));
    label0->setContentsMargins(3,0,0,0);
    label0->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    vbox->addWidget(label0);
    // How much of the terrain shows through the overlays, when drawing: a
    // field and a slider, as the F2 brush rows.
    QSpinBox *opacity = new QSpinBox(this);
    opacity->setRange(0, 100);
    opacity->setSuffix(QStringLiteral(" %"));
    opacity->setFixedWidth(55);
    opacity->setValue(qRound(TerrainOverlays::opacity() * 100.0f));
    QSlider *opacitySlider = new QSlider(Qt::Horizontal, this);
    opacitySlider->setRange(0, 100);
    opacitySlider->setValue(opacity->value());
    QGridLayout *opacityRow = new QGridLayout;
    opacityRow->setSpacing(2);
    opacityRow->setContentsMargins(3,0,1,0);
    opacityRow->addWidget(GuiFunct::newQLabel(
        //% "Opacity:"
        qtTrId("route.editor.geo.tools.label.overlay.opacity"), 70), 0, 0);
    opacityRow->addWidget(opacity, 0, 1);
    opacityRow->addWidget(opacitySlider, 0, 2);
    vbox->addLayout(opacityRow);
    QObject::connect(opacitySlider, &QSlider::valueChanged, opacity, &QSpinBox::setValue);
    QObject::connect(opacity, &QSpinBox::valueChanged, this, [this, opacitySlider](int value) {
        if (opacitySlider->value() != value)
            opacitySlider->setValue(value);
        TerrainOverlays::setOpacity(value / 100.0f);
        emit overlayOpacityChanged();
    });
    vbox->addWidget(buttonTools["terrainOverlayTool:show"]);
    vbox->addWidget(buttonTools["terrainOverlayTool:osm"]);
    vbox->addWidget(buttonTools["terrainOverlayTool:imagery"]);

    label0 = new QLabel(
        //% "Terrain Tile Texture:"
        qtTrId("route.editor.geo.tools.label.texture"));
    label0->setContentsMargins(3,0,0,0);
    label0->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    vbox->addWidget(label0);
    QHBoxLayout *textureRow = new QHBoxLayout;
    textureRow->setSpacing(2);
    textureRow->addWidget(buttonTools["terrainOverlayTool:makeTexture"]);
    textureRow->addWidget(buttonTools["terrainOverlayTool:removeTexture"]);
    vbox->addLayout(textureRow);
    
    label0 = new QLabel(
        //% "Terrain Heightmap:"
        qtTrId("route.editor.geo.tools.label.label0.2"));
    label0->setContentsMargins(3,0,0,0);
    label0->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    vbox->addWidget(label0);
    vbox->addWidget(buttonTools["heightTileLoadTool"]);

    label0 = new QLabel(
        //% "Quad Tree:"
        qtTrId("route.editor.geo.tools.label.quad.tree"));
    label0->setContentsMargins(3,0,0,0);
    label0->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    vbox->addWidget(label0);
    vbox->addWidget(buttonTools["quadTreeTool"]);
    
    label0 = new QLabel(
        //% "Auto tile generation:"
        qtTrId("route.editor.geo.tools.label.label0.3"));
    label0->setContentsMargins(3,0,0,0);
    label0->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    vbox->addWidget(label0);
    QCheckBox *chAutoCreateTile = new QCheckBox(
        //% "Create new tiles if not exist."
        qtTrId("route.editor.geo.tools.option.ch.auto.create.tile"));
    chAutoCreateTile->setChecked(Game::autoNewTiles);
    QCheckBox *chAutoGeoTerrain = new QCheckBox(
        //% "Create terrain from Geodata. "
        qtTrId("route.editor.geo.tools.option.ch.auto.geo.terrain"));
    chAutoGeoTerrain->setChecked(Game::autoGeoTerrain);
    vbox->addWidget(chAutoCreateTile);
    vbox->addWidget(chAutoGeoTerrain);

    label0 = new QLabel(
        //% "Default Terrain Profile:"
        qtTrId("route.editor.geo.tools.label.label0.4"));
    label0->setContentsMargins(3,4,0,0);
    label0->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    vbox->addWidget(label0);
    defaultTerrainProfileName->setContentsMargins(3,0,3,0);
    defaultTerrainProfileName->setWordWrap(true);
    defaultTerrainProfileName->setText(TerrainGridLayout::profileName(
            Game::defaultTerrainHeightProfile,
            Game::defaultTerrainPatchCount));
    vbox->addWidget(defaultTerrainProfileName);
    QPushButton *selectTerrainProfile = new QPushButton(
            //% "Select terrain profile..."
            qtTrId("route.editor.geo.tools.button.select.terrain.profile"), this);
    QObject::connect(selectTerrainProfile, SIGNAL(released()),
                     this, SLOT(selectTerrainProfileEnabled()));
    vbox->addWidget(selectTerrainProfile);
    
    label0 = new QLabel(
        //% "Tiles from marker file:"
        qtTrId("route.editor.geo.tools.label.label0.5"));
    label0->setContentsMargins(3,0,0,0);
    //label0->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    vbox->addWidget(label0);
    vbox->addWidget(&markerFiles);
    markerFiles.setStyleSheet("combobox-popup: 0;");
    QFormLayout *vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    vlist->addRow(
        //% "Radius:"
        qtTrId("route.editor.geo.tools.label.radius"),&this->eRadius);
    eRadius.setRange(0,2);
    eRadius.setValue(0);
    vbox->addItem(vlist);
    QPushButton * checkGeodataFiles = new QPushButton(
        //% "Check if geodata files available."
        qtTrId("route.editor.geo.tools.button.check.geodata.files"), this);
    QObject::connect(checkGeodataFiles, SIGNAL(released()),
                      this, SLOT(checkGeodataFilesEnabled()));
    vbox->addWidget(checkGeodataFiles);
    
    QPushButton * generateTiles = new QPushButton(
        //% "Generate tiles."
        qtTrId("route.editor.geo.tools.button.generate.tiles"), this);
    QObject::connect(generateTiles, SIGNAL(released()),
                      this, SLOT(generateTilesEnabled()));
    vbox->addWidget(generateTiles);

    label0 = new QLabel(
        //% "Distant Terrain:"
        qtTrId("route.editor.geo.tools.label.label0.6"));
    label0->setContentsMargins(3,0,0,0);
    vbox->addWidget(label0);
    QPushButton * checkGeodataLoFiles = new QPushButton(
        //% "Check if geodata files available."
        qtTrId("route.editor.geo.tools.button.check.geodata.lo.files"), this);
    //QObject::connect(checkGeodataFiles, SIGNAL(released()),
    //                  this, SLOT(checkGeodataFilesEnabled()));
    vbox->addWidget(checkGeodataLoFiles);
    
    QPushButton * generateLoTiles = new QPushButton(
        //% "Generate tiles using MKR."
        qtTrId("route.editor.geo.tools.button.generate.lo.tiles"), this);
    QObject::connect(generateLoTiles, SIGNAL(released()),
                      this, SLOT(generateLoTilesEnabled()));
    vbox->addWidget(generateLoTiles);
    QPushButton * generateLoTilesFromTDB = new QPushButton(
        //% "Generate tiles using TDB."
        qtTrId("route.editor.geo.tools.button.generate.lo.tiles.from.tdb"), this);
    QObject::connect(generateLoTilesFromTDB, SIGNAL(released()),
                      this, SLOT(generateLoTilesFromTDBEnabled()));
    vbox->addWidget(generateLoTilesFromTDB);
    
    vbox->addStretch(1);
    this->setLayout(vbox);
    
    
    // signals: a button enables its tool (and action), unchecking it no tool.
    for (auto it = buttonTools.cbegin(); it != buttonTools.cend(); ++it) {
        const QString name = it.key();
        QObject::connect(it.value(), &QPushButton::toggled, this, [this, name](bool val) {
            emit enableTool(val ? name : QString());
        });
    }

    QObject::connect(chAutoCreateTile, SIGNAL(stateChanged(int)),
                      this, SLOT(chAutoCreateTileEnabled(int)));
    
    QObject::connect(chAutoGeoTerrain, SIGNAL(stateChanged(int)),
                      this, SLOT(chAutoGeoTerrainEnabled(int)));
    
}

void GeoTools::mkrList(QMap<QString, Coords*> list){
    mkrFiles = list;
    for (auto it = list.begin(); it != list.end(); ++it ){
        if(it.value() == NULL)
            continue;
        if(!it.value()->loaded)
            continue;
        if(it.key().startsWith("|"))
            continue;
        markerFiles.addItem(it.key());
    }
    //    mkrFilesSelected(markerFiles.itemText(0));
}

void GeoTools::checkGeodataFilesEnabled(){
    if(markerFiles.count() == 0)
        return;
    Coords* c = mkrFiles[markerFiles.currentText()];
    if(c == NULL) 
        return;
    
    QMap<int, QPair<int, int>*> tileList;
    int radius = eRadius.value();
    c->getTileList(tileList, radius);

    /*int x, z;
    QMapIterator<int, QPair<int, int>*> i2(tileList);
    while (i2.hasNext()) {
        i2.next();
        if(i2.value() == NULL)
            continue;
        x = i2.value()->first;
        z = i2.value()->second;
        qDebug() << x << z;
    }*/
    HeightWindow::CheckForMissingGeodataFiles(tileList);
}

void GeoTools::generateTilesEnabled(){
    if(markerFiles.count() == 0)
        return;
    Coords* c = mkrFiles[markerFiles.currentText()];
    if(c == NULL) 
        return;
    
    QMap<int, QPair<int, int>*> tileList;
    int radius = eRadius.value();
    c->getTileList(tileList, radius);
    
    emit createNewTiles(tileList);
}

void GeoTools::generateLoTilesEnabled(){
    if(markerFiles.count() == 0)
        return;
    Coords* c = mkrFiles[markerFiles.currentText()];
    if(c == NULL) 
        return;
    
    QMap<int, QPair<int, int>*> tileList;
    int radius = eRadius.value();
    c->getTileList(tileList, radius, 8);
    
    emit createNewLoTiles(tileList);
}

void GeoTools::generateLoTilesFromTDBEnabled(){

    TDB* tdb = Game::trackDB;
    if(tdb == NULL) 
        return;
    
    QMap<int, QPair<int, int>*> tileList;
    int radius = eRadius.value();
    tdb->getUsedTileList(tileList, radius, 8);
    
    emit createNewLoTiles(tileList);
}

void GeoTools::chAutoCreateTileEnabled(int state){
    if(state == Qt::Checked)
        Game::autoNewTiles = true;
    else
        Game::autoNewTiles = false;
}

void GeoTools::chAutoGeoTerrainEnabled(int state){
    if(state == Qt::Checked)
        Game::autoGeoTerrain = true;
    else
        Game::autoGeoTerrain = false;
}

GeoTools::~GeoTools() {
}

void GeoTools::msg(QString text, QString val){
    if(text == "viewMode"){
        ToolButtons::applyMode(buttonTools, ToolButtons::modeOf(val));
        return;
    }
    if(text == "toolEnabled"){
        QMapIterator<QString, QPushButton*> i(buttonTools);
        while (i.hasNext()) {
            i.next();
            if(i.value() == NULL)
                continue;
            i.value()->blockSignals(true);
            i.value()->setChecked(false);
        }
        if(buttonTools[val] != NULL)
            buttonTools[val]->setChecked(true);
        i.toFront();
        while (i.hasNext()) {
            i.next();
            if(i.value() == NULL)
                continue;
            i.value()->blockSignals(false);
        }
    }
}

void GeoTools::selectTerrainProfileEnabled(){
    TerrainHeightProfile profile = Game::defaultTerrainHeightProfile;
    int patches = Game::defaultTerrainPatchCount;
    if (!TerrainProfileSelector::choose(
            this, "Select default terrain profile", profile, patches))
        return;
    Game::defaultTerrainHeightProfile = profile;
    Game::defaultTerrainPatchCount = patches;
    defaultTerrainProfileName->setText(TerrainGridLayout::profileName(
            profile, patches));
}
