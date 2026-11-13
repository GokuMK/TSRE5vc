/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <routeEditor/AboutWindow.h>
#include <QtWidgets>
#include <QDebug>
#include <tsre/Game.h>

AboutWindow::AboutWindow(QWidget* parent) : QWidget(parent) {
    this->setWindowFlags( Qt::CustomizeWindowHint );
    this->setWindowFlags(Qt::WindowType::Tool);
    this->setFixedSize(600, 345);
    QImage* myImage = new QImage();
    myImage->load(QString("appdata/")+Game::AppDataVersion+"/load.png");

    QLabel* myLabel = new QLabel("");
    myLabel->setContentsMargins(0,0,0,0);   
    
    QLabel* myLabel2 = new QLabel(
        //% "<b>User guide</b> <a href=\"http://koniec.org/tsre5/\"><b>http://koniec.org/tsre5.</b></a>"
        qtTrId("route.editor.about.window.label.my.label2"));
    myLabel2->setOpenExternalLinks(true);
    myLabel2->setContentsMargins(5,0,0,0);
    QLabel* myLabel21 = new QLabel(
        //% "<b>Official Forum</b> <a href=\"http://www.onrails.eu\"><b>http://www.onrails.eu.</b></a>"
        qtTrId("route.editor.about.window.label.my.label21"));
    myLabel21->setOpenExternalLinks(true);
    myLabel21->setContentsMargins(5,0,0,0);
    
    QLabel* myLabel3 = new QLabel(
        //% "Copyright © GokuMK, mail pgadecki@gmail.com "
        qtTrId("route.editor.about.window.label.my.label3"));
    myLabel3->setContentsMargins(5,0,0,0);
    QLabel* myLabel4 = new QLabel(
        //% "Map data from OpenStreetMap © <a href=\"https://www.openstreetmap.org/copyright\">OpenStreetMap contributors</a>, available under the ODbL."
        qtTrId("route.editor.about.window.label.osm"));
    myLabel4->setOpenExternalLinks(true);
    myLabel4->setContentsMargins(5,0,0,0);
    QLabel* myLabel5 = new QLabel(
        //% "Polygon triangulation: earcut.hpp © 2015 Mapbox, <a href=\"earcut\">ISC licence</a>."
        qtTrId("route.editor.about.window.label.earcut"));
    myLabel5->setContentsMargins(5,0,0,0);
    // The licence text ships with the program (resource from thirdparty/earcut/LICENSE).
    connect(myLabel5, &QLabel::linkActivated, this, [this](const QString &) {
        QFile licence(":/licences/earcut/LICENSE");
        if (licence.open(QIODevice::ReadOnly))
            QMessageBox::information(this, "earcut.hpp", QString::fromUtf8(licence.readAll()));
    });
    QLabel* myLabel6 = new QLabel(
        //% "Scripting: Lua © 1994-2026 Lua.org, PUC-Rio, <a href=\"lua\">MIT licence</a>."
        qtTrId("route.editor.about.window.label.lua"));
    myLabel6->setContentsMargins(5,0,0,0);
    // The licence text ships with the program (resource from thirdparty/lua/LICENSE).
    connect(myLabel6, &QLabel::linkActivated, this, [this](const QString &) {
        QFile licence(":/licences/lua/LICENSE");
        if (licence.open(QIODevice::ReadOnly))
            QMessageBox::information(this, "Lua", QString::fromUtf8(licence.readAll()));
    });
    QLabel* myLabel8 = new QLabel(
        //% "Skeletal animation: ozz-animation © 2020 Guillaume Blanc, <a href=\"ozz\">MIT licence</a>."
        qtTrId("route.editor.about.window.label.ozz"));
    myLabel8->setContentsMargins(5,0,0,0);
    // The licence text ships with the program (resource from thirdparty/ozz-animation/LICENSE.md).
    connect(myLabel8, &QLabel::linkActivated, this, [this](const QString &) {
        QFile licence(":/licences/ozz-animation/LICENSE.md");
        if (licence.open(QIODevice::ReadOnly))
            QMessageBox::information(this, "ozz-animation", QString::fromUtf8(licence.readAll()));
    });
    QLabel* myLabel9 = new QLabel(
        //% "Walk areas: recastnavigation © 2009 Mikko Mononen, <a href=\"recast\">zlib licence</a>."
        qtTrId("route.editor.about.window.label.recast"));
    myLabel9->setContentsMargins(5,0,0,0);
    // The licence text ships with the program (resource from thirdparty/recastnavigation/License.txt).
    connect(myLabel9, &QLabel::linkActivated, this, [this](const QString &) {
        QFile licence(":/licences/recastnavigation/License.txt");
        if (licence.open(QIODevice::ReadOnly))
            QMessageBox::information(this, "recastnavigation", QString::fromUtf8(licence.readAll()));
    });
    QLabel* myLabel7 = new QLabel(
        //% "Game UI: RmlUi © The RmlUi Team, <a href=\"rmlui\">MIT licence</a>."
        qtTrId("route.editor.about.window.label.rmlui"));
    myLabel7->setContentsMargins(5,0,0,0);
    // RmlUi's licence, with its containers' and its debugger font's below
    // (resources from thirdparty/rmlui).
    connect(myLabel7, &QLabel::linkActivated, this, [this](const QString &) {
        auto read = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        };
        QMessageBox box(QMessageBox::Information, "RmlUi", read(":/licences/rmlui/LICENSE.txt"), QMessageBox::Ok, this);
        box.setDetailedText(read(":/licences/rmlui/Include/RmlUi/Core/Containers/LICENSE.txt") + "\n\n"
                            + read(":/licences/rmlui/Source/Debugger/LICENSE.txt"));
        box.exec();
    });
    QLabel* myLabel10 = new QLabel(
        //% "People's animations: <a href=\"https://www.ianxmason.com/100style/\">The 100STYLE Dataset</a> © Ian Mason, <a href=\"https://creativecommons.org/licenses/by/4.0/\">CC BY 4.0</a>, cut into loops and retargeted."
        qtTrId("route.editor.about.window.label.style100"));
    myLabel10->setOpenExternalLinks(true);
    myLabel10->setContentsMargins(5,0,0,0);
  
    
    myLabel->setPixmap(QPixmap::fromImage(*myImage));

    QPushButton* browse = new QPushButton(
        //% "Close"
        qtTrId("route.editor.about.window.button.browse"));
    connect(browse, SIGNAL (released()), this, SLOT (close()));

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(myLabel);
    mainLayout->addWidget(myLabel2);
    mainLayout->addWidget(myLabel21); 
    mainLayout->addWidget(myLabel3);
    mainLayout->addWidget(myLabel4);
    mainLayout->addWidget(myLabel5);
    mainLayout->addWidget(myLabel6);
    mainLayout->addWidget(myLabel7);
    mainLayout->addWidget(myLabel8);
    mainLayout->addWidget(myLabel9);
    mainLayout->addWidget(myLabel10);
    mainLayout->addWidget(browse);
    
    mainLayout->setAlignment(myLabel, Qt::AlignTop);
    mainLayout->setAlignment(myLabel2, Qt::AlignTop);
    mainLayout->setAlignment(myLabel3, Qt::AlignTop);
    mainLayout->setAlignment(browse, Qt::AlignBottom);
    mainLayout->setContentsMargins(1,1,1,1);
    this->setLayout(mainLayout);
}

AboutWindow::AboutWindow(const AboutWindow& orig) {
}

AboutWindow::~AboutWindow() {
}

void AboutWindow::exitNow(){
    this->hide();
}

