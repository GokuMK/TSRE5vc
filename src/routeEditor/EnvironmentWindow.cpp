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
#include <tsre/gui/GuiFunct.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QTimeEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QDebug>
#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <algorithm>
#include <cmath>

namespace {
const char *const TimeEnabled = "core.rendering.timeOfDay.enabled";
const char *const Time = "core.rendering.timeOfDay.time";
const char *const Date = "core.rendering.timeOfDay.date";
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

// The reset arrow (an open circle turning anticlockwise, as U+21BA), drawn:
// the usual UI fonts lack that glyph, and Windows looking for it in other
// fonts made the window's first show take up to seconds.
QIcon resetIcon(const QColor &colour) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(14, 14) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(colour, 1.4);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    // Open at the upper left; the arrow at the top points into the gap.
    const QPointF centre(7.0, 7.5);
    const double radius = 4.5, end = 80.0, span = 300.0;
    painter.drawArc(QRectF(centre.x() - radius, centre.y() - radius, 2 * radius, 2 * radius),
                    int((end - span) * 16), int(span * 16));
    const double a = end * M_PI / 180.0;
    const QPointF at(centre.x() + radius * std::cos(a), centre.y() - radius * std::sin(a));
    const QPointF along(-std::sin(a), -std::cos(a)), across(along.y(), -along.x());
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawPolygon(QPolygonF({at + along * 2.6, at + across * 2.3, at - across * 2.3}));
    return QIcon(pixmap);
}

QTime timeOfHours(double hours) {
    const int minutes = std::clamp(int(std::lround(hours * 60.0)), 0, 24 * 60 - 1);
    return QTime(minutes / 60, minutes % 60);
}
}

QStringList EnvironmentWindow::keys() {
    QStringList list;
    for (const char *key : {TimeEnabled, Time, Date, Sun, Moon, SunSize, FogColor, FogDensity, LocalLights,
                            Bloom, Exposure})
        list << QString::fromLatin1(key);
    return list;
}

EnvironmentWindow::EnvironmentWindow(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::WindowType::Tool);
    setWindowTitle(
        //% "Environment"
        qtTrId("route.editor.environment.window.title"));

    // Laid out as the tool panels (F1, F2): headings in the accent colour and
    // compact rows of a label, the value and a reset button.
    constexpr int LabelWidth = 80;
    // Fields of numbers share one width, so their sliders line up.
    constexpr int ValueWidth = 70;
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(1, 1, 1, 1);
    QGridLayout *grid = nullptr;
    auto section = [&](const QString &title) {
        layout->addWidget(GuiFunct::newTQLabel(title));
        grid = new QGridLayout;
        grid->setSpacing(2);
        grid->setContentsMargins(3, 0, 1, 0);
        grid->setColumnStretch(1, 1);
        layout->addLayout(grid);
    };
    // The editor of each setting is named after its key (tests find them).
    auto row = [&](const char *key, const QString &text, QWidget *editor) {
        const int r = grid->rowCount();
        if (editor->objectName().isEmpty())
            editor->setObjectName(QString::fromLatin1(key));
        Control &control = controls[QString::fromLatin1(key)];
        control.label = GuiFunct::newQLabel(text, LabelWidth);
        control.reset = resetButtonFor(QString::fromLatin1(key));
        grid->addWidget(control.label, r, 0);
        grid->addWidget(editor, r, 1);
        grid->addWidget(control.reset, r, 2);
        return &control;
    };
    auto line = [&](std::initializer_list<QWidget *> widgets) {
        auto *holder = new QWidget(this);
        // Not an editor: its widgets carry the names.
        holder->setObjectName("line");
        auto *box = new QHBoxLayout(holder);
        box->setSpacing(2);
        box->setContentsMargins(0, 0, 0, 0);
        for (QWidget *w : widgets)
            box->addWidget(w);
        return holder;
    };
    // A number: its field and a slider over the same range, in steps.
    auto number = [&](const char *key, const QString &text, double low, double high, double step,
                      int decimals, const QString &suffix) {
        const QString name = QString::fromLatin1(key);
        auto *field = new QDoubleSpinBox(this);
        field->setObjectName(name);
        field->setRange(low, high);
        field->setSingleStep(step);
        field->setDecimals(decimals);
        field->setSuffix(suffix);
        field->setFixedWidth(ValueWidth);
        auto *slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(0, int(std::lround((high - low) / step)));
        row(key, text, line({field, slider}))->load = [name, field, slider, low, step] {
            const double value = settings().runtimeFloat(name);
            quietly(field, &QDoubleSpinBox::setValue, value);
            quietly(slider, &QSlider::setValue, int(std::lround((value - low) / step)));
        };
        connect(field, &QDoubleSpinBox::valueChanged, this, [this, name](double value) { set(name, value); });
        connect(slider, &QSlider::valueChanged, this,
                [this, name, low, step](int index) { set(name, low + index * step); });
        return field;
    };

    // Time, with the sun and moon it places.
    section(
        //% "Time:"
        qtTrId("route.editor.environment.window.group.time"));
    timeEnabled = new QCheckBox(this);
    row(TimeEnabled,
        //% "Time of day:"
        qtTrId("route.editor.environment.window.time.of.day"), timeEnabled)->load = [this] {
        quietly(timeEnabled, &QCheckBox::setChecked, settings().runtimeBool(TimeEnabled));
    };
    connect(timeEnabled, &QCheckBox::toggled, this, [this](bool on) { set(TimeEnabled, on); });
    timeEdit = new QTimeEdit(this);
    timeEdit->setObjectName(QString::fromLatin1(Time));
    timeEdit->setDisplayFormat("HH:mm");
    timeEdit->setFixedWidth(ValueWidth);
    timeSlider = new QSlider(Qt::Horizontal, this);
    timeSlider->setRange(0, 24 * 60 - 1);
    timeSlider->setSingleStep(5);
    timeSlider->setPageStep(60);
    row(Time,
        //% "Solar time:"
        qtTrId("route.editor.environment.window.time"), line({timeEdit, timeSlider}))->load = [this] {
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
    dateEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *today = new QToolButton(this);
    today->setText(
        //% "Today"
        qtTrId("route.editor.environment.window.today"));
    row(Date,
        //% "Date:"
        qtTrId("route.editor.environment.window.date"), line({dateEdit, today}))->load = [this] {
        const QDate date = QDate::fromString(settings().runtimeString(Date).trimmed(), "yyyy-MM-dd");
        quietly(dateEdit, &QDateEdit::setDate, date.isValid() ? date : QDate(2026, 6, 21));
    };
    connect(dateEdit, &QDateEdit::dateChanged, this,
            [this](QDate date) { set(Date, date.toString("yyyy-MM-dd")); });
    connect(today, &QToolButton::clicked, this, [this] { dateEdit->setDate(QDate::currentDate()); });
    sun = new QCheckBox(this);
    row(Sun,
        //% "Sun:"
        qtTrId("route.editor.environment.window.sun"), sun)->load = [this] {
        quietly(sun, &QCheckBox::setChecked, settings().runtimeBool(Sun));
    };
    connect(sun, &QCheckBox::toggled, this, [this](bool on) { set(Sun, on); });
    moon = new QCheckBox(this);
    row(Moon,
        //% "Moon:"
        qtTrId("route.editor.environment.window.moon"), moon)->load = [this] {
        quietly(moon, &QCheckBox::setChecked, settings().runtimeBool(Moon));
    };
    connect(moon, &QCheckBox::toggled, this, [this](bool on) { set(Moon, on); });
    sunSize = number(SunSize,
        //% "Size:"
        qtTrId("route.editor.environment.window.sun.size"), 0.2, 5.0, 0.1, 1, QStringLiteral("°"));
    readout = new QLabel(this);
    readout->setWordWrap(true);
    readout->setContentsMargins(0, 2, 0, 2);
    grid->addWidget(readout, grid->rowCount(), 0, 1, 3);

    // Environment.
    section(
        //% "Environment:"
        qtTrId("route.editor.environment.window.group.environment"));
    fogButton = new QPushButton(this);
    row(FogColor,
        //% "Fog colour:"
        qtTrId("route.editor.environment.window.fog.colour"), fogButton)->load = [this] {
        showColour(fogButton, settings().runtimeString(FogColor));
    };
    connect(fogButton, &QPushButton::clicked, this, [this] { chooseColour(FogColor, fogButton); });
    fogDensity = number(FogDensity,
        //% "Fog density:"
        qtTrId("route.editor.environment.window.fog.density"), 0.0, 1.0, 0.01, 2, QString());

    // Rendering.
    section(
        //% "Rendering:"
        qtTrId("route.editor.environment.window.group.rendering"));
    localLights = new QCheckBox(this);
    row(LocalLights,
        //% "Local lights:"
        qtTrId("route.editor.environment.window.local.lights"), localLights)->load = [this] {
        quietly(localLights, &QCheckBox::setChecked, settings().runtimeBool(LocalLights));
    };
    connect(localLights, &QCheckBox::toggled, this, [this](bool on) { set(LocalLights, on); });
    bloom = number(Bloom,
        //% "Bloom:"
        qtTrId("route.editor.environment.window.bloom"), 0.0, 4.0, 0.05, 2, QString());
    exposure = number(Exposure,
        //% "Exposure:"
        qtTrId("route.editor.environment.window.exposure"), -4.0, 4.0, 0.25, 2, QStringLiteral(" EV"));

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
    QWidget *buttons = line({resetButton, saveButton});
    buttons->setContentsMargins(3, 4, 1, 0);
    layout->addWidget(buttons);
    layout->addStretch(1);
    setFixedWidth(260);

    connect(&settings(), &SettingsManager::runtimeSettingsChanged, this, &EnvironmentWindow::refresh);
    refresh(keys());
    environmentInfo(std::nanf(""), std::nanf(""), std::nanf(""), std::nanf(""));
}

QToolButton *EnvironmentWindow::resetButtonFor(const QString &key) {
    auto *button = new QToolButton(this);
    button->setObjectName("reset:" + key);
    static const QIcon arrow = resetIcon(palette().color(QPalette::ButtonText));
    button->setIcon(arrow);
    button->setAutoRaise(true);
    button->setFixedSize(20, 20);
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
    readout->setVisible(!lines.isEmpty());
}

void EnvironmentWindow::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    emit windowClosed();
}
