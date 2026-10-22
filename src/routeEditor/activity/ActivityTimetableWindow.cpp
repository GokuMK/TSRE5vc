/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ActivityTimetableWindow.h"
#include <tsre/trains/ActivityNotifier.h>
#include "ActivityTimetableProperties.h"
#include <routeEditor/activity/OrTrainProperties.h>
#include <tsre/trains/OrTimetable.h>
#include <routeEditor/activity/ActivityEdit.h>
#include <routeEditor/properties/EditFileNameDialog.h>
#include <tsre/Game.h>
#include <tsre/gui/GuiFunct.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/Traffic.h>
#include <routeEditor/activity/ActivityEditLock.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/TimetableCalculation.h>
#include <atomic>

namespace {

enum Column { StartColumn, TrainColumn, StateColumn, PathColumn, ConsistColumn, FirstColumn, LastColumn, Columns };

QString clock(int seconds) {
    return QTime::fromMSecsSinceStartOfDay((seconds % 86400 + 86400) % 86400 * 1000).toString("HH:mm:ss");
}

// A length of time: "4:30", "1:02:00".
QString duration(int seconds) {
    if(seconds >= 3600)
        return QString("%1:%2:%3").arg(seconds / 3600).arg(seconds / 60 % 60, 2, 10, QChar('0'))
            .arg(seconds % 60, 2, 10, QChar('0'));
    return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

QString stepText(int seconds) {
    if(seconds <= 0)
        //% "none"
        return qtTrId("route.editor.activity.timetable.window.round.none");
    if(seconds % 60 == 0)
        //% "%1 min"
        return qtTrId("route.editor.activity.timetable.window.round.minutes").arg(seconds / 60);
    //% "%1 s"
    return qtTrId("route.editor.activity.timetable.window.round.seconds").arg(seconds);
}

// What a Simulate did: the time simulated, whose times it wrote and what
// changed, the trains that did not finish.
QString summary(const Sim::TimetableRun &run) {
    QStringList lines;
    const QString step = run.options.step > 0 ?
        //% "departures rounded up to %1"
        qtTrId("route.editor.activity.timetable.window.summary.step").arg(stepText(run.options.step)) :
        //% "departures not rounded"
        qtTrId("route.editor.activity.timetable.window.summary.no.step");
    //% "Simulated from %1 to %2 in %3 s, %4."
    lines << qtTrId("route.editor.activity.timetable.window.summary.run").arg(clock(int(run.from)), clock(int(run.to)))
        .arg(run.ms / 1000.0, 0, 'f', 1).arg(step);
    int written = 0, changed = 0, largest = 0;
    for(const Sim::TimetableTrain &train : run.trains){
        bool any = false;
        for(const Sim::TimetableStop &stop : train.stops)
            any = any || (stop.reached && stop.arrival >= 0);
        if(!train.written || !any)
            continue;
        ++written;
        if(train.changed() > 0)
            ++changed;
        largest = std::max(largest, train.largestChange());
    }
    QString who;
    if(written == 0)
        //% "No times written."
        who = qtTrId("route.editor.activity.timetable.window.summary.none");
    else if(run.options.playerOnly && !run.sharedWith.isEmpty())
        //% "The player's times written (the traffic pattern is shared with %1)."
        who = qtTrId("route.editor.activity.timetable.window.summary.player.shared").arg(run.sharedWith.join(", "));
    else if(run.options.playerOnly)
        //% "The player's times written."
        who = qtTrId("route.editor.activity.timetable.window.summary.player");
    else
        //% "Times written for %1 trains."
        who = qtTrId("route.editor.activity.timetable.window.summary.all").arg(written);
    if(written == 0)
        ;
    else if(changed == 0)
        //% "Nothing changed."
        who += " " + qtTrId("route.editor.activity.timetable.window.summary.unchanged");
    else if(largest > 0)
        //% "%1 changed, the largest change %2."
        who += " " + qtTrId("route.editor.activity.timetable.window.summary.changed").arg(changed).arg(duration(largest));
    else
        //% "%1 with new times."
        who += " " + qtTrId("route.editor.activity.timetable.window.summary.new").arg(changed);
    lines << who;
    for(const Sim::TimetableTrain &train : run.trains)
        if(!train.finished)
            //% "%1 did not finish: %2."
            lines << qtTrId("route.editor.activity.timetable.window.summary.stuck").arg(train.name, train.state);
    return lines.join("\n");
}

}

ActivityTimetableWindow::ActivityTimetableWindow(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::WindowType::Tool);
    setWindowTitle(
        //% "Timetable"
        qtTrId("route.editor.activity.timetable.window.title.timetable"));
    
    timetableProperties = new ActivityTimetableProperties(this);
    
    trains.setColumnCount(Columns);
    trains.setHeaderLabels({
        //% "Start"
        qtTrId("route.editor.activity.timetable.window.column.start"),
        //% "Train"
        qtTrId("route.editor.activity.timetable.window.column.train"),
        //% "State"
        qtTrId("route.editor.activity.timetable.window.column.state"),
        //% "Path"
        qtTrId("route.editor.activity.timetable.window.column.path"),
        //% "Consist"
        qtTrId("route.editor.activity.timetable.window.column.consist"),
        //% "First Stop"
        qtTrId("route.editor.activity.timetable.window.column.first"),
        //% "Last Stop"
        qtTrId("route.editor.activity.timetable.window.column.last")});
    trains.setRootIsDecorated(false);
    trains.setUniformRowHeights(true);
    trains.setSelectionMode(QAbstractItemView::SingleSelection);
    trains.header()->resizeSection(StartColumn, 70);
    trains.header()->resizeSection(TrainColumn, 170);
    trains.header()->resizeSection(StateColumn, 190);
    trains.setColumnHidden(StateColumn, true);
    trains.header()->resizeSection(PathColumn, 130);
    trains.header()->resizeSection(ConsistColumn, 130);
    trains.header()->resizeSection(FirstColumn, 130);
    trains.setMinimumHeight(120);
    showAll.setText(
        //% "Show All Trains"
        qtTrId("route.editor.activity.timetable.window.show.all"));
    showAll.setToolTip(
        //% "The activity's AI trains on the map and in 3D: their paths and their consists at their starts."
        qtTrId("route.editor.activity.timetable.window.show.all.tooltip"));
    bAdd = new QPushButton(
        //% "Add to Activity"
        qtTrId("route.editor.activity.timetable.window.button.add"));
    bRemove = new QPushButton(
        //% "Remove from Activity"
        qtTrId("route.editor.activity.timetable.window.button.remove"));
    bJump = new QPushButton(
        //% "Jump To"
        qtTrId("route.editor.activity.timetable.window.button.jump"));
    bFollow = new QPushButton(
        //% "Follow"
        qtTrId("route.editor.activity.timetable.window.button.follow"));
    bFollow->setToolTip(
        //% "The camera follows the chosen train while the simulation runs, at its cab's window. In 3D, key 1 views from its cab, key 2 its front and key 3 its back."
        qtTrId("route.editor.activity.timetable.window.button.follow.tooltip"));
    bFollow->setCheckable(true);
    bFollow->setObjectName("timetableFollow");
    bNewTrain = new QPushButton(
        //% "New Train..."
        qtTrId("route.editor.activity.timetable.window.button.new.train"));
    bCloneTrain = new QPushButton(
        //% "Clone Train..."
        qtTrId("route.editor.activity.timetable.window.button.clone.train"));
    bRemoveTrain = new QPushButton(
        //% "Remove Train"
        qtTrId("route.editor.activity.timetable.window.button.remove.train"));
    bNewTrain->setToolTip(
        //% "A new train in the chosen train's timetable, after it: then choose its path and consist and type its times (All stations)."
        qtTrId("route.editor.activity.timetable.window.button.new.train.tooltip"));
    bCloneTrain->setToolTip(
        //% "A copy of the chosen train, with all its times and commands, under a new name."
        qtTrId("route.editor.activity.timetable.window.button.clone.train.tooltip"));
    bNewTrain->setObjectName("timetableNewTrain");
    bCloneTrain->setObjectName("timetableCloneTrain");
    bRemoveTrain->setObjectName("timetableRemoveTrain");
    events.setObjectName("timetableEvents");
    bCopy = new QPushButton(
        //% "Make Own Copy..."
        qtTrId("route.editor.activity.timetable.window.button.copy"));
    bCopy->setToolTip(
        //% "A new traffic pattern with the same runs, for this activity alone."
        qtTrId("route.editor.activity.timetable.window.button.copy.tooltip"));
    savedIn.setWordWrap(true);
    savedIn.setContentsMargins(3, 2, 3, 2);
    trains.setObjectName("timetableTrains");
    showAll.setObjectName("timetableShowAll");
    bAdd->setObjectName("timetableAdd");
    bRemove->setObjectName("timetableRemove");
    bJump->setObjectName("timetableJump");
    bCopy->setObjectName("timetableCopy");
    savedIn.setObjectName("timetableSavedIn");

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addWidget(&showAll);
    buttons->addStretch(1);
    buttons->addWidget(bAdd);
    buttons->addWidget(bRemove);
    buttons->addWidget(bNewTrain);
    buttons->addWidget(bCloneTrain);
    buttons->addWidget(bRemoveTrain);
    buttons->addWidget(bJump);
    buttons->addWidget(bFollow);
    QWidget *top = new QWidget;
    QVBoxLayout *topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(2);
    topLayout->addWidget(&trains);
    topLayout->addLayout(buttons);

    // Simulate (on the left under the chosen train's information): the
    // activity run as fast as possible, its times written.
    bSimulate = new QPushButton(
        //% "Simulate"
        qtTrId("route.editor.activity.timetable.window.button.simulate"));
    bSimulate->setToolTip(
        //% "Runs the activity as fast as possible and writes the times it gives into the timetables. Departures you set since the last Simulate are kept: the train leaves no earlier."
        qtTrId("route.editor.activity.timetable.window.button.simulate.tooltip"));
    for(int seconds : {15, 30, 60, 0})
        roundTo.addItem(stepText(seconds), seconds);
    roundTo.setCurrentIndex(1);
    roundTo.setStyleSheet("combobox-popup: 0;");
    timesFor.setStyleSheet("combobox-popup: 0;");
    roundTo.setToolTip(
        //% "Departures rounded up to it, the train waiting for them; arrivals rounded down."
        qtTrId("route.editor.activity.timetable.window.round.tooltip"));
    timesFor.addItem(
        //% "All trains"
        qtTrId("route.editor.activity.timetable.window.times.all"));
    timesFor.addItem(
        //% "The player's train"
        qtTrId("route.editor.activity.timetable.window.times.player"));
    fromScratch.setText(
        //% "From scratch"
        qtTrId("route.editor.activity.timetable.window.from.scratch"));
    fromScratch.setToolTip(
        //% "Every departure calculated again: the ones you set are not kept."
        qtTrId("route.editor.activity.timetable.window.from.scratch.tooltip"));
    lastRun.setWordWrap(true);
    lastRun.setTextInteractionFlags(Qt::TextSelectableByMouse);
    lastRun.setContentsMargins(3, 2, 3, 2);
    lastRun.hide();
    bSimulate->setObjectName("timetableSimulate");
    roundTo.setObjectName("timetableRoundTo");
    timesFor.setObjectName("timetableTimesFor");
    fromScratch.setObjectName("timetableFromScratch");
    lastRun.setObjectName("timetableLastRun");
    QGridLayout *simulateOptions = new QGridLayout;
    simulateOptions->setContentsMargins(0, 0, 0, 0);
    simulateOptions->setSpacing(2);
    simulateOptions->addWidget(new QLabel(
        //% "Round to:"
        qtTrId("route.editor.activity.timetable.window.round")), 0, 0);
    simulateOptions->addWidget(&roundTo, 0, 1);
    simulateOptions->addWidget(new QLabel(
        //% "Times for:"
        qtTrId("route.editor.activity.timetable.window.times")), 1, 0);
    simulateOptions->addWidget(&timesFor, 1, 1);
    simulateOptions->addWidget(&fromScratch, 2, 0, 1, 2);
    simulateOptions->setColumnStretch(1, 1);

    // Under the trains, side by side: on the left what is known of the
    // chosen train (where its times are saved, with Make Own Copy...; its
    // start, engine and speeds) and Simulate, on the right its stops and
    // times, with the window's height.
    QWidget *saved = new QWidget;
    QVBoxLayout *savedLayout = new QVBoxLayout(saved);
    savedLayout->setContentsMargins(0, 0, 0, 0);
    savedLayout->setSpacing(4);
    savedIn.setAlignment(Qt::AlignTop | Qt::AlignLeft);
    // An Open Rails timetable's train (task simulation 08): its information
    // and commands first, where it is saved under them.
    orTrain = new OrTrainProperties;
    orInfo = ActivityEditLock::Box(orTrain->info);
    orInfo->layout()->setContentsMargins(0, 0, 0, 6);
    savedLayout->addWidget(orInfo);
    savedLayout->addWidget(&savedIn);
    savedLayout->addWidget(bCopy, 0, Qt::AlignLeft);
    // What only an MSTS activity's train has: its information and Simulate
    // (an Open Rails timetable's train, task simulation 08, has neither).
    mstsInfo = new QWidget;
    QVBoxLayout *mstsLayout = new QVBoxLayout(mstsInfo);
    mstsLayout->setContentsMargins(0, 0, 0, 0);
    mstsLayout->setSpacing(4);
    mstsLayout->addSpacing(6);
    mstsLayout->addWidget(GuiFunct::newTQLabel(
        //% "Info:"
        qtTrId("route.editor.activity.timetable.window.section.info")));
    QWidget *info = ActivityEditLock::Box(timetableProperties->info);
    mstsLayout->addWidget(info);
    mstsLayout->addSpacing(6);
    mstsLayout->addWidget(GuiFunct::newTQLabel(
        //% "Simulation:"
        qtTrId("route.editor.activity.timetable.window.section.simulation")));
    mstsLayout->addWidget(bSimulate, 0, Qt::AlignLeft);
    mstsLayout->addLayout(simulateOptions);
    mstsLayout->addWidget(&lastRun);
    savedLayout->addWidget(mstsInfo);
    // The chosen train's events while a simulation runs, taking the room
    // left (a spacer does without a simulation).
    simulationBox = new QWidget;
    QVBoxLayout *simulationLayout = new QVBoxLayout(simulationBox);
    simulationLayout->setContentsMargins(0, 6, 0, 0);
    simulationLayout->setSpacing(2);
    simulationLayout->addWidget(new QLabel(
        //% "Events in the simulation:"
        qtTrId("route.editor.activity.timetable.window.events")));
    simulationLayout->addWidget(&events);
    simulationBox->hide();
    savedLayout->addWidget(simulationBox, 1);
    savedLayout->addStretch(0);
    QWidget *stops = ActivityEditLock::Box(timetableProperties);
    // An Open Rails timetable's train's stops in place of an MSTS
    // activity's.
    QWidget *orStops = ActivityEditLock::Box(orTrain);
    QWidget *right = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(stops);
    rightLayout->addWidget(orStops);
    mstsStops = stops;
    orBox = orStops;
    QSplitter *bottom = new QSplitter(Qt::Horizontal);
    bottom->addWidget(saved);
    bottom->addWidget(right);
    bottom->setChildrenCollapsible(false);
    // The information about 40% of the width, the times the rest.
    bottom->setStretchFactor(0, 2);
    bottom->setStretchFactor(1, 3);

    QSplitter *splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(top);
    splitter->addWidget(bottom);
    splitter->setChildrenCollapsible(false);
    QVBoxLayout *v = new QVBoxLayout;
    v->setSpacing(2);
    v->setContentsMargins(1,1,1,1);
    v->addWidget(splitter);
    this->setLayout(v);
    resize(960, 600);
    bottom->setSizes({384, 576});
    // The stops and times the most of the height.
    splitter->setSizes({200, 400});
    
    // Undo replaces activity data: let go of it before, show it after.
    QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::restoring,
                     this, &ActivityTimetableWindow::activityDataRestoring);
    QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::restored,
                     this, &ActivityTimetableWindow::activityDataRestored);
    QObject::connect(&trains, &QTreeWidget::itemSelectionChanged, this, &ActivityTimetableWindow::trainChosen);
    QObject::connect(&showAll, &QCheckBox::toggled, this, &ActivityTimetableWindow::showAllToggled);
    QObject::connect(bAdd, &QPushButton::released, this, &ActivityTimetableWindow::addToActivity);
    QObject::connect(bRemove, &QPushButton::released, this, &ActivityTimetableWindow::removeFromActivity);
    QObject::connect(bJump, &QPushButton::released, this, &ActivityTimetableWindow::jumpToTrain);
    QObject::connect(bCopy, &QPushButton::released, this, &ActivityTimetableWindow::makeOwnCopy);
    QObject::connect(bNewTrain, &QPushButton::released, this, &ActivityTimetableWindow::newTrainAsked);
    QObject::connect(bCloneTrain, &QPushButton::released, this, &ActivityTimetableWindow::cloneTrainAsked);
    QObject::connect(bRemoveTrain, &QPushButton::released, this, &ActivityTimetableWindow::removeTrainAsked);
    QObject::connect(timetableProperties, &ActivityTimetableProperties::edited,
                     this, &ActivityTimetableWindow::timetableEdited);
    QObject::connect(bFollow, &QPushButton::toggled, this, &ActivityTimetableWindow::followToggled);
    QObject::connect(bSimulate, &QPushButton::released, this, &ActivityTimetableWindow::simulateTimetables);
    // The user's choice, kept while a shared pattern forces the player's.
    QObject::connect(&timesFor, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        timesFor.setProperty("chosen", index);
    });
    // While a simulation exists its times show but are not edited.
    ActivityEditLock::Follow(this, {info, stops, orInfo, orStops});
    QObject::connect(orTrain, &OrTrainProperties::edited, this, &ActivityTimetableWindow::orTrainEdited);
    QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::editingLockChanged,
                     this, [this] { showChosen(); });
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    QObject::connect(simulation, &Sim::SimulationController::stateChanged,
                     this, &ActivityTimetableWindow::simulationChanged);
    QObject::connect(simulation, &Sim::SimulationController::frameUpdated,
                     this, &ActivityTimetableWindow::showStates);
    QObject::connect(simulation, &Sim::SimulationController::eventsAdded,
                     this, &ActivityTimetableWindow::showEvents);
    QObject::connect(simulation, &Sim::SimulationController::followChanged, this, [this](int train) {
        QSignalBlocker blocker(bFollow);
        bFollow->setChecked(train >= 0);
    });
    showChosen();
}


ActivityTimetableWindow::~ActivityTimetableWindow() {
    delete coordinate;
}

void ActivityTimetableWindow::showTimetable(Activity *a){
    if(this->isHidden())
        return;
    if(a != activity){
        timetableProperties->showTimetable(NULL);
        if(activity != NULL)
            activity->shownAITrain = -1;
        trains.clear();
        list.clear();
    }
    activity = a;
    {
        QSignalBlocker blocker(showAll);
        showAll.setChecked(a != NULL && a->showAllAITrains);
    }
    fillTrains();
    showChosen();
}

const ActivityTrains::Train *ActivityTimetableWindow::chosen() const {
    QTreeWidgetItem *item = trains.currentItem();
    if(item == NULL || !item->isSelected() || item->type() < 0 || item->type() >= list.size())
        return NULL;
    return &list[item->type()];
}

// Rows by start time: the player's in bold, runs the activity does not
// have greyed, a warning sign on a train with a problem.
void ActivityTimetableWindow::fillTrains(){
    const ActivityTrains::Train *was = chosen();
    const ActivityTrains::Kind kind = was != NULL ? was->kind : ActivityTrains::AI;
    const QString service = was != NULL ? was->service : QString();
    const int time = was != NULL ? was->time : -1;
    QSignalBlocker blocker(trains);
    trains.clear();
    list = ActivityTrains::List(activity);
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const bool simulating = simulation->shows(activity);
    for(int i = 0; i < list.size(); i++){
        const ActivityTrains::Train &train = list[i];
        QString name = train.service;
        if(train.kind == ActivityTrains::Player)
            //% "%1 (player)"
            name = qtTrId("route.editor.activity.timetable.window.player").arg(train.service);
        else if(train.kind == ActivityTrains::PatternOnly)
            //% "%1 (not in the activity)"
            name = qtTrId("route.editor.activity.timetable.window.not.in.activity").arg(train.service);
        const int simulatedTrain = simulating ? simulated(train) : -1;
        QTreeWidgetItem *item = new QTreeWidgetItem(&trains, {clock(train.time), name,
            simulatedTrain >= 0 ? simulation->stateText(simulatedTrain) : QString(), train.path,
            train.consist, train.firstStop, train.lastStop}, i);
        for(int column = 0; column < Columns; column++)
            item->setToolTip(column, item->text(column));
        if(train.kind == ActivityTrains::Player){
            QFont font = item->font(TrainColumn);
            font.setBold(true);
            item->setFont(TrainColumn, font);
        }
        if(train.kind == ActivityTrains::PatternOnly)
            for(int column = 0; column < Columns; column++){
                item->setForeground(column, QBrush(QColor(140, 140, 140)));
                QFont font = item->font(column);
                font.setItalic(true);
                item->setFont(column, font);
            }
        if(!train.problems.isEmpty()){
            item->setIcon(TrainColumn, style()->standardIcon(QStyle::SP_MessageBoxWarning));
            item->setToolTip(TrainColumn, train.problems.join("\n"));
        }
    }
    if(!service.isEmpty())
        choose(kind, service, time);
}

void ActivityTimetableWindow::choose(ActivityTrains::Kind kind, const QString &service, int time){
    for(int i = 0; i < list.size(); i++){
        const bool same = list[i].kind == kind
                || (kind != ActivityTrains::Player && list[i].kind != ActivityTrains::Player);
        if(same && list[i].service.compare(service, Qt::CaseInsensitive) == 0
                && (kind == ActivityTrains::Player || list[i].time == time)){
            QTreeWidgetItem *item = trains.topLevelItem(i);
            trains.setCurrentItem(item);
            item->setSelected(true);
            trains.scrollToItem(item);
            return;
        }
    }
}

void ActivityTimetableWindow::trainChosen(){
    showChosen();
}

// The chosen train: where its times are saved, what can be done with it,
// its timetable; map and 3D show it.
void ActivityTimetableWindow::showChosen(){
    showSimulate();
    const ActivityTrains::Train *train = chosen();
    if(activity != NULL)
        activity->shownAITrain = train != NULL && train->kind == ActivityTrains::AI ? train->index : -1;
    showAll.setEnabled(activity != NULL);
    const bool locked = ActivityNotifier::editingLocked();
    bAdd->setEnabled(!locked && train != NULL && train->kind == ActivityTrains::PatternOnly);
    bRemove->setEnabled(!locked && train != NULL && train->kind == ActivityTrains::AI);
    bCopy->setEnabled(!locked);
    float start[4];
    bJump->setEnabled(train != NULL && ActivityTrains::StartPosition(activity, *train, start));
    // The simulation's train: outlined, followed when Follow is on, its
    // events listed.
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const int simulatedTrain = train != NULL && simulation->shows(activity) ? simulated(*train) : -1;
    simulation->choose(simulatedTrain);
    bFollow->setEnabled(simulatedTrain >= 0);
    if(simulation->followed() >= 0)
        simulation->follow(simulatedTrain);
    showEvents(0);
    bCopy->setVisible(false);
    savedIn.setStyleSheet(QString());
    // An Open Rails timetable set (task simulation 08): its train's
    // timetable, start and stops, edited there; no traffic pattern.
    const bool timetables = activity != NULL && activity->isTimetables();
    mstsInfo->setVisible(!timetables);
    mstsStops->setVisible(!timetables);
    orInfo->setVisible(timetables);
    orBox->setVisible(timetables);
    bAdd->setVisible(!timetables);
    bRemove->setVisible(!timetables);
    bNewTrain->setVisible(timetables);
    bCloneTrain->setVisible(timetables);
    bRemoveTrain->setVisible(timetables);
    const bool timetableTrain = timetables && train != NULL && train->orTimetable >= 0
            && train->orTimetable < int(activity->timetables.size());
    bNewTrain->setEnabled(timetables && !locked && !activity->timetables.empty());
    bCloneTrain->setEnabled(timetableTrain && !locked);
    bRemoveTrain->setEnabled(timetableTrain && !locked);
    showAll.setVisible(!timetables);
    if(timetables){
        timetableProperties->showTimetable(NULL);
        if(train == NULL || train->orTimetable < 0 || train->orTimetable >= int(activity->timetables.size())){
            savedIn.clear();
            orTrain->showTrain(NULL, -1, -1, {});
            return;
        }
        savedIn.setText(
            //% "The times and commands are saved in the timetable: Save Activities writes it in TSRE's layout, the original kept beside it the first time."
            qtTrId("route.editor.activity.timetable.window.saved.timetable"));
        orTrain->showTrain(activity, train->orTimetable, train->orTrain, train->problems);
        return;
    }
    if(train == NULL){
        savedIn.clear();
        timetableProperties->showTimetable(NULL);
        return;
    }
    Traffic *pattern = ActivityTrains::Pattern(activity);
    const QString patternName = pattern != NULL ? pattern->nameId
            : activity->traffic != NULL ? activity->traffic->name : QString();
    if(train->kind == ActivityTrains::Player){
        savedIn.setText(
            //% "The player's times are saved in the activity."
            qtTrId("route.editor.activity.timetable.window.saved.player"));
    } else if(train->kind == ActivityTrains::PatternOnly){
        savedIn.setText(
            //% "A run of the traffic pattern %1 that the activity does not have: Add to Activity puts it in."
            qtTrId("route.editor.activity.timetable.window.saved.pattern.only").arg(patternName));
    } else if(train->timetable == NULL){
        savedIn.setText(
            //% "Not in the traffic pattern %1: this train has no times."
            qtTrId("route.editor.activity.timetable.window.saved.none").arg(patternName));
    } else {
        const QStringList users = ActivityTrains::OtherUsers(activity);
        if(users.isEmpty()){
            savedIn.setText(
                //% "The times are saved in the traffic pattern %1, used by this activity only."
                qtTrId("route.editor.activity.timetable.window.saved.own").arg(patternName));
        } else {
            savedIn.setText(
                //% "The times are saved in the traffic pattern %1, shared with: %2. Changing them changes those activities too."
                qtTrId("route.editor.activity.timetable.window.saved.shared").arg(patternName, users.join(", ")));
            savedIn.setStyleSheet("QLabel { color : rgb(200, 120, 0); }");
            bCopy->setVisible(true);
        }
    }
    timetableProperties->showTimetable(ActivityTrains::Definition(activity, *train), activity);
}

// A start or stop time changed: the list again, the same train chosen.
void ActivityTimetableWindow::orTrainEdited(){
    const ActivityTrains::Train *was = chosen();
    chooseTrainNamed(was != NULL ? was->service : QString());
}

void ActivityTimetableWindow::chooseTrainNamed(const QString &name){
    fillTrains();
    for(int i = 0; i < list.size(); i++)
        if(list[i].service == name){
            QTreeWidgetItem *item = trains.topLevelItem(i);
            trains.setCurrentItem(item);
            item->setSelected(true);
            break;
        }
    showChosen();
}

QString ActivityTimetableWindow::addTimetableTrain(const QString &typed, bool copy){
    const QString name = typed.trimmed();
    const ActivityTrains::Train *train = chosen();
    if(activity == NULL || !activity->isTimetables() || activity->timetables.empty()
            || (copy && (train == NULL || train->orTimetable < 0)))
        return QString();
    if(name.isEmpty() || name.startsWith('#') || name.contains('$') || name.contains(';') || name.contains(',')
            || name.contains('\t'))
        //% "A train's name cannot be empty, start with # or hold $ ; , or a tab."
        return qtTrId("route.editor.activity.timetable.window.train.name.invalid");
    for(const std::shared_ptr<OrTimetable> &timetable : activity->timetables)
        if(timetable->trainNamed(name) >= 0)
            //% "A train named %1 is in the timetables already."
            return qtTrId("route.editor.activity.timetable.window.train.name.exists").arg(name);
    const bool inTimetable = train != NULL && train->orTimetable >= 0 && train->orTimetable < int(activity->timetables.size());
    OrTimetable &timetable = *activity->timetables[size_t(inTimetable ? train->orTimetable : 0)];
    {
        ActivityEdit edit(activity);
        if(copy)
            timetable.cloneTrain(train->orTrain, name);
        else
            timetable.addTrain(inTimetable ? train->orTrain : -1, name,
                               inTimetable ? std::max(0, timetable.trains[train->orTrain].startTime) : 0);
    }
    chooseTrainNamed(name);
    return QString();
}

void ActivityTimetableWindow::removeTimetableTrain(){
    const ActivityTrains::Train *train = chosen();
    if(activity == NULL || !activity->isTimetables() || train == NULL || train->orTimetable < 0
            || train->orTimetable >= int(activity->timetables.size()))
        return;
    {
        ActivityEdit edit(activity);
        activity->timetables[size_t(train->orTimetable)]->removeTrain(train->orTrain);
    }
    chooseTrainNamed(QString());
}

void ActivityTimetableWindow::newTrainAsked(){
    bool ok = false;
    const QString name = QInputDialog::getText(this,
        //% "New Train"
        qtTrId("route.editor.activity.timetable.window.new.train.title"),
        //% "The new train's name:"
        qtTrId("route.editor.activity.timetable.window.train.name"), QLineEdit::Normal, QString(), &ok);
    if(!ok)
        return;
    const QString problem = addTimetableTrain(name, false);
    if(!problem.isEmpty())
        QMessageBox::warning(this, windowTitle(), problem);
}

void ActivityTimetableWindow::cloneTrainAsked(){
    const ActivityTrains::Train *train = chosen();
    if(train == NULL)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this,
        //% "Clone Train"
        qtTrId("route.editor.activity.timetable.window.clone.train.title"),
        //% "The copy's name:"
        qtTrId("route.editor.activity.timetable.window.clone.name"), QLineEdit::Normal, train->service + "_2", &ok);
    if(!ok)
        return;
    const QString problem = addTimetableTrain(name, true);
    if(!problem.isEmpty())
        QMessageBox::warning(this, windowTitle(), problem);
}

void ActivityTimetableWindow::removeTrainAsked(){
    const ActivityTrains::Train *train = chosen();
    if(activity == NULL || train == NULL || train->orTimetable < 0 || train->orTimetable >= int(activity->timetables.size()))
        return;
    QString question =
        //% "Remove the train %1 from the timetable?"
        qtTrId("route.editor.activity.timetable.window.remove.train.question").arg(train->service);
    const QStringList naming = activity->timetables[size_t(train->orTimetable)]->trainsNaming(train->service);
    if(!naming.isEmpty())
        question += "\n\n" +
            //% "These trains' commands name it and are kept: %1"
            qtTrId("route.editor.activity.timetable.window.remove.train.naming").arg(naming.join(", "));
    if(QMessageBox::question(this,
            //% "Remove Train"
            qtTrId("route.editor.activity.timetable.window.remove.train.title"), question) != QMessageBox::Yes)
        return;
    removeTimetableTrain();
}

void ActivityTimetableWindow::selectTrain(int shownId){
    if(activity == NULL || shownId < 0)
        return;
    if(shownId > 0)
        activity->shownAITrain = shownId - 1;
    for(int i = 0; i < list.size(); i++){
        const bool player = shownId == 0 && list[i].kind == ActivityTrains::Player;
        const bool ai = shownId > 0 && list[i].kind == ActivityTrains::AI && list[i].index == shownId - 1;
        if(player || ai){
            QTreeWidgetItem *item = trains.topLevelItem(i);
            trains.setCurrentItem(item);
            item->setSelected(true);
            trains.scrollToItem(item);
            return;
        }
    }
}

void ActivityTimetableWindow::addToActivity(){
    const ActivityTrains::Train *train = chosen();
    Traffic *pattern = ActivityTrains::Pattern(activity);
    if(train == NULL || train->kind != ActivityTrains::PatternOnly || pattern == NULL
            || train->index < 0 || train->index >= pattern->service.size())
        return;
    const QString service = train->service;
    const int time = train->time;
    {
        ActivityEdit edit(activity);
        activity->addAITrain(pattern->service[train->index]);
    }
    fillTrains();
    choose(ActivityTrains::AI, service, time);
    showChosen();
}

void ActivityTimetableWindow::removeFromActivity(){
    const ActivityTrains::Train *train = chosen();
    if(train == NULL || train->kind != ActivityTrains::AI)
        return;
    const QString service = train->service;
    const int time = train->time;
    const int index = train->index;
    timetableProperties->showTimetable(NULL);
    {
        ActivityEdit edit(activity);
        activity->removeAITrain(index);
    }
    fillTrains();
    choose(ActivityTrains::PatternOnly, service, time);
    showChosen();
}

// As New in the traffic window: a free file name; the activity then names
// the copy (one undo step for the activity; the copy stays).
void ActivityTimetableWindow::makeOwnCopy(){
    Traffic *pattern = ActivityTrains::Pattern(activity);
    if(pattern == NULL)
        return;
    EditFileNameDialog dialog;
    dialog.setWindowTitle(
        //% "Own Copy of the Traffic Pattern"
        qtTrId("route.editor.activity.timetable.window.copy.title"));
    dialog.name.setText(activity->nameid.isEmpty() ? pattern->nameId + "_copy" : activity->nameid);
    dialog.exec();
    const QString name = dialog.name.text().trimmed();
    if(!dialog.isOk || name.isEmpty())
        return;
    if(name.contains(QRegularExpression("[\\\\/:*?\"<>|]"))){
        QMessageBox::warning(this, windowTitle(),
            //% "A file name cannot be empty or contain \\ / : * ? \" < > |."
            qtTrId("route.editor.activity.timetable.window.name.invalid"));
        return;
    }
    if(ActLib::TrafficExists(name, ContentPath::parentDirectory(activity->path))){
        QMessageBox::warning(this, windowTitle(),
            //% "A traffic pattern named %1 exists already."
            qtTrId("route.editor.activity.timetable.window.name.exists").arg(name));
        return;
    }
    timetableProperties->showTimetable(NULL);
    {
        ActivityEdit edit(activity);
        ActivityTrains::CopyPattern(activity, name);
    }
    fillTrains();
    showChosen();
    emit trafficPatternAdded();
}

void ActivityTimetableWindow::jumpToTrain(){
    const ActivityTrains::Train *train = chosen();
    float start[4];
    if(train == NULL || !ActivityTrains::StartPosition(activity, *train, start))
        return;
    if(coordinate == NULL)
        coordinate = new PreciseTileCoordinate();
    coordinate->TileX = start[0];
    coordinate->TileZ = start[1];
    coordinate->setWxyz(start[2], 0, start[3]);
    emit jumpTo(coordinate);
}

int ActivityTimetableWindow::simulated(const ActivityTrains::Train &train) const {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    if(train.kind == ActivityTrains::Player)
        return simulation->trainOf(true, -1);
    if(train.kind == ActivityTrains::AI)
        return simulation->trainOf(false, train.index);
    return -1;
}

void ActivityTimetableWindow::followToggled(bool on){
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    simulation->follow(on ? simulation->chosen() : -1);
}

void ActivityTimetableWindow::simulationChanged(){
    if(isHidden())
        return;
    const bool simulating = Sim::SimulationController::instance()->shows(activity);
    trains.setColumnHidden(StateColumn, !simulating);
    simulationBox->setVisible(simulating);
    showChosen();
    showStates();
}

// Each train's state in the simulation.
void ActivityTimetableWindow::showStates(){
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    if(isHidden() || !simulation->shows(activity))
        return;
    for(int i = 0; i < trains.topLevelItemCount() && i < list.size(); i++){
        const int train = simulated(list[i]);
        const QString state = train >= 0 ? simulation->stateText(train) : QString();
        QTreeWidgetItem *item = trains.topLevelItem(i);
        if(item->text(StateColumn) != state){
            item->setText(StateColumn, state);
            item->setToolTip(StateColumn, state);
        }
    }
}

// The chosen train's events from `first` on (all again from 0).
void ActivityTimetableWindow::showEvents(int first){
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    if(first == 0)
        events.clear();
    const int train = simulation->chosen();
    if(isHidden() || train < 0 || !simulation->shows(activity))
        return;
    const std::vector<Sim::TrainEvent> &all = simulation->events();
    const bool atEnd = events.count() == 0 || events.verticalScrollBar()->value() == events.verticalScrollBar()->maximum();
    // The activity's events (task simulation 06) are the player's: listed
    // with its train's, and with an AI train's that fired them.
    const std::shared_ptr<const Sim::SimulationInput> input = simulation->input();
    const bool player = input != NULL && size_t(train) < input->trains.size() && input->trains[size_t(train)].player;
    for(size_t i = size_t(std::max(0, first)); i < all.size(); i++){
        const bool activityEvent = all[i].kind == Sim::TrainEvent::EventFired || all[i].kind == Sim::TrainEvent::ActivityEnded;
        if(all[i].train == train || (player && activityEvent))
            events.addItem(clock(int(all[i].time)) + "  " + simulation->eventText(all[i]));
    }
    if(atEnd)
        events.scrollToBottom();
}

void ActivityTimetableWindow::showAllToggled(bool on){
    if(activity != NULL)
        activity->showAllAITrains = on;
}

// Times changed in the timetable: the rows' first and last stops again
// (the timetable itself stays as it is being edited).
void ActivityTimetableWindow::timetableEdited(){
    fillTrains();
}

void ActivityTimetableWindow::showSimulate(){
    const bool usable = activity != NULL && !ActivityNotifier::editingLocked();
    const QStringList shared = activity != NULL ? ActivityTrains::OtherUsers(activity) : QStringList();
    Traffic *pattern = activity != NULL ? ActivityTrains::Pattern(activity) : NULL;
    const bool playerOnly = !shared.isEmpty() || pattern == NULL;
    bSimulate->setEnabled(usable && !list.isEmpty());
    roundTo.setEnabled(usable);
    fromScratch.setEnabled(usable);
    timesFor.setEnabled(usable && !playerOnly);
    {
        QSignalBlocker blocker(timesFor);
        timesFor.setCurrentIndex(playerOnly ? 1 : timesFor.property("chosen").toInt());
    }
    if(!shared.isEmpty())
        timesFor.setToolTip(
            //% "The traffic pattern %1 is shared with: %2. Only the player's times are written; Make Own Copy... to write the AI trains' too."
            qtTrId("route.editor.activity.timetable.window.times.shared").arg(pattern->nameId, shared.join(", ")));
    else if(pattern == NULL)
        timesFor.setToolTip(
            //% "The activity has no traffic pattern: the player's times alone."
            qtTrId("route.editor.activity.timetable.window.times.no.pattern"));
    else
        timesFor.setToolTip(
            //% "Whose times Simulate writes: the player's in the activity, the AI trains' in the traffic pattern."
            qtTrId("route.editor.activity.timetable.window.times.tooltip"));
    const Sim::TimetableRun *run = Sim::TimetableCalculation::Last(activity);
    lastRun.setText(run != NULL ? summary(*run) : QString());
    lastRun.setToolTip(run != NULL ? run->warnings.join("\n") : QString());
    lastRun.setVisible(run != NULL);
}

void ActivityTimetableWindow::simulateTimetables(){
    if(activity == NULL || ActivityNotifier::editingLocked())
        return;
    Sim::TimetableOptions options;
    options.step = roundTo.currentData().toInt();
    options.playerOnly = timesFor.currentIndex() == 1;
    options.fromScratch = fromScratch.isChecked();
    QString error;
    std::unique_ptr<Sim::TimetableCalculation> calculation = Sim::TimetableCalculation::Prepare(activity, options, &error);
    if(calculation == nullptr){
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }
    // The run on its own thread, everything else waiting (Cancel offered).
    std::atomic<bool> cancel(false);
    QProgressDialog progress(
        //% "Simulating the activity..."
        qtTrId("route.editor.activity.timetable.window.simulating"),
        //% "Cancel"
        qtTrId("route.editor.activity.timetable.window.simulating.cancel"), 0, 0, this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(400);
    QObject::connect(&progress, &QProgressDialog::canceled, this, [&cancel]{ cancel = true; });
    Sim::TimetableCalculation *running = calculation.get();
    QThread *worker = QThread::create([running, &cancel]{ running->run(&cancel); });
    QEventLoop wait;
    QObject::connect(worker, &QThread::finished, &wait, &QEventLoop::quit);
    worker->start();
    wait.exec();
    worker->wait();
    delete worker;
    progress.reset();
    if(calculation->result().cancelled)
        return;
    {
        ActivityEdit edit(activity);
        if(!calculation->result().options.playerOnly)
            edit.alsoTrafficOf(activity);
        calculation->write(activity);
    }
    fillTrains();
    showChosen();
}

// Other windows change what the rows show: shown again when this one is
// used.
bool ActivityTimetableWindow::event(QEvent *e){
    if(e->type() == QEvent::WindowActivate && activity != NULL)
        fillTrains();
    return QWidget::event(e);
}

// The activity's trains are replaced: let go of them, list them again.
void ActivityTimetableWindow::activityDataRestoring(){
    timetableProperties->showTimetable(NULL);
    QSignalBlocker blocker(trains);
    trains.clear();
    list.clear();
}

void ActivityTimetableWindow::activityDataRestored(){
    if(activity != NULL)
        showTimetable(activity);
}
