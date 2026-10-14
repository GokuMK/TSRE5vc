/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ACTIVITYTIMETABLEPROPERTIES_H
#define	ACTIVITYTIMETABLEPROPERTIES_H

#include <QtWidgets>
#include <QString>

class ActivityServiceDefinition;
class Activity;

class ActivityTimetableProperties : public QWidget {
    Q_OBJECT
public:
    ActivityTimetableProperties(QWidget* parent);
    virtual ~ActivityTimetableProperties();
    // The instance's timetable, or nothing: then the panel is empty and
    // disabled.
    void showTimetable(ActivityServiceDefinition* s, Activity *owner = NULL);
    
public slots:
    void lTimetableSelected(int row, int column);
    void bCalculateSelected();
    
signals:
    // Times or efficiencies changed (a cell, Calculate).
    void edited();
    
private:
    ActivityServiceDefinition *service = NULL;
    // The activity the instance is in.
    Activity *activity = NULL;
    QTableWidget lTimetable;
    QTimeEdit eTime;
    QLineEdit eMainEng;
    QLineEdit eMaxSpeed;
    QLineEdit eAvgSpeed;
};

#endif	/* ACTIVITYTIMETABLEPROPERTIES_H */

