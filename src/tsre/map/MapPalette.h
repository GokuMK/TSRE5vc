/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MAPPALETTE_H
#define MAPPALETTE_H

#include <QColor>
#include <QString>
#include <QStringList>

// Colours of the map mode (task editor 04). Built in: "light" (white
// background, the default) and "dark". Custom palettes are JSON files in
// the user's palette directory (directory()), named by their file name: an
// object with any of the colour names below as "#rrggbb" strings; colours
// they leave out come from the light palette.
struct MapPalette {
    QString name = "light";
    QColor background = QColor(255, 255, 255);
    QColor track = QColor(40, 40, 46);
    QColor road = QColor(176, 128, 64);
    QColor junction = QColor(214, 48, 48);
    QColor end = QColor(48, 96, 214);
    QColor pointer = QColor(255, 120, 0);
    // Track objects, in the colours the 3D view gives their objects, and
    // the border around them.
    QColor signal = QColor(255, 0, 0);
    QColor speedPost = QColor(178, 178, 178);
    QColor platform = QColor(0, 255, 0);
    QColor siding = QColor(255, 178, 0);
    QColor carSpawner = QColor(102, 0, 255);
    QColor levelCrossing = QColor(230, 128, 0);
    QColor hazard = QColor(204, 51, 204);
    QColor pickup = QColor(204, 51, 204);
    QColor soundRegion = QColor(255, 255, 0);
    QColor itemBorder = QColor(30, 30, 34);
    // Paths (a band under the track, and their nodes) and the activity.
    QColor path = QColor(90, 210, 120);
    QColor pathNode = QColor(0, 150, 60);
    QColor wagon = QColor(100, 140, 220);
    QColor engine = QColor(35, 55, 150);
    QColor speedZone = QColor(255, 0, 102);
    QColor failedSignal = QColor(204, 51, 204);
    QColor event = QColor(255, 0, 0);
    // How much Map > Faded Overlay blends terrain and OSM data towards the
    // background. Files may still name it terrainFade, its former name.
    float overlayFade = 0.3f;
    // Borders of the terrain tiles being edited, the thin lines of their
    // quadtree, and the tint of a populated tile without its file.
    QColor terrainBorder = QColor(185, 186, 194);
    QColor quadBorder = QColor(214, 216, 224);
    QColor missingTile = QColor(235, 70, 70);
    // The halo around selected objects.
    QColor selection = QColor(0, 170, 255);

    static MapPalette light();
    static MapPalette dark();
    // The palette of a name: built in, or a file in the palette directory;
    // light when there is none.
    static MapPalette named(const QString &name);
    // Reads a palette from JSON over the light one; false on a parse error.
    static bool fromJson(const QByteArray &json, MapPalette &palette, QString *error = nullptr);
    // Where custom palettes are looked up.
    static QString directory();
    // Built-in names and the custom files found.
    static QStringList available();
};

#endif
