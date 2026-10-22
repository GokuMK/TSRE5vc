/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ActivityTools.h"
#include "tools/ToolButtons.h"
#include <tsre/world/Route.h>
#include <tsre/Game.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Activity.h>
#include <tsre/gui/GuiFunct.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/Traffic.h>
#include <tsre/trains/Path.h>
#include <routeEditor/activity/ActivityServiceProperties.h>
#include <tsre/gui/TextEditDialog.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/gui/UnsavedDialog.h>
#include <tsre/world/Trk.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <routeEditor/activity/ActivityEdit.h>
#include <routeEditor/activity/ActivityCheckWindow.h>
#include <tsre/trains/ActivityCheck.h>
#include <tsre/trains/ActivityOverlay.h>
#include <tsre/trains/ActivityNotifier.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/environment/WorldClock.h>
#include <QDialog>
#include <QEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <cmath>

ActivityTools::ActivityTools(QString name)
    : QWidget(){
    // Undo replaces the activity's data: its settings and lists again.
    QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::restored, this, [this] {
        if(currentActivity() != NULL)
            activitySelected(QString());
    });
    QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::overlayRenamed, this, [this] {
        showOverlay();
    });
    // The tool box scrolls it (RouteEditorWindow); it takes the box's width.
    cSeason.setStyleSheet("combobox-popup: 0;");
    cSeason.addItem(
        //% "Spring"
        qtTrId("route.editor.activity.tools.item.spring"),0);
    cSeason.addItem(
        //% "Summer"
        qtTrId("route.editor.activity.tools.item.summer"),1);
    cSeason.addItem(
        //% "Autumn"
        qtTrId("route.editor.activity.tools.item.autumn"),2);
    cSeason.addItem(
        //% "Winter"
        qtTrId("route.editor.activity.tools.item.winter"),3);
    cWeather.setStyleSheet("combobox-popup: 0;");
    cWeather.addItem(
        //% "Clear"
        qtTrId("route.editor.activity.tools.item.clear"),0);
    cWeather.addItem(
        //% "Rain"
        qtTrId("route.editor.activity.tools.item.rain"),1);
    cWeather.addItem(
        //% "Snow"
        qtTrId("route.editor.activity.tools.item.snow"),2);
    cDifficulty.setStyleSheet("combobox-popup: 0;");
    cDifficulty.addItem(
        //% "Easy"
        qtTrId("route.editor.activity.tools.item.easy"),0);
    cDifficulty.addItem(
        //% "Medium"
        qtTrId("route.editor.activity.tools.item.medium"),1);
    cDifficulty.addItem(
        //% "Hard"
        qtTrId("route.editor.activity.tools.item.hard"),2);
    
    buttonTools["actNewLooseConsistTool"] = new QPushButton(
        //% "Place Consist"
        qtTrId("route.editor.activity.tools.button.place.consist"), this);
    buttonTools["actNewSpeedZoneTool"] = new QPushButton(
        //% "New"
        qtTrId("route.editor.activity.tools.button.new"), this);
    // Edits the chosen path with the path tool (task editor 06).
    buttonTools["trackPathTool"] = new QPushButton(
        //% "Edit"
        qtTrId("route.editor.activity.tools.button.act.paths.edit"), this);

    QMapIterator<QString, QPushButton*> i(buttonTools);
    while (i.hasNext()) {
        i.next();
        i.value()->setCheckable(true);
    }
    QObject::connect(buttonTools["actNewLooseConsistTool"], SIGNAL(toggled(bool)), this, SLOT(actNewLooseConsistToolEnabled(bool)));
    
    //QPushButton *loadActFilesButton = new QPushButton("Load Activities", this);
    QPushButton *newActButton = new QPushButton(
        //% "New Activity"
        qtTrId("route.editor.activity.tools.button.new.act.button"), this);
    //advancedPlacenentButton->setCheckable(true);
    QObject::connect(newActButton, SIGNAL(released()), this, SLOT(newActButtonEnabled()));
    editing << newActButton;

    //radio1->setChecked(true);
    QGridLayout *vlist1 = NULL;
    
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    // The kind of activity first (task simulation 08): MSTS activities or
    // Open Rails timetables; the list, the panel and the activity windows
    // follow it.
    QLabel *kindLabel = new QLabel(
        //% "Kind:"
        qtTrId("route.editor.activity.tools.label.kind"));
    kindLabel->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    kindLabel->setContentsMargins(3,0,0,0);
    vbox->addWidget(kindLabel);
    cKind.setObjectName("activityKind");
    cKind.addItem(
        //% "MSTS activities"
        qtTrId("route.editor.activity.tools.kind.msts"), 0);
    cKind.addItem(
        //% "Open Rails timetables"
        qtTrId("route.editor.activity.tools.kind.timetables"), 1);
    cKind.setStyleSheet("combobox-popup: 0;");
    vbox->addWidget(&cKind);
    editing << &cKind;
    QLabel *label = new QLabel(
        //% "Activity List:"
        qtTrId("route.editor.activity.tools.label.label"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vbox->addWidget(&actShow);
    actShow.setStyleSheet("combobox-popup: 0;");
    actShow.setMaxVisibleItems(35);
    actShow.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    vbox->addWidget(newActButton);
    // A timetable set (task simulation 08): the train the player drives, or
    // none (every train AI, as Open Rails starts); what it holds.
    {
        QLabel *playerLabel = new QLabel(
            //% "Player's train:"
            qtTrId("route.editor.activity.tools.label.player.train"));
        playerLabel->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
        playerLabel->setContentsMargins(3,0,0,0);
        vbox->addWidget(playerLabel);
        cPlayerTrain.setObjectName("activityPlayerTrain");
        cPlayerTrain.setStyleSheet("combobox-popup: 0;");
        cPlayerTrain.setMaxVisibleItems(35);
        cPlayerTrain.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        vbox->addWidget(&cPlayerTrain);
        timetableSummary.setObjectName("activityTimetableSummary");
        timetableSummary.setWordWrap(true);
        timetableSummary.setContentsMargins(3,0,0,0);
        vbox->addWidget(&timetableSummary);
        timetablesOnly << playerLabel << &cPlayerTrain << &timetableSummary;
        editing << &cPlayerTrain;
    }
    
    label = new QLabel(
        //% "Player:"
        qtTrId("route.editor.activity.tools.label.label.2"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    mstsOnly << label;
    vbox->addWidget(&cService);
    editing << &cService;
    cService.setStyleSheet("combobox-popup: 0;");
    cService.setMaxVisibleItems(35);
    cService.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    QPushButton *actServiceOpen = new QPushButton(
        //% "Open Service Editor"
        qtTrId("route.editor.activity.tools.button.act.service.open"));
    QObject::connect(actServiceOpen, SIGNAL(released()), this, SLOT(actServiceOpenEnabled()));
    vbox->addWidget(actServiceOpen);
    
    label = new QLabel(
        //% "Traffic:"
        qtTrId("route.editor.activity.tools.label.label.3"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vbox->addWidget(&cTraffic);
    editing << &cTraffic;
    cTraffic.setStyleSheet("combobox-popup: 0;");
    cTraffic.setMaxVisibleItems(35);
    cTraffic.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    QPushButton *actTrafficOpen = new QPushButton(
        //% "Open Traffic Editor"
        qtTrId("route.editor.activity.tools.button.act.traffic.open"));
    QObject::connect(actTrafficOpen, SIGNAL(released()), this, SLOT(actTrafficOpenEnabled()));
    vbox->addWidget(actTrafficOpen);
    
    label = new QLabel(
        //% "Timetable:"
        qtTrId("route.editor.activity.tools.label.label.4"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    QPushButton *actTimetableOpen = new QPushButton(
        //% "Open Timetable Editor"
        qtTrId("route.editor.activity.tools.button.act.timetable.open"));
    QObject::connect(actTimetableOpen, SIGNAL(released()), this, SLOT(actTimetableOpenEnabled()));
    vbox->addWidget(actTimetableOpen);
    
    label = new QLabel(
        //% "Paths:"
        qtTrId("route.editor.activity.tools.label.label.5"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vbox->addWidget(&cPath);
    cPath.setStyleSheet("combobox-popup: 0;");
    cPath.setMaxVisibleItems(35);
    cPath.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    cPath.installEventFilter(this);
    vlist1 = new QGridLayout;
    vlist1->setSpacing(2);
    vlist1->setContentsMargins(0,0,1,0);
    QPushButton *actPathsNew = new QPushButton(
        //% "New"
        qtTrId("route.editor.activity.tools.button.act.paths.new"));
    QPushButton *actPathsEdit = buttonTools["trackPathTool"];
    QObject::connect(actPathsEdit, SIGNAL(toggled(bool)), this, SLOT(actPathsEditToolEnabled(bool)));
    QObject::connect(&cPath, SIGNAL(currentIndexChanged(int)), this, SLOT(cPathSelected(int)));
    QPushButton *actPathsClone = new QPushButton(
        //% "Clone"
        qtTrId("route.editor.activity.tools.button.act.paths.clone"));
    QPushButton *actPathsDelete = new QPushButton(
        //% "Delete"
        qtTrId("route.editor.activity.tools.button.act.paths.delete"));
    QObject::connect(actPathsNew, SIGNAL(released()), this, SLOT(actPathsNewSelected()));
    QObject::connect(actPathsClone, SIGNAL(released()), this, SLOT(actPathsCloneSelected()));
    QObject::connect(actPathsDelete, SIGNAL(released()), this, SLOT(actPathsDeleteSelected()));
    vlist1->addWidget(actPathsNew,0,0);
    vlist1->addWidget(actPathsEdit,0,1);
    vlist1->addWidget(actPathsClone,0,2);
    vlist1->addWidget(actPathsDelete,0,3);
    editing << actPathsNew << actPathsEdit << actPathsClone << actPathsDelete;
    QPushButton *actPathsRefreshList = new QPushButton(
        //% "Refresh List"
        qtTrId("route.editor.activity.tools.button.act.paths.refresh.list"));
    QObject::connect(actPathsRefreshList, SIGNAL(released()), this, SLOT(actPathsRefreshListSelected()));
    vlist1->addWidget(actPathsRefreshList,1,1,1,3);
    vbox->addItem(vlist1);    
    // What the last path edit did to the services on it.
    pathReport.setWordWrap(true);
    pathReport.setContentsMargins(3,0,0,0);
    pathReport.hide();
    vbox->addWidget(&pathReport);
    QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::pathFollowed, this,
                     [this](const QString &path, const QStringList &lines) {
        pathReport.setText(QString(
            //% "Path %1 edited:"
            qtTrId("route.editor.activity.tools.path.followed")).arg(path) + "\n" + lines.join("\n"));
        pathReport.show();
    });
   
    label = new QLabel(
        //% "Activity Objects List:"
        qtTrId("route.editor.activity.tools.label.label.6"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    mstsOnly << label;
    vbox->addWidget(&consists);
    consists.setStyleSheet("combobox-popup: 0;");
    consists.setMaxVisibleItems(35);
    consists.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    vlist1 = new QGridLayout;
    vlist1->setSpacing(2);
    vlist1->setContentsMargins(0,0,1,0);
    QPushButton *actConsistJump = new QPushButton(
        //% "Jump To"
        qtTrId("route.editor.activity.tools.button.act.consist.jump"));
    QObject::connect(actConsistJump, SIGNAL(released()), this, SLOT(actConsistJumpEnabled()));
    QPushButton *actConsistDelete = new QPushButton(
        //% "Delete"
        qtTrId("route.editor.activity.tools.button.act.consist.delete"));
    QObject::connect(actConsistDelete, SIGNAL(released()), this, SLOT(actConsistDeleteEnabled()));
    vlist1->addWidget(actConsistJump,0,0);
    vlist1->addWidget(actConsistDelete,0,1);
    vlist1->addWidget(buttonTools["actNewLooseConsistTool"],0,2,1,2);
    editing << actConsistDelete << buttonTools["actNewLooseConsistTool"] << &conFilesShow;
    label = new QLabel(
        //% "Consist:"
        qtTrId("route.editor.activity.tools.label.label.7"));
    //label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vlist1->addWidget(label,1,0);
    mstsOnly << label;
    vlist1->addWidget(&conFilesShow,1,1,1,3);
    conFilesShow.setStyleSheet("combobox-popup: 0;");
    conFilesShow.setMaxVisibleItems(35);
    conFilesShow.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    QPushButton *conFilesRefresh = new QPushButton(
        //% "Refresh List"
        qtTrId("route.editor.activity.tools.button.con.files.refresh"));
    QObject::connect(conFilesRefresh, SIGNAL(released()), this, SLOT(conFilesRefreshSelected()));
    vlist1->addWidget(conFilesRefresh,2,1,1,3);
    vbox->addItem(vlist1);
    
    label = new QLabel(
        //% "Restricted Speed Zones List:"
        qtTrId("route.editor.activity.tools.label.label.8"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    mstsOnly << label;
    vbox->addWidget(&speedZones);
    speedZones.setStyleSheet("combobox-popup: 0;");
    speedZones.setMaxVisibleItems(35);
    speedZones.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    vlist1 = new QGridLayout;
    vlist1->setSpacing(2);
    vlist1->setContentsMargins(0,0,1,0);
    QPushButton *actZoneJump = new QPushButton(
        //% "Jump To"
        qtTrId("route.editor.activity.tools.button.act.zone.jump"));
    QObject::connect(actZoneJump, SIGNAL(released()), this, SLOT(actReducedSpeedZonesEnabled()));
    QObject::connect(buttonTools["actNewSpeedZoneTool"], SIGNAL(toggled(bool)), this, SLOT(actZoneNewToolEnabled(bool)));
    QPushButton *actZoneDelete = new QPushButton(
        //% "Delete"
        qtTrId("route.editor.activity.tools.button.act.zone.delete"));
    QObject::connect(actZoneDelete, SIGNAL(released()), this, SLOT(actZoneDeleteEnabled()));
    QPushButton *actZoneDeleteAll = new QPushButton(
        //% "Delete All"
        qtTrId("route.editor.activity.tools.button.act.zone.delete.all"));
    QObject::connect(actZoneDeleteAll, SIGNAL(released()), this, SLOT(actZoneDeleteAllEnabled()));
    vlist1->addWidget(actZoneJump,0,0);
    vlist1->addWidget(buttonTools["actNewSpeedZoneTool"],0,1);
    vlist1->addWidget(actZoneDelete,0,2);
    vlist1->addWidget(actZoneDeleteAll,0,3);
    editing << buttonTools["actNewSpeedZoneTool"] << actZoneDelete << actZoneDeleteAll;
    vbox->addItem(vlist1);
    
    label = new QLabel(
        //% "Failed Signals List:"
        qtTrId("route.editor.activity.tools.label.label.9"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    mstsOnly << label;
    vbox->addWidget(&failedSignals);
    failedSignals.setStyleSheet("combobox-popup: 0;");
    failedSignals.setMaxVisibleItems(35);
    failedSignals.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    vlist1 = new QGridLayout;
    vlist1->setSpacing(2);
    vlist1->setContentsMargins(0,0,1,0);
    QPushButton *actFailedSignalJump = new QPushButton(
        //% "Jump To"
        qtTrId("route.editor.activity.tools.button.act.failed.signal.jump"));
    QObject::connect(actFailedSignalJump, SIGNAL(released()), this, SLOT(actFailedSignalsJumpEnabled()));
    QPushButton *actFailedSignalTool = new QPushButton(
        //% "Disable"
        qtTrId("route.editor.activity.tools.button.act.failed.signal.tool"));
    QObject::connect(actFailedSignalTool, SIGNAL(released()), this, SLOT(actFailedSignalNewToolEnabled()));
    QPushButton *actFailedSignalDelete = new QPushButton(
        //% "Delete"
        qtTrId("route.editor.activity.tools.button.act.failed.signal.delete"));
    QObject::connect(actFailedSignalDelete, SIGNAL(released()), this, SLOT(actFailedSignalDeleteEnabled()));
    QPushButton *actFailedSignalDeleteAll = new QPushButton(
        //% "Delete All"
        qtTrId("route.editor.activity.tools.button.act.failed.signal.delete.all"));
    QObject::connect(actFailedSignalDeleteAll, SIGNAL(released()), this, SLOT(actFailedSignalDeleteAllEnabled()));
    vlist1->addWidget(actFailedSignalJump,0,0);
    vlist1->addWidget(actFailedSignalTool,0,1);
    vlist1->addWidget(actFailedSignalDelete,0,2);
    vlist1->addWidget(actFailedSignalDeleteAll,0,3);
    editing << actFailedSignalTool << actFailedSignalDelete << actFailedSignalDeleteAll;
    vbox->addItem(vlist1);
    
    label = new QLabel(
        //% "Events:"
        qtTrId("route.editor.activity.tools.label.label.10"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    mstsOnly << label;
    QPushButton *actEventsOpen = new QPushButton(
        //% "Open Event Editor"
        qtTrId("route.editor.activity.tools.button.act.events.open"));
    QObject::connect(actEventsOpen, SIGNAL(released()), this, SLOT(actEventsOpenEnabled()));
    vbox->addWidget(actEventsOpen);
    
    label = new QLabel(
        //% "Activity Info:"
        qtTrId("route.editor.activity.tools.label.label.11"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    mstsOnly << label;
    QPushButton *actSettingsOpen = new QPushButton(
        //% "Open Settings ..."
        qtTrId("route.editor.activity.tools.button.act.settings.open"));
    QObject::connect(actSettingsOpen, SIGNAL(released()), this, SLOT(actSettingsOpenEnabled()));
    vbox->addWidget(actSettingsOpen);
    {
        overlayLabel.setWordWrap(true);
        QPushButton *merge = new QPushButton(
            //% "Merge..."
            qtTrId("route.editor.activity.tools.button.overlay.merge"));
        merge->setObjectName("overlayMerge");
        QObject::connect(merge, SIGNAL(released()), this, SLOT(actOverlayMergeEnabled()));
        QHBoxLayout *line = new QHBoxLayout;
        line->setContentsMargins(3,0,0,0);
        line->addWidget(&overlayLabel, 1);
        line->addWidget(merge);
        overlayLine.setLayout(line);
        overlayLine.hide();
        vbox->addWidget(&overlayLine);
    }
    
    label = new QLabel(
        //% "General:"
        qtTrId("route.editor.activity.tools.label.label.13"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    QPushButton *actCheckButton = new QPushButton(
        //% "Check Activity"
        qtTrId("route.editor.activity.tools.button.act.check.button"));
    QObject::connect(actCheckButton, SIGNAL(released()), this, SLOT(actCheckEnabled()));
    vbox->addWidget(actCheckButton);
    QPushButton *actSaveButton = new QPushButton(
        //% "Save Activities"
        qtTrId("route.editor.activity.tools.button.act.save.button"));
    QObject::connect(actSaveButton, SIGNAL(released()), this, SLOT(actSaveEnabled()));
    vbox->addWidget(actSaveButton);

    // The simulation (task simulation 01): it runs the activity as it is
    // when started; the activity is not edited until it stops.
    label = new QLabel(
        //% "Simulation:"
        qtTrId("route.editor.activity.tools.label.simulation"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    {
        simPlay = new QPushButton(
            //% "Play"
            qtTrId("route.editor.activity.tools.button.simulation.play"));
        simPlay->setToolTip(
            //% "Runs the activity's trains at the speed chosen (real time at 1×); the activity is not edited until Stop."
            qtTrId("route.editor.activity.tools.button.simulation.play.tooltip"));
        simPause = new QPushButton(
            //% "Pause"
            qtTrId("route.editor.activity.tools.button.simulation.pause"));
        simStop = new QPushButton(
            //% "Stop"
            qtTrId("route.editor.activity.tools.button.simulation.stop"));
        simReset = new QPushButton(
            //% "Reset"
            qtTrId("route.editor.activity.tools.button.simulation.reset"));
        simReset->setToolTip(
            //% "Back to the activity's start time."
            qtTrId("route.editor.activity.tools.button.simulation.reset.tooltip"));
        simRunTo = new QPushButton(
            //% "Run"
            qtTrId("route.editor.activity.tools.button.simulation.run.to"));
        simRunTo->setToolTip(
            //% "As fast as possible to the time, then paused; an earlier time is reached again from the start."
            qtTrId("route.editor.activity.tools.button.simulation.run.to.tooltip"));
        simPlay->setObjectName("simulationPlay");
        simPause->setObjectName("simulationPause");
        simStop->setObjectName("simulationStop");
        simReset->setObjectName("simulationReset");
        simRunTo->setObjectName("simulationRunTo");
        simSpeed.setObjectName("simulationSpeed");
        simRunToTime.setObjectName("simulationRunToTime");
        simTime.setObjectName("simulationTime");
        simStatus.setObjectName("simulationStatus");
        for(double speed : Sim::SimulationController::Speeds())
            simSpeed.addItem(speed > 0 ?
                //% "%1×"
                qtTrId("route.editor.activity.tools.simulation.speed").arg(speed) :
                //% "As fast as possible"
                qtTrId("route.editor.activity.tools.simulation.speed.fastest"), speed);
        simSpeed.setStyleSheet("combobox-popup: 0;");
        simRunToTime.setDisplayFormat("HH:mm:ss");
        simStatus.setWordWrap(true);
        simStatus.setContentsMargins(3,0,0,0);
        QGridLayout *grid = new QGridLayout;
        grid->setSpacing(2);
        grid->setContentsMargins(0,0,1,0);
        grid->addWidget(simPlay, 0, 0);
        grid->addWidget(simPause, 0, 1);
        grid->addWidget(simStop, 0, 2);
        grid->addWidget(simReset, 0, 3);
        grid->addWidget(new QLabel(
            //% "Speed:"
            qtTrId("route.editor.activity.tools.label.simulation.speed")), 1, 0);
        grid->addWidget(&simSpeed, 1, 1, 1, 3);
        grid->addWidget(new QLabel(
            //% "Time:"
            qtTrId("route.editor.activity.tools.label.simulation.time")), 2, 0);
        grid->addWidget(&simTime, 2, 1, 1, 3);
        grid->addWidget(new QLabel(
            //% "Run to:"
            qtTrId("route.editor.activity.tools.label.simulation.run.to")), 3, 0);
        grid->addWidget(&simRunToTime, 3, 1, 1, 2);
        grid->addWidget(simRunTo, 3, 3);
        vbox->addItem(grid);
        vbox->addWidget(&simStatus);
        QObject::connect(simPlay, SIGNAL(released()), this, SLOT(actPlayEnabled()));
        QObject::connect(simPause, SIGNAL(released()), this, SLOT(simPauseEnabled()));
        QObject::connect(simStop, SIGNAL(released()), this, SLOT(simStopEnabled()));
        QObject::connect(simReset, SIGNAL(released()), this, SLOT(simResetEnabled()));
        QObject::connect(simRunTo, SIGNAL(released()), this, SLOT(simRunToEnabled()));
        QObject::connect(&simSpeed, SIGNAL(activated(int)), this, SLOT(simSpeedSelected(int)));
        Sim::SimulationController *simulation = Sim::SimulationController::instance();
        QObject::connect(simulation, &Sim::SimulationController::stateChanged, this, &ActivityTools::showSimulation);
        QObject::connect(simulation, &Sim::SimulationController::frameUpdated, this, &ActivityTools::showSimulation);
        QObject::connect(simulation, &Sim::SimulationController::eventsAdded, this, &ActivityTools::simulationEvents);
        QObject::connect(WorldClock::instance(), &WorldClock::changed, this, &ActivityTools::showSimulation);
        QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::editingLockChanged,
                         this, &ActivityTools::lockEditing);
        showSimulation();
    }
    
    QHBoxLayout *vbox1 = new QHBoxLayout;
    int row = 0;
    int labelWidth = 70;
    QGridLayout *vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(0,0,1,0);
    vlist->addWidget(new QLabel(
        //% "File Name:"
        qtTrId("route.editor.activity.tools.label.file.name")), row, 0);
    vlist->addWidget(&eFileName, row++, 1, 1, 2);
    QObject::connect(&eFileName, SIGNAL(textEdited(QString)), this, SLOT(eFileNameEnabled(QString)));
    vlist->addWidget(new QLabel(
        //% "Display Name:"
        qtTrId("route.editor.activity.tools.label.display.name")), row, 0);
    vlist->addWidget(&eDisplayName, row++, 1, 1, 2);
    QObject::connect(&eDisplayName, SIGNAL(textEdited(QString)), this, SLOT(eDisplayNameEnabled(QString)));
    vlist->addWidget(new QLabel(
        //% "Difficulty:"
        qtTrId("route.editor.activity.tools.label.difficulty")), row, 0);
    vlist->addWidget(&cDifficulty, row++, 1, 1, 2);
    QObject::connect(&cDifficulty, SIGNAL(activated(int)), this, SLOT(cDifficultyEnabled(int)));
    vlist->addWidget(new QLabel(
        //% "Duration:"
        qtTrId("route.editor.activity.tools.label.duration")), row, 0);
    vlist->addWidget(&eDuration, row++, 1, 1, 2);
    QObject::connect(&eDuration, SIGNAL(editingFinished()), this, SLOT(eDurationEnabled()));
    eDuration.setDisplayFormat("HH:mm");
    vlist->addWidget(new QLabel(
        //% "Start Time:"
        qtTrId("route.editor.activity.tools.label.start.time")), row, 0);
    vlist->addWidget(&eStartTime, row++, 1, 1, 2);
    QObject::connect(&eStartTime, SIGNAL(editingFinished()), this, SLOT(eStartTimeEnabled()));
    eStartTime.setDisplayFormat("HH:mm:ss");
    vlist->addWidget(new QLabel(
        //% "Season:"
        qtTrId("route.editor.activity.tools.label.season")), row, 0);
    vlist->addWidget(&cSeason, row++, 1, 1, 2);
    QObject::connect(&cSeason, SIGNAL(activated(int)), this, SLOT(cSeasonEnabled(int)));
    vlist->addWidget(new QLabel(
        //% "Weather:"
        qtTrId("route.editor.activity.tools.label.weather")), row, 0);
    vlist->addWidget(&cWeather, row++, 1, 1, 2);
    QObject::connect(&cWeather, SIGNAL(activated(int)), this, SLOT(cWeatherEnabled(int)));
    vlist->addWidget(new QLabel(
        //% "Horn At Crossings: "
        qtTrId("route.editor.activity.tools.label.horn.at.crossings")), row, 0);
    cHornAtCrossings.setMinimumHeight(25);
    vlist->addWidget(&cHornAtCrossings, row++, 1, 1, 2);
    QObject::connect(&cHornAtCrossings, SIGNAL(stateChanged(int)), this, SLOT(cHornAtCrossingsEnabled(int)));
    // Open Rails: how AI trains sound the horn; wood, sand and station
    // loads. Each written only when set.
    vlist->addWidget(new QLabel(
        //% "Horn Pattern:"
        qtTrId("route.editor.activity.tools.label.horn.pattern")), row, 0);
    cHornPattern.addItem(
        //% "Not set"
        qtTrId("route.editor.activity.tools.item.not.set"), QString());
    cHornPattern.addItem(
        //% "US (long, long, short, long)"
        qtTrId("route.editor.activity.tools.item.horn.us"), QString("US"));
    cHornPattern.addItem(
        //% "Single blast"
        qtTrId("route.editor.activity.tools.item.horn.single"), QString("Single"));
    vlist->addWidget(&cHornPattern, row++, 1, 1, 2);
    QObject::connect(&cHornPattern, SIGNAL(activated(int)), this, SLOT(cHornPatternEnabled(int)));
    for(QSpinBox *fuel : {&sFuelWood, &sFuelSand}){
        fuel->setRange(-1, 100);
        fuel->setSuffix(" %");
        fuel->setSpecialValueText(qtTrId("route.editor.activity.tools.item.not.set"));
    }
    vlist->addWidget(new QLabel(
        //% "Fuel Wood:"
        qtTrId("route.editor.activity.tools.label.fuel.wood")), row, 0);
    vlist->addWidget(&sFuelWood, row++, 1, 1, 2);
    QObject::connect(&sFuelWood, SIGNAL(valueChanged(int)), this, SLOT(sFuelWoodEnabled(int)));
    vlist->addWidget(new QLabel(
        //% "Fuel Sand:"
        qtTrId("route.editor.activity.tools.label.fuel.sand")), row, 0);
    vlist->addWidget(&sFuelSand, row++, 1, 1, 2);
    QObject::connect(&sFuelSand, SIGNAL(valueChanged(int)), this, SLOT(sFuelSandEnabled(int)));
    vlist->addWidget(new QLabel(
        //% "Station Loads:"
        qtTrId("route.editor.activity.tools.label.load.stations")), row, 0);
    eLoadStations.setToolTip(
        //% "Open Rails: ACTIVITIES/OPENRAILS/<name>.load-stations-loads-or"
        qtTrId("route.editor.activity.tools.tooltip.load.stations"));
    vlist->addWidget(&eLoadStations, row++, 1, 1, 2);
    QObject::connect(&eLoadStations, SIGNAL(textEdited(QString)), this, SLOT(eLoadStationsEnabled(QString)));
    
    eFuelCoal = GuiFunct::newQLineEdit(25,3);
    eFuelDiesel = GuiFunct::newQLineEdit(25,3);
    eFuelWater = GuiFunct::newQLineEdit(25,3);
    eHazardAnimal = GuiFunct::newQLineEdit(25,3);  
    eHazardPeople = GuiFunct::newQLineEdit(25,3);  
            
    vlist->addWidget(new QLabel(
        //% "Fuel Coal:"
        qtTrId("route.editor.activity.tools.label.fuel.coal")), row, 0);
    vlist->addWidget(eFuelCoal, row, 1);
    vlist->addWidget(&sFuelCoal, row++, 2);
    QObject::connect(eFuelCoal, SIGNAL(textEdited(QString)), this, SLOT(eFuelCoalEnabled(QString)));
    QObject::connect(&sFuelCoal, SIGNAL(sliderReleased()), this, SLOT(sFuelCoalEnabled()));
    sFuelCoal.setRange(0, 100);
    sFuelCoal.setOrientation(Qt::Horizontal);
    vlist->addWidget(new QLabel(
        //% "Fuel Diesel:"
        qtTrId("route.editor.activity.tools.label.fuel.diesel")), row, 0);
    vlist->addWidget(eFuelDiesel, row, 1);
    vlist->addWidget(&sFuelDiesel, row++, 2);
    QObject::connect(eFuelDiesel, SIGNAL(textEdited(QString)), this, SLOT(eFuelDieselEnabled(QString)));
    QObject::connect(&sFuelDiesel, SIGNAL(sliderReleased()), this, SLOT(sFuelDieselEnabled()));
    sFuelDiesel.setRange(0, 100);
    sFuelDiesel.setOrientation(Qt::Horizontal);
    vlist->addWidget(new QLabel(
        //% "Fuel Water:"
        qtTrId("route.editor.activity.tools.label.fuel.water")), row, 0);
    vlist->addWidget(eFuelWater, row, 1);
    vlist->addWidget(&sFuelWater, row++, 2);
    QObject::connect(eFuelWater, SIGNAL(textEdited(QString)), this, SLOT(eFuelWaterEnabled(QString)));
    QObject::connect(&sFuelWater, SIGNAL(sliderReleased()), this, SLOT(sFuelWaterEnabled()));
    sFuelWater.setRange(0, 100);
    sFuelWater.setOrientation(Qt::Horizontal);
    vlist->addWidget(new QLabel(
        //% "Hazard Animal:"
        qtTrId("route.editor.activity.tools.label.hazard.animal")), row, 0);
    vlist->addWidget(eHazardAnimal, row, 1);
    vlist->addWidget(&sHazardAnimal, row++, 2);
    QObject::connect(eHazardAnimal, SIGNAL(textEdited(QString)), this, SLOT(eHazardAnimalEnabled(QString)));
    QObject::connect(&sHazardAnimal, SIGNAL(sliderReleased()), this, SLOT(sHazardAnimalEnabled()));
    sHazardAnimal.setRange(0, 100);
    sHazardAnimal.setOrientation(Qt::Horizontal);
    vlist->addWidget(new QLabel(
        //% "Hazard People:"
        qtTrId("route.editor.activity.tools.label.hazard.people")), row, 0);
    vlist->addWidget(eHazardPeople, row, 1);
    vlist->addWidget(&sHazardPeople, row++, 2);
    QObject::connect(eHazardPeople, SIGNAL(textEdited(QString)), this, SLOT(eHazardPeopleEnabled(QString)));
    QObject::connect(&sHazardPeople, SIGNAL(sliderReleased()), this, SLOT(sHazardPeopleEnabled()));
    sHazardPeople.setRange(0, 100);
    sHazardPeople.setOrientation(Qt::Horizontal);
    QVBoxLayout *vbox2 = new QVBoxLayout;
    vbox2->setContentsMargins(0,0,0,0);
    vbox2->addItem(vlist);
    vbox2->addStretch(1);
    vbox1->addItem(vbox2);

    vbox2 = new QVBoxLayout;
    vbox2->setContentsMargins(0,0,0,0);
    vbox2->addWidget(new QLabel(
        //% "Description:"
        qtTrId("route.editor.activity.tools.label.description")));
    vbox2->addWidget(&eDescription);
    QObject::connect(&eDescription, SIGNAL(textChanged()), this, SLOT(eDescriptionEnabled()));
    eDescription.setMinimumWidth(350);
    vbox2->addWidget(new QLabel(
        //% "Briefing:"
        qtTrId("route.editor.activity.tools.label.briefing")));
    vbox2->addWidget(&eBriefing);
    QObject::connect(&eBriefing, SIGNAL(textChanged()), this, SLOT(eBriefingEnabled()));
    eBriefing.setMinimumWidth(350);
    vbox1->addItem(vbox2);
    settingsWidget.setParent(this);
    settingsWidget.setLayout(vbox1);
    settingsWidget.setWindowFlags(Qt::WindowType::Tool);
    settingsWidget.setWindowTitle(
        //% "Activity Settings."
        qtTrId("route.editor.activity.tools.title.activity.settings"));
    settingsWidget.setMinimumWidth(650);
    
    mstsOnly << newActButton << &cService << actServiceOpen << &cTraffic << &consists << actConsistJump
             << actConsistDelete << buttonTools["actNewLooseConsistTool"] << &conFilesShow << conFilesRefresh
             << &speedZones << actZoneJump << buttonTools["actNewSpeedZoneTool"] << actZoneDelete << actZoneDeleteAll
             << &failedSignals << actFailedSignalJump << actFailedSignalTool << actFailedSignalDelete
             << actFailedSignalDeleteAll << actEventsOpen << actSettingsOpen;
    QObject::connect(&cKind, SIGNAL(activated(int)), this, SLOT(kindChosen(int)));
    QObject::connect(&cPlayerTrain, SIGNAL(activated(int)), this, SLOT(cPlayerTrainEnabled(int)));
    showKind();
    vbox->addStretch(1);
    this->setLayout(vbox);    
    
    //QObject::connect(&consists, SIGNAL(itemClicked(QListWidgetItem*)),
    //                  this, SLOT(itemsSelected(QListWidgetItem*)));
    QObject::connect(&actShow, SIGNAL(textActivated(QString)),
                      this, SLOT(activitySelected(QString)));
    QObject::connect(&conFilesShow, SIGNAL(textActivated(QString)),
                      this, SLOT(conFilesShowEnabled(QString)));
    QObject::connect(&cService, SIGNAL(textActivated(QString)),
                      this, SLOT(cServiceEnabled(QString)));
    QObject::connect(&cTraffic, SIGNAL(textActivated(QString)),
                      this, SLOT(cTrafficEnabled(QString)));
    //QObject::connect(loadActFilesButton, SIGNAL(released()),
    //                  this, SLOT(loadActFiles()));
}

void ActivityTools::actSettingsOpenEnabled(){
    if(actShow.currentIndex() < 0)
        return;
    settingsWidget.show();
}

void ActivityTools::actEventsOpenEnabled(){
    if(actShow.currentIndex() < 0)
        return;
    emit showActivityEventEditor();
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    emit showEvents(a);
}

void ActivityTools::actServiceOpenEnabled(){
    emit showActivityServiceEditor();
    emit showServices(route);
}

void ActivityTools::actTimetableOpenEnabled(){
    if(actShow.currentIndex() < 0)
        return;
    emit showActivityTimetableEditor();
    emit showTimetable(currentActivity());
}

void ActivityTools::actTrafficOpenEnabled(){
    emit showActivityTrafficEditor();
    emit showTraffic(route);
}

void ActivityTools::routeLoaded(Route* r){
    route = r;

    consists.clear();
    ConLib::loadSimpleList(Game::root);
    foreach(QString name, ConLib::conFileList){
        conFilesShow.addItem(name.section('/', -1), QVariant(name));
    }
    
    reloadActivityList();
    reloadServicesList();
    reloadTrafficsList();
    reloadPathsList();
}

void ActivityTools::conFilesRefreshSelected(){
    conFilesShow.clear();
    ConLib::loadSimpleList(Game::root, true);
    foreach(QString name, ConLib::conFileList){
        conFilesShow.addItem(name.section('/', -1), QVariant(name));
    }
}

void ActivityTools::actPathsRefreshListSelected(){
    if(route == NULL)
        return;
    
    route->loadPaths();
    
    reloadPathsList();
}

void ActivityTools::reloadActivityList(){
    actShow.clear();
    settingsWidget.setEnabled(false);
    if(route == NULL)
        return;
    const bool timetables = ActivityNotifier::timetables();
    foreach(int id, timetables ? route->timetableSetId : route->activityId){
        const Activity *a = timetables ? ActLib::TimetableSets.value(id) : ActLib::Act.value(id);
        if(a == NULL || a->header == NULL)
            continue;
        actShow.addItem(a->header->name, QVariant(id));
    }
    actShow.setCurrentIndex(-1);
}

Activity *ActivityTools::currentActivity(){
    if(actShow.currentIndex() < 0)
        return NULL;
    bool ok = false;
    const int id = actShow.currentData().toInt(&ok);
    if(!ok)
        return NULL;
    Activity *a = ActivityNotifier::timetables() ? ActLib::TimetableSets.value(id) : ActLib::Act.value(id);
    if(a == NULL || a->loaded != 1 || a->header == NULL)
        return NULL;
    return a;
}

void ActivityTools::kindChosen(int index){
    ActivityNotifier::instance()->setTimetables(cKind.itemData(index).toInt() == 1);
    showKind();
    reloadActivityList();
    activitySelected(QString());
}

// The panel as the kind is: the parts of an MSTS activity, or a timetable
// set's.
void ActivityTools::showKind(){
    const bool timetables = ActivityNotifier::timetables();
    for(QWidget *widget : mstsOnly)
        widget->setVisible(!timetables);
    for(QWidget *widget : timetablesOnly)
        widget->setVisible(timetables);
}

// A timetable set's trains to drive (none, or a running one) and what it
// holds; empty for an MSTS activity.
void ActivityTools::showTimetableSet(Activity *a){
    QSignalBlocker blocker(cPlayerTrain);
    cPlayerTrain.clear();
    timetableSummary.clear();
    if(a == NULL || !a->isTimetables())
        return;
    cPlayerTrain.addItem(
        //% "None (every train AI)"
        qtTrId("route.editor.activity.tools.player.none"), QString());
    int trains = 0, statics = 0, problems = 0;
    for(const ActivityTrains::Train &train : ActivityTrains::List(a)){
        ++(train.staticTrain ? statics : trains);
        problems += train.problems.size();
        if(!train.staticTrain)
            cPlayerTrain.addItem(QString("%1  %2").arg(QTime::fromMSecsSinceStartOfDay(train.time % 86400 * 1000)
                                                           .toString("HH:mm"), train.service), train.service);
    }
    const int chosen = cPlayerTrain.findData(a->playerTrain, Qt::UserRole, Qt::MatchFixedString);
    cPlayerTrain.setCurrentIndex(chosen >= 0 ? chosen : 0);
    //% "%1 timetables: %2 trains and %3 static; %4 problems (Check Activity lists them)."
    timetableSummary.setText(qtTrId("route.editor.activity.tools.timetable.summary").arg(a->timetables.size())
                             .arg(trains).arg(statics).arg(problems));
}

// The train the player drives in the set's run (not saved: chosen when
// played, as in Open Rails).
void ActivityTools::cPlayerTrainEnabled(int index){
    Activity *a = currentActivity();
    if(a == NULL || !a->isTimetables())
        return;
    a->playerTrain = cPlayerTrain.itemData(index).toString();
    emit showTimetable(a);
}

void ActivityTools::reloadServicesList(){
    int idx = cService.currentIndex();
    cService.clear();
    cService.addItem(
        //% "UNDEFINED"
        qtTrId("route.editor.activity.tools.item.undefined"), QVariant(-1));
    for(int i = 0; i < ActLib::jestservice; i++ ){
        if(ActLib::Services[i] == NULL)
            continue;
        cService.addItem(ActLib::Services[i]->displayName, QVariant(i));
    }
    
    if(idx >= 0)
        cService.setCurrentIndex(idx);
}

void ActivityTools::reloadTrafficsList(){
    int idx = cTraffic.currentIndex();
    cTraffic.clear();
    cTraffic.addItem(
        //% "UNDEFINED"
        qtTrId("route.editor.activity.tools.item.undefined.2"), QVariant(-1));
    for(int i = 0; i < ActLib::jesttraffic; i++ ){
        if(ActLib::Traffics[i] == NULL)
            continue;
        cTraffic.addItem(ActLib::Traffics[i]->nameId, QVariant(i));
    }
    
    if(idx >= 0)
        cTraffic.setCurrentIndex(idx);
}

void ActivityTools::reloadPathsList(){
    int idx = cPath.currentIndex();
    cPath.clear();
    
    for(int i = 0; i < route->path.size(); i++ )
        cPath.addItem(route->path[i]->displayName, QVariant(i));
    
    if(idx >= 0)
        cPath.setCurrentIndex(idx);
}
void ActivityTools::conFilesShowEnabled(QString val){
    if(actShow.currentIndex() < 0)
        return;
    QString file = conFilesShow.currentData().toString();
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    a->editorConListSelected = file;
}

// Play: the simulation of the activity chosen runs (started when there is
// none).
void ActivityTools::actPlayEnabled(){
    QString error;
    if(!Sim::SimulationController::instance()->play(currentActivity(), &error))
        QMessageBox::warning(this,
            //% "Simulation"
            qtTrId("route.editor.activity.tools.simulation.title"), error);
}

void ActivityTools::simPauseEnabled(){
    Sim::SimulationController::instance()->pause();
}

void ActivityTools::simStopEnabled(){
    Sim::SimulationController::instance()->stop();
}

void ActivityTools::simResetEnabled(){
    Sim::SimulationController::instance()->reset();
}

void ActivityTools::simRunToEnabled(){
    QString error;
    const int time = simRunToTime.time().msecsSinceStartOfDay() / 1000;
    if(!Sim::SimulationController::instance()->runTo(currentActivity(), time, &error))
        QMessageBox::warning(this, qtTrId("route.editor.activity.tools.simulation.title"), error);
}

// The world's clock's speed (WorldClock): the simulation's, or the speed the
// next one starts at, as the environment window shows it.
void ActivityTools::simSpeedSelected(int index){
    WorldClock::instance()->setSpeed(simSpeed.itemData(index).toDouble());
}

// The controls as the simulation is: what can be done, its time, its
// trains.
void ActivityTools::showSimulation(){
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const bool exists = simulation->exists();
    const bool runs = simulation->runs();
    const bool chosen = currentActivity() != NULL;
    simPlay->setEnabled(chosen && !runs);
    simPause->setEnabled(runs);
    simStop->setEnabled(exists);
    simReset->setEnabled(exists);
    simRunTo->setEnabled(chosen);
    const double pace = WorldClock::instance()->speed();
    int speed = simSpeed.findData(pace);
    if(speed < 0){
        // One the list lacks (the environment window's): in its place.
        speed = 0;
        while(speed < simSpeed.count() && simSpeed.itemData(speed).toDouble() > 0
              && (pace <= 0 || simSpeed.itemData(speed).toDouble() < pace))
            speed++;
        simSpeed.insertItem(speed, pace > 0 ?
            qtTrId("route.editor.activity.tools.simulation.speed").arg(pace) :
            qtTrId("route.editor.activity.tools.simulation.speed.fastest"), pace);
    }
    if(speed != simSpeed.currentIndex())
        simSpeed.setCurrentIndex(speed);
    if(!exists){
        simTime.setText("--:--:--");
        simStatus.clear();
        simStatus.setToolTip(QString());
        simStatus.hide();
        return;
    }
    const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->frame();
    const int seconds = (int(std::floor(frame->time)) % 86400 + 86400) % 86400;
    QString time = QTime::fromMSecsSinceStartOfDay(seconds * 1000).toString("HH:mm:ss");
    // Short of the speed asked (or as fast as possible): the speed reached.
    if(runs && frame->rate > 0 && (frame->speed <= 0 || frame->rate < 0.9 * frame->speed))
        //% "%1 (%2×)"
        time = qtTrId("route.editor.activity.tools.simulation.time.rate").arg(time).arg(frame->rate, 0, 'f', 0);
    simTime.setText(time);
    simStatus.setText(simulation->statusText());
    simStatus.show();
}

// Trains in a deadlock wait for good: the user is told which (once a ring),
// to change their times or paths; the simulation goes on.
void ActivityTools::simulationEvents(int first){
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const std::vector<Sim::TrainEvent> &events = simulation->events();
    if(first == 0){
        // A new run: what was told is of the one before.
        deadlocked.clear();
        if(deadlockBox)
            deadlockBox->close();
    }
    QStringList fresh;
    for(size_t i = size_t(std::max(0, first)); i < events.size(); i++){
        if(events[i].kind != Sim::TrainEvent::Deadlock)
            continue;
        const QString name = simulation->trainName(events[i].train);
        if(!deadlocked.contains(name) && !fresh.contains(name))
            fresh << name;
    }
    if(fresh.isEmpty())
        return;
    deadlocked << fresh;
    if(!deadlockBox){
        deadlockBox = new QMessageBox(QMessageBox::Warning, qtTrId("route.editor.activity.tools.simulation.title"),
                                      QString(), QMessageBox::Ok, this);
        deadlockBox->setAttribute(Qt::WA_DeleteOnClose);
        deadlockBox->setModal(false);
    }
    //% "These trains wait for each other and will not move again (a deadlock): %1. The simulation goes on. Change their times or paths so they meet where one can pass the other, then reset the simulation."
    deadlockBox->setText(qtTrId("route.editor.activity.tools.simulation.deadlock").arg(deadlocked.join(", ")));
    deadlockBox->show();
}

// While a simulation exists the activity is shown, not edited: its choice,
// settings and editing buttons are off.
void ActivityTools::lockEditing(bool locked){
    for(QWidget *widget : editing)
        widget->setEnabled(!locked);
    actShow.setEnabled(!locked);
    settingsWidget.setEnabled(!locked && currentActivity() != NULL);
    if(locked)
        for(QPushButton *tool : {buttonTools["trackPathTool"], buttonTools["actNewLooseConsistTool"],
                                 buttonTools["actNewSpeedZoneTool"]})
            if(tool->isChecked())
                tool->setChecked(false);
    showOverlay();
    showSimulation();
}

void ActivityTools::showOverlay(){
    Activity *a = currentActivity();
    const QString path = ActivityOverlay::PathOf(a);
    overlayLine.setVisible(!path.isEmpty());
    if(path.isEmpty())
        return;
    QPushButton *merge = overlayLine.findChild<QPushButton*>("overlayMerge");
    const bool merged = a->mergedOverlay == path;
    if(merge != NULL)
        merge->setEnabled(!merged && !ActivityNotifier::editingLocked());
    overlayLabel.setText(merged ?
        //% "The overlay %1 is merged; it is renamed when the activity is saved."
        qtTrId("route.editor.activity.tools.overlay.merged").arg(QFileInfo(path).fileName()) :
        //% "Open Rails adds the overlay %1 to this activity."
        qtTrId("route.editor.activity.tools.overlay.present").arg(QFileInfo(path).fileName()));
}

void ActivityTools::actOverlayMergeEnabled(){
    Activity *a = currentActivity();
    const QString path = ActivityOverlay::PathOf(a);
    if(a == NULL || path.isEmpty() || a->mergedOverlay == path)
        return;
    const ActivityOverlay::Result preview = ActivityOverlay::Merge(a, path, false);
    QDialog dialog(this);
    dialog.setWindowTitle(
        //% "Merge the Overlay"
        qtTrId("route.editor.activity.tools.overlay.title"));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *intro = new QLabel(preview.readable ?
        //% "These additions go into the activity as edits (one undo step). Saving the activity renames %1 to %2."
        qtTrId("route.editor.activity.tools.overlay.intro").arg(QFileInfo(path).fileName(), QFileInfo(path).fileName() + ".merged") :
        //% "The overlay %1 cannot be read."
        qtTrId("route.editor.activity.tools.overlay.unreadable").arg(QFileInfo(path).fileName()));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    QListWidget *lines = new QListWidget;
    for(const QString &line : preview.added)
        lines->addItem(line);
    for(const QString &line : preview.skipped){
        QListWidgetItem *item = new QListWidgetItem(
            //% "Left out: %1"
            qtTrId("route.editor.activity.tools.overlay.left.out").arg(line), lines);
        item->setForeground(QBrush(QColor(140, 140, 140)));
    }
    lines->setMinimumSize(480, 220);
    layout->addWidget(lines);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(preview.readable && !preview.added.isEmpty());
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if(dialog.exec() != QDialog::Accepted)
        return;
    {
        ActivityEdit edit(a);
        ActivityOverlay::Merge(a, path, true);
        a->mergedOverlay = path;
    }
    activitySelected(QString());
    emit showEvents(a);
}

void ActivityTools::actCheckEnabled(){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    if(checkWindow == NULL){
        checkWindow = new ActivityCheckWindow(this);
        QObject::connect(checkWindow, &ActivityCheckWindow::findingChosen, this, &ActivityTools::showFinding);
    }
    checkWindow->check(a);
    checkWindow->show();
    checkWindow->raise();
}

// What the finding names: the event, service or traffic in its window, the
// path chosen, the view at the place.
void ActivityTools::showFinding(const ActivityFinding &finding){
    Activity *a = currentActivity();
    if(a == NULL || checkWindow == NULL || checkWindow->checkedActivity() != a)
        return;
    switch(finding.target){
    case ActivityFinding::Event:
        emit showActivityEventEditor();
        emit showEvents(a);
        emit selectEvent(finding.eventId);
        break;
    case ActivityFinding::Service:
        emit showActivityServiceEditor();
        emit showServices(route);
        emit selectService(finding.name);
        break;
    case ActivityFinding::Traffic:
        emit showActivityTrafficEditor();
        emit showTraffic(route);
        emit selectTraffic(finding.name);
        break;
    case ActivityFinding::Path:
        if(route == NULL)
            break;
        for(int i = 0; i < route->path.size(); i++)
            if(route->path[i] != NULL && route->path[i]->isNamed(finding.name)){
                cPath.setCurrentIndex(cPath.findData(i));
                selectCurrentPath();
                break;
            }
        break;
    case ActivityFinding::Position:
        if(coordinate == NULL)
            coordinate = new PreciseTileCoordinate();
        coordinate->TileX = finding.position[0];
        coordinate->TileZ = finding.position[1];
        coordinate->setWxyz(finding.position[2], 0, finding.position[3]);
        emit jumpTo(coordinate);
        break;
    default:
        break;
    }
}

void ActivityTools::actSaveEnabled(){
    QVector<QString> unsavedItems;
    ActLib::GetUnsavedInfo(unsavedItems);
    if(unsavedItems.size() == 0){
        qDebug() << "nic do zapisania";
        return;
    }
    
    UnsavedDialog unsavedDialog("SC");
    unsavedDialog.setWindowTitle(
        //% "Save changes?"
        qtTrId("route.editor.activity.tools.title.save.changes"));
    unsavedDialog.setMsg(
        //% "Save changes in activities?"
        qtTrId("route.editor.activity.tools.message.save.changes.in.activities"));
    for(int i = 0; i < unsavedItems.size(); i++){
        unsavedDialog.items.addItem(unsavedItems[i]);
    }
    // Activities about to be saved with errors Check Activity finds.
    for(Activity *a : ActLib::Act){
        if(a == NULL || a->loaded != 1 || !a->isUnSaved())
            continue;
        int errors = 0;
        for(const ActivityFinding &finding : CheckActivity(a))
            errors += finding.severity == ActivityFinding::Error ? 1 : 0;
        if(errors > 0)
            unsavedDialog.items.addItem(
                //% "%1: %2 errors (Check Activity lists them)"
                qtTrId("route.editor.activity.tools.save.check.errors").arg(a->name).arg(errors));
    }
    unsavedDialog.exec();
    if(unsavedDialog.changed == 1){
        ActLib::SaveAll();
    }
    return;
}

void ActivityTools::activitySelected(QString n){
    Activity *a = currentActivity();
    // Map and 3D, and the windows showing the activity's parts (when they
    // are open), follow the activity chosen, none included.
    if(route != NULL)
        route->activitySelected(a);
    emit showTimetable(a);
    emit showEvents(a);
    emit showServices(route);
    emit showTraffic(route);
    // The settings edit the activity chosen: nothing without one (nor
    // while a simulation runs it).
    settingsWidget.setEnabled(a != NULL && !a->isTimetables() && !ActivityNotifier::editingLocked());
    showSimulation();
    showTimetableSet(a);
    if(a == NULL || a->isTimetables()){
        reloadActivityObjectLists();
        showOverlay();
        return;
    }

    eFileName.setText(a->nameid);
    if(a->isNew())
        eFileName.setEnabled(true);
    else
        eFileName.setEnabled(false);
    eDisplayName.setText(a->header->name);
    eDuration.setTime(QTime(a->header->duration[0], a->header->duration[1]));
    eStartTime.setTime(QTime(a->header->startTime[0], a->header->startTime[1], a->header->startTime[2]));

    eHazardAnimal->setText(QString::number(a->header->animals));
    eHazardPeople->setText(QString::number(a->header->workers));
    eFuelCoal->setText(QString::number(a->header->fuelCoal));
    eFuelWater->setText(QString::number(a->header->fuelWater));
    eFuelDiesel->setText(QString::number(a->header->fuelDiesel));
    sHazardAnimal.setValue(a->header->animals);
    sHazardPeople.setValue(a->header->workers);
    sFuelCoal.setValue(a->header->fuelCoal);
    sFuelWater.setValue(a->header->fuelWater);
    sFuelDiesel.setValue(a->header->fuelDiesel);
    
    cDifficulty.setCurrentIndex(a->header->difficulty);
    cSeason.setCurrentIndex(a->header->season);
    cWeather.setCurrentIndex(a->header->weather);
    
    cHornAtCrossings.blockSignals(true);
    if(a->isOrtsHornAtCrossigns())
        cHornAtCrossings.setChecked(true);
    else
        cHornAtCrossings.setChecked(false);
    cHornAtCrossings.blockSignals(false);
    {
        QSignalBlocker pattern(cHornPattern), wood(sFuelWood), sand(sFuelSand), loads(eLoadStations);
        const int index = cHornPattern.findData(a->ortsAICrossingHornPattern);
        if(index >= 0){
            cHornPattern.setCurrentIndex(index);
        } else {
            cHornPattern.addItem(a->ortsAICrossingHornPattern, a->ortsAICrossingHornPattern);
            cHornPattern.setCurrentIndex(cHornPattern.count() - 1);
        }
        sFuelWood.setValue(a->header->fuelWood);
        sFuelSand.setValue(a->header->fuelSand);
        eLoadStations.setText(a->header->loadStationsPopulation);
    }
   
    if(a->playerServiceDefinition == NULL){
        cService.setCurrentIndex(0);
    } else {
        cService.setCurrentIndex(0);
        QString cname = a->playerServiceDefinition->name;
        for(int i = 0; i < cService.count() ; i++ ){
            int id = cService.itemData(i).toInt();
            if(id < 0)
                continue;
            if(ActLib::Services[id] == NULL)
                continue;
            if(cname == ActLib::Services[id]->nameId){
                cService.setCurrentIndex(i);
                break;
            }
        }
    }
    
    if(a->traffic == NULL){
        cTraffic.setCurrentIndex(0);
    } else {
        cTraffic.setCurrentIndex(0);
        QString cname = a->traffic->name;
        for(int i = 0; i < cTraffic.count() ; i++ ){
            int id = cTraffic.itemData(i).toInt();
            if(id < 0)
                continue;
            if(ActLib::Traffics[id] == NULL)
                continue;
            //qDebug() << cname << route->service[id]->name;
            if(cname.compare(ActLib::Traffics[id]->nameId, Qt::CaseInsensitive) == 0){
                cTraffic.setCurrentIndex(i);
                break;
            }
        }
    }

    reloadActivityObjectLists();
    showOverlay();
    
    eBriefing.blockSignals(true);
    eDescription.blockSignals(true);
    QString txt = a->header->briefing;
    txt.replace("\\n","\n");
    eBriefing.setPlainText(txt);
    txt = a->header->description;
    txt.replace("\\n","\n");
    eDescription.setPlainText(txt);
    eBriefing.blockSignals(false);
    eDescription.blockSignals(false);
}

void ActivityTools::reloadActivityObjectLists(){
    Activity *a = currentActivity();
    if(a == NULL){
        consists.clear();
        speedZones.clear();
        failedSignals.clear();
        return;
    }
    
    Consist *e;
    consists.clear();
    for (int i = 0; i < a->activityObjects.size(); i++){
        e = a->activityObjects[i]->con;
        if(e == NULL) continue;
        consists.addItem(e->showName, QVariant(i));
    }
        
    speedZones.clear();
    ActivityObject *rz;
    for (int i = 0; i < a->restrictedSpeedZone.size(); i++){
        rz = a->restrictedSpeedZone[i];
        if(rz == NULL) continue;
        speedZones.addItem(rz->getSpeedZoneName(), QVariant(i));
    }

    failedSignals.clear();
    for (int i = 0; i < a->activityFailedSignal.size(); i++){
        rz = a->activityFailedSignal[i];
        if(rz == NULL) continue;
        //% "Signal ID: %1"
        failedSignals.addItem(qtTrId("activity.failed.signal.id")
                              .arg(rz->getFailedSignalId()), QVariant(i));
    }
}

void ActivityTools::loadActFiles(){
    if(route == NULL)
        return;
    
}

void ActivityTools::actNewLooseConsistToolEnabled(bool val){
    conFilesShowEnabled("");
    if(val){
        emit enableTool("actNewLooseConsistTool");
    } else {
        emit enableTool("");
    }
}

void ActivityTools::actPathsEditToolEnabled(bool val){
    if(!val){
        emit enableTool("");
        return;
    }
    selectCurrentPath();
    emit enableTool("trackPathTool");
}

void ActivityTools::cPathSelected(int index){
    Q_UNUSED(index);
    // While the path tool is on, it edits the path chosen.
    if(buttonTools["trackPathTool"] != NULL && buttonTools["trackPathTool"]->isChecked())
        selectCurrentPath();
}

bool ActivityTools::askPathNames(QString &id, QString &name){
    const QString directory = Game::root + "/ROUTES/" + Game::route + "/PATHS";
    for(;;){
        QDialog dialog(this);
        dialog.setWindowTitle(
            //% "Path"
            qtTrId("route.editor.activity.tools.path.dialog.title"));
        QFormLayout *form = new QFormLayout(&dialog);
        QLineEdit *idEdit = new QLineEdit(id);
        QLineEdit *nameEdit = new QLineEdit(name);
        form->addRow(
            //% "File name"
            qtTrId("route.editor.activity.tools.path.dialog.file"), idEdit);
        form->addRow(
            //% "Display name"
            qtTrId("route.editor.activity.tools.path.dialog.name"), nameEdit);
        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if(dialog.exec() != QDialog::Accepted)
            return false;
        id = idEdit->text().trimmed();
        name = nameEdit->text().trimmed();
        if(name.isEmpty())
            name = id;
        // Services and activities name a path by its id, the file's name.
        bool taken = QFile::exists(directory + "/" + id + ".pat");
        for(int i = 0; i < route->path.size() && !taken; i++)
            taken = route->path[i] != NULL
                    && (route->path[i]->nameId.compare(id, Qt::CaseInsensitive) == 0
                        || route->path[i]->trPathName.compare(id, Qt::CaseInsensitive) == 0);
        const bool valid = !id.isEmpty() && !id.contains(QRegularExpression("[\\\\/:*?\"<>|]"));
        if(valid && !taken)
            return true;
        QMessageBox::warning(this, qtTrId("route.editor.activity.tools.path.dialog.title"),
            valid ?
            //% "A path named %1 exists already."
            qtTrId("route.editor.activity.tools.path.dialog.taken").arg(id) :
            //% "A file name cannot be empty or contain \\ / : * ? \" < > |."
            qtTrId("route.editor.activity.tools.path.dialog.invalid"));
    }
}

void ActivityTools::addEditedPath(Path *path){
    ActLib::AddNewPath(path);
    route->path.push_back(path);
    reloadPathsList();
    cPath.setCurrentIndex(cPath.findData(QVariant(int(route->path.size()) - 1)));
    // Edit it: a click on the track starts a new path.
    if(buttonTools["trackPathTool"]->isChecked())
        selectCurrentPath();
    else
        buttonTools["trackPathTool"]->setChecked(true);
}

void ActivityTools::actPathsNewSelected(){
    if(route == NULL)
        return;
    QString id, name;
    if(!askPathNames(id, name))
        return;
    const QString directory = Game::root + "/ROUTES/" + Game::route + "/PATHS";
    QDir().mkpath(directory);
    Path *path = new Path(directory, id + ".pat", true);
    path->displayName = name;
    addEditedPath(path);
}

void ActivityTools::actPathsCloneSelected(){
    if(route == NULL || cPath.currentIndex() < 0)
        return;
    const int sourceId = cPath.currentData().toInt();
    if(sourceId < 0 || sourceId >= route->path.size() || route->path[sourceId] == NULL)
        return;
    Path *source = route->path[sourceId];
    QString id = source->nameId + "_copy";
    QString name = source->displayName + " copy";
    if(!askPathNames(id, name))
        return;
    Path *path = new Path(source->path, id + ".pat", true);
    path->copyFrom(*source);
    path->displayName = name;
    addEditedPath(path);
}

void ActivityTools::actPathsDeleteSelected(){
    if(route == NULL || cPath.currentIndex() < 0)
        return;
    const int pathId = cPath.currentData().toInt();
    if(pathId < 0 || pathId >= route->path.size() || route->path[pathId] == NULL)
        return;
    Path *path = route->path[pathId];
    //% "Delete Path"
    const QString title = qtTrId("route.editor.activity.tools.path.delete.title");
    if(!Game::writeEnabled){
        QMessageBox::warning(this, title,
            //% "The route is read-only."
            qtTrId("route.editor.activity.tools.path.delete.read.only"));
        return;
    }
    QString question =
        //% "Delete the path %1 and its file? This cannot be undone."
        qtTrId("route.editor.activity.tools.path.delete.question").arg(path->displayName);
    const QStringList users = ActLib::PathUsers(path);
    if(!users.isEmpty())
        question += "\n\n" +
            //% "These services and activities name it: %1"
            qtTrId("route.editor.activity.tools.path.delete.users").arg(users.join(", "));
    if(QMessageBox::question(this, title, question) != QMessageBox::Yes)
        return;
    if(QFile::exists(path->pathid) && !QFile::remove(path->pathid)){
        QMessageBox::warning(this, title,
            //% "The file %1 could not be deleted."
            qtTrId("route.editor.activity.tools.path.delete.failed").arg(path->pathid));
        return;
    }
    if(path->isSelected())
        emit objectSelected(NULL);
    path->unselect();
    ActLib::RemovePath(path);
    route->path.removeAll(path);
    reloadPathsList();
}

bool ActivityTools::eventFilter(QObject *watched, QEvent *event){
    // Opening the list: names changed in the path properties since it was
    // filled show.
    if(watched == &cPath && event->type() == QEvent::MouseButtonPress)
        refreshPathNames();
    return QWidget::eventFilter(watched, event);
}

void ActivityTools::refreshPathNames(){
    if(route == NULL)
        return;
    for(int i = 0; i < cPath.count(); i++){
        const int id = cPath.itemData(i).toInt();
        if(id >= 0 && id < route->path.size() && route->path[id] != NULL
                && cPath.itemText(i) != route->path[id]->displayName)
            cPath.setItemText(i, route->path[id]->displayName);
    }
}

void ActivityTools::selectCurrentPath(){
    if(route == NULL || cPath.currentIndex() < 0)
        return;
    int currentPathId = cPath.currentData().toInt();
    if(currentPathId < 0 || currentPathId >= route->path.size())
        return;
    for(int i = 0; i < route->path.size(); i++){
        route->path[i]->unselect();
    }
    emit objectSelected(route->path[currentPathId]);
}


void ActivityTools::msg(QString text){
    if(text == "refreshActivityTools"){
        reloadActivityObjectLists();
    }
}
    
void ActivityTools::msg(QString text, QString val){
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

void ActivityTools::cServiceEnabled(QString val){
    if(actShow.currentIndex() < 0)
        return;
    int id = cService.currentData().toInt();
    Activity *a = currentActivity();
    if(a == NULL)
        return;

    ActivityEdit edit(a);
    if(id == -1){
        a->clearPlayerService();
    } else if(ActLib::Services.value(id) != NULL) {
        int startTime = a->header->startTime[0]*60*60 + a->header->startTime[1]*60;
        a->createNewPlayerService(ActLib::Services[id]->nameId, startTime);
    }
}

void ActivityTools::cTrafficEnabled(QString val){
    if(actShow.currentIndex() < 0)
        return;
    int id = cTraffic.currentData().toInt();
    Activity *a = currentActivity();
    if(a == NULL)
        return;

    ActivityEdit edit(a);
    if(id == -1){
        a->clearTraffic();
    } else if(ActLib::Traffics.value(id) != NULL) {
        a->createNewTrafficService(ActLib::Traffics[id]);
    }
}

void ActivityTools::newActButtonEnabled(){
    if(route == NULL || route->getTrk() == NULL)
        return;
    QString pathid = Game::root + "/ROUTES/" + Game::route + "/ACTIVITIES/";
    //ActLib::Act[ActLib::jestact] = new Activity(pathid, "file.act", true);
    for(int i = 0; i < 1000; i++){
        QString name = QString("file")+QString::number(i)+QString(".act");
        // A name in use, loaded (id 0 included) or only on disk.
        if(ActLib::GetAct(pathid, name) >= 0 || QFile::exists(ContentPath::join(pathid, name)))
            continue;
        int id = ActLib::AddAct(pathid, name, true);
        ActLib::Act[id]->init(route->getTrk()->idName, "New Activity"+QString::number(i));
        qDebug()<< ActLib::Act[id]->header->name;
        route->activityId.push_back(id);
        //actShow.addItem(ActLib::Act[id]->header->name, id);
        //actShow.setCurrentIndex(actShow.count()-1); 
        reloadActivityList();
        actShow.setCurrentIndex(actShow.findData(id));
        activitySelected("");
        return;
    }
}
void ActivityTools::actConsistDeleteEnabled(){
    if(actShow.currentIndex() < 0)
        return;
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    int i = consists.currentIndex();
    if(i >= a->activityObjects.size())
        return;
    if(i < 0)
        return;
    
    ActivityEdit edit(a);
    a->activityObjects[i]->remove();
    reloadActivityObjectLists();
    emit sendMsg("unselect");
}

void ActivityTools::actConsistJumpEnabled(){
    if(actShow.currentIndex() < 0)
        return;
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    int i = consists.currentIndex();
    if(i < 0 || i >= a->activityObjects.size())
        return;
    Consist *e = a->activityObjects[i]->con;
    if(e == NULL) 
        return;
    
    float posTW[5];
    bool ok = a->activityObjects[i]->getElementPosition(0, posTW);
    if(!ok)
        return;
    
    if(coordinate == NULL)
        coordinate = new PreciseTileCoordinate();
    
    coordinate->TileX = posTW[0];
    coordinate->TileZ = posTW[1];
    coordinate->setWxyz(posTW[2], 0, posTW[4]);
    
    emit jumpTo(coordinate);
}

void ActivityTools::actFailedSignalsJumpEnabled(){
    if(actShow.currentIndex() < 0)
        return;
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    int i = failedSignals.currentIndex();
    if(i < 0 || i >= a->activityFailedSignal.size())
        return;
    float posTW[5];
    bool ok = a->activityFailedSignal[i]->getElementPosition(0, posTW);
    if(!ok)
        return;
    if(coordinate == NULL)
        coordinate = new PreciseTileCoordinate();
    
    coordinate->TileX = posTW[0];
    coordinate->TileZ = posTW[1];
    coordinate->setWxyz(posTW[2], 0, posTW[4]);
    
    emit jumpTo(coordinate);
}

void ActivityTools::actFailedSignalNewToolEnabled(){
    if(actShow.currentIndex() < 0)
        return;
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    ActivityEdit edit(a);
    bool ok = a->newFailedSignalFromSelected();
    if(!ok){
        edit.cancel();
        QMessageBox msgBox;
        msgBox.setWindowTitle(
            //% "Signal not disabled!"
            qtTrId("route.editor.activity.tools.title.signal.not.disabled"));
        msgBox.setText(
            //% "Select signal before using this button."
            qtTrId("route.editor.activity.tools.text.select.signal.before.using.this.button"));
        msgBox.exec();
        return;
    }
    reloadActivityObjectLists();
}

void ActivityTools::actFailedSignalDeleteEnabled(){
    if(actShow.currentIndex() < 0)
        return;
    Activity *a = currentActivity();
    if(a == NULL)
        return;

    int i = failedSignals.currentIndex();
        qDebug() << i << a->activityFailedSignal.size();
    if(i >= a->activityFailedSignal.size())
        return;
    if(i < 0)
        return;
    ActivityEdit edit(a);
    a->activityFailedSignal[i]->remove();
    reloadActivityObjectLists();
    emit sendMsg("unselect");
}

void ActivityTools::actFailedSignalDeleteAllEnabled(){
    if(actShow.currentIndex() < 0)
        return;
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    
    ActivityEdit edit(a);
    a->removeAllFailedSignals();
    reloadActivityObjectLists();
    emit sendMsg("unselect");
}

void ActivityTools::actReducedSpeedZonesEnabled(){
    if(actShow.currentIndex() < 0)
        return;
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    int i = speedZones.currentIndex();
    if(i < 0 || i >= a->restrictedSpeedZone.size())
        return;
    float posTW[5];
    bool ok = a->restrictedSpeedZone[i]->getElementPosition(0, posTW);
    if(!ok)
        return;
    if(coordinate == NULL)
        coordinate = new PreciseTileCoordinate();
    
    coordinate->TileX = posTW[0];
    coordinate->TileZ = posTW[1];
    coordinate->setWxyz(posTW[2], 0, posTW[4]);
    
    emit jumpTo(coordinate);
}

void ActivityTools::actZoneNewToolEnabled(bool val){
    if(val){
        emit enableTool("actNewSpeedZoneTool");
    } else {
        emit enableTool("");
    }
}

void ActivityTools::actZoneDeleteEnabled(){
    if(actShow.currentIndex() < 0)
        return;
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    
    int i = speedZones.currentIndex();
    if(i >= a->restrictedSpeedZone.size())
        return;
    if(i < 0)
        return;
    ActivityEdit edit(a);
    a->restrictedSpeedZone[i]->remove();
    
    reloadActivityObjectLists();
    emit sendMsg("unselect");
}

void ActivityTools::actZoneDeleteAllEnabled(){
    if(actShow.currentIndex() < 0)
        return;
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    
    ActivityEdit edit(a);
    a->removeAllSpeedZones();
    reloadActivityObjectLists();
    emit sendMsg("unselect");
}

/*void ActivityTools::actServiceNewEnabled(){
    ActivityServiceTools sTools;
    QString pathid = Game::root + "/ROUTES/" + Game::route + "/SERVICES/";
    QString name = "aaaaa1";
    Service *s = new Service(pathid, name+".srv", true);
    sTools.setData(s, route->path);
    sTools.exec();
    if(sTools.changed) {
        route->service.push_back(s);
        reloadServicesList();
    }
}*/

void ActivityTools::eFileNameEnabled(QString val){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    ActivityEdit edit(a, true);
    a->setFileName(val);
}

void ActivityTools::eDisplayNameEnabled(QString val){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    const int id = actShow.currentData().toInt();
    ActivityEdit edit(a, true);
    a->setDisplayName(val);
    
    // The list shows the new name; the same activity stays chosen.
    reloadActivityList();
    actShow.setCurrentIndex(actShow.findData(id));
    settingsWidget.setEnabled(currentActivity() != NULL && !ActivityNotifier::editingLocked());
}

void ActivityTools::cDifficultyEnabled(int val){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    ActivityEdit edit(a);
    a->setDifficulty(val);
}

void ActivityTools::eDurationEnabled(){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    ActivityEdit edit(a);
    a->setDuration(eDuration.time().hour(),  eDuration.time().minute());
}

void ActivityTools::eStartTimeEnabled(){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    ActivityEdit edit(a);
    a->setStartTime(eStartTime.time().hour(), eStartTime.time().minute(), eStartTime.time().second());
}

void ActivityTools::cSeasonEnabled(int val){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    ActivityEdit edit(a);
    a->setSeason(val);
}

void ActivityTools::cWeatherEnabled(int val){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    ActivityEdit edit(a);
    a->setWeather(val);
}

void ActivityTools::cHornAtCrossingsEnabled(int val){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    ActivityEdit edit(a);
    if(val == Qt::Checked)
        a->setOrtsHornAtCrossigns(true);
    else
        a->setOrtsHornAtCrossigns(false);
}

void ActivityTools::cHornPatternEnabled(int index){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    ActivityEdit edit(a);
    a->setOrtsHornPattern(cHornPattern.itemData(index).toString());
}

void ActivityTools::sFuelWoodEnabled(int val){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    ActivityEdit edit(a, true);
    a->setFuelWood(val);
}

void ActivityTools::sFuelSandEnabled(int val){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    ActivityEdit edit(a, true);
    a->setFuelSand(val);
}

void ActivityTools::eLoadStationsEnabled(QString val){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    ActivityEdit edit(a, true);
    a->setLoadStationsPopulation(val.trimmed());
}

void ActivityTools::eFuelCoalEnabled(QString val){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    bool ok = false;
    int ival = val.toInt(&ok);
    if(!ok)
        return;
    ActivityEdit edit(a, true);
    sFuelCoal.setValue(ival);
    a->setFuelCoal(ival);
}

void ActivityTools::sFuelCoalEnabled(){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    eFuelCoal->setText(QString::number(sFuelCoal.value()));
    ActivityEdit edit(a);
    a->setFuelCoal(sFuelCoal.value());
}

void ActivityTools::eFuelDieselEnabled(QString val){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    bool ok = false;
    int ival = val.toInt(&ok);
    if(!ok)
        return;
    ActivityEdit edit(a, true);
    sFuelDiesel.setValue(ival);
    a->setFuelDiesel(ival);
}

void ActivityTools::sFuelDieselEnabled(){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    eFuelDiesel->setText(QString::number(sFuelDiesel.value()));
    ActivityEdit edit(a);
    a->setFuelDiesel(sFuelDiesel.value());
}

void ActivityTools::eFuelWaterEnabled(QString val){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    bool ok = false;
    int ival = val.toInt(&ok);
    if(!ok)
        return;
    ActivityEdit edit(a, true);
    sFuelWater.setValue(ival);
    a->setFuelWater(ival);
}

void ActivityTools::sFuelWaterEnabled(){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    eFuelWater->setText(QString::number(sFuelWater.value()));
    ActivityEdit edit(a);
    a->setFuelWater(sFuelWater.value());
}

void ActivityTools::eHazardAnimalEnabled(QString val){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    bool ok = false;
    int ival = val.toInt(&ok);
    if(!ok)
        return;
    ActivityEdit edit(a, true);
    sHazardAnimal.setValue(ival);
    a->setHazardAnimals(ival);
}

void ActivityTools::sHazardAnimalEnabled(){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    eHazardAnimal->setText(QString::number(sHazardAnimal.value()));
    ActivityEdit edit(a);
    a->setHazardAnimals(sHazardAnimal.value());
}

void ActivityTools::eHazardPeopleEnabled(QString val){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    bool ok = false;
    int ival = val.toInt(&ok);
    if(!ok)
        return;
    ActivityEdit edit(a, true);
    sHazardPeople.setValue(ival);
    a->setHazardWorkers(ival);
}

void ActivityTools::sHazardPeopleEnabled(){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    eHazardPeople->setText(QString::number(sHazardPeople.value()));
    ActivityEdit edit(a);
    a->setHazardWorkers(sHazardPeople.value());
}

void ActivityTools::eDescriptionEnabled(){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    QString txt = eDescription.toPlainText();
    ActivityEdit edit(a, true);
    txt.replace("\n","\\n");
    a->setDescription(txt);
}

void ActivityTools::eBriefingEnabled(){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    QString txt = eBriefing.toPlainText();
    ActivityEdit edit(a, true);
    txt.replace("\n","\\n");
    a->setBriefing(txt);
}

void ActivityTools::descriptionOpenEnabled(){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    TextEditDialog dialog;
    QString txt = a->header->description;
    txt.replace("\\n","\n");
    dialog.textBox.setPlainText(txt);
    dialog.setWindowTitle(
        //% "Activity Description"
        qtTrId("route.editor.activity.tools.title.activity.description"));
    dialog.exec();
    if(dialog.changed == 1){
        txt = dialog.textBox.toPlainText();
        ActivityEdit edit(a);
        txt.replace("\n","\\n");
        a->setDescription(txt);
    }
}

void ActivityTools::briefingOpenEnabled(){
    Activity *a = currentActivity();
    if(a == NULL)
        return;
    TextEditDialog dialog;
    QString txt = a->header->briefing;
    txt.replace("\\n","\n");
    dialog.textBox.setPlainText(txt);
    dialog.setWindowTitle(
        //% "Activity Description"
        qtTrId("route.editor.activity.tools.title.activity.description.2"));
    dialog.exec();
    if(dialog.changed == 1){
        txt = dialog.textBox.toPlainText();
        ActivityEdit edit(a);
        txt.replace("\n","\\n");
        a->setBriefing(txt);
    }
}
    
ActivityTools::~ActivityTools() {
}

