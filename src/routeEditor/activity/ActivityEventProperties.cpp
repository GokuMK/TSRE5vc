/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ActivityEventProperties.h"
#include <tsre/Game.h>
#include <tsre/trains/ActivityEvent.h>
#include <tsre/geo/GeoCoordinates.h>
#include <routeEditor/properties/EditFileNameDialog.h>
#include <tsre/GameObj.h>
#include <tsre/world/objects/WorldObj.h>
#include <tsre/world/OrtsWeatherChange.h>

namespace {
QString translatedActivityId(const QString &id) {
    const QByteArray utf8 = id.toUtf8();
    return qtTrId(utf8.constData());
}
}

ActivityEventProperties::ActivityEventProperties(QWidget* parent) : QWidget(parent) {
    //this->setMinimumHeight(400);
    setMinimumWidth(350);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
        
    QMapIterator<ActivityEvent::EventType, QString> i1(ActivityEvent::EventTypeDescription);
    while (i1.hasNext()) {
        i1.next();
        if(i1.key() == ActivityEvent::EventTypeNone)
            continue;
        if(i1.key() == ActivityEvent::EventTypeLocation)
            continue;
        if(i1.key() == ActivityEvent::EventTypeTime)
            continue;
        cActionType.addItem(translatedActivityId(i1.value()), i1.key());
    }

    QMapIterator<ActivityEvent::Outcome::OutcomeType, QString> i2(ActivityEvent::Outcome::OutcomeTypeDescription);
    while (i2.hasNext()) {
        i2.next();
        cOutcome.addItem(translatedActivityId(i2.value()), i2.key());
    }
    
    buttonTools["pickNewEventLocationTool"] = new QPushButton(
        //% "Pick new location"
        qtTrId("route.editor.activity.event.properties.button.pick.new.location"));
    //buttonTools["pickNewEventWagonTool"] = new QPushButton("Pick new Car");
    QMapIterator<QString, QPushButton*> i(buttonTools);
    while (i.hasNext()) {
        i.next();
        i.value()->setCheckable(true);
    }
    
    cSoundType.addItem(
        //% "Everywhere"
        qtTrId("route.editor.activity.event.properties.item.everywhere"), QString("Everywhere"));
    cSoundType.addItem(
        //% "Cab"
        qtTrId("route.editor.activity.event.properties.item.cab"), QString("Cab"));
    cSoundType.addItem(
        //% "Pass"
        qtTrId("route.editor.activity.event.properties.item.pass"), QString("Pass"));
    cSoundType.addItem(
        //% "Ground"
        qtTrId("route.editor.activity.event.properties.item.ground"), QString("Ground"));

    
    cActionType.setStyleSheet("combobox-popup: 0;");
    cActionType.setMaxVisibleItems(30);
    cActionType.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    cOutcome.setStyleSheet("combobox-popup: 0;");
    cOutcome.setMaxVisibleItems(30);
    cOutcome.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    cOutcomeEvent.setStyleSheet("combobox-popup: 0;");
    cOutcomeEvent.setMaxVisibleItems(30);
    cOutcomeEvent.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    eOutcomeMessage.setFixedHeight(100);
    //eOutcomeMessage.setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    outcomeList.setFixedHeight(100);
    outcomeList.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    //vbox->setAlignment(Qt::AlignTop);
    
    // action event
    QGridLayout *vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    
    int row = 0;
    QLabel *label = new QLabel(
        //% "Action:"
        qtTrId("route.editor.activity.event.properties.label.label"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vlist->addWidget(label, row++, 0, 1, 2);
    
    label = new QLabel(
        //% "Type:"
        qtTrId("route.editor.activity.event.properties.label.label.2"));
    label->setMinimumWidth(100);
    vlist->addWidget(label, row, 0);
    cActionType.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    vlist->addWidget(&cActionType, row++, 1);
    QObject::connect(&cActionType, SIGNAL(textActivated(QString)),
                      this, SLOT(cActionTypeSelected(QString)));
    //vlist->addWidget(new QLabel("Info:"), row, 0);
    //vlist->addWidget(&eActionInfo, row++, 1);
    actionWidget.setLayout(vlist);
    
    // action station
    vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    row = 0;
    label = new QLabel(
        //% "Station:"
        qtTrId("route.editor.activity.event.properties.label.label.3"));
    label->setMinimumWidth(100);
    vlist->addWidget(label, row, 0);
    cStationStopAction.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    cStationStopAction.setStyleSheet("combobox-popup: 0;");
    cStationStopAction.setMaxVisibleItems(30);
    cStationStopAction.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    vlist->addWidget(&cStationStopAction, row++, 1);
    QObject::connect(&cStationStopAction, SIGNAL(textActivated(QString)),
                      this, SLOT(cStationStopActionSelected(QString)));
    actionWidgetStation.setLayout(vlist);
    
    // action siding
    vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    row = 0;
    label = new QLabel(
        //% "Siding:"
        qtTrId("route.editor.activity.event.properties.label.label.4"));
    label->setMinimumWidth(100);
    vlist->addWidget(label, row, 0);
    vlist->addWidget(&eActionSiding, row, 1);
    eActionSiding.setDisabled(true);
    bActionSiding.setText(
        //% "Link Selected"
        qtTrId("route.editor.activity.event.properties.text.link.selected"));
    QObject::connect(&bActionSiding, SIGNAL(released()),
                      this, SLOT(bActionSidingSelected()));
    vlist->addWidget(&bActionSiding, row++, 2);
    actionWidgetSiding.setLayout(vlist);
    
    // action speed
    vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    row = 0;
    label = new QLabel(
        //% "Speed:"
        qtTrId("route.editor.activity.event.properties.label.label.5"));
    label->setMinimumWidth(100);
    vlist->addWidget(label, row, 0);
    eActionSpeed.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    vlist->addWidget(&eActionSpeed, row++, 1);
    eActionSpeed.setRange(0,1000);
    QObject::connect(&eActionSpeed, SIGNAL(editingFinished()),
                      this, SLOT(eActionSpeedSelected()));
    actionWidgetSpeed.setLayout(vlist);
    
    // action wagon list
    vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(1,0,1,0);
    QPushButton *bRemoveCar = new QPushButton(
        //% "Remove Selected"
        qtTrId("route.editor.activity.event.properties.button.b.remove.car"));
    QObject::connect(bRemoveCar, SIGNAL(released()),
                      this, SLOT(bRemoveCarSelected()));
    QPushButton *bJumpToCar = new QPushButton(
        //% "Jump To Selected"
        qtTrId("route.editor.activity.event.properties.button.b.jump.to.car"));
    QObject::connect(bJumpToCar, SIGNAL(released()),
                      this, SLOT(bJumpToCarSelected()));
    QPushButton *bDescCar = new QPushButton(
        //% "Edit description"
        qtTrId("route.editor.activity.event.properties.button.b.desc.car"));
    QObject::connect(bDescCar, SIGNAL(released()),
                      this, SLOT(bDescCarSelected()));
    bJumpToCar->setMinimumWidth(100);
    label = new QLabel(
        //% "Wagon List:"
        qtTrId("route.editor.activity.event.properties.label.label.6"));
    label->setMaximumHeight(25);
    vlist->addWidget(label, 0, 0);
    vlist->addWidget(&wagonList, 0, 1, 5, 1);
    QPushButton *pickNewEventWagon = new QPushButton(
        //% "Pick Selected"
        qtTrId("route.editor.activity.event.properties.button.pick.new.event.wagon"));
    vlist->addWidget(pickNewEventWagon, 1, 0);
    QObject::connect(pickNewEventWagon, SIGNAL(released()),
                      this, SLOT(bPickNewEventWagonToolSelected()));
    vlist->addWidget(bJumpToCar, 2, 0);
    vlist->addWidget(bRemoveCar, 3, 0);
    vlist->addWidget(bDescCar, 4, 0);
    QStringList list;
    //% "ID:"
    list.append(qtTrId("activity.event.wagon.header.id"));
    //% "Description:"
    list.append(qtTrId("activity.event.wagon.header.description"));
    wagonList.setColumnCount(2);
    wagonList.setHeaderLabels(list);
    //wagonList.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    actionWidgetWagonList.setLayout(vlist);
    
    // location event
    vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    
    row = 0;
    label = new QLabel(
        //% "Location:"
        qtTrId("route.editor.activity.event.properties.label.label.7"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vlist->addWidget(label, row++, 0, 1, 2);

    label = new QLabel(
        //% "Position:"
        qtTrId("route.editor.activity.event.properties.label.label.8"));
    label->setMinimumWidth(100);
    vlist->addWidget(label, row, 0);
    vlist->addWidget(&eLocationPosition, row++, 1, 1, 2);
    QPushButton *button = new QPushButton(
        //% "Jump to location"
        qtTrId("route.editor.activity.event.properties.button.button"));
    QObject::connect(button, SIGNAL(released()),
                      this, SLOT(bJumpToEventLocationSelected()));
    vlist->addWidget(button, row, 1);
    QObject::connect(buttonTools["pickNewEventLocationTool"], SIGNAL(toggled(bool)),
                      this, SLOT(bPickEventLocationSelected(bool)));
    vlist->addWidget(buttonTools["pickNewEventLocationTool"], row++, 2);
    vlist->addWidget(new QLabel(
        //% "Radius:"
        qtTrId("route.editor.activity.event.properties.label.radius")), row, 0);
    vlist->addWidget(&eLocationRadius, row++, 1, 1, 2);
    eLocationRadius.setRange(0,100);
    QObject::connect(&eLocationRadius, SIGNAL(editingFinished()),
                      this, SLOT(eLocationRadiusSelected()));
    label = new QLabel(
        //% "Train must stop:"
        qtTrId("route.editor.activity.event.properties.label.label.9"));
    label->setMinimumHeight(25);
    vlist->addWidget(label, row, 0);
    vlist->addWidget(&cLocationStop, row++, 1, 1, 2);
    QObject::connect(&cLocationStop, SIGNAL(stateChanged(int)),
                      this, SLOT(cLocationStopSelected(int)));
    
    locationWidget.setLayout(vlist);
    
    // time event
    vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    
    row = 0;
    label = new QLabel(
        //% "Time:"
        qtTrId("route.editor.activity.event.properties.label.label.10"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vlist->addWidget(label, row++, 0, 1, 2);
    
    label = new QLabel(
        //% "Activation Time:"
        qtTrId("route.editor.activity.event.properties.label.label.11"));
    label->setMinimumWidth(100);
    vlist->addWidget(label, row, 0);
    vlist->addWidget(&eTime, row++, 1);
    QObject::connect(&eTime, SIGNAL(timeChanged(QTime)),
                      this, SLOT(eTimeSelected(QTime)));
    eTime.setDisplayFormat("HH:mm:ss");
    eTime.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    
    timeWidget.setLayout(vlist);
    vbox->addWidget(&actionWidget);
    vbox->addWidget(&actionWidgetSpeed);
    vbox->addWidget(&actionWidgetStation);
    vbox->addWidget(&actionWidgetSiding);
    vbox->addWidget(&actionWidgetWagonList);
    vbox->addWidget(&locationWidget);
    vbox->addWidget(&timeWidget);
    //
    
    vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    
    label = new QLabel(
        //% "Event:"
        qtTrId("route.editor.activity.event.properties.label.label.12"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vlist->addWidget(label, row++, 0, 1, 2);
    
    label = new QLabel(
        //% "Name:"
        qtTrId("route.editor.activity.event.properties.label.label.13"));
    label->setMinimumWidth(100);
    vlist->addWidget(label, row, 0);
    vlist->addWidget(&eName, row++, 1);
    QObject::connect(&eName, SIGNAL(editingFinished()),
                      this, SLOT(eNameSelected()));
    vlist->addWidget(new QLabel(
        //% "Activation Level:"
        qtTrId("route.editor.activity.event.properties.label.activation.level")), row, 0);
    vlist->addWidget(&eActivationLevel, row++, 1);
    eActivationLevel.setRange(-100,100);
    QObject::connect(&eActivationLevel, SIGNAL(editingFinished()),
                      this, SLOT(eActivationLevelSelected()));
    vlist->addWidget(new QLabel(
        //% "Triggered Text:"
        qtTrId("route.editor.activity.event.properties.label.triggered.text")), row, 0);
    vlist->addWidget(&eTriggeredText, row++, 1);
    QObject::connect(&eTriggeredText, SIGNAL(textEdited(QString)),
                      this, SLOT(eTriggeredTextSelected(QString)));
    vlist->addWidget(new QLabel(
        //% "Untriggered Text:"
        qtTrId("route.editor.activity.event.properties.label.untriggered.text")), row, 0);
    vlist->addWidget(&eUntriggeredText, row++, 1);
    QObject::connect(&eUntriggeredText, SIGNAL(textEdited(QString)),
                      this, SLOT(eUntriggeredTextSelected(QString)));
    vlist->addWidget(new QLabel(
        //% "Notes:"
        qtTrId("route.editor.activity.event.properties.label.notes")), row, 0);
    vlist->addWidget(&eNotes, row++, 1);
    QObject::connect(&eNotes, SIGNAL(textEdited(QString)),
                      this, SLOT(eNotesSelected(QString)));
    cAutoContinueLabel.setText(
        //% "Disable pause:"
        qtTrId("route.editor.activity.event.properties.text.disable.pause"));
    cAutoContinueLabel.setMinimumHeight(22);
    vlist->addWidget(&cAutoContinueLabel, row, 0);
    QObject::connect(&cAutoContinueLabel, SIGNAL(stateChanged(int)),
                      this, SLOT(cAutoContinueLabelSelected(int)));
    vlist->addWidget(&eAutoContinue, row++, 1);
    QObject::connect(&eAutoContinue, SIGNAL(editingFinished()),
                      this, SLOT(eAutoContinueSelected()));
    //lReversable.setText("Reversable:");
    cReversable.setMinimumHeight(22);
    cReversable.setText(
        //% "Reversable."
        qtTrId("route.editor.activity.event.properties.text.reversable"));
    //vlist->addWidget(&lReversable, row, 0);
    vlist->addWidget(&cReversable, row++, 0);
    QObject::connect(&cReversable, SIGNAL(stateChanged(int)),
                      this, SLOT(cReversableSelected(int)));
    
    label = new QLabel(
        //% "Outcomes:"
        qtTrId("route.editor.activity.event.properties.label.label.14"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vlist->addWidget(label, row++, 0, 1, 2);
    vlist->addWidget(&outcomeList, row++, 0, 1, 2);
    QPushButton *bAddOutcome = new QPushButton(
        //% "Add New"
        qtTrId("route.editor.activity.event.properties.button.b.add.outcome"));
    QPushButton *bRemoveOutcome = new QPushButton(
        //% "Remove Selected"
        qtTrId("route.editor.activity.event.properties.button.b.remove.outcome"));
    QObject::connect(bAddOutcome, SIGNAL(released()),
                      this, SLOT(bAddOutcomeSelected()));
    QObject::connect(bRemoveOutcome, SIGNAL(released()),
                      this, SLOT(bRemoveOutcomeSelected()));
    
    vlist->addWidget(bAddOutcome, row++, 0, 1, 2);
    vlist->addWidget(bRemoveOutcome, row++, 0, 1, 2);
    
    label = new QLabel(
        //% "Selected Outcome:"
        qtTrId("route.editor.activity.event.properties.label.label.15"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vlist->addWidget(label, row++, 0, 1, 2);
    
    vlist->addWidget(new QLabel(
        //% "Action:"
        qtTrId("route.editor.activity.event.properties.label.action")), row, 0);
    vlist->addWidget(&cOutcome, row++, 1);
    QObject::connect(&cOutcome, SIGNAL(textActivated(QString)),
                      this, SLOT(outcomeActoionListSelected(QString)));
    vbox->addItem(vlist);

    // Outcome Event
    
    vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    row = 0;
    label = new QLabel(
        //% "Event:"
        qtTrId("route.editor.activity.event.properties.label.label.16"));
    label->setMinimumWidth(100);
    vlist->addWidget(label, row, 0);
    cOutcomeEvent.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    vlist->addWidget(&cOutcomeEvent, row++, 1);
    QObject::connect(&cOutcomeEvent, SIGNAL(textActivated(QString)),
                      this, SLOT(cOutcomeEventSelected(QString)));
    outcomeProperties[(int)ActivityEvent::Outcome::CategoryEvent] = new QWidget(this);
    outcomeProperties[(int)ActivityEvent::Outcome::CategoryEvent]->setLayout(vlist);
    vbox->addWidget(outcomeProperties[(int)ActivityEvent::Outcome::CategoryEvent]);
    
    // Outcome info
    
    vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    row = 0;
    vlist->addWidget(new QLabel(
        //% "Message:"
        qtTrId("route.editor.activity.event.properties.label.message")), row++, 0, 1, 2);
    vlist->addWidget(&eOutcomeMessage, row++, 0, 1, 2);
    QObject::connect(&eOutcomeMessage, SIGNAL(textChanged()),
                      this, SLOT(eOutcomeMessageSelected()));
    outcomeProperties[(int)ActivityEvent::Outcome::CategoryInfo] = new QWidget(this);
    outcomeProperties[(int)ActivityEvent::Outcome::CategoryInfo]->setLayout(vlist);
    vbox->addWidget(outcomeProperties[(int)ActivityEvent::Outcome::CategoryInfo]);
    
    // Outcome Sound File
    
    vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    row = 0;
    label = new QLabel(
        //% "Sound File:"
        qtTrId("route.editor.activity.event.properties.label.label.17"));
    label->setMinimumWidth(100);
    vlist->addWidget(label, row, 0);
    eSoundFileName.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    vlist->addWidget(&eSoundFileName, row++, 1);
    QObject::connect(&eSoundFileName, SIGNAL(textEdited(QString)), this, SLOT(eSoundFileNameEdited(QString)));
    vlist->addWidget(new QLabel(
        //% "Sound Type:"
        qtTrId("route.editor.activity.event.properties.label.sound.type")), row, 0);
    vlist->addWidget(&cSoundType, row++, 1);
    QObject::connect(&cSoundType, SIGNAL(textActivated(QString)), this, SLOT(cSoundTypeSelected(QString)));
    outcomeProperties[(int)ActivityEvent::Outcome::CategorySoundFile] = new QWidget(this);
    outcomeProperties[(int)ActivityEvent::Outcome::CategorySoundFile]->setLayout(vlist);
    vbox->addWidget(outcomeProperties[(int)ActivityEvent::Outcome::CategorySoundFile]);
    
    // Outcome Weather Change
    
    vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    row = 0;
    label = new QLabel(
        //% "Weather Change:"
        qtTrId("route.editor.activity.event.properties.label.label.18"));
    label->setMinimumWidth(100);
    vlist->addWidget(label, row, 0);
    cWeatherChange.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    cWeatherChange.setStyleSheet("combobox-popup: 0;");
    cWeatherChange.setMaxVisibleItems(30);
    cWeatherChange.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    vlist->addWidget(&cWeatherChange, row++, 1);
    QObject::connect(&cWeatherChange, SIGNAL(textActivated(QString)), this, SLOT(cWeatherChangeSelected(QString)));
    outcomeProperties[(int)ActivityEvent::Outcome::CategoryWeatherChange] = new QWidget(this);
    outcomeProperties[(int)ActivityEvent::Outcome::CategoryWeatherChange]->setLayout(vlist);
    vbox->addWidget(outcomeProperties[(int)ActivityEvent::Outcome::CategoryWeatherChange]);
    
    vbox->addStretch(1);
    this->setLayout(vbox);
    
    QObject::connect(&outcomeList, SIGNAL(itemClicked(QListWidgetItem*)),
                      this, SLOT(outcomeListSelected(QListWidgetItem*)));
    
    actionWidget.hide();
    actionWidgetSpeed.hide();
    actionWidgetStation.hide();
    actionWidgetSiding.hide();
    actionWidgetWagonList.hide();
    locationWidget.hide();
    timeWidget.hide();
    
    foreach(QWidget *w, outcomeProperties){
        if(w == NULL)
            continue;
        w->hide();
    }
}

ActivityEventProperties::~ActivityEventProperties() {
}

void ActivityEventProperties::showEvent(ActivityEvent *e){
    if(e == NULL){
        event = NULL;
        return;
    }
    
    if(event != NULL)
        event->unselect();
    
    event = e;
    event->select();
    
    actionWidget.hide();
    actionWidgetSpeed.hide();
    actionWidgetStation.hide();
    actionWidgetSiding.hide();
    actionWidgetWagonList.hide();
    locationWidget.hide();
    timeWidget.hide();
    cReversable.hide();
        
    if(event->category == ActivityEvent::CategoryAction){
        actionWidget.show();
        cActionType.setCurrentIndex(cActionType.findData((int)event->eventType));
        cReversable.setChecked(event->reversableEvent);
        cReversable.show();
        
        if(event->eventType == ActivityEvent::EventTypeReachSpeed){
            actionWidgetSpeed.show();
            eActionSpeed.setValue(event->speed);
        }
        if(event->eventType == ActivityEvent::EventTypePickupPassengers){
            actionWidgetStation.show();
            cStationStopAction.setCurrentIndex(event->stationStop);
        }
        if((event->eventType == ActivityEvent::EventTypeAssembleTrainAtLocation)
                || (event->eventType == ActivityEvent::EventTypeDropoffWagonsAtLocation) ){
            actionWidgetSiding.show();
            if(event->sidingItem > 0) {
                eActionSiding.setStyleSheet("color: green");
                eActionSiding.setText(QString("[")+QString::number(event->sidingItem)+"] "+event->getSidingDescription());
            } else {
                eActionSiding.setStyleSheet("color: red");
                eActionSiding.setText(
                    //% "Not Linked."
                    qtTrId("route.editor.activity.event.properties.text.not.linked"));
            }
        }
        if((event->eventType == ActivityEvent::EventTypeAssembleTrain)
                || (event->eventType == ActivityEvent::EventTypePickupWagons)
                || (event->eventType == ActivityEvent::EventTypeDropoffWagonsAtLocation)
                || (event->eventType == ActivityEvent::EventTypeAssembleTrainAtLocation) ){
            actionWidgetWagonList.show();
            wagonList.clear();
            QStringList list;
            QList<QTreeWidgetItem *> items;
            for(int i = 0; i < event->getWagonListSize(); i++ ){
                list.clear();
                list.append(event->getWagonListIdDescription(i));
                list.append(event->getWagonListDescription(i));
                QTreeWidgetItem *item = new QTreeWidgetItem((QTreeWidget*)0, list, i );
                //item->setCheckState(0, Qt::Unchecked);
                items.append(item);
            }  
            wagonList.insertTopLevelItems(0, items);
        }
    }
    if(event->category == ActivityEvent::CategoryLocation){
        locationWidget.show();
        if(event->location != NULL){
            eLocationRadius.setValue(event->location[4]);
            cLocationStop.blockSignals(true);
            if(event->triggerOnStop != 1){
                cLocationStop.setChecked(false);
            } else {
                cLocationStop.setChecked(true);
            }
            cLocationStop.blockSignals(false);
                
            eLocationPosition.setText(QString::number(event->location[0]) +" "+ QString::number(event->location[1]) +" "+ QString::number(event->location[2]) +" "+ QString::number(event->location[3]));
        }
    }
    if(event->category == ActivityEvent::CategoryTime){
        timeWidget.show();
        //QTime time(0, 0);
        eTime.blockSignals(true);
        eTime.setTime(QTime::fromMSecsSinceStartOfDay(event->time*1000));
        eTime.blockSignals(false);
    }
    
    eName.setText(event->name);
    eName.setCursorPosition(0);
    eActivationLevel.setValue(event->activationLevel);
    eTriggeredText.setText(event->textToDisplayOnCompletionIfTriggered);
    eTriggeredText.setCursorPosition(0);
    eUntriggeredText.setText(event->textToDisplayOnCompletionIfNotTriggered);
    eUntriggeredText.setCursorPosition(0);
    eNotes.setText(event->textToDisplayDescriptionOfTask);
    eNotes.setCursorPosition(0);
    cAutoContinueLabel.blockSignals(true);
    if(event->ortsContinue != -99999){
        cAutoContinueLabel.setChecked(true);
        eAutoContinue.show();
        eAutoContinue.setValue(event->ortsContinue);
    } else {
        cAutoContinueLabel.setChecked(false);
        eAutoContinue.hide();
    }
    cAutoContinueLabel.blockSignals(false);

    
    outcomeList.clear();
    for(int i = 0; i < event->outcomes.size(); i++){
       new QListWidgetItem(translatedActivityId(
               ActivityEvent::Outcome::OutcomeTypeDescription[event->outcomes[i]->type]),
               &outcomeList, i);
    }
    
    if(outcomeList.count() > 0){
        outcomeList.item(0)->setSelected(true);
        outcomeList.setCurrentRow(0);
        outcomeListSelected(outcomeList.item(0));
    }
}

void ActivityEventProperties::bActionSidingSelected(){
    if(event == NULL)
        return;
    bool ok = event->setSidingFromSelected();
    if(!ok){
        QMessageBox msgBox;
        msgBox.setWindowTitle(
            //% "Siding not linked!"
            qtTrId("route.editor.activity.event.properties.title.siding.not.linked"));
        msgBox.setText(
            //% "Select siding before using this button."
            qtTrId("route.editor.activity.event.properties.text.select.siding.before.using.this.button"));
        msgBox.exec();
        return;
    }
    int id = outcomeList.currentRow();
    this->showEvent(event);   
    selctOutcomeOnList(id);
}

void ActivityEventProperties::bAddOutcomeSelected(){
    if(event == NULL)
        return;
    int id = event->newOutcome();
    this->showEvent(event);   
    selctOutcomeOnList(id);
}

void ActivityEventProperties::bRemoveOutcomeSelected(){
    if(event == NULL)
        return;
    int id = outcomeList.currentRow();
    if(id < 0)
        return;
    event->removeOutcome(id);
    this->showEvent(event);
    id--;
    if(id < 0)
        id = 0;
    selctOutcomeOnList(id);
}

void ActivityEventProperties::outcomeListSelected(QListWidgetItem* item){
    if(event == NULL)
        return;
    outcome = event->outcomes[item->type()];
    if(outcome == NULL)
        return;
    
    foreach(QWidget *w, outcomeProperties){
        if(w == NULL)
            continue;
        w->hide();
    }
        
    cOutcome.setCurrentIndex(cOutcome.findData((int)outcome->type));
    
    if(outcomeProperties[(int)outcome->category] == NULL)
        return;
    outcomeProperties[(int)outcome->category]->show();
    
    if(outcome->category == ActivityEvent::Outcome::CategoryInfo){
        QString txt = outcome->value.toString();
        txt.replace("\\n","\n");
        eOutcomeMessage.blockSignals(true);
        eOutcomeMessage.setPlainText(txt);
        eOutcomeMessage.blockSignals(false);
    }
    
    if(outcome->category == ActivityEvent::Outcome::CategoryEvent){
        int index = cOutcomeEvent.findData((int)outcome->value.toInt());
        cOutcomeEvent.setCurrentIndex(index);
    }
    
    if(outcome->category == ActivityEvent::Outcome::CategorySoundFile){
        eSoundFileName.setText(outcome->value.toStringList()[0]);
        cSoundType.setCurrentIndex(cSoundType.findData(outcome->value.toStringList()[1]));
    }
    
    if(outcome->category == ActivityEvent::Outcome::CategoryWeatherChange){
        cWeatherChange.clear();
        QMapIterator<QString, OrtsWeatherChange*> i3(OrtsWeatherChange::OrtsWeatherChanges);
        while (i3.hasNext()) {
            i3.next();
            cWeatherChange.addItem(i3.key(), i3.key());
        }
        int index = cWeatherChange.findData(outcome->value.toString());
        cWeatherChange.setCurrentIndex(index);
    }
}

void ActivityEventProperties::cActionTypeSelected(QString item){
    if(event == NULL)
        return;

    event->setActionToNewType((ActivityEvent::EventType)cActionType.currentData().toInt());
    int id = outcomeList.currentRow();
    showEvent(event);
    selctOutcomeOnList(id);
}

void ActivityEventProperties::cStationStopActionSelected(QString item){
    if(event == NULL)
        return;
    event->setStationStop(cStationStopAction.currentData().toInt());
}

void ActivityEventProperties::outcomeActoionListSelected(QString item){
    if(outcome == NULL)
        return;

    outcome->setToNewType((ActivityEvent::Outcome::OutcomeType)cOutcome.currentData().toInt());
    
    int id = outcomeList.currentRow();
    showEvent(event);
    selctOutcomeOnList(id);
}

void ActivityEventProperties::cOutcomeEventSelected(QString val){
    if(outcome == NULL)
        return;
    outcome->setEventLinkId(cOutcomeEvent.currentData().toInt());
}

void ActivityEventProperties::eSoundFileNameEdited(QString val){
    if(outcome == NULL)
        return;
    outcome->setSoundFileName(val);
}

void ActivityEventProperties::cSoundTypeSelected(QString val){
    if(outcome == NULL)
        return;
    Q_UNUSED(val);
    outcome->setSoundType(cSoundType.currentData().toString());
}

void ActivityEventProperties::cWeatherChangeSelected(QString val){
    if(outcome == NULL)
        return;
    Q_UNUSED(val);
    outcome->setWeatherName(cWeatherChange.currentData().toString());
}

void ActivityEventProperties::setEventList(QMap<int, QString> eventNames){
    cOutcomeEvent.clear();
    QMapIterator<int, QString> i1(eventNames);
    while (i1.hasNext()) {
        i1.next();
        //qDebug() << "item"<< i1.key() << i1.value();
        cOutcomeEvent.addItem(i1.value(), i1.key());
    }
}

void ActivityEventProperties::setStationStopList(QMap<int, QString> eventNames){
    cStationStopAction.clear();
    QMapIterator<int, QString> i1(eventNames);
    while (i1.hasNext()) {
        i1.next();
        //qDebug() << "item"<< i1.key() << i1.value();
        cStationStopAction.addItem(i1.value(), i1.key());
    }
}

void ActivityEventProperties::selctOutcomeOnList(int id){
    if(id < 0)
        return;
    if(outcomeList.count() > id){
        outcomeList.item(id)->setSelected(true);
        outcomeList.setCurrentRow(id);
        outcomeListSelected(outcomeList.item(id));
    }
}

void ActivityEventProperties::eOutcomeMessageSelected(){
    if(outcome == NULL)
        return;
    outcome->setMessage(eOutcomeMessage.toPlainText());
}

void ActivityEventProperties::eActivationLevelSelected(){
    if(event == NULL)
        return;
    event->setActivationLevel(eActivationLevel.value());
}

void ActivityEventProperties::eNameSelected(){
    if(event == NULL)
        return;
    if(eName.text() == event->name)
        return;
    event->setName(eName.text());
    emit eventNameChanged(event->id);
}

void ActivityEventProperties::eTriggeredTextSelected(QString val){
    if(event == NULL)
        return;
    event->setTriggeredText(val);
}

void ActivityEventProperties::eUntriggeredTextSelected(QString val){
    if(event == NULL)
        return;
    event->setUntriggeredText(val);
}

void ActivityEventProperties::eActionSpeedSelected(){
    if(event == NULL)
        return;
    
    event->setSpeed(eActionSpeed.value());
}

void ActivityEventProperties::eNotesSelected(QString val){
    if(event == NULL)
        return;
    event->setNotes(val);
}

void ActivityEventProperties::eLocationRadiusSelected(){
    if(event == NULL)
        return;
    event->setLocationRadius(eLocationRadius.value());
}

void ActivityEventProperties::cLocationStopSelected(int val){
    if(event == NULL)
        return;
    if(val == Qt::Checked)
        event->setLocationStop(true);
    else
        event->setLocationStop(false);
}

void ActivityEventProperties::cReversableSelected(int val){
    if(event == NULL)
        return;
    if(val == Qt::Checked)
        event->setReversable(true);
    else
        event->setReversable(false);
}

void ActivityEventProperties::eAutoContinueSelected(){
    if(event == NULL)
        return;
    event->setAutoContinue(eAutoContinue.value());
}

void ActivityEventProperties::cAutoContinueLabelSelected(int val){
    if(event == NULL)
        return;
    if(val == Qt::Checked){
        event->setAutoContinue(0);
        eAutoContinue.show();
        eAutoContinue.setValue(0);
    } else { 
        event->setAutoContinue(-99999);
        eAutoContinue.hide();
    }
}

void ActivityEventProperties::eTimeSelected(QTime val){
    if(event == NULL)
        return;
    event->setTime(val.msecsSinceStartOfDay()/1000);
}

void ActivityEventProperties::bJumpToEventLocationSelected(){
    if(event == NULL)
        return;
    if(coordinate == NULL)
        coordinate = new PreciseTileCoordinate();
    
    coordinate->TileX = event->location[0];
    coordinate->TileZ = event->location[1];
    coordinate->setWxyz(event->location[2], 0, event->location[3]);
    
    emit jumpTo(coordinate);
}

void ActivityEventProperties::bPickEventLocationSelected(bool val){
    if(event == NULL)
        return;
    if(val)
        emit enableTool("pickNewEventLocationTool");
    else
        emit enableTool("");
}

void ActivityEventProperties::bPickNewEventWagonToolSelected(){
    if(event == NULL)
        return;
    event->addSelectedWagonToList();
    
    int id = outcomeList.currentRow();
    showEvent(event);
    selctOutcomeOnList(id);
}

void ActivityEventProperties::msg(QString text, QString val){
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

void ActivityEventProperties::bJumpToCarSelected(){
    if(event == NULL)
        return;
    float position[5];
    bool ok = event->getWagonListItemPosition(wagonList.currentItem()->type(), position);
    if(!ok)
        return;
    
    if(coordinate == NULL)
        coordinate = new PreciseTileCoordinate();
    
    coordinate->TileX = position[0];
    coordinate->TileZ = position[1];
    coordinate->setWxyz(position[2], 0, position[4]);
    
    emit jumpTo(coordinate);
}

void ActivityEventProperties::bRemoveCarSelected(){
    if(event == NULL)
        return;
    event->removeWagonListItem(wagonList.currentItem()->type());
    
    int id = outcomeList.currentRow();
    showEvent(event);
    selctOutcomeOnList(id);
}

void ActivityEventProperties::bDescCarSelected(){
    if(event == NULL)
        return;
    EditFileNameDialog dialog;
    dialog.setWindowTitle(
        //% "Wagon Item Description."
        qtTrId("route.editor.activity.event.properties.title.wagon.item.description"));
    qDebug() << wagonList.currentItem()->type();
    dialog.name.setText(event->getWagonListItemDescription(wagonList.currentItem()->type()));
    dialog.exec();
    if(dialog.isOk)
        event->setWagonListItemDescription(wagonList.currentItem()->type(), dialog.name.text());
    
    int id = outcomeList.currentRow();
    showEvent(event);
    selctOutcomeOnList(id);
}