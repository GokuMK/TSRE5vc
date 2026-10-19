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
#include <routeEditor/activity/ActivityEdit.h>
#include <routeEditor/tools/ToolButtons.h>
#include <tsre/Game.h>
#include <tsre/trains/ActivityEvent.h>
#include <tsre/geo/GeoCoordinates.h>
#include <routeEditor/properties/EditFileNameDialog.h>
#include <tsre/GameObj.h>
#include <tsre/world/objects/WorldObj.h>
#include <tsre/world/OrtsWeatherChange.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/WaitingPoint.h>

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
    buttonTools["pickSoundLocationTool"] = new QPushButton(
        //% "Pick on the map"
        qtTrId("route.editor.activity.event.properties.button.pick.sound.location"));
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
    cSoundType.addItem(
        //% "At a place"
        qtTrId("route.editor.activity.event.properties.item.location"), QString("Location"));

    
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
    eActionSpeed.setRangeSi(0, 300);
    eActionSpeed.setDecimals(1);
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
    // Open Rails: an AI train passing triggers the event.
    vlist->addWidget(new QLabel(
        //% "Triggered by:"
        qtTrId("route.editor.activity.event.properties.label.triggering.train")), row, 0);
    vlist->addWidget(&cTriggeringTrain, row++, 1, 1, 2);
    cTriggeringTrain.setToolTip(
        //% "Open Rails: an AI train of the activity triggers the event, not the player's."
        qtTrId("route.editor.activity.event.properties.tooltip.triggering.train"));
    QObject::connect(&cTriggeringTrain, SIGNAL(activated(int)), this, SLOT(cTriggeringTrainSelected(int)));
    
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
    // Where a sound "at a place" plays: tile x and z, x, y, z.
    vlist->addWidget(new QLabel(
        //% "Place:"
        qtTrId("route.editor.activity.event.properties.label.sound.location")), row, 0);
    vlist->addWidget(&eSoundLocation, row++, 1);
    eSoundLocation.setToolTip(
        //% "Tile x, tile z, x, y, z"
        qtTrId("route.editor.activity.event.properties.tooltip.sound.location"));
    QObject::connect(&eSoundLocation, SIGNAL(editingFinished()), this, SLOT(eSoundLocationEdited()));
    QObject::connect(buttonTools["pickSoundLocationTool"], SIGNAL(toggled(bool)),
                      this, SLOT(bPickSoundLocationSelected(bool)));
    vlist->addWidget(buttonTools["pickSoundLocationTool"], row++, 1);
    bSoundAtEvent = new QPushButton(
        //% "At the event's location"
        qtTrId("route.editor.activity.event.properties.button.sound.at.event"));
    QObject::connect(bSoundAtEvent, SIGNAL(released()), this, SLOT(bSoundAtEventSelected()));
    vlist->addWidget(bSoundAtEvent, row++, 1);
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
    vlist->addWidget(&cWeatherChange, row++, 1, 1, 2);
    QObject::connect(&cWeatherChange, SIGNAL(textActivated(QString)), this, SLOT(cWeatherChangeSelected(QString)));
    // Open Rails' values, each with its transition seconds.
    const QString weatherNames[4] = {
        //% "Overcast (0-1):"
        qtTrId("route.editor.activity.event.properties.weather.overcast"),
        //% "Fog:"
        qtTrId("route.editor.activity.event.properties.weather.fog.distance"),
        //% "Precipitation (0-0.02):"
        qtTrId("route.editor.activity.event.properties.weather.intensity"),
        //% "Rain (1) or snow (0):"
        qtTrId("route.editor.activity.event.properties.weather.liquidity")};
    const double weatherMax[4] = {1, 100000, 0.02, 1};
    const int weatherDecimals[4] = {2, 0, 4, 2};
    eFog.setRangeSi(0, weatherMax[1]);
    eFog.setDecimals(0);
    eFog.setSingleStep(100);
    QObject::connect(&eFog, SIGNAL(siChanged(double)), this, SLOT(weatherEdited()));
    for(int i = 0; i < 4; i++){
        cWeatherSet[i].setText(weatherNames[i]);
        eWeatherValue[i].setRange(0, weatherMax[i]);
        eWeatherValue[i].setDecimals(weatherDecimals[i]);
        eWeatherValue[i].setSingleStep(weatherMax[i] / 20);
        eWeatherTime[i].setRange(0, 86400);
        eWeatherTime[i].setSuffix(" s");
        vlist->addWidget(&cWeatherSet[i], row, 0);
        vlist->addWidget(i == 1 ? static_cast<QWidget *>(&eFog) : &eWeatherValue[i], row, 1);
        vlist->addWidget(&eWeatherTime[i], row++, 2);
        QObject::connect(&cWeatherSet[i], SIGNAL(toggled(bool)), this, SLOT(weatherEdited()));
        QObject::connect(&eWeatherValue[i], SIGNAL(valueChanged(double)), this, SLOT(weatherEdited()));
        QObject::connect(&eWeatherTime[i], SIGNAL(valueChanged(int)), this, SLOT(weatherEdited()));
    }
    outcomeProperties[(int)ActivityEvent::Outcome::CategoryWeatherChange] = new QWidget(this);
    outcomeProperties[(int)ActivityEvent::Outcome::CategoryWeatherChange]->setLayout(vlist);
    vbox->addWidget(outcomeProperties[(int)ActivityEvent::Outcome::CategoryWeatherChange]);

    // Outcome Restart a Waiting Train (Open Rails)

    vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    row = 0;
    vlist->addWidget(new QLabel(
        //% "AI train:"
        qtTrId("route.editor.activity.event.properties.label.restart.train")), row, 0);
    vlist->addWidget(&cRestartTrain, row++, 1);
    QObject::connect(&cRestartTrain, SIGNAL(activated(int)), this, SLOT(cRestartTrainSelected(int)));
    vlist->addWidget(new QLabel(
        //% "Delay:"
        qtTrId("route.editor.activity.event.properties.label.restart.delay")), row, 0);
    eRestartDelay.setRange(0, 86400);
    eRestartDelay.setSuffix(" s");
    vlist->addWidget(&eRestartDelay, row++, 1);
    QObject::connect(&eRestartDelay, SIGNAL(valueChanged(int)), this, SLOT(eRestartDelayEdited()));
    vlist->addWidget(new QLabel(
        //% "Waiting point:"
        qtTrId("route.editor.activity.event.properties.label.restart.waiting.point")), row, 0);
    cRestartWaitingPoint.setEditable(true);
    cRestartWaitingPoint.setToolTip(
        //% "The value of the waiting point in the AI train's path where it waits"
        qtTrId("route.editor.activity.event.properties.tooltip.restart.waiting.point"));
    vlist->addWidget(&cRestartWaitingPoint, row++, 1);
    QObject::connect(&cRestartWaitingPoint, SIGNAL(activated(int)), this, SLOT(cRestartWaitingPointEdited()));
    QObject::connect(cRestartWaitingPoint.lineEdit(), SIGNAL(editingFinished()), this, SLOT(cRestartWaitingPointEdited()));
    outcomeProperties[(int)ActivityEvent::Outcome::CategoryRestartTrain] = new QWidget(this);
    outcomeProperties[(int)ActivityEvent::Outcome::CategoryRestartTrain]->setLayout(vlist);
    vbox->addWidget(outcomeProperties[(int)ActivityEvent::Outcome::CategoryRestartTrain]);
    
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
    // No event shown yet.
    setEnabled(false);
}

ActivityEventProperties::~ActivityEventProperties() {
}

// Shows the event, or nothing: then the panel is empty and disabled. The
// fields are filled with their signals blocked where a signal would edit.
void ActivityEventProperties::showEvent(ActivityEvent *e){
    if(event != NULL && event != e)
        event->unselect();
    if(eventActivity() != NULL)
        eventActivity()->currentOutcomeSelected = NULL;
    outcome = NULL;
    if(e == NULL){
        event = NULL;
        actionWidget.hide();
        locationWidget.hide();
        timeWidget.hide();
        cReversable.hide();
        foreach(QWidget *w, outcomeProperties){
            if(w != NULL)
                w->hide();
        }
        outcomeList.clear();
        wagonList.clear();
        eName.clear();
        eTriggeredText.clear();
        eUntriggeredText.clear();
        eNotes.clear();
        setEnabled(false);
        return;
    }
    setEnabled(true);

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
        {
            QSignalBlocker blocker(cReversable);
            cReversable.setChecked(event->reversableEvent);
        }
        cReversable.show();
        
        if(event->eventType == ActivityEvent::EventTypeReachSpeed){
            actionWidgetSpeed.show();
            eActionSpeed.setSi(event->speed);
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
        fillTrains(cTriggeringTrain, event->triggeringService, event->triggeringTime);
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
    ActivityEdit edit(eventActivity());
    bool ok = event->setSidingFromSelected();
    if(!ok){
        edit.cancel();
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
    ActivityEdit edit(eventActivity());
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
    ActivityEdit edit(eventActivity());
    outcome = NULL;
    event->removeOutcome(id);
    this->showEvent(event);
    id--;
    if(id < 0)
        id = 0;
    selctOutcomeOnList(id);
}

void ActivityEventProperties::outcomeListSelected(QListWidgetItem* item){
    outcome = NULL;
    if(eventActivity() != NULL)
        eventActivity()->currentOutcomeSelected = NULL;
    if(event == NULL || item == NULL)
        return;
    if(item->type() < 0 || item->type() >= event->outcomes.size())
        return;
    outcome = event->outcomes[item->type()];
    if(outcome == NULL)
        return;
    if(eventActivity() != NULL)
        eventActivity()->currentOutcomeSelected = outcome;
    
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
        const QStringList sound = outcome->value.toStringList();
        eSoundFileName.setText(sound.value(0));
        cSoundType.setCurrentIndex(cSoundType.findData(sound.value(1)));
        const bool at = sound.value(1) == "Location";
        eSoundLocation.setEnabled(at);
        buttonTools["pickSoundLocationTool"]->setEnabled(at);
        bSoundAtEvent->setEnabled(at && event->location != NULL);
        eSoundLocation.setText(outcome->soundAt ? QString("%1 %2 %3 %4 %5").arg(outcome->soundTile[0]).arg(outcome->soundTile[1])
                .arg(outcome->soundPosition[0]).arg(outcome->soundPosition[1]).arg(outcome->soundPosition[2]) : QString());
    }
    if(outcome->category == ActivityEvent::Outcome::CategoryRestartTrain){
        fillTrains(cRestartTrain, outcome->restartService, outcome->restartTime);
        QSignalBlocker blocker(eRestartDelay);
        eRestartDelay.setValue(outcome->restartDelay);
        fillWaitingPoints(outcome->restartService, outcome->restartWaitingPoint);
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
        for(int i = 0; i < 4; i++){
            QSignalBlocker set(cWeatherSet[i]), value(eWeatherValue[i]), fog(eFog), time(eWeatherTime[i]);
            cWeatherSet[i].setChecked(outcome->weather[i] >= 0);
            if(i == 1)
                eFog.setSi(std::max(0.0f, outcome->weather[i]));
            else
                eWeatherValue[i].setValue(std::max(0.0f, outcome->weather[i]));
            eWeatherTime[i].setValue(std::max(0, outcome->weatherTime[i]));
            (i == 1 ? static_cast<QWidget &>(eFog) : eWeatherValue[i]).setEnabled(outcome->weather[i] >= 0);
            eWeatherTime[i].setEnabled(outcome->weather[i] >= 0);
        }
    }
}

void ActivityEventProperties::cActionTypeSelected(QString item){
    if(event == NULL)
        return;

    ActivityEdit edit(eventActivity());
    event->setActionToNewType((ActivityEvent::EventType)cActionType.currentData().toInt());
    int id = outcomeList.currentRow();
    showEvent(event);
    selctOutcomeOnList(id);
}

void ActivityEventProperties::cStationStopActionSelected(QString item){
    if(event == NULL)
        return;
    ActivityEdit edit(eventActivity());
    event->setStationStop(cStationStopAction.currentData().toInt());
}

void ActivityEventProperties::outcomeActoionListSelected(QString item){
    if(outcome == NULL)
        return;

    ActivityEdit edit(eventActivity());
    outcome->setToNewType((ActivityEvent::Outcome::OutcomeType)cOutcome.currentData().toInt());
    
    int id = outcomeList.currentRow();
    showEvent(event);
    selctOutcomeOnList(id);
}

void ActivityEventProperties::cOutcomeEventSelected(QString val){
    if(outcome == NULL)
        return;
    ActivityEdit edit(eventActivity());
    outcome->setEventLinkId(cOutcomeEvent.currentData().toInt());
}

void ActivityEventProperties::eSoundFileNameEdited(QString val){
    if(outcome == NULL)
        return;
    ActivityEdit edit(eventActivity(), true);
    outcome->setSoundFileName(val);
}

void ActivityEventProperties::cSoundTypeSelected(QString val){
    if(outcome == NULL)
        return;
    Q_UNUSED(val);
    {
        ActivityEdit edit(eventActivity());
        outcome->setSoundType(cSoundType.currentData().toString());
    }
    // A sound at a place shows its place.
    const int id = outcomeList.currentRow();
    showEvent(event);
    selctOutcomeOnList(id);
}

void ActivityEventProperties::cWeatherChangeSelected(QString val){
    if(outcome == NULL)
        return;
    Q_UNUSED(val);
    {
        ActivityEdit edit(eventActivity());
        outcome->setWeatherName(cWeatherChange.currentData().toString());
    }
    const int id = outcomeList.currentRow();
    showEvent(event);
    selctOutcomeOnList(id);
}

void ActivityEventProperties::fillTrains(QComboBox &combo, const QString &service, int time){
    QSignalBlocker blocker(combo);
    combo.clear();
    combo.addItem(
        //% "(none)"
        qtTrId("route.editor.activity.event.properties.item.no.train"), QStringList());
    Activity *activity = eventActivity();
    if(activity != NULL && activity->traffic != NULL)
        for(const ActivityServiceDefinition &train : activity->traffic->service)
            combo.addItem(QString("%1 %2").arg(train.name,
                          QTime::fromMSecsSinceStartOfDay(((train.time % 86400) + 86400) % 86400 * 1000).toString("HH:mm:ss")),
                          QStringList({train.name, QString::number(train.time)}));
    combo.setCurrentIndex(0);
    for(int i = 1; i < combo.count(); i++){
        const QStringList data = combo.itemData(i).toStringList();
        if(data.value(0).compare(service, Qt::CaseInsensitive) == 0 && (time < 0 || data.value(1).toInt() == time)){
            combo.setCurrentIndex(i);
            break;
        }
    }
    // A train named that is not in the activity is shown as it is.
    if(!service.isEmpty() && combo.currentIndex() == 0){
        combo.addItem(time >= 0 ? QString("%1 %2").arg(service).arg(time) : service,
                      QStringList({service, QString::number(time)}));
        combo.setCurrentIndex(combo.count() - 1);
    }
}

void ActivityEventProperties::fillWaitingPoints(const QString &service, int value){
    QSignalBlocker blocker(cRestartWaitingPoint);
    cRestartWaitingPoint.clear();
    Activity *activity = eventActivity();
    Service *train = activity != NULL ? ActLib::GetServiceByName(service, ContentPath::parentDirectory(activity->path)) : NULL;
    Path *path = train != NULL ? ActLib::GetPathByName(train->pathId, ContentPath::parentDirectory(activity->path)) : NULL;
    if(path != NULL)
        for(const PathTrack::Node &node : path->nodes())
            if(node.type == PathTrack::NodeType::Wait)
                cRestartWaitingPoint.addItem(QString("%1 (%2)").arg(node.waitTime).arg(WaitingPoint::describe(node.waitTime)),
                                             node.waitTime);
    int index = cRestartWaitingPoint.findData(value);
    if(index < 0){
        cRestartWaitingPoint.addItem(QString::number(value), value);
        index = cRestartWaitingPoint.count() - 1;
    }
    cRestartWaitingPoint.setCurrentIndex(index);
}

void ActivityEventProperties::cTriggeringTrainSelected(int index){
    if(event == NULL)
        return;
    const QStringList data = cTriggeringTrain.itemData(index).toStringList();
    ActivityEdit edit(eventActivity());
    event->setTriggeringTrain(data.value(0), data.size() > 1 ? data[1].toInt() : -1);
}

void ActivityEventProperties::eSoundLocationEdited(){
    if(outcome == NULL || outcome->category != ActivityEvent::Outcome::CategorySoundFile)
        return;
    const QStringList parts = eSoundLocation.text().simplified().split(' ', Qt::SkipEmptyParts);
    ActivityEdit edit(eventActivity());
    if(parts.size() == 5)
        outcome->setSoundLocation(true, parts[0].toInt(), parts[1].toInt(), parts[2].toFloat(),
                                  parts[3].toFloat(), parts[4].toFloat());
    else if(parts.isEmpty())
        outcome->setSoundLocation(false, 0, 0, 0, 0, 0);
}

void ActivityEventProperties::bSoundAtEventSelected(){
    if(event == NULL || event->location == NULL || outcome == NULL
            || outcome->category != ActivityEvent::Outcome::CategorySoundFile)
        return;
    {
        ActivityEdit edit(eventActivity());
        outcome->setSoundLocation(true, event->location[0], event->location[1], event->location[2], 0, event->location[3]);
    }
    const int id = outcomeList.currentRow();
    showEvent(event);
    selctOutcomeOnList(id);
}

void ActivityEventProperties::bPickSoundLocationSelected(bool val){
    if(outcome == NULL)
        return;
    emit enableTool(val ? "pickSoundLocationTool" : "");
}

void ActivityEventProperties::weatherEdited(){
    if(outcome == NULL || outcome->category != ActivityEvent::Outcome::CategoryWeatherChange)
        return;
    ActivityEdit edit(eventActivity(), true);
    for(int i = 0; i < 4; i++){
        const bool set = cWeatherSet[i].isChecked();
        (i == 1 ? static_cast<QWidget &>(eFog) : eWeatherValue[i]).setEnabled(set);
        eWeatherTime[i].setEnabled(set);
        const double value = i == 1 ? eFog.si() : eWeatherValue[i].value();
        outcome->setWeather(i, set ? value : -1, eWeatherTime[i].value());
    }
}

void ActivityEventProperties::cRestartTrainSelected(int index){
    if(outcome == NULL || outcome->category != ActivityEvent::Outcome::CategoryRestartTrain)
        return;
    const QStringList data = cRestartTrain.itemData(index).toStringList();
    {
        ActivityEdit edit(eventActivity());
        outcome->setRestartTrain(data.value(0), data.size() > 1 ? data[1].toInt() : -1);
    }
    fillWaitingPoints(outcome->restartService, outcome->restartWaitingPoint);
}

void ActivityEventProperties::eRestartDelayEdited(){
    if(outcome == NULL || outcome->category != ActivityEvent::Outcome::CategoryRestartTrain)
        return;
    ActivityEdit edit(eventActivity(), true);
    outcome->setRestartDelay(eRestartDelay.value());
}

void ActivityEventProperties::cRestartWaitingPointEdited(){
    if(outcome == NULL || outcome->category != ActivityEvent::Outcome::CategoryRestartTrain)
        return;
    // A waiting point chosen, or its value typed.
    bool ok = false;
    int value = cRestartWaitingPoint.currentText().section(' ', 0, 0).toInt(&ok);
    if(!ok)
        return;
    ActivityEdit edit(eventActivity());
    outcome->setRestartWaitingPoint(value);
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
    ActivityEdit edit(eventActivity(), true);
    outcome->setMessage(eOutcomeMessage.toPlainText());
}

void ActivityEventProperties::eActivationLevelSelected(){
    if(event == NULL)
        return;
    ActivityEdit edit(eventActivity());
    event->setActivationLevel(eActivationLevel.value());
}

void ActivityEventProperties::eNameSelected(){
    if(event == NULL)
        return;
    if(eName.text() == event->name)
        return;
    ActivityEdit edit(eventActivity());
    event->setName(eName.text());
    emit eventNameChanged(event->id);
}

void ActivityEventProperties::eTriggeredTextSelected(QString val){
    if(event == NULL)
        return;
    ActivityEdit edit(eventActivity(), true);
    event->setTriggeredText(val);
}

void ActivityEventProperties::eUntriggeredTextSelected(QString val){
    if(event == NULL)
        return;
    ActivityEdit edit(eventActivity(), true);
    event->setUntriggeredText(val);
}

void ActivityEventProperties::eActionSpeedSelected(){
    if(event == NULL)
        return;
    
    ActivityEdit edit(eventActivity());
    event->setSpeed(eActionSpeed.si());
}

void ActivityEventProperties::eNotesSelected(QString val){
    if(event == NULL)
        return;
    ActivityEdit edit(eventActivity(), true);
    event->setNotes(val);
}

void ActivityEventProperties::eLocationRadiusSelected(){
    if(event == NULL)
        return;
    ActivityEdit edit(eventActivity());
    event->setLocationRadius(eLocationRadius.value());
}

void ActivityEventProperties::cLocationStopSelected(int val){
    if(event == NULL)
        return;
    ActivityEdit edit(eventActivity());
    if(val == Qt::Checked)
        event->setLocationStop(true);
    else
        event->setLocationStop(false);
}

void ActivityEventProperties::cReversableSelected(int val){
    if(event == NULL)
        return;
    ActivityEdit edit(eventActivity());
    if(val == Qt::Checked)
        event->setReversable(true);
    else
        event->setReversable(false);
}

void ActivityEventProperties::eAutoContinueSelected(){
    if(event == NULL)
        return;
    ActivityEdit edit(eventActivity());
    event->setAutoContinue(eAutoContinue.value());
}

void ActivityEventProperties::cAutoContinueLabelSelected(int val){
    if(event == NULL)
        return;
    ActivityEdit edit(eventActivity());
    if(val == Qt::Checked){
        event->setAutoContinue(0);
        eAutoContinue.show();
        eAutoContinue.setValue(0);
    } else { 
        event->setAutoContinue(-99999);
        eAutoContinue.hide();
    }
}

Activity *ActivityEventProperties::eventActivity(){
    return event != NULL ? event->getParentActivity() : NULL;
}

void ActivityEventProperties::eTimeSelected(QTime val){
    if(event == NULL)
        return;
    ActivityEdit edit(eventActivity(), true);
    event->setTime(val.msecsSinceStartOfDay()/1000);
}

void ActivityEventProperties::bJumpToEventLocationSelected(){
    if(event == NULL || event->location == NULL)
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
    ActivityEdit edit(eventActivity());
    event->addSelectedWagonToList();
    
    int id = outcomeList.currentRow();
    showEvent(event);
    selctOutcomeOnList(id);
}

void ActivityEventProperties::msg(QString text, QString val){
    // A sound's place picked on the map.
    if(text == "refreshActivityEvent"){
        if(event != NULL){
            const int id = outcomeList.currentRow();
            showEvent(event);
            selctOutcomeOnList(id);
        }
        return;
    }
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

void ActivityEventProperties::bJumpToCarSelected(){
    if(event == NULL || wagonList.currentItem() == NULL)
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
    if(event == NULL || wagonList.currentItem() == NULL)
        return;
    ActivityEdit edit(eventActivity());
    event->removeWagonListItem(wagonList.currentItem()->type());
    
    int id = outcomeList.currentRow();
    showEvent(event);
    selctOutcomeOnList(id);
}

void ActivityEventProperties::bDescCarSelected(){
    if(event == NULL || wagonList.currentItem() == NULL)
        return;
    const int wagon = wagonList.currentItem()->type();
    EditFileNameDialog dialog;
    dialog.setWindowTitle(
        //% "Wagon Item Description."
        qtTrId("route.editor.activity.event.properties.title.wagon.item.description"));
    dialog.name.setText(event->getWagonListItemDescription(wagon));
    dialog.exec();
    if(dialog.isOk && event != NULL){
        ActivityEdit edit(eventActivity());
        event->setWagonListItemDescription(wagon, dialog.name.text());
    }
    
    int id = outcomeList.currentRow();
    showEvent(event);
    selctOutcomeOnList(id);
}