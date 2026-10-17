/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ACTIVITYEVENTACTIONPROPERTIES_H
#define	ACTIVITYEVENTACTIONPROPERTIES_H

#include <QtWidgets>
#include <QMap>
#include <tsre/trains/ActivityEvent.h>
#include <tsre/gui/UnitSpinBox.h>

class PreciseTileCoordinate;

class ActivityEventProperties : public QWidget {
    Q_OBJECT
public:
    ActivityEventProperties(QWidget* parent);
    virtual ~ActivityEventProperties();
    void showEvent(ActivityEvent *e);
    void setEventList(QMap<int, QString> eventNames);
    void setStationStopList(QMap<int, QString> stationNames);
    
public slots:
    void outcomeListSelected(QListWidgetItem* item);
    void outcomeActoionListSelected(QString item);
    void cOutcomeEventSelected(QString val);
    void eSoundFileNameEdited(QString val);
    void cSoundTypeSelected(QString val);
    void cWeatherChangeSelected(QString va);
    void cActionTypeSelected(QString item);
    void cStationStopActionSelected(QString item);
    void bActionSidingSelected();
    void bAddOutcomeSelected();
    void bRemoveOutcomeSelected();
    void eOutcomeMessageSelected();
    void eActivationLevelSelected();
    void eNameSelected();
    void eTriggeredTextSelected(QString val);
    void eUntriggeredTextSelected(QString val);
    void eNotesSelected(QString val);
    void eTimeSelected(QTime val);
    void bJumpToEventLocationSelected();
    void bPickEventLocationSelected(bool val);
    void msg(QString text, QString val);
    void eLocationRadiusSelected();
    void cLocationStopSelected(int val);
    void cReversableSelected(int val);
    void cAutoContinueLabelSelected(int val);
    void eAutoContinueSelected();
    void bJumpToCarSelected();
    void bRemoveCarSelected();
    void bDescCarSelected();
    void bPickNewEventWagonToolSelected();
    void eActionSpeedSelected();
    // Open Rails' additions.
    void cTriggeringTrainSelected(int index);
    void eSoundLocationEdited();
    void bSoundAtEventSelected();
    void bPickSoundLocationSelected(bool val);
    void weatherEdited();
    void cRestartTrainSelected(int index);
    void eRestartDelayEdited();
    void cRestartWaitingPointEdited();
        
signals:
    void eventNameChanged(int id);
    void enableTool(QString val);
    void jumpTo(PreciseTileCoordinate* val);
    
private:
    QWidget actionWidget;
    QWidget actionWidgetSpeed;
    QWidget actionWidgetStation;
    QWidget actionWidgetSiding;
    QWidget actionWidgetWagonList;
    QWidget locationWidget;
    QWidget timeWidget;
    //QListWidget wagonList;
    QTreeWidget wagonList;
    
    QMap<int, QWidget*> outcomeProperties;
    
    QLineEdit eName;
    QSpinBox eActivationLevel;
    QLineEdit eTriggeredText;
    QLineEdit eUntriggeredText;
    QLineEdit eNotes;
    QPlainTextEdit eOutcomeMessage;
    QComboBox cOutcome;
    QComboBox cOutcomeEvent;
    QComboBox cStationStopAction;
    QLineEdit eSoundFileName;
    QComboBox cSoundType;
    QComboBox cWeatherChange;
    QComboBox cActionType;
    //QPushButton bActionStationId;
    QLineEdit eActionSiding;
    QPushButton bActionSiding;
    // In the units the editors show (task core si-units).
    UnitSpinBox eActionSpeed{Units::Quantity::Speed};
    QTimeEdit eTime;
    //QLineEdit eOutcomeEvent;
    QCheckBox cReversable;
    QCheckBox cAutoContinueLabel;
    QSpinBox eAutoContinue;
    //QLabel lReversable;
    QLineEdit eActionInfo;
    QLineEdit eLocationPosition;
    QSpinBox eLocationRadius;
    QCheckBox cLocationStop;
    QComboBox cTriggeringTrain;
    QLineEdit eSoundLocation;
    QPushButton *bSoundAtEvent = NULL;
    QCheckBox cWeatherSet[4];
    QDoubleSpinBox eWeatherValue[4];
    // The fog's distance, in place of eWeatherValue[1] (a length shown in the
    // editors' units).
    UnitSpinBox eFog{Units::Quantity::Length};
    QSpinBox eWeatherTime[4];
    QComboBox cRestartTrain;
    QSpinBox eRestartDelay;
    QComboBox cRestartWaitingPoint;
    // The activity's AI trains in the combo (service and start time as
    // data), the one given chosen; "none" first.
    void fillTrains(QComboBox &combo, const QString &service, int time);
    // The waiting points of the train's path.
    void fillWaitingPoints(const QString &service, int value);
    QListWidget outcomeList;
    
    ActivityEvent* event = NULL;
    ActivityEvent::Outcome* outcome = NULL;
    
    QMap<QString, QPushButton*> buttonTools;
    // The activity of the event shown, or NULL.
    Activity *eventActivity();
    PreciseTileCoordinate* coordinate = NULL;
    
    void selctOutcomeOnList(int id);
};

#endif	/* ACTIVITYEVENTACTIONPROPERTIES_H */

