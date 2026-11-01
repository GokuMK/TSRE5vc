/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SHAPEDISPLAYS_H
#define SHAPEDISPLAYS_H

#include <QString>
#include <QVector>

class FileBuffer;

// A shape's displays (task scripting 02): its .sd's ESD_TSREDisplay blocks,
// documents drawn into textures the shape shows.
namespace ShapeDisplays {

struct Display {
    // The .rml file, in the documents folder (documentsFolder).
    QString document;
    // A texture replaced: the image as the shape's `images` name it.
    QString image;
    // A square: the matrix it hangs on (empty: the root), its centre in
    // that matrix's space (m), its turn (degrees about x, then y, then z;
    // facing +z before it) and its width, height and depth (m).
    QString hierarchy;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};
    float size[3] = {0.0f, 0.0f, 0.0f};
    bool square = false;
    // Pixels; 0: the replaced texture's.
    int resolution[2] = {0, 0};
    enum class Surface { Glass, Paper };
    Surface surface = Surface::Glass;
    // Redraws a second; 0 draws it once.
    float refreshRate = 0.0f;
    enum class Input { None, Clock, Platform, Station };
    Input input = Input::None;
};

// The displays of the shape's .sd (next to it); none without one. A shape
// reads its own as it reads its .sd (ComplexShape::displays).
QVector<Display> read(const QString &shapePath);
// An ESD_TSREDisplay block's values, its name read: false for one without a
// document, or with neither an image nor a square.
bool readBlock(FileBuffer *data, Display &display);
// Where its documents are: the route's DISPLAYS folder for a route's shape
// (ROUTES/<route>/SHAPES/...), the trainset's for a car's or a cab's
// (TRAINS/TRAINSET/<dir>/...); empty for others.
QString documentsFolder(const QString &shapePath);

}

#endif // SHAPEDISPLAYS_H
