/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef NEWROUTEWINDOW_H
#define NEWROUTEWINDOW_H

#include <QDialog>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/geo/GeoPresetData.h>

class QCheckBox;
class QComboBox;
class QCompleter;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QModelIndex;
class QPushButton;
class QStandardItemModel;
class QWidget;

struct NewRouteSelection {
    QString name;
    QString countryCode;
    double startLatitude = 0.0;
    double startLongitude = 0.0;
    GeoProjectionType projectionType = GeoProjectionType::TransverseMercator;
    GeoProjectionParameters projection;
    int routeTileX = 0;
    int routeTileZ = 0;
};

class NewRouteWindow : public QDialog {
    Q_OBJECT
public:
    explicit NewRouteWindow(QWidget *parent = nullptr);
    const NewRouteSelection &selection() const { return result; }

private slots:
    void updatePlaceResults(const QString &text);
    void selectPlace(const QModelIndex &index);
    void updateOriginResults(const QString &text);
    void selectOrigin(const QModelIndex &index);
    void startCoordinateEdited(const QString &text);
    void originCoordinateEdited(const QString &text);
    void projectionChanged();
    void customOriginChanged(bool enabled);
    void calculateIghOriginOffset();
    void updateState();
    void createRoute();

private:
    static bool coordinate(const QLineEdit *edit, double minimum,
                           double maximum, double &value);
    void updateNameStatus();
    void setCoordinatePair(QLineEdit *latitudeEdit, QLineEdit *longitudeEdit,
                           double latitude, double longitude);
    void chooseNearestOrigin(const QString &countryCode = QString());
    void applyOrigin(int index);
    bool startCoordinates(double &latitude, double &longitude) const;
    bool originCoordinates(double &latitude, double &longitude) const;
    GeoProjectionType selectedProjection() const;
    void showAssetStatus(const QStringList &errors);

    GeoPlacePresetIndex placeIndex;
    GeoProjectionPresetList projectionPresets;
    NewRouteSelection result;
    QString selectedCountryCode;
    bool changingCoordinates = false;
    bool originWasChosenManually = false;
    int selectedOriginIndex = -1;

    QLineEdit *nameEdit;
    QLabel *nameStatus;
    QLineEdit *placeSearch;
    QCompleter *placeCompleter;
    QStandardItemModel *placeModel;
    QLineEdit *selectedPlaceEdit;
    QLineEdit *startLatitudeEdit;
    QLineEdit *startLongitudeEdit;
    QWidget *projectionPanel;
    QComboBox *projectionCombo;
    QLineEdit *originSearch;
    QCompleter *originCompleter;
    QStandardItemModel *originModel;
    QLineEdit *selectedOriginEdit;
    QLineEdit *originLatitudeEdit;
    QLineEdit *originLongitudeEdit;
    QCheckBox *customOrigin;
    QWidget *routeTilePanel;
    QLineEdit *projectionTileOffsetXEdit;
    QLineEdit *projectionTileOffsetZEdit;
    QPushButton *ighOffsetButton;
    QLabel *assetStatus;
    QDialogButtonBox *buttons;
    QPushButton *createButton;
};

#endif /* NEWROUTEWINDOW_H */
