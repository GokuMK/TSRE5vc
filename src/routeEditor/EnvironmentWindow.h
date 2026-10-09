/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#ifndef ENVIRONMENTWINDOW_H
#define ENVIRONMENTWINDOW_H

#include <QWidget>
#include <QHash>
#include <QStringList>
#include <functional>

class QCheckBox;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimeEdit;
class QToolButton;

// The environment of the running editor (task editor 05): time of day with
// the sun and moon, fog, and lights. Every control sets a session value of its
// setting, which applies at once and leaves the profile alone; Reset goes
// back to the profile's value, Save to profile keeps the current values.
class EnvironmentWindow : public QWidget {
    Q_OBJECT
public:
    explicit EnvironmentWindow(QWidget *parent);
    // The settings the window edits.
    static QStringList keys();

public slots:
    void environmentInfo(float sunElevation, float sunAzimuth, float moonElevation, float moonFraction);
    // Clears every session value the window set.
    void resetAll();
    // Writes the current values into the profile and saves it.
    bool saveToProfile(QString *error = nullptr);

signals:
    void windowClosed();

protected:
    void hideEvent(QHideEvent *e) override;

private:
    struct Control {
        QLabel *label = nullptr;
        QToolButton *reset = nullptr;
        // Shows the setting's current value in the editor.
        std::function<void()> load;
    };
    QHash<QString, Control> controls;
    QCheckBox *timeEnabled = nullptr;
    QSlider *timeSlider = nullptr;
    QTimeEdit *timeEdit = nullptr;
    QDateEdit *dateEdit = nullptr;
    QPushButton *fogButton = nullptr;
    QDoubleSpinBox *fogDensity = nullptr;
    QCheckBox *localLights = nullptr;
    QDoubleSpinBox *bloom = nullptr;
    QDoubleSpinBox *exposure = nullptr;
    QCheckBox *sun = nullptr;
    QCheckBox *moon = nullptr;
    QDoubleSpinBox *sunSize = nullptr;
    QLabel *readout = nullptr;
    QPushButton *saveButton = nullptr;
    QPushButton *resetButton = nullptr;
    void set(const QString &key, const QVariant &value);
    QToolButton *resetButtonFor(const QString &key);
    void refresh(const QStringList &changedKeys);
    void refreshMarks();
    void chooseColour(const QString &key, QPushButton *button);
    static void showColour(QPushButton *button, const QString &colour);
};

#endif
