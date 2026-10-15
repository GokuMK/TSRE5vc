/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ACTIVITYTOOLS_H
#define	ACTIVITYTOOLS_H

#include <QtWidgets>
#include <QString>

class Route;
class ActivityCheckWindow;
struct ActivityFinding;
class GameObj;
class Activity;
class ActivityServiceDefinition;
class Path;
class PreciseTileCoordinate;

class ActivityTools : public QWidget{
    Q_OBJECT
public:
    ActivityTools(QString name);
    virtual ~ActivityTools();

public slots:
    void loadActFiles();
    void activitySelected(QString n);
    void conFilesShowEnabled(QString);
    void routeLoaded(Route* r);
    void actNewLooseConsistToolEnabled(bool val);
    void actPathsEditToolEnabled(bool val);
    void actPathsNewSelected();
    void actPathsCloneSelected();
    void actPathsDeleteSelected();
    void cPathSelected(int index);
    void actConsistDeleteEnabled();
    void actConsistJumpEnabled();
    void actFailedSignalsJumpEnabled();
    void actFailedSignalNewToolEnabled();
    void actFailedSignalDeleteEnabled();
    void actFailedSignalDeleteAllEnabled();
    void actReducedSpeedZonesEnabled();
    void actZoneNewToolEnabled(bool val);
    void actZoneDeleteEnabled();
    void actZoneDeleteAllEnabled();
    void newActButtonEnabled();
    void cServiceEnabled(QString val);
    void cTrafficEnabled(QString val);
    //void actServiceNewEnabled();
    //void actServiceEditEnabled();
    void msg(QString text);
    void msg(QString text, QString val);
    void eFileNameEnabled(QString val);
    void eDisplayNameEnabled(QString val);
    void cDifficultyEnabled(int val);
    void eDurationEnabled();
    void eStartTimeEnabled();
    void cSeasonEnabled(int val);
    void cWeatherEnabled(int val);
    void cHornAtCrossingsEnabled(int val);
    // Open Rails' additions to the activity.
    void cHornPatternEnabled(int index);
    void sFuelWoodEnabled(int val);
    void sFuelSandEnabled(int val);
    void eLoadStationsEnabled(QString val);
    void eFuelCoalEnabled(QString val);
    void sFuelCoalEnabled();
    void eFuelDieselEnabled(QString val);
    void sFuelDieselEnabled();
    void eFuelWaterEnabled(QString val);
    void sFuelWaterEnabled();
    void eHazardAnimalEnabled(QString val);
    void sHazardAnimalEnabled();
    void eHazardPeopleEnabled(QString val);
    void sHazardPeopleEnabled();
    void descriptionOpenEnabled();
    void briefingOpenEnabled();
    void eDescriptionEnabled();
    void eBriefingEnabled();
    void actEventsOpenEnabled();
    void actServiceOpenEnabled();
    void actTrafficOpenEnabled();
    void actTimetableOpenEnabled();
    void actSettingsOpenEnabled();
    // The simulation's controls (task simulation 01).
    void actPlayEnabled();
    void simPauseEnabled();
    void simStopEnabled();
    void simResetEnabled();
    void simRunToEnabled();
    void simSpeedSelected(int index);
    void showSimulation();
    void simulationEvents(int first);
    void lockEditing(bool locked);
    void actSaveEnabled();
    // Check Activity: the activity chosen, its findings in their window.
    void actCheckEnabled();
    // Merge the activity's Open Rails overlay into it (what it adds shown
    // first).
    void actOverlayMergeEnabled();
    
    void reloadActivityList();
    void reloadServicesList();
    void reloadTrafficsList();
    void reloadPathsList();
    void conFilesRefreshSelected();
    void actPathsRefreshListSelected();
    
signals:
    void enableTool(QString name);
    void sendMsg(QString);
    void objectSelected(GameObj* obj);
    void showActivityEventEditor();
    void showActivityServiceEditor();
    void showEvents(Activity *a);
    void showServices(Route *r);
    void showActivityTrafficEditor();
    void showTraffic(Route *r);
    void showActivityTimetableEditor();
    // Check Activity's findings: what they name, shown.
    void selectEvent(int id);
    void selectService(QString nameId);
    void selectTraffic(QString nameId);
    void showTimetable(Activity *a);
    void jumpTo(PreciseTileCoordinate* c);
    
private:
    // Selects the path chosen in the list, which the path tool edits.
    void selectCurrentPath();
    // The list shows the paths' names as they are now (path properties).
    void refreshPathNames();
    bool eventFilter(QObject *watched, QEvent *event) override;
    // Asks for a new path's file name (its id) and display name; false when
    // cancelled.
    bool askPathNames(QString &id, QString &name);
    // A new or cloned path: listed, chosen and edited.
    void addEditedPath(Path *path);
    Route *route = NULL;
    QComboBox consists;
    QComboBox speedZones;
    QComboBox failedSignals;
    QComboBox actShow;
    QComboBox conFilesShow;
    QMap<QString, QPushButton*> buttonTools;
    
    // Before the fields it holds: members are destroyed in reverse order,
    // and it would free the fields declared before it.
    QWidget settingsWidget;
    QLabel pathReport;
    QLineEdit eDisplayName;
    QComboBox cHornPattern;
    QSpinBox sFuelWood;
    QSpinBox sFuelSand;
    QLineEdit eLoadStations;
    QLineEdit eFileName;

    QTimeEdit eDuration;
    QTimeEdit eStartTime;
    
    QLineEdit* eHazardAnimal;
    QLineEdit* eHazardPeople;
    QLineEdit* eFuelCoal;
    QLineEdit* eFuelWater;
    QLineEdit* eFuelDiesel;
    QSlider sHazardAnimal;
    QSlider sHazardPeople;
    QSlider sFuelCoal;
    QSlider sFuelWater;
    QSlider sFuelDiesel;
    
    QComboBox cDifficulty;
    QComboBox cSeason;
    QComboBox cWeather;
    QCheckBox cHornAtCrossings;

    QComboBox cService;
    QComboBox cTraffic;
    QComboBox cPath;
    
    QPlainTextEdit eDescription;
    QPlainTextEdit eBriefing;
    
    PreciseTileCoordinate* coordinate = NULL;
    ActivityCheckWindow *checkWindow = NULL;
    // What edits the activity, off while a simulation exists.
    QList<QWidget*> editing;
    QPushButton *simPlay = NULL;
    QPushButton *simPause = NULL;
    QPushButton *simStop = NULL;
    QPushButton *simReset = NULL;
    QPushButton *simRunTo = NULL;
    QComboBox simSpeed;
    QTimeEdit simRunToTime;
    QLabel simTime;
    QLabel simStatus;
    // Tells of deadlocks as they happen; the run goes on.
    QPointer<QMessageBox> deadlockBox;
    QStringList deadlocked;
    // A line under Activity Info while the activity has an Open Rails
    // overlay: Merge... puts it in.
    QWidget overlayLine;
    QLabel overlayLabel;
    void showOverlay();
    void showFinding(const ActivityFinding &finding);
    
    void reloadActivityObjectLists();
    // The activity chosen in the list, or NULL: none chosen (the list
    // starts so), or one that did not load.
    Activity *currentActivity();
};

#endif	/* ACTIVITYTOOLS_H */

