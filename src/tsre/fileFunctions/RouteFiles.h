/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ROUTEFILES_H
#define ROUTEFILES_H

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

class FileBuffer;
class QImage;

// The route's files as its loaders read them (task server 03).
//
// A local route: the disk, as before. A server client: the route data its
// server sent (kinds of data, held in memory; the client never names a path
// to the server) for every path in a route's folder outside its heavy
// folders, and in GLOBAL outside its shapes and textures. Such a path the
// server did not send does not exist on a client: the client's own copies
// of those files are never read, of any route. Heavy folders (a route's
// SHAPES, TEXTURES, TERRTEX, SOUND, ENVFILES/TEXTURES, GLOBAL's SHAPES and
// TEXTURES) and everything outside ROUTES and GLOBAL are read from the
// disk.
//
// Thread safe: textures read through it on their loader threads. Needs
// QtCore only (images: RouteFilesImage.cpp).
namespace RouteFiles {

// The server's files, by their path under the client's game root `root`
// ("GLOBAL/x", "ROUTES/<route>/x"), replacing any before; from then on this
// is a client.
void setServed(const QString &root, const QHash<QString, QByteArray> &files);
// One file more, or replaced (a terrain tile's while it loads).
void putServed(const QString &rootRelative, const QByteArray &bytes);
void dropServed(const QString &rootRelative);
// Still a client, without files (a route left, the next one's to come).
void dropAllServed();
// Back to a local route: the disk for everything.
void clearServed();
// A client holding its server's files.
bool serving();
// The files held, by path under the game root (the checks).
QStringList servedNames();

// The path under the client's game root that `path` names ("" outside it,
// or on a local route), as the server's files are keyed: forward slashes,
// no "." or "..".
QString rootRelative(const QString &path);
// Whether `path` is one a client takes from its server's files.
bool served(const QString &path);

bool exists(const QString &path);
qint64 size(const QString &path);
bool read(const QString &path, QByteArray &bytes, QString *error = nullptr);
// As ReadFile::read (a compressed SIMIS file uncompressed) and
// ReadFile::readRAW; NULL when there is no such file.
FileBuffer *buffer(const QString &path);
FileBuffer *rawBuffer(const QString &path);
// An image file (QImage's formats); null when there is none.
QImage image(const QString &path);
// The files of `folder` (not its subfolders) whose names match `filters`
// (QDir's wildcards, case ignored), sorted by name, case ignored.
QStringList entries(const QString &folder, const QStringList &filters = QStringList());
// Whether `folder` holds any file, or any folder (a client: any file under
// it).
bool folderExists(const QString &folder);

namespace Internal {
enum class Source { Disk, Held, Missing };
// Where `path` is read from, and the server's bytes when held.
Source lookup(const QString &path, QByteArray *bytes);
}

}

#endif // ROUTEFILES_H
