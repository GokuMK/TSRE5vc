/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef PROPERTIESGROUP_H
#define	PROPERTIESGROUP_H

#include <routeEditor/properties/PropertiesAbstract.h>

class GroupObj;

class PropertiesGroup : public PropertiesAbstract{
    Q_OBJECT
public:
    PropertiesGroup();
    virtual ~PropertiesGroup();
    bool support(GameObj* obj);
    void showObj(GameObj* obj);
    void updateObj(GameObj* obj);
    void setPinnedSelectionState(GameObj* pinnedObj, GameObj* selectedObj);

public slots:
    void enableCustomDetailLevelEnabled(int val);
    void customDetailLevelEdited(QString val);
    void checkboxAnimEdited(int val);
    void checkboxTerrainEdited(int val);
    void cShadowTypeEdited(int val);
    void chIndividualRotationEdited(int val);
    void editPositionYEnabled(QString val);
    void childSelectionChanged();
    void selectChildEnabled();
    void selectSimilarChildEnabled();
    void reselectGroupEnabled();

signals:
    void customSelectionRequested(GameObj* pinnedObj, GameObj* selectedObj);
    void reselectGroupRequested(GameObj* obj);

private:
    void refreshChildList(GroupObj* gobj);
    WorldObj* getSelectedChild() const;
    QString getChildLabel(WorldObj* obj) const;

    QCheckBox checkboxAnim;
    QCheckBox checkboxTerrain;
    QCheckBox chSeparateRotation;
    QComboBox cShadowType;
    QListWidget childList;
    QPushButton reselectGroupButton;
    QPushButton selectChildButton;
    QPushButton selectSimilarChildButton;
    QLabel childListLabel;
    QLabel pinnedLabel;
    GroupObj* customSelectionGroup = NULL;
    GameObj* pinnedGroupObj = NULL;
    GameObj* pinnedSelectionObj = NULL;
};

#endif	/* PROPERTIESGROUP_H */

