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
#include <QStringList>
#include <QVector>
#include <tsre/fileFunctions/SimisDocument.h>

class FileBuffer;
class RenderItem;
namespace SFileDetail {
struct Node;
}

// A shape's displays (task scripting 02): its .sd's ESD_TSREDisplay blocks,
// documents drawn into textures the shape shows.
namespace ShapeDisplays {

struct Display {
    // The .rml file as written: in the documents folder (documentsFolder),
    // or from a root (documentFile).
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
    // Shape (none given): a replaced texture's parts lit as the shape has
    // them (a square: Glass).
    enum class Surface { Shape, Glass, Paper };
    Surface surface = Surface::Shape;
    // RefreshRate: the seconds between redraws; 0 draws it once.
    float refreshSeconds = 0.0f;
    // What it shows besides the clock: a platform's or a station's
    // departures and arrivals (a placed object's), its train's data (a
    // car's: given by its train), its train's and its car's (task scripting
    // 03: a document per car); Eng reserved (the vehicle's own data, later:
    // the clock only).
    enum class Input { None, Clock, Platform, Station, Train, Car, Eng };
    Input input = Input::None;
};

// The displays of the shape's .sd (next to it); none without one. A shape
// reads its own as it reads its .sd (ComplexShape::displays).
QVector<Display> read(const QString &shapePath);
// An ESD_TSREDisplay block's values, its name read: false for one without a
// document, or with neither an image nor a square.
bool readBlock(FileBuffer *data, Display &display);
// Why a block read so was left out (for the messages; translated).
QString leftOut(const Display &display);
// The displays of a parsed .sd's shape block (SFileComplex's); the blocks
// left out, why, into `problems`.
QVector<Display> read(const SFileDetail::Node &shape, QStringList *problems = nullptr);
// A display's surface on a packet showing it, a preset in place of the
// part's own material (its light material, or its PBR factors and maps, the
// .sd's included): glass shines as painted at any hour, paper is lit by the
// scene. An MSTS part as full bright or normal light material; a PBR part
// stays PBR (glTF's too), unlit or a matte dielectric, its texture
// coordinates and alpha mode kept. Shape leaves the packet as it is.
void applySurface(RenderItem &item, Display::Surface surface);
// Where its documents are: the route's DISPLAYS folder for a route's shape
// (ROUTES/<route>/SHAPES/...), the trainset's for a car's or a cab's
// (TRAINS/TRAINSET/<dir>/...); empty for others.
QString documentsFolder(const QString &shapePath);
// The roots a display's paths take (task scripting 02's file access, the
// user's 2026-11-03): the game folder the content `path` (a shape's, a
// document's) lies in, the trainset's folder for one in
// TRAINS/TRAINSET/<dir> ([[Vehicle]]), the route being run or edited
// ([[Route]]); no game folder for a path in neither ROUTES nor TRAINS.
SimisDocument::Roots roots(const QString &path);
// A display's document file: its .sd's Document by the documents' rule
// (SimisDocument::Rule::Documents), relative to the shape's documents
// folder and down from it, or from a root; empty, with `problem`, for one
// breaking the rule or a shape with no documents folder.
QString documentFile(const QString &shapePath, const QString &document, QString *problem = nullptr);
// Where a document's pictures are (the user, 2026-11-01): its asset's
// texture folder, as its shapes' textures (and glTF's) are: the route's
// TEXTURES for a document in ROUTES/<route>/DISPLAYS, the trainset's own
// folder for one in TRAINS/TRAINSET/<dir>/DISPLAYS (its subfolders too);
// empty for a document elsewhere.
QString picturesFolder(const QString &documentPath);
// A file name a picture's (an image format TexLib reads), by its extension.
bool isPicture(const QString &path);

}

#endif // SHAPEDISPLAYS_H
