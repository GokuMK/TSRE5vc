/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#include <routeEditor/EnvironmentWindow.h>
#include <settings/SettingsManager.h>
#include <QCheckBox>
#include <QColorDialog>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QTimeEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace {
const char *const TimeEnabled = "core.rendering.timeOfDay.enabled";
const char *const Time = "core.rendering.timeOfDay.time";
const char *const Date = "core.rendering.timeOfDay.date";
const char *const SkyColor = "core.rendering.skyColor";
const char *const FogColor = "core.rendering.fogColor";
const char *const FogDensity = "core.rendering.fogDensity";
const char *const LocalLights = "core.rendering.localLights.enabled";
const char *const Bloom = "core.rendering.bloom";
const char *const Exposure = "core.rendering.exposure";
const char *const Sun = "core.rendering.sky.sun";
const char *const Moon = "core.rendering.sky.moon";
const char *const SunSize = "core.rendering.sky.sunSize";

SettingsManager &settings() {
    return SettingsManager::instance();
}

// Sets a widget's value without it reporting the change back.
template <typename Widget, typename Class, typename Argument, typename Value>
void quietly(Widget *widget, void (Class::*setter)(Argument), const Value &value) {
    const bool blocked = widget->blockSignals(true);
    (widget->*setter)(value);
    widget->blockSignals(blocked);
}

QTime timeOfHours(double hours) {
    const int minutes = std::clamp(int(std::lround(hours * 60.0)), 0, 24 * 60 - 1);
    return QTime(minutes / 60, minutes % 60);
}
}

QStringList EnvironmentWindow::keys() {
    QStringList list;
    for (const char *key : {TimeEnabled, Time, Date, SkyColor, FogColor, FogDensity, LocalLights, Bloom,
                            Exposure, Sun, Moon, SunSize})
        list << QString::fromLatin1(key);
    return list;
}

EnvironmentWindow::EnvironmentWindow(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::WindowType::Tool);
    setWindowTitle(
        //% "Environment"
        qtTrId("route.editor.environment.window.title"));

    auto group = [&](const QString &title, QGridLayout *&grid) {
        auto *box = new QGroupBox(title, this);
        grid = new QGridLayout(box);
        grid->setColumnStretch(1, 1);
        return box;
    };
    // The editor of each setting is named after its key (tests find them).
    auto row = [&](QGridLayout *grid, const char *key, const QString &text, QWidget *editor) {
        const int r = grid->rowCount();
        if (editor->objectName().isEmpty())
            editor->setObjectName(QString::fromLatin1(key));
        Control &control = controls[QString::fromLatin1(key)];
        control.label = new QLabel(text, this);
        control.reset = resetButtonFor(QString::fromLatin1(key));
        grid->addWidget(control.label, r, 0);
        grid->addWidget(editor, r, 1);
        grid->addWidget(control.reset, r, 2);
        return &control;
    };
    auto layoutOf = [&](std::initializer_list<QWidget *> widgets) {
        auto *holder = new QWidget(this);
        // Not an editor: its widgets carry the names.
        holder->setObjectName("line");
        auto *line = new QHBoxLayout(holder);
        line->setContentsMargins(0, 0, 0, 0);
        for (QWidget *w : widgets)
            line->addWidget(w);
        return holder;
    };

    // Time of day.
    QGridLayout *timeGrid;
    QGroupBox *timeBox = group(
        //% "Time"
        qtTrId("route.editor.environment.window.group.time"), timeGrid);
    timeEnabled = new QCheckBox(this);
    row(timeGrid, TimeEnabled,
        //% "Time of day"
        qtTrId("route.editor.environment.window.time.of.day"), timeEnabled)->load = [this] {
        quietly(timeEnabled, &QCheckBox::setChecked, settings().runtimeBool(TimeEnabled));
    };
    connect(timeEnabled, &QCheckBox::toggled, this, [this](bool on) { set(TimeEnabled, on); });
    timeSlider = new QSlider(Qt::Horizontal, this);
    timeSlider->setRange(0, 24 * 60 - 1);
    timeSlider->setSingleStep(5);
    timeSlider->setPageStep(60);
    timeEdit = new QTimeEdit(this);
    timeEdit->setObjectName(QString::fromLatin1(Time));
    timeEdit->setDisplayFormat("HH:mm");
    row(timeGrid, Time,
        //% "Solar time"
        qtTrId("route.editor.environment.window.time"), layoutOf({timeSlider, timeEdit}))->load = [this] {
        const QTime time = timeOfHours(settings().runtimeFloat(Time));
        quietly(timeSlider, &QSlider::setValue, time.hour() * 60 + time.minute());
        quietly(timeEdit, &QTimeEdit::setTime, time);
    };
    connect(timeSlider, &QSlider::valueChanged, this, [this](int minutes) { set(Time, minutes / 60.0); });
    connect(timeEdit, &QTimeEdit::timeChanged, this,
            [this](QTime time) { set(Time, time.hour() + time.minute() / 60.0); });
    dateEdit = new QDateEdit(this);
    dateEdit->setObjectName(QString::fromLatin1(Date));
    dateEdit->setDisplayFormat("yyyy-MM-dd");
    dateEdit->setCalendarPopup(true);
    auto *today = new QPushButton(
        //% "Today"
        qtTrId("route.editor.environment.window.today"), this);
    row(timeGrid, Date,
        //% "Date"
        qtTrId("route.editor.environment.window.date"), layoutOf({dateEdit, today}))->load = [this] {
        const QDate date = QDate::fromString(settings().runtimeString(Date).trimmed(), "yyyy-MM-dd");
        quietly(dateEdit, &QDateEdit::setDate, date.isValid() ? date : QDate(2026, 6, 21));
    };
    connect(dateEdit, &QDateEdit::dateChanged, this,
            [this](QDate date) { set(Date, date.toString("yyyy-MM-dd")); });
    connect(today, &QPushButton::clicked, this, [this] { dateEdit->setDate(QDate::currentDate()); });
    readout = new QLabel(this);
    readout->setWordWrap(true);
    timeGrid->addWidget(readout, timeGrid->rowCount(), 0, 1, 3);

    // Sky and fog.
    QGridLayout *skyGrid;
    QGroupBox *skyBox = group(
        //% "Sky and fog"
        qtTrId("route.editor.environment.window.group.sky"), skyGrid);
    skyButton = new QPushButton(this);
    row(skyGrid, SkyColor,
        //% "Sky colour"
        qtTrId("route.editor.environment.window.sky.colour"), skyButton)->load = [this] {
        showColour(skyButton, settings().runtimeString(SkyColor));
    };
    connect(skyButton, &QPushButton::clicked, this, [this] { chooseColour(SkyColor, skyButton); });
    fogButton = new QPushButton(this);
    row(skyGrid, FogColor,
        //% "Fog colour"
        qtTrId("route.editor.environment.window.fog.colour"), fogButton)->load = [this] {
        showColour(fogButton, settings().runtimeString(FogColor));
    };
    connect(fogButton, &QPushButton::clicked, this, [this] { chooseColour(FogColor, fogButton); });
    fogSlider = new QSlider(Qt::Horizontal, this);
    fogSlider->setRange(0, 100);
    fogSpin = new QDoubleSpinBox(this);
    fogSpin->setObjectName(QString::fromLatin1(FogDensity));
    fogSpin->setRange(0.0, 1.0);
    fogSpin->setSingleStep(0.01);
    row(skyGrid, FogDensity,
        //% "Fog density"
        qtTrId("route.editor.environment.window.fog.density"), layoutOf({fogSlider, fogSpin}))->load = [this] {
        const double density = settings().runtimeFloat(FogDensity);
        quietly(fogSlider, &QSlider::setValue, int(std::lround(density * 100.0)));
        quietly(fogSpin, &QDoubleSpinBox::setValue, density);
    };
    connect(fogSlider, &QSlider::valueChanged, this, [this](int value) { set(FogDensity, value / 100.0); });
    connect(fogSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) { set(FogDensity, value); });

    // Lights.
    QGridLayout *lightGrid;
    QGroupBox *lightBox = group(
        //% "Lights"
        qtTrId("route.editor.environment.window.group.lights"), lightGrid);
    localLights = new QCheckBox(this);
    row(lightGrid, LocalLights,
        //% "Local lights"
        qtTrId("route.editor.environment.window.local.lights"), localLights)->load = [this] {
        quietly(localLights, &QCheckBox::setChecked, settings().runtimeBool(LocalLights));
    };
    connect(localLights, &QCheckBox::toggled, this, [this](bool on) { set(LocalLights, on); });
    bloom = new QDoubleSpinBox(this);
    bloom->setRange(0.0, 4.0);
    bloom->setSingleStep(0.25);
    row(lightGrid, Bloom,
        //% "Bloom"
        qtTrId("route.editor.environment.window.bloom"), bloom)->load = [this] {
        quietly(bloom, &QDoubleSpinBox::setValue, settings().runtimeFloat(Bloom));
    };
    connect(bloom, &QDoubleSpinBox::valueChanged, this, [this](double value) { set(Bloom, value); });
    exposure = new QDoubleSpinBox(this);
    exposure->setRange(-4.0, 4.0);
    exposure->setSingleStep(0.25);
    exposure->setSuffix(" EV");
    row(lightGrid, Exposure,
        //% "Exposure"
        qtTrId("route.editor.environment.window.exposure"), exposure)->load = [this] {
        quietly(exposure, &QDoubleSpinBox::setValue, settings().runtimeFloat(Exposure));
    };
    connect(exposure, &QDoubleSpinBox::valueChanged, this, [this](double value) { set(Exposure, value); });

    // Sun and moon.
    QGridLayout *skyBodyGrid;
    QGroupBox *skyBodyBox = group(
        //% "Sun and moon"
        qtTrId("route.editor.environment.window.group.sun.moon"), skyBodyGrid);
    sun = new QCheckBox(this);
    row(skyBodyGrid, Sun,
        //% "Sun"
        qtTrId("route.editor.environment.window.sun"), sun)->load = [this] {
        quietly(sun, &QCheckBox::setChecked, settings().runtimeBool(Sun));
    };
    connect(sun, &QCheckBox::toggled, this, [this](bool on) { set(Sun, on); });
    moon = new QCheckBox(this);
    row(skyBodyGrid, Moon,
        //% "Moon"
        qtTrId("route.editor.environment.window.moon"), moon)->load = [this] {
        quietly(moon, &QCheckBox::setChecked, settings().runtimeBool(Moon));
    };
    connect(moon, &QCheckBox::toggled, this, [this](bool on) { set(Moon, on); });
    sunSize = new QDoubleSpinBox(this);
    sunSize->setRange(0.2, 10.0);
    sunSize->setSingleStep(0.1);
    sunSize->setSuffix(QStringLiteral("°"));
    row(skyBodyGrid, SunSize,
        //% "Size"
        qtTrId("route.editor.environment.window.sun.size"), sunSize)->load = [this] {
        quietly(sunSize, &QDoubleSpinBox::setValue, settings().runtimeFloat(SunSize));
    };
    connect(sunSize, &QDoubleSpinBox::valueChanged, this, [this](double value) { set(SunSize, value); });

    resetButton = new QPushButton(
        //% "Reset all"
        qtTrId("route.editor.environment.window.reset.all"), this);
    resetButton->setToolTip(
        //% "Go back to the profile's values."
        qtTrId("route.editor.environment.window.reset.all.tooltip"));
    connect(resetButton, &QPushButton::clicked, this, &EnvironmentWindow::resetAll);
    saveButton = new QPushButton(
        //% "Save to profile"
        qtTrId("route.editor.environment.window.save"), this);
    saveButton->setToolTip(
        //% "Keep the current values in the profile's settings."
        qtTrId("route.editor.environment.window.save.tooltip"));
    connect(saveButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!saveToProfile(&error))
            QMessageBox::warning(this,
                //% "Environment"
                qtTrId("route.editor.environment.window.title"), error);
    });

    auto *layout = new QVBoxLayout(this);
    for (QWidget *box : {static_cast<QWidget *>(timeBox), static_cast<QWidget *>(skyBox),
                         static_cast<QWidget *>(lightBox), static_cast<QWidget *>(skyBodyBox)})
        layout->addWidget(box);
    layout->addWidget(layoutOf({resetButton, saveButton}));
    layout->addStretch(1);
    setMinimumWidth(360);

    connect(&settings(), &SettingsManager::runtimeSettingsChanged, this, &EnvironmentWindow::refresh);
    refresh(keys());
    environmentInfo(std::nanf(""), std::nanf(""), std::nanf(""), std::nanf(""));
}

QToolButton *EnvironmentWindow::resetButtonFor(const QString &key) {
    auto *button = new QToolButton(this);
    button->setObjectName("reset:" + key);
    button->setText(QStringLiteral("↺"));
    button->setToolTip(
        //% "Back to the profile's value"
        qtTrId("route.editor.environment.window.reset.one"));
    connect(button, &QToolButton::clicked, this, [key] { settings().clearSessionValue(key); });
    return button;
}

void EnvironmentWindow::set(const QString &key, const QVariant &value) {
    QString error;
    if (!settings().setSessionValue(key, value, &error))
        qWarning() << "Environment window:" << error;
}

void EnvironmentWindow::refresh(const QStringList &changedKeys) {
    for (const QString &key : changedKeys) {
        const auto control = controls.constFind(key);
        if (control != controls.constEnd() && control->load)
            control->load();
    }
    refreshMarks();
}

// Values that differ from the profile (set here for the session) are bold
// and can be reset.
void EnvironmentWindow::refreshMarks() {
    bool any = false;
    for (auto it = controls.begin(); it != controls.end(); ++it) {
        const bool session = settings().runtimeValueSource(it.key()) == SettingsManager::ForcedSession;
        any = any || session;
        QFont font = it->label->font();
        font.setBold(session);
        it->label->setFont(font);
        it->reset->setEnabled(session);
    }
    resetButton->setEnabled(any);
    saveButton->setEnabled(any);
}

void EnvironmentWindow::resetAll() {
    for (const QString &key : keys())
        settings().clearSessionValue(key);
}

bool EnvironmentWindow::saveToProfile(QString *error) {
    SettingsManager &s = settings();
    QStringList saved;
    for (const QString &key : keys()) {
        if (s.runtimeValueSource(key) != SettingsManager::ForcedSession)
            continue;
        if (!s.setValue(key, s.runtimeValue(key), error))
            return false;
        saved << key;
    }
    if (saved.isEmpty())
        return true;
    if (!s.save(error) || !s.applyProfileToRuntime(s.document(), nullptr, error))
        return false;
    // The profile holds them now.
    for (const QString &key : saved)
        s.clearSessionValue(key);
    return true;
}

void EnvironmentWindow::chooseColour(const QString &key, QPushButton *button) {
    const QColor current(settings().runtimeString(key));
    const QColor colour = QColorDialog::getColor(current.isValid() ? current : Qt::white, this,
                                                 controls.value(key).label->text());
    if (colour.isValid())
        set(key, colour.name(QColor::HexRgb).toUpper());
    showColour(button, settings().runtimeString(key));
}

void EnvironmentWindow::showColour(QPushButton *button, const QString &colour) {
    const QColor c(colour);
    button->setText(colour);
    button->setStyleSheet(c.isValid() ? QString("background-color: %1; color: %2;")
                                                .arg(c.name(), c.lightness() > 128 ? "black" : "white")
                                      : QString());
}

void EnvironmentWindow::environmentInfo(float sunElevation, float sunAzimuth, float moonElevation,
                                        float moonFraction) {
    QStringList lines;
    if (!std::isnan(sunElevation)) {
        if (std::isnan(sunAzimuth))
            lines << QString(
                //% "Sun %1° above the horizon (fixed light)"
                qtTrId("route.editor.environment.window.readout.fixed.sun")).arg(sunElevation, 0, 'f', 1);
        else
            lines << QString(
                //% "Sun %1° above the horizon, bearing %2°"
                qtTrId("route.editor.environment.window.readout.sun"))
                    .arg(sunElevation, 0, 'f', 1).arg(sunAzimuth, 0, 'f', 0);
    }
    if (!std::isnan(moonElevation))
        lines << QString(
            //% "Moon %1° above the horizon, %2% lit"
            qtTrId("route.editor.environment.window.readout.moon"))
                .arg(moonElevation, 0, 'f', 1).arg(moonFraction * 100.0f, 0, 'f', 0);
    readout->setText(lines.join('\n'));
}

void EnvironmentWindow::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    emit windowClosed();
}
