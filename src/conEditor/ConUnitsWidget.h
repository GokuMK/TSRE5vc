/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef CONUNITSWIDGET_H
#define	CONUNITSWIDGET_H

#include <QWidget>
#include <QtWidgets>

class Consist;
class EngLib;
class ConUnitsWidget : public QWidget {
    Q_OBJECT
public:
    ConUnitsWidget();
    virtual ~ConUnitsWidget();
    void setCon(Consist * c);
    // A car chosen elsewhere (the consist's view): its row and its number.
    void selectUnit(int idx);
    Consist* con = NULL;
    //EngLib* englib;

public slots:
    void itemsSelected(QListWidgetItem* it);
    void bDelReleased();
    void bUpReleased();
    void bDownReleased();
    void bFlipReleased();
    void numberEdited();
    // The chosen car's control cut out, and its coupling's lines (task
    // driving 10).
    void controlOffToggled(bool off);
    void couplingChosen(QAction *action);
    
signals:
    void selected(int id);
    void refreshItem();
        
private:
    QListWidget items;
    QLineEdit count;
    // The chosen car's number (its UiD, task scripting 03).
    QSpinBox number;
    // The chosen car cut out of its train's control group (TSREControl (
    // Off )), and the lines of its coupling to the next car (TSRECoupling;
    // its couplers' own by default), task driving 10.
    QCheckBox controlOff;
    QToolButton coupling;
    QMenu couplingMenu;
    // The lines the chosen car's coupling has: the consist's, else the two
    // couplers' own; -1 for the last car.
    int couplingLines(int idx) const;
    void showCoupling(int idx);
};
#endif	/* CONUNITSWIDGET_H */

