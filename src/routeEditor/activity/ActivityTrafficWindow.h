/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ACTIVITYTRAFFICWINDOW_H
#define	ACTIVITYTRAFFICWINDOW_H

#include <QWidget>
#include <QtWidgets>
#include <memory>
#include <vector>

class Route;
class Traffic;
class Service;
class ActivityTrafficProperties;
class Activity;
class OrTimetable;
class QTextBrowser;

class ActivityTrafficWindow : public QWidget {
    Q_OBJECT
public:
    ActivityTrafficWindow(QWidget* parent);
    virtual ~ActivityTrafficWindow();
    ActivityTrafficProperties* trafficProperties = NULL;
    
public slots:
    void lTrafficSelected(QTreeWidgetItem * item, int column);
    void showTraffic(Route* r);
    void bNewTrafficSelected();
    void bDeleteTrafficSelected();
    // The traffic (by its file's name) shown and chosen in the list.
    void selectTraffic(QString nameId);
    // Undo replaces activity data (ActivityNotifier).
    void activityDataRestoring();
    void activityDataRestored();
    
signals:
    void reloadTrafficsList();
    
private:
    Route *route = NULL;
    QTreeWidget lTraffic;
    // Open Rails timetables (task simulation 08): listed, and the chosen
    // one's summary and problems; the traffic editor and its buttons hidden.
    void showTimetables();
    void showTimetableInfo(int index);
    std::vector<std::pair<std::shared_ptr<OrTimetable>, Activity*>> shownTimetables;
    QTextBrowser *timetableInfo = NULL;
    QWidget *trafficEditor = NULL;
    QPushButton *newButton = NULL;
    QPushButton *deleteButton = NULL;
};

#endif	/* ACTIVITYTRAFFICWINDOW_H */

