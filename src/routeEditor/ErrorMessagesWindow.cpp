/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <routeEditor/ErrorMessagesWindow.h>
#include <tsre/ErrorMessagesLib.h>
#include <QDebug>
#include <tsre/Game.h>
#include <tsre/ErrorMessage.h>
#include "ErrorMessageProperties.h"
#include <tsre/geo/GeoCoordinates.h>

ErrorMessagesWindow::ErrorMessagesWindow(QWidget* parent) : QWidget(parent) {
    brushes[(int)ErrorMessage::Type_Error] = QBrush(QColor(Game::StyleRedText));
    brushes[(int)ErrorMessage::Type_Warning] = QBrush(QColor(200,200,0));
    brushes[(int)ErrorMessage::Type_Info] = QBrush(QColor(Game::StyleGreenText));
    brushes[(int)ErrorMessage::Type_AutoFix] =QBrush(QColor(20,20,200));
    brushes[1000] = QBrush(QColor(Game::StyleMainLabel));
    this->setWindowFlags(Qt::WindowType::Tool);
    //this->setFixedWidth(350);
    this->setMinimumWidth(730);
    this->setFixedHeight(400);
    this->setWindowTitle(
        //% "Errors & Messages"
        qtTrId("route.editor.error.messages.window.title.errors.messages"));
    
    properties = new ErrorMessageProperties(this);
    connect(properties, &ErrorMessageProperties::messageChanged,
            this, &ErrorMessagesWindow::refreshErrorList);
    
    QVBoxLayout *errorListLayout = new QVBoxLayout;
    errorListLayout->setContentsMargins(0,0,0,0);
    errorListLayout->setSpacing(0);
    /*QPushButton *bNewActionEvent = new QPushButton("New Service");
    QObject::connect(bNewActionEvent, SIGNAL(released()),
                      this, SLOT(bNewServiceSelected()));
    QPushButton *bDeleteActionEvent = new QPushButton("Delete");
    QObject::connect(bDeleteActionEvent, SIGNAL(released()),
                      this, SLOT(bDeleteServiceSelected()));*/
    errorListLayout->addWidget(&errorList);
    errorListLayout->addWidget(properties);
    //errorListLayout->addWidget(bNewActionEvent);
    //errorListLayout->addWidget(bDeleteActionEvent);
    QStringList list;
    //% "ID:"
    list.append(qtTrId("route.errors.header.id"));
    //% "Time:"
    list.append(qtTrId("route.errors.header.time"));
    //% "Type:"
    list.append(qtTrId("route.errors.header.type"));
    //% "Source:"
    list.append(qtTrId("route.errors.header.source"));
    //% "Message:"
    list.append(qtTrId("route.errors.header.message"));
    //list.append("Any:");
    //errorList.setFixedWidth(250);
    errorList.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    errorList.setColumnCount(3);
    errorList.setHeaderLabels(list);
    errorList.setRootIsDecorated(false);
    errorList.header()->resizeSection(0,30);    
    errorList.header()->resizeSection(1,50);    
    errorList.header()->resizeSection(2,70);    
    errorList.header()->resizeSection(3,50);    
    errorList.header()->resizeSection(4,500);    
    //QHBoxLayout *v = new QHBoxLayout;
    //v->setSpacing(2);
    //v->setContentsMargins(1,1,1,1);
    //v->addItem(errorListLayout);
    //v->addWidget(serviceProperties);
    this->setLayout(errorListLayout);
    
    connect(&errorList, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) { errorListSelected(item, 0); });
    QObject::connect(properties, SIGNAL(jumpTo(PreciseTileCoordinate*)),
                      this, SLOT(jumpRequestReceived(PreciseTileCoordinate*)));
    QObject::connect(properties, SIGNAL(selectObject(GameObj*)),
                      this, SLOT(selectRequestReceived(GameObj*)));
    refreshErrorList();
}

void ErrorMessagesWindow::selectRequestReceived(GameObj* o){
    emit selectObject(o);
}

void ErrorMessagesWindow::jumpRequestReceived(PreciseTileCoordinate* c){
    emit jumpTo(c);
}

void ErrorMessagesWindow::errorListSelected(QTreeWidgetItem* item, int column){
    properties->showMessage(item ? ErrorMessagesLib::ErrorMessages.value(item->type(), nullptr) : nullptr);
}

void ErrorMessagesWindow::refreshErrorList(){
    auto *selected = errorList.currentItem()
        ? ErrorMessagesLib::ErrorMessages.value(errorList.currentItem()->type(), nullptr) : nullptr;
    errorList.clear();
    QList<QTreeWidgetItem *> items;
    QStringList list;
    for(int i = ErrorMessagesLib::ErrorMessages.size() - 1; i >= 0 ; i-- ){
        if(ErrorMessagesLib::ErrorMessages[i] == NULL)
            continue;
        
        ErrorMessage *msg = ErrorMessagesLib::ErrorMessages[i];
        list.clear();
        
       //QTime time = QDateTime::fromMSecsSinceEpoch(msg->time).toString("HH:mm:ss");
        //qDebug() << msg->time << time.isValid()<< time.toString();
        list.append(QString::number(i));
        list.append(QDateTime::fromMSecsSinceEpoch(msg->time).toString("HH:mm:ss"));
        list.append(ErrorMessage::TypeNames[msg->type]);
        list.append(ErrorMessage::SourceNames[msg->source]);
        list.append(msg->description);
        QTreeWidgetItem *item = new QTreeWidgetItem((QTreeWidget*)0, list, i );
        //item->setCheckState(0, Qt::Unchecked);
        //item->setCheckState(1, Qt::Unchecked);
        //item->setCheckState(2, Qt::Unchecked);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        item->setForeground(2, brushes[(int)msg->type]);
        item->setForeground(3, brushes[1000]);
        item->setTextAlignment(1, Qt::AlignCenter);
        item->setTextAlignment(2, Qt::AlignCenter);
        item->setTextAlignment(3, Qt::AlignCenter);
        items.append(item);
    }
    errorList.insertTopLevelItems(0, items);
    selectMessage(selected);
}

bool ErrorMessagesWindow::selectMessage(ErrorMessage *message) {
    if (!message) return false;
    const int index = ErrorMessagesLib::ErrorMessages.indexOf(message);
    if (index < 0) return false;
    for (int row = 0; row < errorList.topLevelItemCount(); ++row) {
        auto *item = errorList.topLevelItem(row);
        if (item->type() != index) continue;
        errorList.setCurrentItem(item);
        errorList.scrollToItem(item, QAbstractItemView::PositionAtCenter);
        return true;
    }
    return false;
}

bool ErrorMessagesWindow::showMessage(ErrorMessage *message) {
    if (!message || !ErrorMessagesLib::ErrorMessages.contains(message)) return false;
    show();
    selectMessage(message);
    raise();
    activateWindow();
    errorList.setFocus();
    return true;
}

void ErrorMessagesWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    emit windowShown();
}

void ErrorMessagesWindow::show(){
     refreshErrorList();
     QWidget::show();
}

ErrorMessagesWindow::~ErrorMessagesWindow() {
}

void ErrorMessagesWindow::hideEvent(QHideEvent *e){
    emit windowClosed();
}
