/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <routeEditor/EnvironmentTools.h>
#include <routeEditor/EnvironmentWindow.h>
#include <routeEditor/RouteEditorGLWidget.h>
#include <settings/SettingsManager.h>
#include <tsre/environment/Weather.h>
#include <tsre/environment/WeatherPresets.h>
#include <tsre/gui/GuiFunct.h>

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

namespace {

QString presetTitle(const QString &name) {
    if (name == "clear")
        //% "Clear"
        return qtTrId("route.editor.environment.tools.preset.clear");
    if (name == "fair_cumulus")
        //% "Fair cumulus"
        return qtTrId("route.editor.environment.tools.preset.fair.cumulus");
    if (name == "scattered")
        //% "Scattered clouds"
        return qtTrId("route.editor.environment.tools.preset.scattered");
    if (name == "broken")
        //% "Broken clouds"
        return qtTrId("route.editor.environment.tools.preset.broken");
    if (name == "overcast")
        //% "Overcast"
        return qtTrId("route.editor.environment.tools.preset.overcast");
    if (name == "rain")
        //% "Rain"
        return qtTrId("route.editor.environment.tools.preset.rain");
    if (name == "snow")
        //% "Snow"
        return qtTrId("route.editor.environment.tools.preset.snow");
    if (name == "thunderstorm")
        //% "Thunderstorm"
        return qtTrId("route.editor.environment.tools.preset.thunderstorm");
    if (name == "front")
        //% "Passing front"
        return qtTrId("route.editor.environment.tools.preset.front");
    return name;
}

// The cloud type's name nearest to a type value.
QString typeName(int layer, float type) {
    QString best;
    float distance = 2.0f;
    for (const QString &name : Weather::typeNames(layer)) {
        const float d = std::abs(Weather::typeValue(name) - type);
        if (d < distance) {
            distance = d;
            best = name;
        }
    }
    return best;
}

} // namespace

EnvironmentTools::EnvironmentTools(RouteEditorGLWidget *view, EnvironmentWindow *session, QWidget *parent)
    : QWidget(parent), view(view) {
    constexpr int LabelWidth = 80;
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);
    auto *content = new QWidget(scroll);
    scroll->setWidget(content);
    auto *layout = new QVBoxLayout(content);
    layout->setSpacing(2);
    layout->setContentsMargins(1, 1, 1, 1);
    auto grid = [&]() {
        auto *g = new QGridLayout;
        g->setSpacing(2);
        g->setContentsMargins(3, 0, 1, 0);
        g->setColumnStretch(1, 1);
        layout->addLayout(g);
        return g;
    };

    // The env file.
    layout->addWidget(GuiFunct::newTQLabel(
        //% "Environment File:"
        qtTrId("route.editor.environment.tools.group.file")));
    QGridLayout *fileGrid = grid();
    file = new QComboBox(content);
    file->setObjectName("environmentFile");
    file->setToolTip(
        //% "The env file the editor shows: a season and weather of the route (TRK), or any file in ENVFILES."
        qtTrId("route.editor.environment.tools.file.tooltip"));
    fileGrid->addWidget(file, 0, 0, 1, 2);
    reloadButton = new QPushButton(
        //% "Reload"
        qtTrId("route.editor.environment.tools.reload"), content);
    saveButton = new QPushButton(
        //% "Save"
        qtTrId("route.editor.environment.tools.save"), content);
    saveButton->setToolTip(
        //% "Write the weather into the env file."
        qtTrId("route.editor.environment.tools.save.tooltip"));
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    buttons->addWidget(reloadButton);
    buttons->addWidget(saveButton);
    fileGrid->addLayout(buttons, 1, 0, 1, 2);
    connect(file, QOverload<int>::of(&QComboBox::activated), this, &EnvironmentTools::chooseFile);
    connect(reloadButton, &QPushButton::clicked, this, [this] { chooseFile(file->currentIndex()); });
    connect(saveButton, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this,
                //% "Save Weather"
                qtTrId("route.editor.environment.tools.save.title"),
                //% "Write the weather into %1? Other programs reading the file keep its MSTS contents."
                qtTrId("route.editor.environment.tools.save.question").arg(this->view->environmentFile()))
                != QMessageBox::Yes)
            return;
        QString error;
        if (!this->view->saveEnvironment(&error))
            QMessageBox::warning(this, qtTrId("route.editor.environment.tools.save.title"), error);
    });

    // The weather.
    layout->addWidget(GuiFunct::newTQLabel(
        //% "Weather:"
        qtTrId("route.editor.environment.tools.group.weather")));
    QGridLayout *weatherGrid = grid();
    preset = new QComboBox(content);
    preset->setObjectName("weatherPreset");
    //% "From the file"
    preset->addItem(qtTrId("route.editor.environment.tools.preset.file"), QString());
    for (const QString &name : WeatherPresets::names())
        preset->addItem(presetTitle(name), name);
    weatherGrid->addWidget(GuiFunct::newQLabel(
        //% "Preset:"
        qtTrId("route.editor.environment.tools.preset"), LabelWidth), 0, 0);
    weatherGrid->addWidget(preset, 0, 1);
    seed = new QSpinBox(content);
    seed->setObjectName("weatherSeed");
    seed->setRange(1, 999999);
    seed->setToolTip(
        //% "Another number places the clouds differently."
        qtTrId("route.editor.environment.tools.seed.tooltip"));
    weatherGrid->addWidget(GuiFunct::newQLabel(
        //% "Seed:"
        qtTrId("route.editor.environment.tools.seed"), LabelWidth), 1, 0);
    weatherGrid->addWidget(seed, 1, 1);
    onMap = new QCheckBox(
        //% "Show on the map"
        qtTrId("route.editor.environment.tools.on.map"), content);
    onMap->setObjectName("weatherOnMap");
    weatherGrid->addWidget(onMap, 2, 0, 1, 2);
    info = new QLabel(content);
    info->setWordWrap(true);
    info->setTextFormat(Qt::PlainText);
    info->setContentsMargins(0, 2, 0, 2);
    weatherGrid->addWidget(info, 3, 0, 1, 2);
    connect(preset, QOverload<int>::of(&QComboBox::activated), this, [this](int) { choosePreset(); });
    connect(seed, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        if (!preset->currentData().toString().isEmpty())
            choosePreset();
    });
    connect(onMap, &QCheckBox::toggled, this, &EnvironmentTools::weatherOnMapToggled);

    // The editor session: the former environment window, as it was.
    layout->addWidget(GuiFunct::newTQLabel(
        //% "Editor Session:"
        qtTrId("route.editor.environment.tools.group.session")));
    session->setParent(content);
    session->setWindowFlags(Qt::Widget);
    session->setMinimumWidth(0);
    session->setMaximumWidth(QWIDGETSIZE_MAX);
    layout->addWidget(session);
    session->show();
    layout->addStretch(1);
    setFixedWidth(260);

    connect(view, &RouteEditorGLWidget::weatherChanged, this, &EnvironmentTools::refresh);
    connect(&SettingsManager::instance(), &SettingsManager::runtimeSettingsChanged, this, &EnvironmentTools::refresh);
    connect(view, &RouteEditorGLWidget::routeLoaded, this, [this](Route *) { reloadFiles(); });
    reloadFiles();
}

void EnvironmentTools::setWeatherOnMap(bool shown) {
    const bool blocked = onMap->blockSignals(true);
    onMap->setChecked(shown);
    onMap->blockSignals(blocked);
}

void EnvironmentTools::reloadFiles() {
    const bool blocked = file->blockSignals(true);
    file->clear();
    int current = -1;
    for (const RouteEditorGLWidget::EnvironmentSlot &slot : view->environmentSlots()) {
        file->addItem(slot.name + ": " + slot.file, QStringList{slot.file, slot.name});
        if (slot.name == view->environmentSlot() && slot.file.compare(view->environmentFile(), Qt::CaseInsensitive) == 0)
            current = file->count() - 1;
    }
    if (file->count() > 0)
        file->insertSeparator(file->count());
    for (const QString &name : view->environmentFiles()) {
        file->addItem(name, QStringList{name, QString()});
        if (current < 0 && view->environmentSlot().isEmpty()
                && name.compare(view->environmentFile(), Qt::CaseInsensitive) == 0)
            current = file->count() - 1;
    }
    file->setCurrentIndex(current);
    file->blockSignals(blocked);
    refresh();
}

void EnvironmentTools::chooseFile(int index) {
    const QStringList data = file->itemData(index).toStringList();
    if (data.size() < 2)
        return;
    QString error;
    if (!view->chooseEnvironment(data[0], data[1], &error))
        QMessageBox::warning(this,
            //% "Environment"
            qtTrId("route.editor.environment.tools.title"), error);
}

void EnvironmentTools::choosePreset() {
    view->setWeatherPreset(preset->currentData().toString(), quint32(seed->value()));
}

void EnvironmentTools::refresh() {
    const Weather *weather = view->currentWeather();
    saveButton->setEnabled(weather != nullptr);
    reloadButton->setEnabled(file->currentIndex() >= 0);
    if (weather == nullptr) {
        info->clear();
        return;
    }
    // The preset shown: the chosen one while the weather is not the file's.
    const QString shownPreset = view->environmentModified() ? weather->preset : QString();
    const bool blocked = preset->blockSignals(true);
    preset->setCurrentIndex(std::max(0, preset->findData(shownPreset)));
    preset->blockSignals(blocked);
    if (view->environmentModified()) {
        const bool seedBlocked = seed->blockSignals(true);
        seed->setValue(int(weather->seed));
        seed->blockSignals(seedBlocked);
    }

    QStringList lines;
    QString source = weather->source == "derived"
            //% "Derived from the env file (%1)"
            ? qtTrId("route.editor.environment.tools.source.derived").arg(presetTitle(weather->preset))
            : view->environmentModified()
            //% "Preset, not saved"
            ? qtTrId("route.editor.environment.tools.source.unsaved")
            //% "The env file's weather (%1)"
            : qtTrId("route.editor.environment.tools.source.file").arg(weather->source);
    lines << source;
    const double hours = view->weatherHours();
    //% "%1 keyframes over %2 h; now %3 h"
    lines << qtTrId("route.editor.environment.tools.keyframes").arg(weather->keyframes.size())
                 .arg(weather->duration(), 0, 'f', 1).arg(hours, 0, 'f', 1);
    if (!weather->keyframes.isEmpty()) {
        const Weather::Blend b = weather->blend(hours);
        const WeatherKeyframe &ka = weather->keyframes[b.a], &kb = weather->keyframes[b.b];
        const float w = float(b.weight);
        auto mix = [w](float x, float y) { return x + (y - x) * w; };
        const QString names[WeatherLayer::Count] = {
            //% "Low"
            qtTrId("route.editor.environment.tools.layer.low"),
            //% "Mid"
            qtTrId("route.editor.environment.tools.layer.mid"),
            //% "High"
            qtTrId("route.editor.environment.tools.layer.high")};
        for (int l = 0; l < WeatherLayer::Count; ++l) {
            const WeatherLayerValues &a = ka.layers[l], &c = kb.layers[l];
            const float coverage = mix(a.coverage, c.coverage);
            if (coverage <= 0.0f) {
                //% "%1: none"
                lines << qtTrId("route.editor.environment.tools.layer.none").arg(names[l]);
                continue;
            }
            //% "%1: %2 %3%, %4-%5 m"
            lines << qtTrId("route.editor.environment.tools.layer.values").arg(names[l])
                         .arg(typeName(l, mix(a.type, c.type))).arg(int(std::lround(coverage * 100)))
                         .arg(int(std::lround(mix(a.base, c.base)))).arg(int(std::lround(mix(a.top, c.top))));
        }
        const WeatherWind wind = weather->wind(hours, WeatherLayer::Low);
        //% "Wind (low): from %1°, %2 m/s"
        lines << qtTrId("route.editor.environment.tools.wind").arg(int(std::lround(wind.direction)))
                     .arg(wind.speed, 0, 'f', 1);
        const WeatherKeyframe &near = w < 0.5f ? ka : kb;
        const float rate = mix(ka.precipitationRate, kb.precipitationRate);
        if (near.precipitation == WeatherPrecipitation::None || rate <= 0.0f)
            //% "Precipitation: none"
            lines << qtTrId("route.editor.environment.tools.precipitation.none");
        else
            //% "Precipitation: %1, %2 mm/h"
            lines << qtTrId("route.editor.environment.tools.precipitation").arg(
                         near.precipitation == WeatherPrecipitation::Snow
                         //% "snow"
                         ? qtTrId("route.editor.environment.tools.snow")
                         //% "rain"
                         : qtTrId("route.editor.environment.tools.rain")).arg(rate, 0, 'f', 1);
        //% "Visibility: %1 km"
        lines << qtTrId("route.editor.environment.tools.visibility")
                     .arg(mix(ka.visibility, kb.visibility) / 1000.0, 0, 'f', 1);
    }
    info->setText(lines.join('\n'));
}
