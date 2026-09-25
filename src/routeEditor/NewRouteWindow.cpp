/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <routeEditor/NewRouteWindow.h>
#include <routeEditor/NewRouteValidation.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QThread>
#include <QVBoxLayout>
#include <cmath>
#include <memory>
#include <tsre/Game.h>

namespace {
QLabel *sectionLabel(const QString &text, QWidget *parent) {
    QLabel *label = new QLabel(QStringLiteral("<b>%1</b>").arg(text), parent);
    label->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                         .arg(Game::StyleMainLabel));
    return label;
}

QString coordinateText(double value) {
    return QString::number(value, 'g', 12);
}
}

NewRouteWindow::NewRouteWindow(QWidget *parent)
    : QDialog(parent),
      nameEdit(new QLineEdit(this)),
      nameStatus(new QLabel(this)),
      placeSearch(new QLineEdit(this)),
      placeCompleter(new QCompleter(this)),
      placeModel(new QStandardItemModel(this)),
      selectedPlaceEdit(new QLineEdit(this)),
      startLatitudeEdit(new QLineEdit(this)),
      startLongitudeEdit(new QLineEdit(this)),
      projectionPanel(new QWidget(this)),
      projectionCombo(new QComboBox(projectionPanel)),
      originSearch(new QLineEdit(projectionPanel)),
      originCompleter(new QCompleter(this)),
      originModel(new QStandardItemModel(this)),
      selectedOriginEdit(new QLineEdit(projectionPanel)),
      originLatitudeEdit(new QLineEdit(projectionPanel)),
      originLongitudeEdit(new QLineEdit(projectionPanel)),
      customOrigin(new QCheckBox(
          //% "Custom origin"
          tr("Custom origin"),
          projectionPanel)),
      routeTilePanel(new QWidget(projectionPanel)),
      projectionTileOffsetXEdit(new QLineEdit(QStringLiteral("0"), routeTilePanel)),
      projectionTileOffsetZEdit(new QLineEdit(QStringLiteral("0"), routeTilePanel)),
      ighOffsetButton(new QPushButton(
          tr("Create origin offset using MSTS IGH"), projectionPanel)),
      assetStatus(new QLabel(this)),
      buttons(new QDialogButtonBox(QDialogButtonBox::Cancel, this)),
      createButton(buttons->addButton(
          //% "New route"
          tr("New route"),
          QDialogButtonBox::AcceptRole)) {
    setWindowTitle(
        //% "New route"
        qtTrId("route.editor.load.window.title.new.route"));
    setMinimumWidth(560);

    QVBoxLayout *layout = new QVBoxLayout(this);
    QFormLayout *identity = new QFormLayout;
    identity->addRow(
        //% "Name ID:"
        qtTrId("route.editor.new.route.window.label.name.id"), nameEdit);
    nameStatus->setWordWrap(true);
    nameStatus->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                              .arg(Game::StyleRedText));
    identity->addRow(QString(), nameStatus);
    layout->addLayout(identity);

    layout->addWidget(sectionLabel(
        //% "Choose Route Starting Point"
        tr("Choose Route Starting Point"), this));
    placeSearch->setPlaceholderText(
        //% "Search for a city or place"
        tr("Search for a city or place"));
    layout->addWidget(placeSearch);
    placeCompleter->setModel(placeModel);
    placeCompleter->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    placeCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    placeCompleter->setMaxVisibleItems(10);
    placeSearch->setCompleter(placeCompleter);
    selectedPlaceEdit->setEnabled(false);
    selectedPlaceEdit->setPlaceholderText(tr("No place selected"));
    QFormLayout *selectedPlaceLayout = new QFormLayout;
    selectedPlaceLayout->addRow(tr("Selected place:"), selectedPlaceEdit);
    layout->addLayout(selectedPlaceLayout);

    QGridLayout *startCoordinatesLayout = new QGridLayout;
    startLatitudeEdit->setPlaceholderText(
        //% "Latitude"
        tr("Latitude"));
    startLongitudeEdit->setPlaceholderText(
        //% "Longitude"
        tr("Longitude"));
    startCoordinatesLayout->addWidget(new QLabel(tr("Latitude:"), this), 0, 0);
    startCoordinatesLayout->addWidget(new QLabel(tr("Longitude:"), this), 0, 1);
    startCoordinatesLayout->addWidget(startLatitudeEdit, 1, 0);
    startCoordinatesLayout->addWidget(startLongitudeEdit, 1, 1);
    layout->addLayout(startCoordinatesLayout);

    layout->addSpacing(8);
    layout->addWidget(sectionLabel(
        //% "Route Geographic Projection"
        tr("Route Geographic Projection"), this));
    layout->addWidget(projectionPanel);

    QVBoxLayout *projectionLayout = new QVBoxLayout(projectionPanel);
    projectionLayout->setContentsMargins(0, 0, 0, 0);
    projectionCombo->addItem(
        //% "Transverse Mercator"
        tr("Transverse Mercator"),
                             int(GeoProjectionType::TransverseMercator));
    projectionCombo->addItem(
        //% "Local Ellipsoidal Equirectangular - Legacy TSRE"
        tr("Local Ellipsoidal Equirectangular - Legacy TSRE"),
                             int(GeoProjectionType::LocalEllipsoidalEquirectangular));
    projectionCombo->addItem(
        //% "Interrupted Goode Homolosine - Legacy MSTS"
        tr("Interrupted Goode Homolosine - Legacy MSTS"),
                             int(GeoProjectionType::InterruptedGoodeHomolosine));
    projectionCombo->setStyleSheet(QStringLiteral("combobox-popup: 0;"));
    projectionLayout->addWidget(projectionCombo);
    originSearch->setPlaceholderText(
        //% "Search for a projection origin"
        tr("Search for a projection origin"));
    projectionLayout->addWidget(originSearch);
    originCompleter->setModel(originModel);
    originCompleter->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    originCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    originCompleter->setMaxVisibleItems(10);
    originSearch->setCompleter(originCompleter);
    selectedOriginEdit->setEnabled(false);
    selectedOriginEdit->setPlaceholderText(tr("No projection origin selected"));
    QFormLayout *selectedOriginLayout = new QFormLayout;
    selectedOriginLayout->addRow(tr("Selected origin:"), selectedOriginEdit);
    projectionLayout->addLayout(selectedOriginLayout);
    projectionLayout->addWidget(customOrigin);

    QGridLayout *originCoordinatesLayout = new QGridLayout;
    originLatitudeEdit->setPlaceholderText(
        //% "Origin latitude"
        tr("Origin latitude"));
    originLongitudeEdit->setPlaceholderText(
        //% "Origin longitude"
        tr("Origin longitude"));
    originLatitudeEdit->setReadOnly(true);
    originLongitudeEdit->setReadOnly(true);
    originLatitudeEdit->setEnabled(false);
    originLongitudeEdit->setEnabled(false);
    originCoordinatesLayout->addWidget(
            new QLabel(tr("Origin latitude:"), projectionPanel), 0, 0);
    originCoordinatesLayout->addWidget(
            new QLabel(tr("Origin longitude:"), projectionPanel), 0, 1);
    originCoordinatesLayout->addWidget(originLatitudeEdit, 1, 0);
    originCoordinatesLayout->addWidget(originLongitudeEdit, 1, 1);
    projectionLayout->addLayout(originCoordinatesLayout);

    QHBoxLayout *routeTileLayout = new QHBoxLayout(routeTilePanel);
    routeTileLayout->setContentsMargins(0, 0, 0, 0);
    routeTileLayout->addWidget(new QLabel(
        //% "Projection tile offset X:"
        tr("Projection tile offset X:"), routeTilePanel));
    routeTileLayout->addWidget(projectionTileOffsetXEdit);
    routeTileLayout->addWidget(new QLabel(
        //% "Projection tile offset Z:"
        tr("Projection tile offset Z:"), routeTilePanel));
    routeTileLayout->addWidget(projectionTileOffsetZEdit);
    projectionTileOffsetXEdit->setValidator(
            new QIntValidator(projectionTileOffsetXEdit));
    projectionTileOffsetZEdit->setValidator(
            new QIntValidator(projectionTileOffsetZEdit));
    routeTilePanel->setEnabled(false);
    projectionLayout->addWidget(routeTilePanel);
    ighOffsetButton->setEnabled(false);
    projectionLayout->addWidget(ighOffsetButton);

    assetStatus->setWordWrap(true);
    assetStatus->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                               .arg(Game::StyleRedText));
    assetStatus->setVisible(false);
    layout->addWidget(assetStatus);
    layout->addWidget(buttons);

    connect(placeSearch, &QLineEdit::textEdited,
            this, &NewRouteWindow::updatePlaceResults);
    connect(placeCompleter,
            QOverload<const QModelIndex &>::of(&QCompleter::activated),
            this, &NewRouteWindow::selectPlace);
    connect(originSearch, &QLineEdit::textEdited,
            this, &NewRouteWindow::updateOriginResults);
    connect(originCompleter,
            QOverload<const QModelIndex &>::of(&QCompleter::activated),
            this, &NewRouteWindow::selectOrigin);
    connect(startLatitudeEdit, &QLineEdit::textEdited,
            this, &NewRouteWindow::startCoordinateEdited);
    connect(startLongitudeEdit, &QLineEdit::textEdited,
            this, &NewRouteWindow::startCoordinateEdited);
    connect(originLatitudeEdit, &QLineEdit::textEdited,
            this, &NewRouteWindow::originCoordinateEdited);
    connect(originLongitudeEdit, &QLineEdit::textEdited,
            this, &NewRouteWindow::originCoordinateEdited);
    connect(nameEdit, &QLineEdit::textChanged, this, &NewRouteWindow::updateState);
    connect(startLatitudeEdit, &QLineEdit::textChanged,
            this, &NewRouteWindow::updateState);
    connect(startLongitudeEdit, &QLineEdit::textChanged,
            this, &NewRouteWindow::updateState);
    connect(originLatitudeEdit, &QLineEdit::textChanged,
            this, &NewRouteWindow::updateState);
    connect(originLongitudeEdit, &QLineEdit::textChanged,
            this, &NewRouteWindow::updateState);
    connect(projectionTileOffsetXEdit, &QLineEdit::textChanged,
            this, &NewRouteWindow::updateState);
    connect(projectionTileOffsetZEdit, &QLineEdit::textChanged,
            this, &NewRouteWindow::updateState);
    connect(projectionCombo, &QComboBox::currentIndexChanged,
            this, &NewRouteWindow::projectionChanged);
    connect(customOrigin, &QCheckBox::toggled,
            this, &NewRouteWindow::customOriginChanged);
    connect(ighOffsetButton, &QPushButton::clicked,
            this, &NewRouteWindow::calculateIghOriginOffset);
    connect(createButton, &QPushButton::clicked,
            this, &NewRouteWindow::createRoute);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QStringList assetErrors;
    QString error;
    const QString geoPath = QStringLiteral("appdata/") + Game::AppDataVersion
            + QStringLiteral("/geo/");
    if (!projectionPresets.load(
            geoPath + QStringLiteral("geo_projection_presets_countries.json"),
            &error))
        assetErrors.append(error);
    showAssetStatus(assetErrors);
    placeSearch->setEnabled(false);
    placeSearch->setPlaceholderText(tr("Loading places..."));
    auto loadedPlaces = std::make_shared<GeoPlacePresetIndex>();
    auto placeError = std::make_shared<QString>();
    QThread *loader = QThread::create(
            [loadedPlaces, placeError] {
        loadedPlaces->loadDefault(placeError.get());
    });
    connect(loader, &QThread::finished, this,
            [this, loadedPlaces, placeError] {
        if (placeError->isEmpty()) {
            placeIndex = std::move(*loadedPlaces);
            placeSearch->setEnabled(true);
            placeSearch->setPlaceholderText(tr("Search for a city or place"));
            if (!placeSearch->text().isEmpty())
                updatePlaceResults(placeSearch->text());
        } else {
            showAssetStatus(QStringList(*placeError));
            placeSearch->setPlaceholderText(tr("Place presets unavailable"));
        }
    });
    connect(loader, &QThread::finished, loader, &QObject::deleteLater);
    loader->start();
    updateState();
}

bool NewRouteWindow::coordinate(const QLineEdit *edit, double minimum,
                                double maximum, double &value) {
    bool ok = false;
    value = edit->text().trimmed().toDouble(&ok);
    return ok && std::isfinite(value) && value >= minimum && value <= maximum;
}

void NewRouteWindow::setCoordinatePair(QLineEdit *latitudeEdit,
                                       QLineEdit *longitudeEdit,
                                       double latitude, double longitude) {
    changingCoordinates = true;
    latitudeEdit->setText(coordinateText(latitude));
    longitudeEdit->setText(coordinateText(longitude));
    changingCoordinates = false;
}

void NewRouteWindow::updatePlaceResults(const QString &text) {
    placeModel->clear();
    for (int index : placeIndex.search(text)) {
        const GeoPlacePreset &place = placeIndex.place(index);
        QStandardItem *item = new QStandardItem(
                QStringLiteral("%1 (%2)  %3, %4")
                .arg(place.name, place.countryCode,
                     coordinateText(place.latitude),
                     coordinateText(place.longitude)));
        item->setData(index, Qt::UserRole);
        placeModel->appendRow(item);
    }
    if (placeModel->rowCount() > 0)
        placeCompleter->complete();
    else
        placeCompleter->popup()->hide();
}

void NewRouteWindow::selectPlace(const QModelIndex &index) {
    if (!index.isValid()) return;
    const GeoPlacePreset &place = placeIndex.place(index.data(Qt::UserRole).toInt());
    placeSearch->setText(place.name);
    selectedPlaceEdit->setText(index.data(Qt::DisplayRole).toString());
    setCoordinatePair(startLatitudeEdit, startLongitudeEdit,
                      place.latitude, place.longitude);
    selectedCountryCode = place.countryCode;
    originWasChosenManually = false;
    chooseNearestOrigin(selectedCountryCode);
    updateState();
}

void NewRouteWindow::updateOriginResults(const QString &text) {
    originModel->clear();
    for (int index : projectionPresets.search(text)) {
        const GeoProjectionPreset &preset = projectionPresets.preset(index);
        QStandardItem *item = new QStandardItem(
                QStringLiteral("%1 (%2)  %3, %4")
                .arg(preset.name, preset.countryCode,
                     coordinateText(preset.latitude),
                     coordinateText(preset.longitude)));
        item->setData(index, Qt::UserRole);
        originModel->appendRow(item);
    }
    if (originModel->rowCount() > 0)
        originCompleter->complete();
    else
        originCompleter->popup()->hide();
}

void NewRouteWindow::selectOrigin(const QModelIndex &index) {
    if (!index.isValid()) return;
    originWasChosenManually = true;
    applyOrigin(index.data(Qt::UserRole).toInt());
    updateState();
}

void NewRouteWindow::startCoordinateEdited(const QString &text) {
    if (changingCoordinates) return;
    selectedCountryCode.clear();
    selectedPlaceEdit->clear();
    double latitude = 0.0;
    double longitude = 0.0;
    if (NewRouteValidation::coordinatePair(text, latitude, longitude))
        setCoordinatePair(startLatitudeEdit, startLongitudeEdit,
                          latitude, longitude);
    if (!customOrigin->isChecked() && !originWasChosenManually)
        chooseNearestOrigin();
    updateState();
}

void NewRouteWindow::originCoordinateEdited(const QString &text) {
    if (changingCoordinates || !customOrigin->isChecked()) return;
    selectedOriginIndex = -1;
    selectedOriginEdit->clear();
    double latitude = 0.0;
    double longitude = 0.0;
    if (NewRouteValidation::coordinatePair(text, latitude, longitude))
        setCoordinatePair(originLatitudeEdit, originLongitudeEdit,
                          latitude, longitude);
    updateState();
}

void NewRouteWindow::chooseNearestOrigin(const QString &countryCode) {
    if (customOrigin->isChecked() || originWasChosenManually) return;
    double latitude = 0.0;
    double longitude = 0.0;
    if (!startCoordinates(latitude, longitude)) return;
    int index = projectionPresets.nearest(latitude, longitude, countryCode);
    if (index < 0 && !countryCode.isEmpty())
        index = projectionPresets.nearest(latitude, longitude);
    if (index >= 0) applyOrigin(index);
}

void NewRouteWindow::applyOrigin(int index) {
    const GeoProjectionPreset &preset = projectionPresets.preset(index);
    selectedOriginIndex = index;
    originSearch->setText(preset.name);
    selectedOriginEdit->setText(QStringLiteral("%1 (%2)  %3, %4")
            .arg(preset.name, preset.countryCode,
                 coordinateText(preset.latitude),
                 coordinateText(preset.longitude)));
    setCoordinatePair(originLatitudeEdit, originLongitudeEdit,
                      preset.latitude, preset.longitude);
}

bool NewRouteWindow::startCoordinates(double &latitude, double &longitude) const {
    return coordinate(startLatitudeEdit, -90.0, 90.0, latitude)
            && coordinate(startLongitudeEdit, -180.0, 180.0, longitude);
}

bool NewRouteWindow::originCoordinates(double &latitude, double &longitude) const {
    return coordinate(originLatitudeEdit, -90.0, 90.0, latitude)
            && coordinate(originLongitudeEdit, -180.0, 180.0, longitude);
}

GeoProjectionType NewRouteWindow::selectedProjection() const {
    return GeoProjectionType(projectionCombo->currentData().toInt());
}

void NewRouteWindow::projectionChanged() {
    const bool local = selectedProjection()
            != GeoProjectionType::InterruptedGoodeHomolosine;
    originSearch->setEnabled(local);
    customOrigin->setEnabled(local);
    if (!local) {
        customOrigin->setChecked(false);
        originWasChosenManually = false;
        selectedOriginIndex = -1;
        originSearch->clear();
        selectedOriginEdit->clear();
        originLatitudeEdit->clear();
        originLongitudeEdit->clear();
        projectionTileOffsetXEdit->clear();
        projectionTileOffsetZEdit->clear();
    } else {
        if (projectionTileOffsetXEdit->text().isEmpty())
            projectionTileOffsetXEdit->setText(QStringLiteral("0"));
        if (projectionTileOffsetZEdit->text().isEmpty())
            projectionTileOffsetZEdit->setText(QStringLiteral("0"));
        chooseNearestOrigin(selectedCountryCode);
    }
    originLatitudeEdit->setEnabled(local && customOrigin->isChecked());
    originLongitudeEdit->setEnabled(local && customOrigin->isChecked());
    routeTilePanel->setEnabled(local && customOrigin->isChecked());
    ighOffsetButton->setEnabled(local && customOrigin->isChecked());
    originCompleter->popup()->hide();
    updateState();
}

void NewRouteWindow::customOriginChanged(bool enabled) {
    const bool local = selectedProjection()
            != GeoProjectionType::InterruptedGoodeHomolosine;
    originLatitudeEdit->setEnabled(enabled && local);
    originLongitudeEdit->setEnabled(enabled && local);
    originLatitudeEdit->setReadOnly(false);
    originLongitudeEdit->setReadOnly(false);
    routeTilePanel->setEnabled(enabled && local);
    ighOffsetButton->setEnabled(enabled && local);
    if (!enabled) {
        if (local) {
            projectionTileOffsetXEdit->setText(QStringLiteral("0"));
            projectionTileOffsetZEdit->setText(QStringLiteral("0"));
        }
        originWasChosenManually = false;
        if (local) chooseNearestOrigin(selectedCountryCode);
    }
    updateState();
}

void NewRouteWindow::calculateIghOriginOffset() {
    double latitude = 0.0;
    double longitude = 0.0;
    if (!customOrigin->isChecked()
            || !originCoordinates(latitude, longitude))
        return;

    GeoMstsCoordinateConverter converter;
    IghCoordinate projected;
    PreciseTileCoordinate tile;
    converter.ConvertToInternal(latitude, longitude, &projected);
    converter.ConvertToTile(&projected, &tile);
    projectionTileOffsetXEdit->setText(QString::number(tile.TileX));
    projectionTileOffsetZEdit->setText(QString::number(tile.TileZ));
}

void NewRouteWindow::updateState() {
    double latitude = 0.0;
    double longitude = 0.0;
    const bool hasStart = startCoordinates(latitude, longitude);
    projectionPanel->setEnabled(hasStart);

    updateNameStatus();
    bool valid = hasStart && NewRouteValidation::validateName(
            nameEdit->text(), Game::root) == NewRouteValidation::NameError::None;
    if (valid && selectedProjection()
            != GeoProjectionType::InterruptedGoodeHomolosine) {
        valid = originCoordinates(latitude, longitude);
        if (customOrigin->isChecked()) {
            bool xOk = false;
            bool zOk = false;
            projectionTileOffsetXEdit->text().toInt(&xOk);
            projectionTileOffsetZEdit->text().toInt(&zOk);
            valid = valid && xOk && zOk;
        }
    }
    createButton->setEnabled(valid);
}

void NewRouteWindow::updateNameStatus() {
    using NameError = NewRouteValidation::NameError;
    const NameError error = NewRouteValidation::validateName(
            nameEdit->text(), Game::root);
    QString message;
    switch (error) {
    case NameError::None:
    case NameError::Empty:
        break;
    case NameError::TooShort:
        message = tr("Route name must contain at least two characters.");
        break;
    case NameError::TooLong:
        message = tr("Route name must not exceed 64 characters.");
        break;
    case NameError::InvalidCharacter:
        message = tr("Use only letters, numbers, spaces, hyphens and underscores.");
        break;
    case NameError::Reserved:
        message = tr("This route name is reserved by the operating system.");
        break;
    case NameError::AlreadyExists:
        message = tr("A route with this name already exists.");
        break;
    }
    nameStatus->setText(message.isEmpty() ? QStringLiteral(" ") : message);
}

void NewRouteWindow::createRoute() {
    double startLatitude = 0.0;
    double startLongitude = 0.0;
    if (!startCoordinates(startLatitude, startLongitude)
            || NewRouteValidation::validateName(nameEdit->text(), Game::root)
                    != NewRouteValidation::NameError::None)
        return;

    result = NewRouteSelection();
    result.name = nameEdit->text().trimmed();
    result.startLatitude = startLatitude;
    result.startLongitude = startLongitude;
    result.projectionType = selectedProjection();
    result.countryCode = selectedCountryCode;
    if (result.countryCode.isEmpty())
        result.countryCode = placeIndex.nearestCountry(
                startLatitude, startLongitude);
    if (result.countryCode.isEmpty() && selectedOriginIndex >= 0)
        result.countryCode = projectionPresets.preset(
                selectedOriginIndex).countryCode;

    if (result.projectionType == GeoProjectionType::InterruptedGoodeHomolosine) {
        GeoMstsCoordinateConverter converter;
        IghCoordinate projected;
        PreciseTileCoordinate tile;
        converter.ConvertToInternal(startLatitude, startLongitude, &projected);
        converter.ConvertToTile(&projected, &tile);
        result.routeTileX = tile.TileX;
        result.routeTileZ = tile.TileZ;
        accept();
        return;
    }

    double originLatitude = 0.0;
    double originLongitude = 0.0;
    if (!originCoordinates(originLatitude, originLongitude)) return;
    result.projection.originLatitude = originLatitude;
    result.projection.originLongitude = originLongitude;
    const int projectionTileOffsetX = customOrigin->isChecked()
            ? projectionTileOffsetXEdit->text().toInt() : 0;
    const int projectionTileOffsetZ = customOrigin->isChecked()
            ? projectionTileOffsetZEdit->text().toInt() : 0;
    result.projection.offsetX = 2048.0 * projectionTileOffsetX;
    // TSRE's normalized local Z=0 lies on the upper boundary of its tile.
    result.projection.offsetZ = 2048.0 * (projectionTileOffsetZ + 1);
    if (selectedOriginIndex >= 0)
        result.projection.scaleFactor = projectionPresets.preset(
                selectedOriginIndex).scaleFactor;

    std::unique_ptr<GeoWorldCoordinateConverter> converter(
            GeoWorldCoordinateConverter::Create(result.projectionType,
                                                 &result.projection));
    if (!converter) return;
    IghCoordinate projected;
    PreciseTileCoordinate tile;
    converter->ConvertToInternal(startLatitude, startLongitude, &projected);
    converter->ConvertToTile(&projected, &tile);
    result.routeTileX = tile.TileX;
    result.routeTileZ = tile.TileZ;
    accept();
}

void NewRouteWindow::showAssetStatus(const QStringList &errors) {
    if (errors.isEmpty()) return;
    //% "Some geographic presets could not be loaded. Manual coordinates remain available. %1"
    QString message = tr("Some geographic presets could not be loaded. "
                         "Manual coordinates remain available. %1")
            .arg(errors.join(QStringLiteral("\n")));
    if (assetStatus->isVisible() && !assetStatus->text().isEmpty())
        message = assetStatus->text() + QStringLiteral("\n") + message;
    assetStatus->setText(message);
    assetStatus->setVisible(true);
}
