/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef VEHICLEPRELOAD_H
#define VEHICLEPRELOAD_H

#include <QSet>
#include <QString>
#include <QStringList>
#include <memory>

class VehicleDefinition;

// Vehicle files read ahead: when a timetable set is chosen, worker threads
// read its consists' engine and wagon files into definitions, which
// Eng::load takes instead of reading the files again when the run starts
// (Prudnik's PSK_2026: 691 files, 1 to 1.5 s of Play's 3.3 s). Start,
// cancel and take on the main thread; the reading itself on the workers
// (VehicleDefinition::read and the consist files' reader keep no shared
// state).
namespace VehiclePreload {

// Reads the vehicles of these consist files in the background, but those
// already loaded (keys as EngLib's hashid); the read before cancelled.
// owner: what they are for (a timetable set): chosen again, not read again.
void start(const void *owner, const QStringList &consistFiles, const QString &trainsetDirectory,
           const QString &game, bool openRails, const QSet<QString> &loaded);
// Stopped, its definitions dropped.
void cancel();
// The definition read ahead for this file (as Eng::load reads it), taken
// out of the held ones; null when not read (yet), or the file changed since.
std::shared_ptr<const VehicleDefinition> take(const QString &folder, const QString &name, bool openRails);
// For tests: waits for the workers (ms); the definitions held.
bool waitForDone(int ms);
int held();

}

#endif
