/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <routeEditor/WeatherImportDialog.h>
#include <routeEditor/RouteEditorGLWidget.h>
#include <tsre/Game.h>
#include <tsre/environment/WeatherImport.h>

#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>
#include <tsre/gui/UnitSpinBox.h>
#include <tsre/units/Units.h>

WeatherImportDialog::WeatherImportDialog(RouteEditorGLWidget *view, QWidget *parent)
    : QDialog(parent), view(view) {
    setWindowTitle(
        //% "Import Weather"
        qtTrId("route.editor.weather.import.title"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    date = new QDateEdit(this);
    date->setDisplayFormat("yyyy-MM-dd");
    date->setCalendarPopup(true);
    const QDate editorDate = QDate::fromString(Game::timeOfDayDate.trimmed(), "yyyy-MM-dd");
    // Past days from the archives, coming ones from the forecasts.
    date->setMaximumDate(QDate::currentDate().addDays(14));
    date->setDate(editorDate.isValid() && editorDate <= date->maximumDate() ? editorDate
                                                                            : date->maximumDate());
    form->addRow(
        //% "Day:"
        qtTrId("route.editor.weather.import.day"), date);
    startHour = new QSpinBox(this);
    startHour->setRange(0, 23);
    startHour->setValue(6);
    startHour->setSuffix(":00 UTC");
    form->addRow(
        //% "Start:"
        qtTrId("route.editor.weather.import.start"), startHour);
    hours = new QSpinBox(this);
    hours->setRange(1, 72);
    hours->setValue(12);
    hours->setSuffix(" h");
    form->addRow(
        //% "Length:"
        qtTrId("route.editor.weather.import.length"), hours);
    model = new QComboBox(this);
    //% "Automatic (the finest with data here)"
    model->addItem(qtTrId("route.editor.weather.import.model.automatic"), QString::fromLatin1(WeatherImport::Automatic));
    for (const WeatherImport::Model &m : WeatherImport::models())
        model->addItem(m.title, m.id);
    form->addRow(
        //% "Model:"
        qtTrId("route.editor.weather.import.model"), model);
    spacing = new QComboBox(this);
    //% "Model's grid, 4 km at least"
    spacing->addItem(qtTrId("route.editor.weather.import.spacing.model"), 0.0);
    for (int km : {2, 4, 8, 16})
        spacing->addItem(Units::format(km * 1000.0, Units::Quantity::Distance, 1), km * 1000.0);
    spacing->setToolTip(
        //% "Points sampled from the model; each counts towards Open-Meteo's free 10000 a day."
        qtTrId("route.editor.weather.import.spacing.tooltip"));
    form->addRow(
        //% "Samples:"
        qtTrId("route.editor.weather.import.spacing"), spacing);
    margin = new UnitSpinBox(Units::Quantity::Distance, this);
    margin->setDecimals(0);
    margin->setRangeSi(0, 300000);
    margin->setSi(30000);
    margin->setToolTip(
        //% "Around the route's track: clouds an hour's wind away still drift over it."
        qtTrId("route.editor.weather.import.margin.tooltip"));
    form->addRow(
        //% "Margin:"
        qtTrId("route.editor.weather.import.margin"), margin);
    cellSize = new QComboBox(this);
    for (int size : {2048, 4096, 8192, 16384})
        cellSize->addItem(QString("%1 m").arg(size), size);
    cellSize->setCurrentIndex(1);
    form->addRow(
        //% "Cells:"
        qtTrId("route.editor.weather.import.cells"), cellSize);
    summary = new QLabel(this);
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto *note = new QLabel(
        //% "Data: Open-Meteo.com (CC BY 4.0) and the model's maker (Meteo-France, UK Met Office, DMI, DWD, KNMI, ECMWF, Copernicus). The weather keeps the attribution."
        qtTrId("route.editor.weather.import.attribution.models"), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton *importButton = buttons->addButton(
        //% "Import"
        qtTrId("route.editor.weather.import.button"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(importButton, &QPushButton::clicked, this, &WeatherImportDialog::import);
    for (QSpinBox *box : {startHour, hours})
        connect(box, QOverload<int>::of(&QSpinBox::valueChanged), this, &WeatherImportDialog::updateSummary);
    connect(margin, &UnitSpinBox::siChanged, this, &WeatherImportDialog::updateSummary);
    connect(Units::Notifier::instance(), &Units::Notifier::changed, this, &WeatherImportDialog::updateSummary);
    connect(model, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WeatherImportDialog::updateSummary);
    connect(cellSize, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WeatherImportDialog::updateSummary);
    connect(spacing, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WeatherImportDialog::updateSummary);
    connect(date, &QDateEdit::dateChanged, this, &WeatherImportDialog::updateSummary);
    updateSummary();
}

bool WeatherImportDialog::request(WeatherImport::Request &out, QString *error) const {
    out.model = model->currentData().toString();
    out.spacing = spacing->currentData().toDouble();
    out.date = date->date();
    out.startHour = startHour->value();
    out.hours = hours->value();
    out.cellSize = cellSize->currentData().toInt();
    double west, north, east, south;
    if (!view->routeArea(west, north, east, south)) {
        // No track: the camera's surroundings.
        double e, n;
        view->cameraWorldPosition(e, n);
        west = e - 10000.0;
        east = e + 10000.0;
        south = n - 10000.0;
        north = n + 10000.0;
    }
    const double m = margin->si();
    out.west = west - m;
    out.east = east + m;
    out.south = south - m;
    out.north = north + m;
    return true;
}

void WeatherImportDialog::updateSummary() {
    WeatherImport::Request r;
    QString error;
    if (!request(r, &error)) {
        summary->setText(error);
        return;
    }
    // Automatic: the regional models' 4 km until a model is chosen.
    WeatherImport::Request sized = r;
    if (sized.model == QLatin1String(WeatherImport::Automatic))
        sized.model = WeatherImport::models().front().id;
    const double step = WeatherImport::spacing(sized);
    const int points = (int(std::ceil((r.east - r.west) / step)) + 1)
            * (int(std::ceil((r.north - r.south) / step)) + 1);
    //% "Area %1 x %2, %3 sample points, %4 keyframes."
    summary->setText(qtTrId("route.editor.weather.import.summary.area")
                     .arg(Units::formatNumber(r.east - r.west, Units::Quantity::Distance, 0))
                     .arg(Units::format(r.north - r.south, Units::Quantity::Distance, 0))
                     .arg(points).arg(r.hours + 1));
}

void WeatherImportDialog::import() {
    WeatherImport::Request r;
    QString error;
    if (!request(r, &error)) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }
    QProgressDialog progress(
        //% "Downloading the weather..."
        qtTrId("route.editor.weather.import.progress"),
        //% "Cancel"
        qtTrId("route.editor.weather.import.cancel"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.show();
    QApplication::processEvents();
    const bool ok = view->importWeather(r, &error, [&progress] { return progress.wasCanceled(); });
    progress.close();
    if (!ok) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }
    accept();
}
