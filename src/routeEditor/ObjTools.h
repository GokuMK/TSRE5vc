/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TOOLBOX_H
#define	TOOLBOX_H

#include <QtWidgets>
#include <tsre/world/Route.h>
#include <tsre/world/Ref.h>
#include <deque>

class ObjTools : public QWidget{
    Q_OBJECT

public:
    ObjTools(QString name);
    virtual ~ObjTools();
    
public slots:
    void routeLoaded(Route * a);
    void refClassSelected(const QString & text);
    void refTrackSelected(const QString & text);
    void refOtherSelected(const QString & text);
    void refSearchSelected(const QString & text);
    void refListSelected(QListWidgetItem * item);
    void refListSelected(QListWidgetItem * item, QListWidgetItem * previous);
    void lastItemsListSelected(QListWidgetItem * item);
    void selectToolEnabled(bool val);
    void placeToolEnabled(bool val);
    void continuousFlexToolEnabled(bool val);
    void continuousFlexRoadToolEnabled(bool val);
    void continuousRulerToolEnabled(bool val);
    void continuousFlexOptionsButtonEnabled(bool val);
    void continuousProfileChanged(const QString &value);
    void continuousRulerNodeShapeChanged();
    void continuousFlexOptionsChanged();
    void autoPlacementButtonEnabled(bool val);
    void itemSelected(Ref::RefItem* item);
    void stickToTDBEnabled(int state);
    void autoPlacementLengthEnabled(QString val);
    void resetRotationButtonEnabled();
    void advancedPlacementButtonEnabled(bool val);
    void autoPlacementDeleteLastEnabled();
    void autoPlacementRotTypeSelected(QString val);
    void autoPlacementTargetSelected(QString val);
    void autoPlacementOffsetEnabled(QString val);
    void autoSnapableRadiusEnabled(QString val);
    void chSnapableOnlyRotation(int val);

    void showLastItemsContextMenu(QPoint val);
    void lastItemsMenuFindSimilar();
    
    void refreshObjLists();
    
    void msg(QString name);
    void msg(QString name, bool val);
    void msg(QString name, int val);
    void msg(QString name, float val);
    void msg(QString name, QString val);
    
signals:
    void enableTool(QString name);

    void sendMsg(QString name);
    void sendMsg(QString name, bool val);
    void sendMsg(QString name, int val);
    void sendMsg(QString name, float val);
    void sendMsg(QString name, QString val);
    
private:
    enum class ContinuousToolMode {
        Track,
        Road,
        Ruler
    };

    Route* route = NULL;
    QListWidget refList;
    QListWidget lastItems;
    QComboBox refClass;
    QComboBox refTrack;
    QComboBox refRoad;
    QComboBox refOther;
    Ref::RefItem itemRef;
    std::deque<Ref::RefItem*> lastItemsPtr;
    QVector<Ref::RefItem*> currentItemList;
    QCheckBox stickToTDB;
    QCheckBox stickToRDB;
    QLineEdit autoPlacementLength;
    QLineEdit searchBox;
    //QPushButton *selectTool;
    //QPushButton *placeTool;
    //QPushButton *autoPlacementButton;
    QMap<QString, QPushButton*> buttonTools;

    QWidget advancedPlacementWidget;
    QWidget continuousFlexOptionsWidget;
    QWidget continuousFlexTrackOptionsWidget;
    QPushButton *continuousFlexOptionsButton = NULL;
    QComboBox continuousProfile;
    QComboBox continuousRulerNodeShape;
    QCheckBox continuousFlexLeft;
    QCheckBox continuousFlexRight;
    QDoubleSpinBox continuousFlexSeparation;
    QDoubleSpinBox continuousFlexMinimumRadius;
    ContinuousToolMode continuousToolMode = ContinuousToolMode::Track;
    double continuousFlexTrackSeparation = 4.0;
    double continuousFlexTrackMinimumRadius = 15.0;
    double continuousFlexRoadMinimumRadius = 6.0;
    QString continuousFlexTrackProfile;
    QString continuousFlexRoadProfile = "RdProfile";
    QString continuousRulerProfile;
    QString continuousRulerNodeShapeName;
    const Ref *continuousRulerNodeShapeRef = nullptr;
    QLineEdit autoPlacementPosX;
    QLineEdit autoPlacementPosY;
    QLineEdit autoPlacementPosZ;
    QLineEdit autoPlacementRotX;
    QLineEdit autoPlacementRotY;
    QLineEdit autoPlacementRotZ;
    QLineEdit autoSnapableRadius;
    QCheckBox autoSnapableOnlyRotation;
    QComboBox autoPlacementRotType;
    QComboBox autoPlacementTarget;

    void enableContinuousTool(ContinuousToolMode mode, bool enabled);
    void refreshContinuousProfiles();
    void refreshContinuousRulerNodeShapes();
    QString selectedContinuousRulerNodeShape() const;

};

#endif	/* TOOLBOX_H */

