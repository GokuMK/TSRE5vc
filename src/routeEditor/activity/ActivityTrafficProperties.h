/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ACTIVITYTRAFFICPROPERTIES_H
#define	ACTIVITYTRAFFICPROPERTIES_H

#include <QtWidgets>
#include <QString>

class Traffic;

class ActivityTimetable;

class ActivityTrafficProperties : public QWidget {
    Q_OBJECT
public:
    ActivityTrafficProperties(QWidget* parent);
    virtual ~ActivityTrafficProperties();
    // The traffic, or nothing: then the panel is empty and disabled.
    void showTraffic(Traffic* t);
    Traffic *shownTraffic() const;
    
public slots:
    void lServciesSelected(QTreeWidgetItem* item, int column);
    void bAddServiceSelected();
    void bRemoveServiceSelected();
    void bCloneOutcomeSelected();
    void serviceActoionListSelected(QString val);
    void eTimeSelected(QTime val);
    
signals:
    
private:
    Traffic *traffic = NULL;
    // The instance of the selected row, or NULL.
    ActivityTimetable *selectedInstance();
    
    QLineEdit eFileName;
    QLineEdit eDisplayName;
    QTreeWidget lServcies;
    QComboBox cServiceList;
    QTimeEdit eTime;
    
    
};

#endif	/* ACTIVITYTRAFFICPROPERTIES_H */

