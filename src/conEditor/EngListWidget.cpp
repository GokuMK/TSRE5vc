/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <conEditor/EngListWidget.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/Eng.h>
#include <tsre/Game.h>

EngListWidget::EngListWidget() : QWidget(){
    addBegButton.setText(
        //% "Add Beg"
        qtTrId("con.editor.eng.list.widget.text.add.beg"));
    addCurButton.setText(
        //% "Add Cur"
        qtTrId("con.editor.eng.list.widget.text.add.cur"));
    addEndButton.setText(
        //% "Add End"
        qtTrId("con.editor.eng.list.widget.text.add.end"));
    addRandButton.setText(
        //% "Add Rand"
        qtTrId("con.editor.eng.list.widget.text.add.rand"));
    addNum.setText("1");
    
    engType.addItem(
        //% "ALL"
        qtTrId("con.editor.eng.list.widget.item.all"), QString());
    engType.addItem(
        //% "electric"
        qtTrId("con.editor.eng.list.widget.item.electric"), "electric");
    engType.addItem(
        //% "diesel"
        qtTrId("con.editor.eng.list.widget.item.diesel"), "diesel");
    engType.addItem(
        //% "steam"
        qtTrId("con.editor.eng.list.widget.item.steam"), "steam");
    engType.addItem(
        //% "carriage"
        qtTrId("con.editor.eng.list.widget.item.carriage"), "carriage");
    engType.addItem(
        //% "freight"
        qtTrId("con.editor.eng.list.widget.item.freight"), "freight");
    engType.addItem(
        //% "tender"
        qtTrId("con.editor.eng.list.widget.item.tender"), "tender");

    couplingType.addItem(
        //% "ALL"
        qtTrId("con.editor.eng.list.widget.item.all.2"), QString());
    couplingType.addItem(
        //% "Chain"
        qtTrId("con.editor.eng.list.widget.item.chain"), "Chain");
    couplingType.addItem(
        //% "Automatic"
        qtTrId("con.editor.eng.list.widget.item.automatic"), "Automatic");
    couplingType.addItem(
        //% "Bar"
        qtTrId("con.editor.eng.list.widget.item.bar"), "Bar");
    
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    QFormLayout *vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    vlist->addRow(
        //% "Total:"
        qtTrId("con.editor.eng.list.widget.label.total"), &totalVal);
    vlist->addRow(
        //% "Type:"
        qtTrId("con.editor.eng.list.widget.label.type"), &engType);
    vlist->addRow(
        //% "Coupling:"
        qtTrId("con.editor.eng.list.widget.label.coupling"), &couplingType);
    vlist->addRow(
        //% "Search"
        qtTrId("con.editor.eng.list.widget.label.search"), &searchBox);
    vlist->addRow(
        //% "Num to add"
        qtTrId("con.editor.eng.list.widget.label.num.add"), &addNum);
    vbox->addItem(vlist);
    QHBoxLayout *addbuttons = new QHBoxLayout;
    addbuttons->addWidget(&addBegButton);
    addbuttons->addWidget(&addCurButton);
    addbuttons->addWidget(&addEndButton);
    addbuttons->addWidget(&addRandButton);
    //vbox->addWidget();
    vbox->addItem(addbuttons);
    vbox->addWidget(&items);
    //vbox->addStretch(1);
    this->setLayout(vbox);
    engType.setStyleSheet("combobox-popup: 0;");
    this->setMinimumWidth(250);
    couplingType.setStyleSheet("combobox-popup: 0;");
    
    QObject::connect(&engType, SIGNAL(textActivated(QString)),
                      this, SLOT(fs(QString)));
    QObject::connect(&couplingType, SIGNAL(textActivated(QString)),
                      this, SLOT(fs(QString)));
    QObject::connect(&searchBox, SIGNAL(textEdited(QString)),
                      this, SLOT(fs(QString)));
    
    QObject::connect(&items, SIGNAL(itemSelectionChanged()),
                      this, SLOT(itemsSelected()));
    
    QObject::connect(&addBegButton, SIGNAL(released()),
                      this, SLOT(addBegButtonSelected()));
    QObject::connect(&addCurButton, SIGNAL(released()),
                      this, SLOT(addCurButtonSelected()));
    QObject::connect(&addEndButton, SIGNAL(released()),
                      this, SLOT(addEndButtonSelected()));
    QObject::connect(&addRandButton, SIGNAL(released()),
                      this, SLOT(addRndButtonSelected()));
    
    items.viewport()->installEventFilter(this);
    items.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    totalVal.setEnabled(false);
}

EngListWidget::~EngListWidget() {
}

void EngListWidget::fs(QString n){
    Q_UNUSED(n);
    const QString ef = engType.currentData().toString();
    const QString cf = couplingType.currentData().toString();
    QString sf = searchBox.text();
    qDebug() << ef <<" "<< cf <<" "<< sf;
    fillEngList(ef, cf, sf);
}

void EngListWidget::fillEngList(){
    fillEngList("","","");
}

void EngListWidget::fillEngList(QString engFilter, QString couplingFilter, QString searchFilter){
    items.clear();

    Eng * e;
    totalVal.setText(QString::number(Game::currentEngLib->jesteng));
    
    for (int i = 0; i < Game::currentEngLib->jesteng; i++){
        e = Game::currentEngLib->eng[i];
        if(e == NULL) continue;
        if(e->loaded !=1) continue;
        if(!e->engFilter(engFilter)) continue;
        if(!e->couplingFilter(couplingFilter)) continue;
        if(!e->searchFilter(searchFilter)) continue;
        
        new QListWidgetItem ( e->displayName, &items, i);
    }
    items.sortItems(Qt::AscendingOrder);
    
    if(items.count() < Game::currentEngLib->jesteng)
    totalVal.setText(QString::number(items.count()) + " / " + QString::number(Game::currentEngLib->jesteng));
}

void EngListWidget::itemsSelected(){
    QListWidgetItem * item = items.currentItem();
    //qDebug() << item->type() << " " << item->text();
    emit engListSelected(item->type());
}

void EngListWidget::addBegButtonSelected(){
    bool ok = false;
    int count = addNum.text().toInt(&ok);
    if(!ok) count = 1;
    addBegButtonSelected(count);
}

void EngListWidget::addBegButtonSelected(int count){
    QListWidgetItem * item = items.currentItem();
    if(item == NULL) return;
    emit addToConSelected(item->type(), 0, count);
}

void EngListWidget::addCurButtonSelected(){
    bool ok = false;
    int count = addNum.text().toInt(&ok);
    if(!ok) count = 1;
    addCurButtonSelected(count);
}

void EngListWidget::addCurButtonSelected(int count){
    QListWidgetItem * item = items.currentItem();
    if(item == NULL) return;
    emit addToConSelected(item->type(), 1, count);
}

void EngListWidget::addEndButtonSelected(){
    bool ok = false;
    int count = addNum.text().toInt(&ok);
    if(!ok) count = 1;
    addEndButtonSelected(count);
}

void EngListWidget::addEndButtonSelected(int count){
    QListWidgetItem * item = items.currentItem();
    if(item == NULL) return;
    emit addToConSelected(item->type(), 2, count);
}

void EngListWidget::addRndButtonSelected(){
    QListWidgetItem * item = items.currentItem();
    if(item == NULL) return;
    emit addToRandomConsist(item->type());
}

bool EngListWidget::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent * mouseEvent = static_cast <QMouseEvent *> (event);
        if (mouseEvent->button() == Qt::LeftButton) {
            addEndButtonSelected(1);
        }
    }
    return QWidget::eventFilter(obj, event);
}