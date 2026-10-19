/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef WEATHERGENERATEDIALOG_H
#define WEATHERGENERATEDIALOG_H

#include <QDialog>

class UnitSpinBox;

class RouteEditorGLWidget;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
namespace WeatherGenerator { struct Settings; }

// Generates a route-scale weather (task environment 01): a front of the
// chosen kind sweeping over the route's area, its direction, speed and the
// hour it crosses the area's centre; the area is the route's track and a
// margin, as for an import.
class WeatherGenerateDialog : public QDialog {
    Q_OBJECT
public:
    explicit WeatherGenerateDialog(RouteEditorGLWidget *view, QWidget *parent = nullptr);

private:
    RouteEditorGLWidget *view;
    QComboBox *scenario = nullptr;
    QSpinBox *heading = nullptr;
    // In the units the editors show (task core si-units).
    UnitSpinBox *speed = nullptr;
    QDoubleSpinBox *arrival = nullptr;
    QSpinBox *seed = nullptr;
    UnitSpinBox *margin = nullptr;
    QComboBox *cellSize = nullptr;
    QLabel *summary = nullptr;
    void settings(WeatherGenerator::Settings &out) const;
    void updateSummary();
    void generate();
};

#endif
