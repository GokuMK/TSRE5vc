/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ActivityServiceWindow.h"
#include <routeEditor/activity/ActivityEditLock.h>
#include <tsre/trains/ActivityNotifier.h>
#include <routeEditor/activity/ActivityServiceProperties.h>
#include <tsre/Game.h>
#include <tsre/trains/Activity.h>
#include <tsre/world/Route.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/ActLib.h>
#include <routeEditor/properties/EditFileNameDialog.h>

ActivityServiceWindow::ActivityServiceWindow(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::WindowType::Tool);
    setWindowTitle(
        //% "Services"
        qtTrId("route.editor.activity.service.window.title.services"));
    
    serviceProperties = new ActivityServiceProperties(this);
    
    QVBoxLayout *actionListLayout = new QVBoxLayout;
    actionListLayout->setContentsMargins(0,0,0,0);
    actionListLayout->setSpacing(0);
    QPushButton *bNewActionEvent = new QPushButton(
        //% "New Service"
        qtTrId("route.editor.activity.service.window.button.b.new.action.event"));
    // Undo replaces activity data: let go of it before, show it after.
    QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::restoring,
                     this, &ActivityServiceWindow::activityDataRestoring);
    QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::restored,
                     this, &ActivityServiceWindow::activityDataRestored);
    QObject::connect(bNewActionEvent, SIGNAL(released()),
                      this, SLOT(bNewServiceSelected()));
    QPushButton *bDeleteActionEvent = new QPushButton(
        //% "Delete"
        qtTrId("route.editor.activity.service.window.button.b.delete.action.event"));
    QObject::connect(bDeleteActionEvent, SIGNAL(released()),
                      this, SLOT(bDeleteServiceSelected()));
    actionListLayout->addWidget(&serviceList);
    actionListLayout->addWidget(bNewActionEvent);
    actionListLayout->addWidget(bDeleteActionEvent);
    QStringList list;
    //% "Name:"
    list.append(qtTrId("activity.service.list.header.name"));
    //% "This:"
    list.append(qtTrId("activity.service.list.header.this"));
    //% "Any:"
    list.append(qtTrId("activity.service.list.header.any"));
    serviceList.setFixedWidth(250);
    serviceList.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    serviceList.setColumnCount(3);
    serviceList.setHeaderLabels(list);
    serviceList.setRootIsDecorated(false);
    serviceList.header()->resizeSection(0,170);    
    serviceList.header()->resizeSection(1,30);    
    serviceList.header()->resizeSection(2,30);    
    QHBoxLayout *v = new QHBoxLayout;
    v->setSpacing(2);
    v->setContentsMargins(1,1,1,1);
    v->addItem(actionListLayout);
    QWidget *editor = ActivityEditLock::Box(serviceProperties);
    v->addWidget(editor);
    this->setLayout(v);
    ActivityEditLock::Follow(this, {editor, bNewActionEvent, bDeleteActionEvent});
    
    QObject::connect(&serviceList, SIGNAL(itemClicked(QTreeWidgetItem*, int)),
                      this, SLOT(serviceListSelected(QTreeWidgetItem*, int)));
}

ActivityServiceWindow::~ActivityServiceWindow() {
}

void ActivityServiceWindow::showServices(Route* r){
    if(this->isHidden())
        return;
    route = r;
    serviceList.clear();
    if(route == NULL){
        activity = NULL;
        serviceProperties->showService(NULL);
        return;
    }
    serviceProperties->setPaths(route->path);
    activity = route->getCurrentActivity();

    QList<QTreeWidgetItem *> items;
    QStringList list;
    for(int i = 0; i < ActLib::jestservice; i++ ){
        if(ActLib::Services[i] == NULL)
            continue;
        //new QListWidgetItem ( route->service[i]->displayName, &serviceList, i );
        list.clear();
        list.append(ActLib::Services[i]->displayName);
        list.append("");
        list.append("");
        QTreeWidgetItem *item = new QTreeWidgetItem((QTreeWidget*)0, list, i );
        item->setCheckState(0, Qt::Unchecked);
        item->setCheckState(1, Qt::Unchecked);
        if(activity != NULL){
            if(activity->isPlayerServiceInUse(ActLib::Services[i]->nameId))
                item->setCheckState(0, Qt::Checked);
            if(activity->isServiceInUse(ActLib::Services[i]->nameId))
                item->setCheckState(1, Qt::Checked);
        }

        // Any: an activity of the route runs it.
        if(ActLib::IsServiceInUse(ActLib::Services[i]->nameId))
            item->setCheckState(2, Qt::Checked);
        else
            item->setCheckState(2, Qt::Unchecked);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        items.append(item);
    }
    serviceList.insertTopLevelItems(0, items);
}

void ActivityServiceWindow::selectService(QString nameId){
    for(int row = 0; row < serviceList.topLevelItemCount(); row++){
        QTreeWidgetItem *item = serviceList.topLevelItem(row);
        Service *service = ActLib::Services.value(item->type());
        if(service == NULL || service->nameId.compare(nameId, Qt::CaseInsensitive) != 0)
            continue;
        serviceList.setCurrentItem(item);
        serviceListSelected(item, 0);
        return;
    }
}

Service *ActivityServiceWindow::selectedService(){
    QTreeWidgetItem *item = serviceList.currentItem();
    if(item == NULL || item->type() < 0 || item->type() >= ActLib::jestservice)
        return NULL;
    return ActLib::Services[item->type()];
}

void ActivityServiceWindow::serviceListSelected(QTreeWidgetItem * item, int column){
    if(route == NULL || item == NULL)
        return;
    if(item->type() < 0 || item->type() >= ActLib::jestservice)
        return;
    serviceProperties->showService(ActLib::Services[item->type()]);
}

void ActivityServiceWindow::serviceNameChanged(int id){

}

void ActivityServiceWindow::bNewServiceSelected(){
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
            qtTrId("route.editor.activity.service.window.name.invalid"));
        return;
    }
    if(ActLib::ServiceExists(name)){
        QMessageBox::warning(this, windowTitle(),
            //% "A service named %1 exists already."
            qtTrId("route.editor.activity.service.window.name.exists").arg(name));
        return;
    }
    ActLib::AddService(Game::root + "/ROUTES/" + Game::route + "/SERVICES/", name+".srv", true);
    showServices(route);
    emit reloadServicesList();
}

// As a path is deleted: the activities and traffic naming the service are
// listed in the question; they keep its name.
void ActivityServiceWindow::bDeleteServiceSelected(){
    Service *service = selectedService();
    if(route == NULL || service == NULL)
        return;
    //% "Delete Service"
    const QString title = qtTrId("route.editor.activity.service.window.delete.title");
    if(!Game::writeEnabled){
        QMessageBox::warning(this, title,
            //% "The route is read-only."
            qtTrId("route.editor.activity.service.window.delete.read.only"));
        return;
    }
    QString question =
        //% "Delete the service %1 and its file? This cannot be undone."
        qtTrId("route.editor.activity.service.window.delete.question").arg(service->displayName);
    const QStringList users = ActLib::ServiceUsers(service);
    if(!users.isEmpty())
        question += "\n\n" +
            //% "These activities and traffic name it: %1"
            qtTrId("route.editor.activity.service.window.delete.users").arg(users.join(", "));
    if(QMessageBox::question(this, title, question) != QMessageBox::Yes)
        return;
    if(QFile::exists(service->pathid) && !QFile::remove(service->pathid)){
        QMessageBox::warning(this, title,
            //% "The file %1 could not be deleted."
            qtTrId("route.editor.activity.service.window.delete.failed").arg(service->pathid));
        return;
    }
    serviceProperties->showService(NULL);
    ActLib::RemoveService(service);
    showServices(route);
    emit reloadServicesList();
}
// Services are restored in place: the one shown is shown again.
void ActivityServiceWindow::activityDataRestoring(){
}

void ActivityServiceWindow::activityDataRestored(){
    if(route == NULL)
        return;
    Service *service = serviceProperties->shownService();
    showServices(route);
    if(service != NULL)
        serviceProperties->showService(service);
}
