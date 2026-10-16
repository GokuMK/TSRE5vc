/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ACTIVITYEVENTWINDOW_H
#define	ACTIVITYEVENTWINDOW_H

#include <QtWidgets>

class ActivityEventProperties;
class Activity;

class ActivityEventWindow : public QWidget {
    Q_OBJECT
public:
    ActivityEventWindow(QWidget* parent);
    virtual ~ActivityEventWindow();
    
    ActivityEventProperties *eventProperties = NULL;
public slots:
    void showEvents(Activity* act);
    void eventListSelected(QListWidgetItem * item);
    void eventNameChanged(int id);
    void bNewEventSelected();
    void bDeleteEventSelected();
    // The event with this ID shown and chosen in its list.
    void selectEvent(int id);
    // Undo replaces activity data (ActivityNotifier).
    void activityDataRestoring();
    void activityDataRestored();
signals:
    
        
private:
    // The ID of the event shown when undo replaced the activity's events.
    int restoredEventId = -99999;
    QWidget actionWidget;
    QWidget locationWidget;
    QWidget timeWidget;
    
    QTabWidget *tabs = NULL;
    QListWidget actionList;
    QListWidget locationList;
    QListWidget timeList;
    
    
    
    Activity *activity = NULL;
    // Lists the activity's events again and selects the one shown.
    void selectShownEvent();
};

#endif	/* ACTIVITYEVENTWINDOW_H */

