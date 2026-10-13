/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ActivityEventWindow.h"
#include <tsre/trains/ActivityNotifier.h>
#include <routeEditor/activity/ActivityEdit.h>
#include "ActivityEventProperties.h"
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityEvent.h>

ActivityEventWindow::ActivityEventWindow(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::WindowType::Tool);
    setWindowTitle(
        //% "Activity Events"
        qtTrId("route.editor.activity.event.window.title.activity.events"));
    
    eventProperties = new ActivityEventProperties(this);
    
    QVBoxLayout *actionListLayout = new QVBoxLayout;
    actionListLayout->setContentsMargins(0,0,0,0);
    actionListLayout->setSpacing(0);
    QPushButton *bNewActionEvent = new QPushButton(
        //% "New Action Event"
        qtTrId("route.editor.activity.event.window.button.b.new.action.event"));
    QObject::connect(bNewActionEvent, SIGNAL(released()),
                      this, SLOT(bNewEventSelected()));
    QPushButton *bDeleteActionEvent = new QPushButton(
        //% "Delete"
        qtTrId("route.editor.activity.event.window.button.b.delete.action.event"));
    QObject::connect(bDeleteActionEvent, SIGNAL(released()),
                      this, SLOT(bDeleteEventSelected()));
    actionListLayout->addWidget(&actionList);
    actionListLayout->addWidget(bNewActionEvent);
    actionListLayout->addWidget(bDeleteActionEvent);
    actionList.setFixedWidth(170);
    actionList.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QVBoxLayout *locationListLayout = new QVBoxLayout;
    locationListLayout->setContentsMargins(0,0,0,0);
    locationListLayout->setSpacing(0);
    QPushButton *bNewLocationEvent = new QPushButton(
        //% "New Location Event"
        qtTrId("route.editor.activity.event.window.button.b.new.location.event"));
    QObject::connect(bNewLocationEvent, SIGNAL(released()),
                      this, SLOT(bNewEventSelected()));
    QPushButton *bDeleteLocationEvent = new QPushButton(
        //% "Delete"
        qtTrId("route.editor.activity.event.window.button.b.delete.location.event"));
    QObject::connect(bDeleteLocationEvent, SIGNAL(released()),
                      this, SLOT(bDeleteEventSelected()));
    locationListLayout->addWidget(&locationList);
    locationListLayout->addWidget(bNewLocationEvent);
    locationListLayout->addWidget(bDeleteLocationEvent);
    locationList.setFixedWidth(170);
    locationList.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    QVBoxLayout *timeListLayout = new QVBoxLayout;
    timeListLayout->setContentsMargins(0,0,0,0);
    timeListLayout->setSpacing(0);
    QPushButton *bNewTimeEvent = new QPushButton(
        //% "New Time Event"
        qtTrId("route.editor.activity.event.window.button.b.new.time.event"));
    QObject::connect(bNewTimeEvent, SIGNAL(released()),
                      this, SLOT(bNewEventSelected()));
    QPushButton *bDeleteTimeEvent = new QPushButton(
        //% "Delete"
        qtTrId("route.editor.activity.event.window.button.b.delete.time.event"));
    QObject::connect(bDeleteTimeEvent, SIGNAL(released()),
                      this, SLOT(bDeleteEventSelected()));
    timeListLayout->addWidget(&timeList);
    timeListLayout->addWidget(bNewTimeEvent);
    timeListLayout->addWidget(bDeleteTimeEvent);
    timeList.setFixedWidth(170);
    timeList.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    actionWidget.setLayout(actionListLayout);
    locationWidget.setLayout(locationListLayout);
    timeWidget.setLayout(timeListLayout);
    
    QTabWidget *tabWidget = new QTabWidget;
    tabWidget->setContentsMargins(1,1,1,1);
    tabWidget->addTab(&actionWidget,
        //% "Action"
        qtTrId("route.editor.activity.event.window.tab.action"));
    tabWidget->addTab(&locationWidget,
        //% "Location"
        qtTrId("route.editor.activity.event.window.tab.location"));
    tabWidget->addTab(&timeWidget,
        //% "Time"
        qtTrId("route.editor.activity.event.window.tab.time"));
    
    QHBoxLayout *v = new QHBoxLayout;
    v->setSpacing(2);
    v->setContentsMargins(1,1,1,1);
    v->addWidget(tabWidget);
    v->addWidget(eventProperties);
    this->setLayout(v);
    
    QObject::connect(&actionList, SIGNAL(itemClicked(QListWidgetItem*)),
                      this, SLOT(eventListSelected(QListWidgetItem*)));
    
    QObject::connect(&locationList, SIGNAL(itemClicked(QListWidgetItem*)),
                      this, SLOT(eventListSelected(QListWidgetItem*)));
    
    QObject::connect(&timeList, SIGNAL(itemClicked(QListWidgetItem*)),
                      this, SLOT(eventListSelected(QListWidgetItem*)));
    
    // Undo replaces activity data: let go of it before, show it after.
    QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::restoring,
                     this, &ActivityEventWindow::activityDataRestoring);
    QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::restored,
                     this, &ActivityEventWindow::activityDataRestored);
    QObject::connect(eventProperties, SIGNAL(eventNameChanged(int)),
                      this, SLOT(eventNameChanged(int)));
    
}

void ActivityEventWindow::eventNameChanged(int id){
    Q_UNUSED(id);
    if(activity == NULL)
        return;
    showEvents(activity);
}

void ActivityEventWindow::showEvents(Activity* act){
    if(this->isHidden())
        return;
    if(act != activity)
        eventProperties->showEvent(NULL);
    activity = act;

    actionList.clear();
    locationList.clear();
    timeList.clear();
    if(activity == NULL)
        return;

    for(int i = 0; i < activity->event.size(); i++ ){
        const ActivityEvent *e = activity->event[i];
        if(e == NULL)
            continue;
        if(e->category == ActivityEvent::CategoryAction){
            new QListWidgetItem ( e->name, &actionList, i );
        }
        if(e->category == ActivityEvent::CategoryLocation){
            new QListWidgetItem ( e->name, &locationList, i );
        }
        if(e->category == ActivityEvent::CategoryTime){
            new QListWidgetItem ( e->name, &timeList, i );
        }
    }

    eventProperties->setEventList(act->getEventIdNameList());
    eventProperties->setStationStopList(act->getServiceStationStopNameList());
    selectShownEvent();
}

void ActivityEventWindow::selectShownEvent(){
    if(activity == NULL || activity->currentEventSelected == NULL)
        return;
    const int index = activity->event.indexOf(activity->currentEventSelected);
    for(QListWidget *list : {&actionList, &locationList, &timeList}){
        for(int row = 0; row < list->count(); row++){
            if(list->item(row)->type() == index){
                QSignalBlocker blocker(list);
                list->setCurrentRow(row);
            }
        }
    }
}

void ActivityEventWindow::eventListSelected(QListWidgetItem * item){
    if(activity == NULL || item == NULL)
        return;
    const int index = item->type();
    if(index < 0 || index >= activity->event.size() || activity->event[index] == NULL)
        return;
    eventProperties->showEvent(activity->event[index]);
    activity->currentEventSelected = activity->event[index];

    this->resize(this->width(), this->minimumHeight());
}

void ActivityEventWindow::bNewEventSelected(){
    if(activity == NULL)
        return;
    ActivityEvent::EventCategory category = ActivityEvent::CategoryUndefined;

    if(actionWidget.isVisible())
        category = ActivityEvent::CategoryAction;
    if(timeWidget.isVisible())
        category = ActivityEvent::CategoryTime;
    if(locationWidget.isVisible())
        category = ActivityEvent::CategoryLocation;
    if(category == ActivityEvent::CategoryUndefined)
        return;

    ActivityEdit edit(activity);
    ActivityEvent *e = activity->newEvent(category);
    activity->currentEventSelected = e;
    eventProperties->showEvent(e);
    showEvents(activity);
}

void ActivityEventWindow::bDeleteEventSelected(){
    if(activity == NULL || activity->currentEventSelected == NULL)
        return;
    ActivityEdit edit(activity);
    // The properties stop showing it before it is freed.
    eventProperties->showEvent(NULL);
    activity->deleteEvent(activity->currentEventSelected);
    showEvents(activity);
}

ActivityEventWindow::~ActivityEventWindow() {
}
// The event shown is found again by its ID.
void ActivityEventWindow::activityDataRestoring(){
    restoredEventId = activity != NULL && activity->currentEventSelected != NULL
            ? activity->currentEventSelected->id : -99999;
    eventProperties->showEvent(NULL);
    actionList.clear();
    locationList.clear();
    timeList.clear();
}

void ActivityEventWindow::activityDataRestored(){
    if(activity == NULL)
        return;
    for(ActivityEvent *e : activity->event)
        if(e != NULL && e->id == restoredEventId){
            activity->currentEventSelected = e;
            eventProperties->showEvent(e);
            break;
        }
    showEvents(activity);
}
