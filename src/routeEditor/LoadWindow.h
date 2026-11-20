/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef LOADWINDOW_H
#define	LOADWINDOW_H

#include <QtWidgets>

class LoadWindow : public QWidget {
    Q_OBJECT

public:
    LoadWindow();
    // The game root chosen last in the Load window (the last of its recent
    // roots), empty without one (task explore 01: --explore without one).
    static QString recentRoot();

public slots:
    void exitNow();
    void handleBrowseButton(QString directory = "");
    void routeLoad();
    // Explore: the explore mode window, the route chosen in the list (if
    // any) chosen first there (task explore 01).
    void exploreLoad();
    void setNewRoute();
    void setLoadRoute();
    void cRecentEnabled(QString val);
signals:
    void showMainWindow();
    void showExplore(const QString &route);
private:
    void listRoutes();
    QListWidget routeList;
    QComboBox cRecent;
    QPushButton *browse;
    QPushButton *explore;
    QPushButton *load;
    QPushButton *neww;
    QPushButton *exit;
    QLineEdit *nowaTrasa;
    QWidget* nowa;
    void downloadTemplateRoute(QString path);
    void listRoots();
};

#endif	/* LOADWINDOW_H */

