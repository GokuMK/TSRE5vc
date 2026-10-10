/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ENVIRONMENTTOOLS_H
#define ENVIRONMENTTOOLS_H

#include <QWidget>

class EnvironmentWindow;
class RouteEditorGLWidget;
class QCheckBox;
class QComboBox;
class QDialog;
class QLabel;
class QPushButton;
class QSpinBox;

// The Environment panel (F5, task environment 01), a tool panel as F1-F4:
// the env file the editor shows (a TRK season and weather slot or any file
// in ENVFILES) with reload and save, its weather (a preset and seed, and
// the values at the editor's time), and the editor session's controls
// (time, date, sun, moon, fog, lights: the former environment window).
class EnvironmentTools : public QWidget {
    Q_OBJECT
public:
    EnvironmentTools(RouteEditorGLWidget *view, EnvironmentWindow *session, QWidget *parent = nullptr);
    // The Map > Weather layer's state, shown by the panel's check box.
    void setWeatherOnMap(bool shown);

public slots:
    // The route was loaded: its slots and files.
    void reloadFiles();
    // Another env file or weather, or another time: the values shown.
    void refresh();

signals:
    void weatherOnMapToggled(bool shown);

private:
    RouteEditorGLWidget *view;
    QComboBox *file = nullptr;
    QPushButton *reloadButton = nullptr;
    QPushButton *saveButton = nullptr;
    QComboBox *preset = nullptr;
    QSpinBox *seed = nullptr;
    QCheckBox *onMap = nullptr;
    // Weather Details: a dialog with the weather's values at the editor's
    // time, kept up to date while it is open.
    QPushButton *detailsButton = nullptr;
    QDialog *details = nullptr;
    QLabel *info = nullptr;
    void chooseFile(int index);
    void choosePreset();
};

#endif
