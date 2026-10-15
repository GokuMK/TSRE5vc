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
#include <routeEditor/activity/ActivityEdit.h>
#include <routeEditor/properties/EditFileNameDialog.h>
#include <tsre/Game.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/Traffic.h>
#include <routeEditor/activity/ActivityEditLock.h>
#include <tsre/sim/SimulationController.h>

namespace {

enum Column { StartColumn, TrainColumn, StateColumn, PathColumn, ConsistColumn, FirstColumn, LastColumn, Columns };

QString clock(int seconds) {
    return QTime::fromMSecsSinceStartOfDay((seconds % 86400 + 86400) % 86400 * 1000).toString("HH:mm:ss");
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
        //% "The camera follows the chosen train while the simulation runs."
        qtTrId("route.editor.activity.timetable.window.button.follow.tooltip"));
    bFollow->setCheckable(true);
    bFollow->setObjectName("timetableFollow");
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
    buttons->addWidget(bJump);
    buttons->addWidget(bFollow);
    QWidget *top = new QWidget;
    QVBoxLayout *topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(2);
    topLayout->addWidget(&trains);
    topLayout->addLayout(buttons);

    // Under the trains, side by side: on the left what is known of the
    // chosen train (where its times are saved, with Make Own Copy...; its
    // start, engine and speeds), on the right its stops and times.
    QWidget *saved = new QWidget;
    QVBoxLayout *savedLayout = new QVBoxLayout(saved);
    savedLayout->setContentsMargins(0, 0, 0, 0);
    savedLayout->setSpacing(4);
    savedIn.setAlignment(Qt::AlignTop | Qt::AlignLeft);
    savedLayout->addWidget(&savedIn);
    savedLayout->addWidget(bCopy, 0, Qt::AlignLeft);
    savedLayout->addSpacing(6);
    QWidget *info = ActivityEditLock::Box(timetableProperties->info);
    savedLayout->addWidget(info);
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
    QSplitter *bottom = new QSplitter(Qt::Horizontal);
    bottom->addWidget(saved);
    bottom->addWidget(stops);
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
    QObject::connect(timetableProperties, &ActivityTimetableProperties::edited,
                     this, &ActivityTimetableWindow::timetableEdited);
    QObject::connect(bFollow, &QPushButton::toggled, this, &ActivityTimetableWindow::followToggled);
    // While a simulation exists its times show but are not edited.
    ActivityEditLock::Follow(this, {info, stops});
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
    for(size_t i = size_t(std::max(0, first)); i < all.size(); i++)
        if(all[i].train == train)
            events.addItem(clock(int(all[i].time)) + "  " + simulation->eventText(all[i]));
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
