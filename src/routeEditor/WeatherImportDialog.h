/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef WEATHERIMPORTDIALOG_H
#define WEATHERIMPORTDIALOG_H

#include <QDialog>

class UnitSpinBox;

class RouteEditorGLWidget;
class QComboBox;
class QDateEdit;
class QLabel;
class QSpinBox;
namespace WeatherImport { struct Request; }

// Imports a historical weather from Open-Meteo (task environment 01, step 3):
// a day, start hour (UTC) and length, the model, the margin around the route
// and the weather's cell size; the area is the route's track and the margin.
class WeatherImportDialog : public QDialog {
    Q_OBJECT
public:
    explicit WeatherImportDialog(RouteEditorGLWidget *view, QWidget *parent = nullptr);

private:
    RouteEditorGLWidget *view;
    QDateEdit *date = nullptr;
    QSpinBox *startHour = nullptr;
    QSpinBox *hours = nullptr;
    QComboBox *model = nullptr;
    QComboBox *spacing = nullptr;
    // In the units the editors show (task core si-units).
    UnitSpinBox *margin = nullptr;
    QComboBox *cellSize = nullptr;
    QLabel *summary = nullptr;
    bool request(WeatherImport::Request &out, QString *error) const;
    void updateSummary();
    void import();
};

#endif
