/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef PROPERTIESACTIVITYPATH_H
#define	PROPERTIESACTIVITYPATH_H

#include <routeEditor/properties/PropertiesAbstract.h>
#include <QCheckBox>

class Path;

class PropertiesActivityPath : public PropertiesAbstract{
    Q_OBJECT
public:
    PropertiesActivityPath();
    virtual ~PropertiesActivityPath();
    bool support(GameObj* obj);
    void showObj(GameObj* obj);
    // The view asks a few times a second: the panel shows the path again
    // when it changed (an edit of the path tool, undo).
    void updateObj(GameObj* obj);

private slots:
    void nameEdited();
    void startEdited();
    void endEdited();
    void flagsEdited();
    void notPlayerToggled(bool value);

private:
    // Shows the path; a field being typed in keeps its text.
    void refresh();
    Path *pathObj = NULL;
    unsigned int shownChanges = 0;
    QLineEdit ePathFName;
    QLineEdit eName;
    QLineEdit ePathStart;
    QLineEdit ePathEnd;
    QLineEdit eFlags;
    QCheckBox cNotPlayer;

    QListWidget nodeList;
};

#endif	/* PROPERTIESACTIVITYPATH_H */

