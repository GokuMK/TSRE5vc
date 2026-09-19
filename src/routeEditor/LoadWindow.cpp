/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <routeEditor/LoadWindow.h>
#include <QtWidgets>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <tsre/Game.h>
#include <QDebug>
#include "NewRouteWindow.h"
#include <routeEditor/TrkWindow.h>
#include <settings/SettingsManager.h>
#include <tsre/fileFunctions/TarFile.h>
#include <tsre/world/RouteCreator.h>
#include <tsre/world/Trk.h>

LoadWindow::LoadWindow() {
    //this->setWindowFlags( Qt::CustomizeWindowHint );
    //% "%1 %2 Route Editor"
    setWindowTitle(qtTrId("route.editor.load.title")
                   .arg(Game::AppName, Game::AppVersion));
    this->setFixedSize(600, 700);
    QImage* myImage = new QImage();
    myImage->load(QString("appdata/")+Game::AppDataVersion+"/load.png");

    QLabel* myLabel = new QLabel("");
    myLabel->setContentsMargins(0,0,0,0);
    QLabel* myLabel2 = new QLabel(
        //% "Choose folder containing 'Global' and 'Routes': "
        qtTrId("route.editor.load.window.label.my.label2"));
    myLabel2->setContentsMargins(5,0,0,0);
    QLabel* myLabel3 = new QLabel(
        //% "Select route above or enter name for new route: "
        qtTrId("route.editor.load.window.label.my.label3"));
    myLabel3->setContentsMargins(5,0,0,0);
    
    myLabel->setPixmap(QPixmap::fromImage(*myImage));

    browse = new QPushButton(
        //% "Browse"
        qtTrId("route.editor.load.window.button.browse"));
    connect(browse, SIGNAL (released()), this, SLOT (handleBrowseButton()));
    load = new QPushButton(
        //% "Load"
        qtTrId("route.editor.load.window.button.load"));
    load->setStyleSheet(QString("background-color: ")+Game::StyleGreenButton);
    connect(load, SIGNAL (released()), this, SLOT (routeLoad()));
    neww = new QPushButton(
        //% "New"
        qtTrId("route.editor.load.window.button.neww"));
    neww->setStyleSheet(QString("background-color: ")+Game::StyleYellowButton);
    connect(neww, SIGNAL (released()), this, SLOT (setNewRoute()));
    exit = new QPushButton(
        //% "Exit"
        qtTrId("route.editor.load.window.button.exit"));
    exit->setStyleSheet(QString("background-color: ")+Game::StyleRedButton);
    
    
    nowaTrasa = new QLineEdit();
    QRegularExpression rx("^[a-zA-Z0-9\\_\\-\\ ]*$");
    qDebug() << "QRegularExpression" << rx.isValid();
    //QRegExp rx("[\\/<>|\":?*].");
    QRegularExpressionValidator* v = new QRegularExpressionValidator(rx);
    nowaTrasa->setValidator(v);
    
    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(myLabel);
    mainLayout->addWidget(myLabel2);
    mainLayout->addWidget(browse);
    QFormLayout *recentLayout = new QFormLayout;
    recentLayout->setContentsMargins(0,0,0,0);
    cRecent.setMaxVisibleItems(10);
    cRecent.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    cRecent.setStyleSheet("combobox-popup: 0;");
    QObject::connect(&cRecent, SIGNAL(textActivated(QString)),
                      this, SLOT(cRecentEnabled(QString)));
    recentLayout->addRow(
        //% "Recent: "
        qtTrId("route.editor.load.window.label.recent"), &cRecent);
    mainLayout->addItem(recentLayout);
    mainLayout->addWidget(&routeList);
    
    /*nowa = new QWidget();
    QHBoxLayout *vbox1 = new QHBoxLayout;
    vbox1->addWidget(myLabel3);
    vbox1->addWidget(nowaTrasa);
    vbox1->setContentsMargins(0,0,0,0);
    nowa->setLayout(vbox1);
    mainLayout->addWidget(nowa);*/
    
    QWidget* box = new QWidget();
    QHBoxLayout *vbox = new QHBoxLayout;
    vbox->addWidget(load);
    vbox->addWidget(neww);
    vbox->addWidget(exit);
    vbox->setContentsMargins(0,0,0,0);
    box->setLayout(vbox);
    mainLayout->addWidget(box);
    
    mainLayout->setAlignment(myLabel, Qt::AlignTop);
    mainLayout->setAlignment(myLabel2, Qt::AlignTop);
    mainLayout->setAlignment(browse, Qt::AlignTop);
    mainLayout->setAlignment(load, Qt::AlignTop);
    mainLayout->setAlignment(box, Qt::AlignBottom);
    //mainLayout->setAlignment(nowa, Qt::AlignBottom);
    mainLayout->setContentsMargins(1,1,1,1);
    //mainLayout->addWidget(naviBox);
    this->setLayout(mainLayout);
    
    
    //nowaTrasa->hide();

    QObject::connect(exit, SIGNAL (released()), this, SLOT (close()));
    QObject::connect(&routeList, SIGNAL(itemClicked(QListWidgetItem*)),
                      this, SLOT(setLoadRoute()));
    //QObject::connect(nowaTrasa, SIGNAL(textChanged(QString)),
    //                  this, SLOT(setNewRoute()));
    
    listRoots();
    
    if(Game::checkRoot(Game::root)){
        qDebug()<<"ok";
        load->show();
        neww->show();
        neww->setFixedWidth(100);
        exit->setFixedWidth(100);
        browse->setText(Game::root);
        browse->setStyleSheet(QString("color: ")+Game::StyleGreenText);
        this->listRoutes();
    } else {
        exit->setFixedWidth(600);
        load->hide();
        //nowa->hide();
        neww->hide();
    }
}

void LoadWindow::handleBrowseButton(QString directory){
    if(directory == ""){
        QFileDialog *fd = new QFileDialog;
        //QTreeView *tree = fd->findChild <QTreeView*>();
        //tree->setRootIsDecorated(true);
        //tree->setItemsExpandable(true);
        fd->setFileMode(QFileDialog::Directory);
        fd->setOption(QFileDialog::ShowDirsOnly);
        //fd->setViewMode(QFileDialog::Detail);
        int result = fd->exec();
        if (result)
        {
            directory = fd->selectedFiles()[0];
            qDebug()<<directory;
        }
    }
    //Game::root = directory;
    browse->setText(directory);
    browse->setStyleSheet(QString("color: ")+Game::StyleRedText);
    load->hide();
    //nowa->hide();
    neww->hide();
    exit->setFixedWidth(600);
    routeList.clear();
    if(Game::checkRoot(directory)){
        qDebug()<<"ok";
        load->show();
        neww->show();
        neww->setFixedWidth(100);
        exit->setFixedWidth(100);
        browse->setStyleSheet(QString("color: ")+Game::StyleGreenText);
        Game::root = directory;
        this->listRoutes();
        
        int i = 0;
        for(i = 0; i < cRecent.count(); i++){
            if(cRecent.itemText(i) == directory)
                break;
        }
        if(i == cRecent.count())
            cRecent.addItem(directory);
        
        QString path;
        path = "cerecent.txt";
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return;
        QTextStream in(&file);
        QString line;
        for(int i = 0; i < cRecent.count(); i++){
            in << cRecent.itemText(i) << "\n";
        }
        in.flush();
        file.close();
    }
}

void LoadWindow::routeLoad(){
    if(routeList.currentRow() < 0) return;
    Game::route = routeList.currentItem()->text();
    if (!Game::checkRoute(Game::route)) return;
    qDebug() << Game::route;
    this->hide();
    emit showMainWindow();
}

void LoadWindow::listRoutes(){
    QDir dir(Game::root+"/ROUTES");
    dir.setFilter(QDir::Dirs);
    
    foreach(QString dirFile, dir.entryList()){
        if(dirFile == "." || dirFile == "..")   
            continue;
        if(!Game::checkRoute(dirFile))  
            continue;
        this->routeList.addItem(dirFile);  
    }
} 

void LoadWindow::setLoadRoute(){
    //qDebug() << "load";
    this->load->setText(
        //% "Load"
        qtTrId("route.editor.load.window.text.load"));
}

void LoadWindow::cRecentEnabled(QString val){
    handleBrowseButton(val);
}

void LoadWindow::setNewRoute(){
    //qDebug() << "new";
    //this->load->setText("New");
    
    //Check if template route available.
    if (!Game::checkRoot(Game::root)) return;

    const QString path = "./assets/templateRoute_0.6";
    QString templateError;
    if (!RouteCreator::templateResourcesAvailable(&templateError)) {
        downloadTemplateRoute(path);
        if (!RouteCreator::templateResourcesAvailable(&templateError)) {
            QMessageBox::critical(
                this,
                tr("Cannot create route"),
                tr("The route template is unavailable. %1").arg(templateError));
            return;
        }
    }
    
    NewRouteWindow newWindow;
    if (newWindow.exec() == QDialog::Accepted) {
        const NewRouteSelection &selection = newWindow.selection();
        const QString routeDirectory = selection.name.toUpper();
        std::unique_ptr<Trk> routeTemplate =
                Trk::createNewRouteTemplate(routeDirectory);
        routeTemplate->startTileX = selection.routeTileX;
        routeTemplate->startTileZ = selection.routeTileZ;
        routeTemplate->geoProjectionType = selection.projectionType;
        if (selection.projectionType
                == GeoProjectionType::InterruptedGoodeHomolosine) {
            routeTemplate->geoProjection.reset();
        } else {
            routeTemplate->geoProjection = selection.projection;
        }

        TrkWindow trkWindow(TrkWindow::Mode::NewRouteTemplate);
        trkWindow.trk = routeTemplate.get();
        trkWindow.exec(); // Skip/close intentionally keeps the prepared values.

        QString settingsError;
        if (!SettingsManager::instance().setSessionValue(
                    "core.route.saving.enabled", true, &settingsError)) {
            QMessageBox::critical(
                this, tr("Cannot create route"),
                tr("Route writing could not be enabled. %1").arg(settingsError));
            return;
        }
        Game::writeEnabled = true;
        Game::writeTDBSessionAllowed = true;

        QString creationError;
        if (!RouteCreator::create(routeDirectory, std::move(routeTemplate),
                                  &creationError)) {
            QMessageBox::critical(
                this, tr("Cannot create route"), creationError);
            return;
        }
        if (!Game::checkRoute(Game::route)) {
            QMessageBox::critical(
                this, tr("Cannot create route"),
                tr("The route was created but its TRK file could not be found."));
            return;
        }
        qDebug() << "Created route" << Game::route;
        hide();
        emit showMainWindow();
    }
}

void LoadWindow::exitNow(){
    this->hide();
}

void LoadWindow::downloadTemplateRoute(QString path){
    QDir().mkdir(path);
    
    // Download and extract Route Data
    QNetworkAccessManager* mgr = new QNetworkAccessManager();
    qDebug() << "Wait ..";
    QString Url = "http://koniec.org/tsre5/data/appdata/templateRoute_0.6.tar";
    qDebug() << Url;
    QNetworkRequest req;
    req.setUrl(QUrl(Url));
    qDebug() << req.url();
    QNetworkReply* r = mgr->get(req);
    QEventLoop loop;
    QObject::connect(r, SIGNAL(finished()), &loop, SLOT(quit()));
    loop.exec();
    
    qDebug() << "Network Reply Loop End";
    QByteArray data = r->readAll();
    FileBuffer *fileData = new FileBuffer((unsigned char*)data.data(), data.length());
    TarFile tarFile(fileData);
    tarFile.extractTo("./assets/");

}

void LoadWindow::listRoots(){
    QString sh;
    QString path;
    path = "cerecent.txt";
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    qDebug() << path;

    QTextStream in(&file);
    QString line;
    while (!in.atEnd()) {
        line = in.readLine();
        cRecent.addItem(line);
    }
    file.close();
}
