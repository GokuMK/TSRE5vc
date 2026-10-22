/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ActivityTrafficWindow.h"
#include <routeEditor/activity/ActivityEditLock.h>
#include <tsre/trains/ActivityNotifier.h>
#include "ActivityTrafficProperties.h"
#include <tsre/world/Route.h>
#include <tsre/trains/Traffic.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActLib.h>
#include <tsre/Game.h>
#include <routeEditor/properties/EditFileNameDialog.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/trains/OrTimetable.h>
#include <QSet>
#include <QTextBrowser>

ActivityTrafficWindow::ActivityTrafficWindow(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::WindowType::Tool);
    setWindowTitle(
        //% "Traffic"
        qtTrId("route.editor.activity.traffic.window.title.traffic"));
    
    trafficProperties = new ActivityTrafficProperties(this);
    
    QVBoxLayout *actionListLayout = new QVBoxLayout;
    actionListLayout->setContentsMargins(0,0,0,0);
    actionListLayout->setSpacing(0);
    QPushButton *bNew = new QPushButton(
        //% "New Traffic"
        qtTrId("route.editor.activity.traffic.window.button.b.new"));
    // Undo replaces activity data: let go of it before, show it after.
    QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::restoring,
                     this, &ActivityTrafficWindow::activityDataRestoring);
    QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::restored,
                     this, &ActivityTrafficWindow::activityDataRestored);
    QObject::connect(bNew, SIGNAL(released()),
                      this, SLOT(bNewTrafficSelected()));
    QPushButton *bDelete = new QPushButton(
        //% "Delete"
        qtTrId("route.editor.activity.traffic.window.button.b.delete"));
    QObject::connect(bDelete, SIGNAL(released()),
                      this, SLOT(bDeleteTrafficSelected()));
    QStringList list;
    //% "Name:"
    list.append(qtTrId("activity.traffic.list.header.name"));
    //% "This:"
    list.append(qtTrId("activity.traffic.list.header.this"));
    //% "Any:"
    list.append(qtTrId("activity.traffic.list.header.any"));
    lTraffic.setObjectName("trafficList");
    lTraffic.setFixedWidth(250);
    lTraffic.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lTraffic.setColumnCount(3);
    lTraffic.setHeaderLabels(list);
    lTraffic.setRootIsDecorated(false);
    lTraffic.header()->resizeSection(0,170);    
    lTraffic.header()->resizeSection(1,30);    
    lTraffic.header()->resizeSection(2,30);    
    actionListLayout->addWidget(&lTraffic);
    actionListLayout->addWidget(bNew);
    actionListLayout->addWidget(bDelete);
    QHBoxLayout *v = new QHBoxLayout;
    v->setSpacing(2);
    v->setContentsMargins(1,1,1,1);
    v->addItem(actionListLayout);
    QWidget *editor = ActivityEditLock::Box(trafficProperties);
    v->addWidget(editor);
    // Open Rails timetables (task simulation 08): what the chosen one holds
    // and its problems, in place of the traffic pattern's editor.
    timetableInfo = new QTextBrowser;
    timetableInfo->setObjectName("trafficTimetableInfo");
    timetableInfo->setMinimumWidth(380);
    timetableInfo->hide();
    v->addWidget(timetableInfo, 1);
    this->setLayout(v);
    ActivityEditLock::Follow(this, {editor, bNew, bDelete});
    trafficEditor = editor;
    newButton = bNew;
    deleteButton = bDelete;
    QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::kindChanged, this, [this] {
        showTraffic(route);
    });
    
    QObject::connect(&lTraffic, SIGNAL(itemClicked(QTreeWidgetItem*, int)),
                      this, SLOT(lTrafficSelected(QTreeWidgetItem*, int)));
}

ActivityTrafficWindow::~ActivityTrafficWindow() {
}

void ActivityTrafficWindow::showTraffic(Route* r){
    if(this->isHidden())
        return;
    route = r;
    lTraffic.clear();
    if(route == NULL){
        trafficProperties->showTraffic(NULL);
        return;
    }
    const bool timetables = ActivityNotifier::timetables();
    trafficEditor->setVisible(!timetables);
    newButton->setVisible(!timetables);
    deleteButton->setVisible(!timetables);
    timetableInfo->setVisible(timetables);
    lTraffic.setHeaderLabels(timetables ? QStringList{
        //% "Timetable:"
        qtTrId("activity.traffic.list.header.timetable"),
        //% "Trains:"
        qtTrId("activity.traffic.list.header.trains"),
        //% "Problems:"
        qtTrId("activity.traffic.list.header.problems")} : QStringList{
        qtTrId("activity.traffic.list.header.name"), qtTrId("activity.traffic.list.header.this"),
        qtTrId("activity.traffic.list.header.any")});
    if(timetables){
        showTimetables();
        return;
    }
    Activity *activity = route->getCurrentActivity();
    //serviceProperties->setPaths(route->path);
    //activity = r->s
    
    lTraffic.clear();
    QList<QTreeWidgetItem *> items;
    QStringList list;
    for(int i = 0; i < ActLib::jesttraffic; i++ ){
        if(ActLib::Traffics[i] == NULL)
            continue;
        //new QListWidgetItem ( route->service[i]->displayName, &serviceList, i );
        list.clear();
        list.append(ActLib::Traffics[i]->nameId);
        list.append("");
        list.append("");
        QTreeWidgetItem *item = new QTreeWidgetItem((QTreeWidget*)0, list, i );
        //if(i == 0){
        //    item->setCheckState(1, Qt::Checked);
        //}else{
        item->setCheckState(1, Qt::Unchecked);
        if(activity != NULL){
            if(activity->isTrafficInUse(ActLib::Traffics[i]->nameId))
                item->setCheckState(1, Qt::Checked);
        }
        // Any: an activity of the route names it.
        if(!ActLib::TrafficUsers(ActLib::Traffics[i]).isEmpty())
            item->setCheckState(2, Qt::Checked);
        else
            item->setCheckState(2, Qt::Unchecked);
        //}
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        items.append(item);
    }
    lTraffic.insertTopLevelItems(0, items);
}

// The route's Open Rails timetables (each file once: groups share them),
// with their trains and problems.
void ActivityTrafficWindow::showTimetables(){
    shownTimetables.clear();
    timetableInfo->clear();
    QSet<QString> listed;
    for(int id : route->timetableSetId){
        Activity *set = ActLib::TimetableSets.value(id);
        if(set == NULL)
            continue;
        for(const std::shared_ptr<OrTimetable> &timetable : set->timetables){
            if(listed.contains(timetable->file))
                continue;
            listed.insert(timetable->file);
            const QVector<OrTimetableProblem> problems = CheckTimetable(*timetable, ActivityTrains::RouteDirectory(set),
                                                                        Game::root + "/TRAINS/CONSISTS", Game::trackDB);
            QTreeWidgetItem *item = new QTreeWidgetItem((QTreeWidget*)0, {
                timetable->description.isEmpty() ? QFileInfo(timetable->file).fileName() : timetable->description,
                QString::number(timetable->trains.size()), QString::number(problems.size())}, int(shownTimetables.size()));
            item->setToolTip(0, timetable->file);
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            lTraffic.addTopLevelItem(item);
            shownTimetables.push_back({timetable, set});
        }
    }
}

void ActivityTrafficWindow::showTimetableInfo(int index){
    if(index < 0 || index >= int(shownTimetables.size()))
        return;
    const std::shared_ptr<OrTimetable> &timetable = shownTimetables[size_t(index)].first;
    int statics = 0, stops = 0;
    for(const OrTimetable::Train &train : timetable->trains){
        statics += train.isStatic ? 1 : 0;
        stops += train.stops.size();
    }
    QString html = QString("<b>%1</b><br>%2<br><br>").arg(timetable->description.toHtmlEscaped(),
                                                           timetable->file.toHtmlEscaped());
    //% "%1 trains (%2 static), %3 stations, %4 stops."
    html += qtTrId("route.editor.activity.traffic.window.timetable.summary").arg(timetable->trains.size())
            .arg(statics).arg(timetable->stations.size()).arg(stops);
    if(timetable->modified)
        //% "Changed, not saved."
        html += "<br>" + qtTrId("route.editor.activity.traffic.window.timetable.changed");
    const QVector<OrTimetableProblem> problems = CheckTimetable(*timetable, ActivityTrains::RouteDirectory(
        shownTimetables[size_t(index)].second), Game::root + "/TRAINS/CONSISTS", Game::trackDB);
    if(!problems.isEmpty()){
        //% "Problems (Check Activity lists them too):"
        html += "<br><br><b>" + qtTrId("route.editor.activity.traffic.window.timetable.problems") + "</b><ul>";
        for(const OrTimetableProblem &problem : problems)
            html += "<li>" + (problem.train.isEmpty() ? problem.text : problem.train + ": " + problem.text).toHtmlEscaped()
                    + "</li>";
        html += "</ul>";
    }
    timetableInfo->setHtml(html);
}

void ActivityTrafficWindow::lTrafficSelected(QTreeWidgetItem *item, int column){
    if(route == NULL || item == NULL)
        return;
    if(ActivityNotifier::timetables()){
        showTimetableInfo(item->type());
        return;
    }
    if(item->type() < 0 || item->type() >= ActLib::jesttraffic)
        return;
    trafficProperties->showTraffic(ActLib::Traffics[item->type()]);
}

void ActivityTrafficWindow::bNewTrafficSelected(){
    if(route == NULL)
        return;
    EditFileNameDialog eWindow;
    eWindow.exec();
    const QString name = eWindow.name.text().trimmed();
    if(!eWindow.isOk || name.isEmpty())
        return;
    if(name.contains(QRegularExpression("[\\\\/:*?\"<>|]"))){
        QMessageBox::warning(this, windowTitle(),
            //% "A file name cannot be empty or contain \\ / : * ? \" < > |."
            qtTrId("route.editor.activity.traffic.window.name.invalid"));
        return;
    }
    if(ActLib::TrafficExists(name)){
        QMessageBox::warning(this, windowTitle(),
            //% "A traffic pattern named %1 exists already."
            qtTrId("route.editor.activity.traffic.window.name.exists").arg(name));
        return;
    }
    ActLib::AddTraffic(Game::root + "/ROUTES/" + Game::route + "/TRAFFIC/", name+".trf", true);
    showTraffic(route);
    emit reloadTrafficsList();
}

void ActivityTrafficWindow::selectTraffic(QString nameId){
    for(int row = 0; row < lTraffic.topLevelItemCount(); row++){
        QTreeWidgetItem *item = lTraffic.topLevelItem(row);
        Traffic *traffic = ActLib::Traffics.value(item->type());
        if(traffic == NULL || traffic->nameId.compare(nameId, Qt::CaseInsensitive) != 0)
            continue;
        lTraffic.setCurrentItem(item);
        lTrafficSelected(item, 0);
        return;
    }
}

// As a path is deleted: the activities naming the traffic are listed in
// the question; they keep its name.
void ActivityTrafficWindow::bDeleteTrafficSelected(){
    QTreeWidgetItem *item = lTraffic.currentItem();
    if(route == NULL || item == NULL || item->type() < 0 || item->type() >= ActLib::jesttraffic)
        return;
    Traffic *traffic = ActLib::Traffics[item->type()];
    if(traffic == NULL)
        return;
    //% "Delete Traffic"
    const QString title = qtTrId("route.editor.activity.traffic.window.delete.title");
    if(!Game::writeEnabled){
        QMessageBox::warning(this, title,
            //% "The route is read-only."
            qtTrId("route.editor.activity.traffic.window.delete.read.only"));
        return;
    }
    QString question =
        //% "Delete the traffic pattern %1 and its file? This cannot be undone."
        qtTrId("route.editor.activity.traffic.window.delete.question").arg(traffic->nameId);
    const QStringList users = ActLib::TrafficUsers(traffic);
    if(!users.isEmpty())
        question += "\n\n" +
            //% "These activities name it: %1"
            qtTrId("route.editor.activity.traffic.window.delete.users").arg(users.join(", "));
    if(QMessageBox::question(this, title, question) != QMessageBox::Yes)
        return;
    if(QFile::exists(traffic->pathid) && !QFile::remove(traffic->pathid)){
        QMessageBox::warning(this, title,
            //% "The file %1 could not be deleted."
            qtTrId("route.editor.activity.traffic.window.delete.failed").arg(traffic->pathid));
        return;
    }
    trafficProperties->showTraffic(NULL);
    ActLib::RemoveTraffic(traffic);
    showTraffic(route);
    emit reloadTrafficsList();
}
// Traffic is restored in place, its instances anew: shown again.
void ActivityTrafficWindow::activityDataRestoring(){
}

void ActivityTrafficWindow::activityDataRestored(){
    Traffic *traffic = trafficProperties->shownTraffic();
    if(traffic != NULL)
        trafficProperties->showTraffic(traffic);
}
