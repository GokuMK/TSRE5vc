/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ACTIVITYTIMETABLEWINDOW_H
#define	ACTIVITYTIMETABLEWINDOW_H

#include <QWidget>
#include <QtWidgets>
#include <tsre/trains/ActivityTrains.h>

class Activity;
class ActivityTimetableProperties;
class PreciseTileCoordinate;

// The activity's trains (task editor 07, phase 7): the player's, its AI
// trains and its traffic pattern's runs it does not have, by start time;
// where the chosen train's times are saved; its timetable. The chosen AI
// train, or all, are shown on the map and in 3D. While a simulation runs
// the activity (task simulation 01): each train's state, the chosen
// train's events, and Follow, the camera following it.
class ActivityTimetableWindow : public QWidget {
    Q_OBJECT
public:
    ActivityTimetableWindow(QWidget* parent);
    virtual ~ActivityTimetableWindow();
    ActivityTimetableProperties *timetableProperties = NULL;
    
public slots:
    void showTimetable(Activity *a);
    // A train chosen on the map or in 3D, by its selection number: 0 the
    // player's, an AI train's place in the activity's traffic + 1.
    void selectTrain(int shownId);
    // Undo replaces activity data (ActivityNotifier).
    void activityDataRestoring();
    void activityDataRestored();
    
signals:
    void jumpTo(PreciseTileCoordinate *c);
    // Make Own Copy... added a traffic pattern and the activity names it.
    void trafficPatternAdded();

protected:
    bool event(QEvent *e) override;

private slots:
    void trainChosen();
    void addToActivity();
    void removeFromActivity();
    void makeOwnCopy();
    void jumpToTrain();
    void showAllToggled(bool on);
    void timetableEdited();
    void followToggled(bool on);
    // The simulation started, stopped or changed what it does.
    void simulationChanged();
    void showStates();
    void showEvents(int first);
    
private:
    QTreeWidget trains;
    QCheckBox showAll;
    QPushButton *bAdd = NULL;
    QPushButton *bRemove = NULL;
    QPushButton *bJump = NULL;
    QPushButton *bFollow = NULL;
    QWidget *simulationBox = NULL;
    QListWidget events;
    QLabel savedIn;
    QPushButton *bCopy = NULL;
    Activity *activity = NULL;
    QVector<ActivityTrains::Train> list;
    PreciseTileCoordinate *coordinate = NULL;
    // The chosen train, or none.
    const ActivityTrains::Train *chosen() const;
    // The rows again, the train chosen kept (by kind, service and time).
    void fillTrains();
    void choose(ActivityTrains::Kind kind, const QString &service, int time);
    void showChosen();
    // The simulation's train of a row, or -1.
    int simulated(const ActivityTrains::Train &train) const;
};

#endif	/* ACTIVITYTIMETABLEWINDOW_H */

