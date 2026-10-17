/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ActivityTimetableProperties.h"
#include <routeEditor/activity/ActivityEdit.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityTimetable.h>
#include <tsre/Game.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/world/Route.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/Consist.h>
#include <tsre/sim/TimetableCalculation.h>
#include <algorithm>

namespace {

enum Column { StationColumn, ArriveColumn, DepartColumn, ChangeColumn, PerformanceColumn, Columns };

// A change of a time ("+1:30", "−0:30", "+1:02:00"), the shorter way round
// midnight; nothing for none.
QString change(int now, int before){
    int d = ((now - before) % 86400 + 86400) % 86400;
    if(d > 43200)
        d -= 86400;
    if(d == 0)
        return QString();
    const int a = std::abs(d);
    const QString sign = d > 0 ? "+" : QString(QChar(0x2212));
    if(a >= 3600)
        return sign + QString("%1:%2:%3").arg(a / 3600).arg(a / 60 % 60, 2, 10, QChar('0')).arg(a % 60, 2, 10, QChar('0'));
    return sign + QString("%1:%2").arg(a / 60).arg(a % 60, 2, 10, QChar('0'));
}

}

ActivityTimetableProperties::ActivityTimetableProperties(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(450);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
        
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);

    QGridLayout *vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    
    int row = 0;
    QLabel *label = NULL;
    
    QStringList list;
    //% "Station:"
    list.append(qtTrId("activity.timetable.header.station"));
    //% "Arrive:"
    list.append(qtTrId("activity.timetable.header.arrive"));
    //% "Depart:"
    list.append(qtTrId("activity.timetable.header.depart"));
    //% "Change:"
    list.append(qtTrId("activity.timetable.header.change"));
    //% "Performance:"
    list.append(qtTrId("activity.timetable.header.performance"));
    //lTimetable.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    //lTimetable.setColumnCount(4);
    //lTimetable.setHeaderLabels(list);
    //lTimetable.setRootIsDecorated(false);
    //lTimetable.header()->resizeSection(0,200);    
    //lTimetable.header()->resizeSection(1,100);    
    //tableWidget->setRowCount(10);
    lTimetable.setColumnCount(Columns);
    lTimetable.setHorizontalHeaderLabels(list);
    lTimetable.horizontalHeaderItem(ChangeColumn)->setToolTip(
        //% "What the last Simulate changed: arrival, and departure where it differs; new times; stops not reached."
        qtTrId("route.editor.activity.timetable.properties.change.tooltip"));
    lTimetable.horizontalHeaderItem(PerformanceColumn)->setToolTip(
        //% "The train's performance on the way to this stop, from the stop before (from its start for the first): it runs at most at this share of its consist's top speed."
        qtTrId("route.editor.activity.timetable.properties.performance.tooltip"));
    lTimetable.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lTimetable.verticalHeader()->hide();
    lTimetable.verticalHeader()->setDefaultSectionSize(25);
    lTimetable.horizontalHeader()->resizeSection(StationColumn,220);
    lTimetable.horizontalHeader()->resizeSection(ArriveColumn,60);
    lTimetable.horizontalHeader()->resizeSection(DepartColumn,60);
    lTimetable.horizontalHeader()->resizeSection(ChangeColumn,90);
    lTimetable.horizontalHeader()->resizeSection(PerformanceColumn,80);
    lTimetable.horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    // The station's name takes the width the times leave.
    lTimetable.horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    vlist->addWidget(&lTimetable, row++, 0, 1, 2);
    lTimetable.setObjectName("timetableStops");
    vbox->addItem(vlist);

    info = new QWidget(this);
    QGridLayout *infoGrid = new QGridLayout(info);
    infoGrid->setSpacing(2);
    infoGrid->setContentsMargins(0, 0, 0, 0);
    row = 0;
    infoGrid->addWidget(new QLabel(
        //% "Start Time:"
        qtTrId("route.editor.activity.timetable.properties.label.start.time")), row, 0);
    infoGrid->addWidget(&eTime, row++, 1);
    eTime.setDisplayFormat("HH:mm:ss");
    eTime.setDisabled(true);
    infoGrid->addWidget(new QLabel(
        //% "Consist main ENG:"
        qtTrId("route.editor.activity.timetable.properties.label.consist.main.eng")), row, 0);
    infoGrid->addWidget(&eMainEng, row++, 1);
    eMainEng.setDisabled(true);
    infoGrid->addWidget(new QLabel(
        //% "Max ENG Speed:"
        qtTrId("route.editor.activity.timetable.properties.label.max.eng.speed")), row, 0);
    infoGrid->addWidget(&eMaxSpeed, row++, 1);
    eMaxSpeed.setDisabled(true);
    infoGrid->addWidget(new QLabel(
        //% "Timetable Avg Speed:"
        qtTrId("route.editor.activity.timetable.properties.label.timetable.avg.speed")), row, 0);
    infoGrid->addWidget(&eAvgSpeed, row++, 1);
    eAvgSpeed.setDisabled(true);
    // Until the window takes it: under the stops.
    vbox->addWidget(info);

    //vbox->addStretch(1);
    this->setLayout(vbox);
    
    QObject::connect(&lTimetable, SIGNAL(cellChanged(int, int)),
                      this, SLOT(lTimetableSelected(int, int)));
    // No timetable shown yet.
    setEnabled(false);
}

ActivityTimetableProperties::~ActivityTimetableProperties() {
}

void ActivityTimetableProperties::showTimetable(ActivityServiceDefinition* s, Activity *owner){
    service = s;
    activity = s != NULL ? owner : NULL;
    ActivityTimetable *t = service != NULL ? service->trafficDefinition : NULL;
    if(t == NULL){
        service = NULL;
        QSignalBlocker blocker(lTimetable);
        lTimetable.clearContents();
        lTimetable.setRowCount(0);
        eMainEng.clear();
        eMaxSpeed.clear();
        eAvgSpeed.clear();
        eTime.setTime(QTime(0, 0));
        setEnabled(false);
        info->setEnabled(false);
        return;
    }
    setEnabled(true);
    info->setEnabled(true);

    QTableWidgetItem *newItem;
    QTime time;
    // Without the track database the stops show their item ids.
    TDB *tdb = Game::trackDB;
    
    lTimetable.blockSignals(true);
    lTimetable.clearContents();
    lTimetable.setRowCount(0);
    QString name = "";
    // The stops' arrays may differ in length (a file missing a field, an
    // instance changed to another service): rows only for complete stops.
    const int stops = std::min({t->platformStartID.size(), t->arrivalTime.size(), t->departTime.size()});
    // What the last Simulate wrote for the train, shown while the times are
    // as it wrote them.
    const bool player = activity != NULL && s == activity->playerServiceDefinition;
    const Sim::TimetableTrain *run = activity != NULL ? Sim::TimetableCalculation::LastTrain(activity, player, s->name, s->time) : NULL;
    if(run != NULL && !run->written)
        run = NULL;
    for(int i = 0; i < stops; i++){
        TRitem *trit = NULL;
        if(tdb != NULL){
            const auto found = tdb->trackItems.find(t->platformStartID[i]);
            if(found != tdb->trackItems.end())
                trit = found->second;
        }
        if(trit != NULL){
            name = trit->stationName;
        } else {
            name = "Platform ID: "+QString::number(t->platformStartID[i]);
        }
        newItem = new QTableWidgetItem(name);
        lTimetable.insertRow(lTimetable.rowCount());
        lTimetable.setItem(i, 0, newItem);
        time = QTime::fromMSecsSinceStartOfDay(t->arrivalTime[i]*1000);
        newItem = new QTableWidgetItem(time.toString("HH:mm:ss"));
        lTimetable.setItem(i, 1, newItem);
        time = QTime::fromMSecsSinceStartOfDay(t->departTime[i]*1000);
        newItem = new QTableWidgetItem(time.toString("HH:mm:ss"));
        lTimetable.setItem(i, 2, newItem);
        QTableWidgetItem *depart = newItem;
        QTableWidgetItem *changed = new QTableWidgetItem();
        changed->setFlags(changed->flags() & ~Qt::ItemIsEditable);
        lTimetable.setItem(i, ChangeColumn, changed);
        newItem = new QTableWidgetItem(i < s->efficiency.size() ? QString::number(s->efficiency[i]) : QString());
        lTimetable.setItem(i, PerformanceColumn, newItem);
        if(run == NULL || i >= int(run->stops.size()))
            continue;
        const Sim::TimetableStop &stop = run->stops[size_t(i)];
        if(!stop.simulated){
            changed->setText(QString(QChar(0x2013)));
            changed->setToolTip(
                //% "Its platform is not on the train's path: not simulated."
                qtTrId("route.editor.activity.timetable.properties.not.simulated"));
            continue;
        }
        if(!stop.reached){
            changed->setText(
                //% "not reached"
                qtTrId("route.editor.activity.timetable.properties.not.reached"));
            changed->setToolTip(run->state);
            for(int column = 0; column < Columns; column++)
                if(lTimetable.item(i, column) != NULL)
                    lTimetable.item(i, column)->setForeground(QBrush(QColor(140, 140, 140)));
            continue;
        }
        const bool departSet = stop.departure >= 0 && t->departTime[i] != stop.departure;
        if(t->arrivalTime[i] == stop.arrival && !departSet){
            if(stop.arrivalBefore < 0){
                changed->setText(
                    //% "new"
                    qtTrId("route.editor.activity.timetable.properties.new"));
            } else {
                const QString arrival = change(stop.arrival, stop.arrivalBefore);
                const QString departure = stop.departure >= 0 && stop.departureBefore >= 0
                        ? change(stop.departure, stop.departureBefore) : arrival;
                changed->setText(arrival == departure ? arrival
                    : QString("%1 / %2").arg(arrival.isEmpty() ? QString("0:00") : arrival,
                                             departure.isEmpty() ? QString("0:00") : departure));
            }
        }
        // The user's departures in bold: kept by the run, or set since.
        if(departSet || stop.kept){
            QFont font = depart->font();
            font.setBold(true);
            depart->setFont(font);
            depart->setToolTip(departSet ?
                //% "Set by you: the next Simulate keeps it (the train leaves no earlier)."
                qtTrId("route.editor.activity.timetable.properties.depart.set") :
                //% "Your departure, kept: the train left no earlier."
                qtTrId("route.editor.activity.timetable.properties.depart.kept"));
        }
    }
    lTimetable.blockSignals(false);
    
    Service *srv = ActLib::GetServiceByName(service->name, service->routePath);
    if(srv == NULL)
        return;
    if(srv->trainConfig.isEmpty())
        return;
    Consist *con = ConLib::con[ConLib::addCon(Game::root+"/TRAINS/CONSISTS/", srv->trainConfig+".con")];
    if(con == NULL || con->engItems.isEmpty())
        return;
    eMainEng.setText(con->engItems[0].ename);
    eMaxSpeed.setText(QString::number((int)(con->maxVelocity[0]*3.6)) + " km/h");
    eTime.setTime(QTime::fromMSecsSinceStartOfDay((t->time*1000)));
    if(stops > 1 && !t->distanceDownPath.isEmpty() && t->arrivalTime.last() > t->time)
        eAvgSpeed.setText(QString::number(3.6*(t->distanceDownPath.last()/(t->arrivalTime.last()-t->time))) + " km/h");
    else
        eAvgSpeed.setText(
            //% "NONE"
            qtTrId("route.editor.activity.timetable.properties.text.none"));
}

void ActivityTimetableProperties::lTimetableSelected(int row, int column){
    if(service == NULL)
        return;
    ActivityTimetable *t = service->trafficDefinition;
    
    if(column < ArriveColumn || column == ChangeColumn || t == NULL)
        return;
    ActivityEdit edit(activity);
    edit.alsoTrafficOf(activity);
    QTime time;
    if(column == ArriveColumn){
        time = QTime::fromString(lTimetable.item(row, column)->text(), "HH:mm:ss");
        t->setArrival(row, time.msecsSinceStartOfDay()/1000);
    }
    if(column == DepartColumn){
        time = QTime::fromString(lTimetable.item(row, column)->text(), "HH:mm:ss");
        t->setDepart(row, time.msecsSinceStartOfDay()/1000);
    }
    if(column == PerformanceColumn){
        service->setTimetableEfficiency(row, lTimetable.item(row, column)->text().toFloat());
    }
    emit edited();
    // Shown again once the edit has ended: a departure set since the last
    // Simulate in bold, its change gone.
    ActivityServiceDefinition *shown = service;
    QTimer::singleShot(0, this, [this, shown]{
        if(service == shown)
            showTimetable(service, activity);
    });
}