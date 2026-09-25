/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "NaviWindow.h"
#include <tsre/geo/GeoCoordinates.h>
#include <QtWidgets>
#include <QDebug>
#include <tsre/coords/Coords.h>
#include <tsre/coords/CoordsMkr.h>
#include <tsre/coords/CoordsKml.h>
#include <tsre/Game.h>
#include <tsre/geo/GeoCoordinateText.h>
#include <algorithm>

NaviWindow::NaviWindow(QWidget* parent) : QWidget(parent) {
    this->setWindowFlags(Qt::WindowType::Tool);
    //this->setWindowFlags(Qt::WindowStaysOnTopHint);
    this->setFixedWidth(320);
    this->setFixedHeight(210);
    this->setWindowTitle(
        //% "Navi Window"
        qtTrId("route.editor.navi.window.title.navi.window"));
    markerFiles.setStyleSheet("combobox-popup: 0;");
    markerFiles.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    markerList.setStyleSheet("combobox-popup: 0;");
    markerList.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    markerSearch.setPlaceholderText(
        //% "Search places"
        qtTrId("route.editor.navi.window.search.places"));
    markerSearch.setEnabled(false);
    markerCompleter = new QCompleter(this);
    markerSearchModel = new QStandardItemModel(this);
    markerCompleter->setModel(markerSearchModel);
    markerCompleter->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    markerCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    markerCompleter->setMaxVisibleItems(12);
    markerSearch.setCompleter(markerCompleter);
    
    QPushButton *jumpButton = new QPushButton(
        //% "Jump"
        qtTrId("route.editor.navi.window.button.jump.button"), this);
    QLabel *cameraPosLabel = new QLabel(
        //% "Camera:"
        qtTrId("route.editor.navi.window.label.camera.pos.label"), this);
    QLabel *pointerPosLabel = new QLabel(
        //% "Pointer:"
        qtTrId("route.editor.navi.window.label.pointer.pos.label"), this);
    QLabel *txLabel = new QLabel("X", this);
    QLabel *tyLabel = new QLabel("Y", this);
    QLabel *xLabel = new QLabel("x", this);
    QLabel *yLabel = new QLabel("y", this);
    QLabel *zLabel = new QLabel("z", this);
    QLabel *pxLabel = new QLabel("x", this);
    QLabel *pyLabel = new QLabel("y", this);
    QLabel *pzLabel = new QLabel("z", this);
    QLabel *latLabel = new QLabel("lat", this);
    QLabel *lonLabel = new QLabel("lon", this);
    QLabel *empty = new QLabel(" ", this);
    
    QLabel *label1 = new QLabel(
        //% "Position:"
        qtTrId("route.editor.navi.window.label.label1"));
    label1->setContentsMargins(3,0,0,0);
    label1->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");

    QVBoxLayout *v = new QVBoxLayout;
    v->setSpacing(2);
    v->setContentsMargins(0,1,1,1);
    v->addWidget(&markerFiles);
    v->addWidget(&markerSearch);
    v->addWidget(&markerList);
    
    QGridLayout *vbox = new QGridLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(3,0,1,0);    
    vbox->addWidget(pointerPosLabel,0,0);
    vbox->addWidget(pxLabel,0,1);
    vbox->addWidget(&pxBox,0,2);
    vbox->addWidget(pyLabel,0,3);
    vbox->addWidget(&pyBox,0,4);
    vbox->addWidget(pzLabel,0,5);
    vbox->addWidget(&pzBox,0,6);
    pxBox.setEnabled(false);
    pyBox.setEnabled(false);
    pzBox.setEnabled(false);
    vbox->addWidget(cameraPosLabel,1,0);
    vbox->addWidget(xLabel,1,1);
    vbox->addWidget(&xBox,1,2);
    vbox->addWidget(yLabel,1,3);
    vbox->addWidget(&yBox,1,4);
    vbox->addWidget(zLabel,1,5);
    vbox->addWidget(&zBox,1,6);
    v->addItem(vbox);
        
    vbox = new QGridLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(3,0,1,0);    
    //int row = 0;
    vbox->addWidget(txLabel,0,0);
    vbox->addWidget(&txBox,0,1);
    vbox->addWidget(tyLabel,0,2);
    vbox->addWidget(&tyBox,0,3);
    vbox->addWidget(latLabel,1,0);
    vbox->addWidget(&latBox,1,1);
    vbox->addWidget(lonLabel,1,2);
    vbox->addWidget(&lonBox,1,3);
    v->addItem(vbox);
    //vbox = new QGridLayout;
    //vbox->setSpacing(2);
    //vbox->setContentsMargins(3,0,1,0);    
    v->addWidget(&tileInfo);
    v->addWidget(jumpButton);
    //vbox->addStretch(1);
    this->setLayout(v);
    
    QObject::connect(&txBox, SIGNAL(textEdited(QString)),
                      this, SLOT(xyChanged(QString)));
    QObject::connect(&tyBox, SIGNAL(textEdited(QString)),
                      this, SLOT(xyChanged(QString)));
    QObject::connect(&xBox, SIGNAL(textEdited(QString)),
                      this, SLOT(xyChanged(QString)));
    QObject::connect(&yBox, SIGNAL(textEdited(QString)),
                      this, SLOT(xyChanged(QString)));
    QObject::connect(&zBox, SIGNAL(textEdited(QString)),
                      this, SLOT(xyChanged(QString)));
    QObject::connect(&latBox, SIGNAL(textEdited(QString)),
                      this, SLOT(latLonChanged(QString)));
    QObject::connect(&lonBox, SIGNAL(textEdited(QString)),
                      this, SLOT(latLonChanged(QString)));
    
    QObject::connect(jumpButton, SIGNAL(released()),
                      this, SLOT(jumpTileSelected()));
    
    QObject::connect(&markerFiles, SIGNAL(textActivated(QString)),
                      this, SLOT(mkrFilesSelected(QString)));
    QObject::connect(&markerList, SIGNAL(textActivated(QString)),
                      this, SLOT(mkrListSelected(QString)));
    QObject::connect(&markerSearch, &QLineEdit::textEdited,
                     this, &NaviWindow::updateMarkerSearch);
    QObject::connect(markerCompleter,
            QOverload<const QModelIndex &>::of(&QCompleter::activated),
            this, &NaviWindow::selectMarkerCompletion);
    

    tileInfo.setText(" ");
}

void NaviWindow::latLonChanged(QString val){
    if (changingLatLon) return;
    double latitude = 0.0;
    double longitude = 0.0;
    if (GeoCoordinateText::parseLatitudeLongitude(
                val, latitude, longitude)) {
        changingLatLon = true;
        latBox.setText(QString::number(latitude, 'g', 12));
        lonBox.setText(QString::number(longitude, 'g', 12));
        changingLatLon = false;
    }
    this->jumpType = "latlon";
}
void NaviWindow::xyChanged(QString val){
    this->jumpType = "xy";
}

void NaviWindow::jumpTileSelected(){
    if(aCoords == NULL)
        aCoords = new PreciseTileCoordinate();
    
    if(this->jumpType == "xy"){
        aCoords->setWxyz(xBox.text().toInt(), yBox.text().toInt(), zBox.text().toInt());
        aCoords->TileX = txBox.text().toInt();
        aCoords->TileZ = tyBox.text().toInt();
        emit jumpTo(aCoords);
    }
    if(this->jumpType == "latlon"){
        igh = Game::GeoCoordConverter->ConvertToInternal(latBox.text().toDouble(), lonBox.text().toDouble(), igh);
        aCoords = Game::GeoCoordConverter->ConvertToTile(igh, aCoords);
        aCoords->setWxyz();
        aCoords->wZ = -aCoords->wZ;
        emit jumpTo(aCoords);
    }
    if(this->jumpType == "marker"){
        if (activeCoords == NULL) return;
        const Coords::Marker *marker = activeCoords->markerAt(
                markerList.currentData().toInt());
        if (marker == NULL) return;
        igh = Game::GeoCoordConverter->ConvertToInternal(
                marker->lat, marker->lon, igh);
        aCoords = Game::GeoCoordConverter->ConvertToTile(igh, aCoords);
        aCoords->setWxyz();
        aCoords->wZ = -aCoords->wZ;
        emit jumpTo(aCoords);
    }

}

void NaviWindow::naviInfo(int all, int hidden){
    if(all != objCount || hidden != objHidden ){
        objCount = all;
        objHidden = hidden;
        //% "Objects: %1 (including %2 hidden)"
        this->tileInfo.setText(qtTrId("route.navigation.window.tile.objects.summary")
                               .arg(all).arg(hidden));
    }
}

void NaviWindow::pointerInfo(float* coords){
    this->pxBox.setText(QString::number(coords[0]));
    this->pyBox.setText(QString::number(coords[1]));
    this->pzBox.setText(QString::number(-coords[2]));
}

void NaviWindow::posInfo(PreciseTileCoordinate* coords){
    if(lastX != coords->X || lastY != coords->Y || lastZ != coords->Z || lastTX != coords->TileX || lastTZ != coords->TileZ){
        lastX = coords->wX;
        lastY = coords->wY;
        lastZ = coords->wZ;
        lastTX = coords->TileX;
        lastTZ = coords->TileZ;
        this->txBox.setText(QString::number(lastTX, 10));
        this->tyBox.setText(QString::number(lastTZ, 10));
        this->xBox.setText(QString::number(lastX, 10));
        this->yBox.setText(QString::number(lastY, 10));
        this->zBox.setText(QString::number(-lastZ, 10));
        igh = Game::GeoCoordConverter->ConvertToInternal(coords);
        
        latlon = Game::GeoCoordConverter->ConvertToLatLon(igh);
        this->latBox.setText(QString::number(latlon->Latitude));
        this->lonBox.setText(QString::number(latlon->Longitude));
    }
}

void NaviWindow::mkrList(QMap<QString, Coords*> list){
    const QString previousSource = markerFiles.currentText();
    mkrFiles = list;
    markerFiles.blockSignals(true);
    markerFiles.clear();
    for (auto it = list.begin(); it != list.end(); ++it ){
        if(it.value() == NULL)
            continue;
        if(!it.value()->loaded)
            continue;
        markerFiles.addItem(it.key());
    }
    markerFiles.blockSignals(false);
    if(markerFiles.count() > 0) {
        int selectedSource = markerFiles.findText(previousSource);
        if (selectedSource < 0) selectedSource = 0;
        markerFiles.setCurrentIndex(selectedSource);
        mkrFilesSelected(markerFiles.itemText(selectedSource));
    }
    else {
        activeCoords = NULL;
        markerList.clear();
        markerSearch.clear();
        markerSearch.setEnabled(false);
    }
}

void NaviWindow::mkrFilesSelected(QString item){
    Coords* c = mkrFiles.value(item, NULL);
    if(c == NULL) return;
    activeCoords = c;
    this->sendMsg("mkrFile", item);
    markerSearch.clear();
    markerSearch.setEnabled(true);
    markerSearchModel->clear();
    markerList.clear();
    QVector<int> order;
    order.reserve(c->markerList.size());
    for (int i = 0; i < c->markerList.size(); ++i) order.append(i);
    std::sort(order.begin(), order.end(), [c](int left, int right) {
        return c->markerList[left].name.compare(
                c->markerList[right].name, Qt::CaseInsensitive) < 0;
    });
    for (int index : order)
        markerList.addItem(c->markerList[index].name, index);
    markerList.setMaxVisibleItems(25);
}

void NaviWindow::mkrListSelected(QString item){
    Q_UNUSED(item)
    this->jumpType = "marker";
    if (activeCoords == NULL) return;
    const Coords::Marker *marker = activeCoords->markerAt(
            markerList.currentData().toInt());
    if (marker == NULL) return;
    latBox.setText(QString::number(marker->lat, 'g', 12));
    lonBox.setText(QString::number(marker->lon, 'g', 12));
}

void NaviWindow::updateMarkerSearch(const QString &text) {
    markerSearchModel->clear();
    if (activeCoords == NULL || text.trimmed().isEmpty()) {
        markerCompleter->popup()->hide();
        return;
    }
    for (int index : activeCoords->search(text, 20)) {
        const Coords::Marker *marker = activeCoords->markerAt(index);
        if (marker == NULL) continue;
        QString display = marker->name;
        if (!marker->countryCode.isEmpty())
            display += QStringLiteral(" (%1)").arg(marker->countryCode);
        display += QStringLiteral("  %1, %2")
                .arg(QString::number(marker->lat, 'f', 6),
                     QString::number(marker->lon, 'f', 6));
        QStandardItem *item = new QStandardItem(display);
        item->setData(index, Qt::UserRole);
        markerSearchModel->appendRow(item);
    }
    if (markerSearchModel->rowCount() > 0)
        markerCompleter->complete();
    else
        markerCompleter->popup()->hide();
}

void NaviWindow::selectMarkerCompletion(const QModelIndex &index) {
    if (!index.isValid() || activeCoords == NULL) return;
    const int markerIndex = index.data(Qt::UserRole).toInt();
    const int comboIndex = markerList.findData(markerIndex);
    if (comboIndex < 0) return;
    markerList.setCurrentIndex(comboIndex);
    const Coords::Marker *marker = activeCoords->markerAt(markerIndex);
    if (marker != NULL) markerSearch.setText(marker->name);
    mkrListSelected(markerList.currentText());
}
NaviWindow::~NaviWindow() {
    delete igh;
    delete latlon;
    delete aCoords;
}

void NaviWindow::hideEvent(QHideEvent *e){
    emit windowClosed();
}
