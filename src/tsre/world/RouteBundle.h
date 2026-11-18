/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ROUTEBUNDLE_H
#define ROUTEBUNDLE_H

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

// The route data a server sends a client after its login, in one message
// (task server 03). Kinds of data, chosen by the server; the client never
// names a path:
// - what TSRE edits and saves at once or on Save (the environment files and
//   their weather maps, terrainmaterials.dat): as the server's files are
//   when the client logs in;
// - what TSRE never writes once the route is made (GLOBAL/tsection.dat and
//   its includes, or the route's OPENRAILS one; speedpost.dat, sigcfg.dat,
//   forests.dat, carspawn.dat and the OPENRAILS one, ssource.dat,
//   ttype.dat, telepole.dat, humans.dat, the track profiles, PROCEDURAL,
//   DISPLAYS, the route's images): as the server read them at its start;
// - the heavy folders' files (the route's SHAPES, TEXTURES, TERRTEX, SOUND,
//   ENVFILES/TEXTURES; GLOBAL's SHAPES and TEXTURES): their names and
//   sizes only, compared with the client's own.
// Not sent: markers, activities, services, traffic, paths, timetables
// (not loaded on a client); the TRK, TDB, RDB, tsection, world and terrain
// tiles, quad trees and .ref (their own messages).
namespace RouteBundle {

struct HeavyFile {
    QString name; // under the game root
    qint64 size = 0;
};

struct Bundle {
    QHash<QString, QByteArray> files; // by path under the game root
    QVector<HeavyFile> heavy;
    // What was left out (too large), told on the server.
    QStringList skipped;
};

// What TSRE never writes once the route is made: read once (the server's
// start), as a local load reads it.
Bundle gatherFixed(const QString &root, const QString &route);
// What TSRE edits, and the heavy folders' list: at each login.
void gatherCurrent(const QString &root, const QString &route, Bundle &bundle);

// After the binary envelope's token: the bundle's bytes, and back.
QByteArray encode(const Bundle &bundle);
bool decode(const QByteArray &bytes, Bundle &bundle, QString &error);

// The heavy folders' files under `root` (the route's and GLOBAL's).
QVector<HeavyFile> heavyFiles(const QString &root, const QString &route);
// The client's heavy files against the server's: one line a difference
// kind (missing here, of another size, not on the server), each followed
// by its files; empty when they agree.
struct HeavyDifferences {
    QStringList missing, otherSize, extra;
    bool empty() const { return missing.isEmpty() && otherSize.isEmpty() && extra.isEmpty(); }
};
HeavyDifferences compareHeavy(const QVector<HeavyFile> &server, const QVector<HeavyFile> &client);

}

#endif // ROUTEBUNDLE_H
