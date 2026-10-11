/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <routeEditor/WeatherGenerateDialog.h>
#include <routeEditor/RouteEditorGLWidget.h>
#include <tsre/environment/WeatherGenerator.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

namespace {

// The compass point nearest a heading.
QString compassPoint(int degrees) {
    static const char *points[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return points[((degrees % 360 + 360) % 360 + 22) / 45 % 8];
}

}

WeatherGenerateDialog::WeatherGenerateDialog(RouteEditorGLWidget *view, QWidget *parent)
    : QDialog(parent), view(view) {
    setWindowTitle(
        //% "Generate Weather"
        qtTrId("route.editor.weather.generate.title"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    scenario = new QComboBox(this);
    scenario->addItem(
        //% "Cold front: showers and storms on a narrow band, clearing behind"
        qtTrId("route.editor.weather.generate.cold.front"), int(WeatherGenerator::ColdFront));
    scenario->addItem(
        //% "Warm front: cloud thickening for hundreds of km, then steady rain"
        qtTrId("route.editor.weather.generate.warm.front"), int(WeatherGenerator::WarmFront));
    form->addRow(
        //% "Scenario:"
        qtTrId("route.editor.weather.generate.scenario"), scenario);
    heading = new QSpinBox(this);
    heading->setRange(0, 359);
    heading->setWrapping(true);
    heading->setSingleStep(15);
    heading->setValue(70);
    heading->setSuffix(QString::fromUtf8("°"));
    heading->setToolTip(
        //% "The direction the front moves towards, on the route's grid (0 north, 90 east). In Europe fronts mostly come from the west."
        qtTrId("route.editor.weather.generate.heading.tooltip"));
    form->addRow(
        //% "Moving towards:"
        qtTrId("route.editor.weather.generate.heading"), heading);
    speed = new QSpinBox(this);
    speed->setRange(5, 90);
    speed->setValue(35);
    speed->setSuffix(" km/h");
    form->addRow(
        //% "Speed:"
        qtTrId("route.editor.weather.generate.speed"), speed);
    arrival = new QDoubleSpinBox(this);
    arrival->setRange(0.0, 23.5);
    arrival->setSingleStep(0.5);
    arrival->setDecimals(1);
    arrival->setValue(14.0);
    arrival->setSuffix(" h");
    arrival->setToolTip(
        //% "The hour the front's line crosses the middle of the route's area. The day repeats."
        qtTrId("route.editor.weather.generate.arrival.tooltip"));
    form->addRow(
        //% "Over the route at:"
        qtTrId("route.editor.weather.generate.arrival"), arrival);
    seed = new QSpinBox(this);
    seed->setRange(1, 99999);
    seed->setValue(1);
    form->addRow(
        //% "Seed:"
        qtTrId("route.editor.weather.generate.seed"), seed);
    margin = new QSpinBox(this);
    margin->setRange(0, 300);
    margin->setValue(40);
    margin->setSuffix(" km");
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
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton *generateButton = buttons->addButton(
        //% "Generate"
        qtTrId("route.editor.weather.generate.button"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(generateButton, &QPushButton::clicked, this, &WeatherGenerateDialog::generate);
    for (QSpinBox *box : {heading, speed, margin})
        connect(box, QOverload<int>::of(&QSpinBox::valueChanged), this, &WeatherGenerateDialog::updateSummary);
    connect(arrival, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &WeatherGenerateDialog::updateSummary);
    connect(scenario, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &WeatherGenerateDialog::updateSummary);
    connect(cellSize, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &WeatherGenerateDialog::updateSummary);
    updateSummary();
}

void WeatherGenerateDialog::settings(WeatherGenerator::Settings &out) const {
    out.scenario = WeatherGenerator::Scenario(scenario->currentData().toInt());
    out.heading = heading->value();
    out.speed = speed->value();
    out.arrival = arrival->value();
    out.seed = quint32(seed->value());
    out.cellSize = cellSize->currentData().toInt();
    double west, north, east, south;
    if (!view->routeArea(west, north, east, south)) {
        double e, n;
        view->cameraWorldPosition(e, n);
        west = e - 10000.0;
        east = e + 10000.0;
        south = n - 10000.0;
        north = n + 10000.0;
    }
    const double m = margin->value() * 1000.0;
    out.west = west - m;
    out.east = east + m;
    out.south = south - m;
    out.north = north + m;
}

void WeatherGenerateDialog::updateSummary() {
    WeatherGenerator::Settings s;
    settings(s);
    const double width = (s.east - s.west) / 1000.0, height = (s.north - s.south) / 1000.0;
    // How long the line takes to cross the area along its motion.
    const double h = s.heading * M_PI / 180.0;
    const double across = std::abs(width * std::sin(h)) + std::abs(height * std::cos(h));
    //% "Area %1 x %2 km. The front comes from the %3 and moves towards the %4; its line takes about %5 h to cross the area."
    summary->setText(qtTrId("route.editor.weather.generate.summary")
                     .arg(width, 0, 'f', 0).arg(height, 0, 'f', 0)
                     .arg(compassPoint(int(s.heading) + 180)).arg(compassPoint(int(s.heading)))
                     .arg(across / std::max(1.0, s.speed), 0, 'f', 1));
}

void WeatherGenerateDialog::generate() {
    WeatherGenerator::Settings s;
    settings(s);
    if (view->generateWeather(s))
        accept();
}
