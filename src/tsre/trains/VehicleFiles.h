/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef VEHICLEFILES_H
#define VEHICLEFILES_H

#include <QString>
#include <QVector>
#include <functional>

class Consist;
class Eng;

// The files a vehicle or a train is made of, by the main folders of the game
// they are in: what to copy for it to work on another machine (the Consist
// Editor's Vehicle files, task maszyna 02).
namespace VehicleFiles {

struct File {
    QString path;
    bool missing = false;
    // A vehicle's own file while its OPENRAILS copy is the one read.
    bool notRead = false;
    qint64 bytes = 0;
    // Text data, which can be opened in an editor.
    bool text() const;
};

struct Folder {
    QString name;       // from the game root; a whole path outside it
    QVector<File> files;
    qint64 bytes = 0;
    int missing = 0;
};

// Every file of a vehicle: its definition and includes, shapes, materials,
// textures, sound and cab definitions, the livery's only. Its shapes are
// loaded for this.
QVector<File> ofVehicle(Eng &eng);
// Its consist file, then its cars'.
QVector<File> ofTrain(Consist &train);

// By main folder: the first file's first, the others by name; in each the
// text files first.
QVector<Folder> byFolder(const QVector<File> &files, const QString &gameRoot);
// The path from the game root, empty for a file outside it.
QString fromRoot(const QString &path, const QString &gameRoot);

struct Archived {
    bool ok = false;
    int written = 0;
    // Missing, or outside the game root.
    int left = 0;
    QString error;
};
// A zip of the files with their paths from the game root, to extract in
// another game root. `progress` (done, total) false stops it, and no archive
// is left.
Archived archive(const QVector<File> &files, const QString &gameRoot, const QString &zipPath,
                 const std::function<bool(int, int)> &progress = nullptr);

}

#endif // VEHICLEFILES_H
