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
class OrTrainProperties;
class PreciseTileCoordinate;

// The activity's trains (task editor 07, phase 7): the player's, its AI
// trains and its traffic pattern's runs it does not have, by start time;
// where the chosen train's times are saved; its timetable. The chosen AI
// train, or all, are shown on the map and in 3D. While a simulation runs
// the activity (task simulation 01): each train's state, the chosen
// train's events, and Follow, the camera following it. Simulate (task
// simulation 04): the activity run as fast as possible, the times it gives
// written into the timetables, what changed shown.
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
    // An Open Rails timetable set's trains (the user's review of task
    // simulation 08): a new train, or a copy of the chosen one, named so, in
    // the chosen train's timetable after it (the first timetable's end
    // without one), then chosen; the chosen train gone. Each an undo step.
    // A name already in the set, empty or one Open Rails reads otherwise
    // ('#', '$', the separator) is refused: the reason, else empty.
    QString addTimetableTrain(const QString &name, bool copy);
    void removeTimetableTrain();
    
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
    // Simulate: the run, the times written (one undo step).
    void simulateTimetables();
    void orTrainEdited();
    void newTrainAsked();
    void cloneTrainAsked();
    void removeTrainAsked();
    
private:
    QTreeWidget trains;
    QCheckBox showAll;
    QPushButton *bAdd = NULL;
    QPushButton *bRemove = NULL;
    QPushButton *bJump = NULL;
    QPushButton *bFollow = NULL;
    QWidget *simulationBox = NULL;
    // An MSTS activity's train's information and stops, an Open Rails
    // timetable's train's (task simulation 08).
    QWidget *mstsInfo = NULL;
    QWidget *mstsStops = NULL;
    QWidget *orInfo = NULL;
    QWidget *orBox = NULL;
    QPushButton *bNewTrain = NULL;
    QPushButton *bCloneTrain = NULL;
    QPushButton *bRemoveTrain = NULL;
    // The train to choose after the list is filled again.
    void chooseTrainNamed(const QString &name);
    // The State column and the chosen train's events while a simulation
    // shows the activity: when it starts or stops, and when the window
    // opens (it may have started while the window was closed).
    void showSimulating();
    OrTrainProperties *orTrain = NULL;
    QListWidget events;
    QLabel savedIn;
    QPushButton *bCopy = NULL;
    QPushButton *bSimulate = NULL;
    QComboBox roundTo;
    QComboBox timesFor;
    QCheckBox fromScratch;
    QLabel lastRun;
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
    // Simulate's controls for the activity (whose times it may write), and
    // what its last run did.
    void showSimulate();
};

#endif	/* ACTIVITYTIMETABLEWINDOW_H */

