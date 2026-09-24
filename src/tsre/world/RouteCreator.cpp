/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine or Editors.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/world/RouteCreator.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QStringList>
#include <tsre/Game.h>
#include <tsre/tdb/TDB.h>
#include <tsre/texture/AceLib.h>
#include <tsre/texture/Texture.h>
#include <tsre/world/TerrainLibQt.h>
#include <tsre/world/Tile.h>
#include <tsre/world/Trk.h>

namespace {

void setError(QString *error, const QString &value) {
    if (error != nullptr)
        *error = value;
}

bool copyFileChecked(const QString &source, const QString &destination,
                     QString *error) {
    if (!QFile::copy(source, destination)) {
        setError(error, QStringLiteral("Unable to copy %1 to %2")
                 .arg(source, destination));
        return false;
    }
    return true;
}

bool copyDirectoryFiles(const QString &sourcePath, const QString &destinationPath,
                        QString *error, bool recursive = true) {
    const QDir source(sourcePath);
    if (!source.exists()) {
        setError(error, QStringLiteral("Template directory is missing: %1")
                 .arg(sourcePath));
        return false;
    }
    if (!QDir().mkpath(destinationPath)) {
        setError(error, QStringLiteral("Unable to create directory: %1")
                 .arg(destinationPath));
        return false;
    }

    const QFileInfoList entries = source.entryInfoList(
            QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
            QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString destination = QDir(destinationPath).filePath(entry.fileName());
        if (entry.isDir()) {
            if (recursive
                    && !copyDirectoryFiles(entry.absoluteFilePath(), destination,
                                           error, true))
                return false;
        } else if (!copyFileChecked(entry.absoluteFilePath(), destination, error)) {
            return false;
        }
    }
    return true;
}

class RouteDirectoryContext {
public:
    explicit RouteDirectoryContext(const QString &temporaryRoute)
        : previousRoute(Game::route) {
        Game::route = temporaryRoute;
    }

    ~RouteDirectoryContext() {
        Game::route = previousRoute;
    }

private:
    const QString previousRoute;
};

} // namespace

QString RouteCreator::templateDirectory() {
    return QDir::current().absoluteFilePath(
            QStringLiteral("assets/templateRoute_0.6"));
}

bool RouteCreator::templateResourcesAvailable(QString *error) {
    const QDir source(templateDirectory());
    const QStringList files = {
        QStringLiteral("sigcfg.dat"), QStringLiteral("sigscr.dat"),
        QStringLiteral("ttype.dat"), QStringLiteral("template.ref"),
        QStringLiteral("carspawn.dat"), QStringLiteral("deer.haz"),
        QStringLiteral("forests.dat"), QStringLiteral("speedpost.dat"),
        QStringLiteral("spotter.haz"), QStringLiteral("ssource.dat"),
        QStringLiteral("telepole.dat")
    };
    const QStringList directories = {
        QStringLiteral("envfiles"), QStringLiteral("shapes"),
        QStringLiteral("sound"), QStringLiteral("terrtex"),
        QStringLiteral("textures")
    };

    if (!source.exists()) {
        setError(error, QStringLiteral("Route template directory is missing: %1")
                 .arg(source.absolutePath()));
        return false;
    }
    for (const QString &file : files) {
        if (!QFileInfo::exists(source.filePath(file))) {
            setError(error, QStringLiteral("Route template file is missing: %1")
                     .arg(source.filePath(file)));
            return false;
        }
    }
    for (const QString &directory : directories) {
        if (!QFileInfo(source.filePath(directory)).isDir()) {
            setError(error, QStringLiteral("Route template directory is missing: %1")
                     .arg(source.filePath(directory)));
            return false;
        }
    }
    return true;
}

bool RouteCreator::create(const QString &routeDirectoryName,
                          std::unique_ptr<Trk> routeTemplate,
                          QString *error) {
    if (!Game::writeEnabled) {
        setError(error, QStringLiteral("Route writing is disabled."));
        return false;
    }
    if (routeTemplate == nullptr) {
        setError(error, QStringLiteral("No TRK template was provided."));
        return false;
    }

    const QString routeDirectory = routeDirectoryName.trimmed();
    if (routeDirectory.isEmpty() || routeDirectory == QStringLiteral(".")
            || routeDirectory == QStringLiteral("..")
            || routeDirectory.contains('/') || routeDirectory.contains('\\')
            || QFileInfo(routeDirectory).fileName() != routeDirectory) {
        setError(error, QStringLiteral("Invalid route directory name: %1")
                 .arg(routeDirectoryName));
        return false;
    }
    if (routeTemplate->routeName.isEmpty()
            || routeTemplate->routeName.contains('/')
            || routeTemplate->routeName.contains('\\')
            || QFileInfo(routeTemplate->routeName).fileName()
                != routeTemplate->routeName) {
        setError(error, QStringLiteral("Invalid TRK FileName value: %1")
                 .arg(routeTemplate->routeName));
        return false;
    }
    if (!templateResourcesAvailable(error))
        return false;

    const QString routesPath = QDir(Game::root).filePath(QStringLiteral("ROUTES"));
    QDir routesDirectory(routesPath);
    if (!routesDirectory.exists()) {
        setError(error, QStringLiteral("Routes directory does not exist: %1")
                 .arg(routesPath));
        return false;
    }
    const QString finalPath = routesDirectory.filePath(routeDirectory);
    if (QFileInfo::exists(finalPath)) {
        setError(error, QStringLiteral("Route directory already exists: %1")
                 .arg(finalPath));
        return false;
    }

    QTemporaryDir staging(routesDirectory.filePath(
            QStringLiteral(".tsre-new-%1-XXXXXX").arg(routeDirectory)));
    if (!staging.isValid()) {
        setError(error, QStringLiteral("Unable to create a temporary route directory in %1")
                 .arg(routesPath));
        return false;
    }

    const QStringList directories = {
        QStringLiteral("ENVFILES/TEXTURES"), QStringLiteral("PATHS"),
        QStringLiteral("SHAPES"), QStringLiteral("SOUND"),
        QStringLiteral("TEXTURES"), QStringLiteral("TERRTEX"),
        QStringLiteral("TILES"), QStringLiteral("TD"),
        QStringLiteral("WORLD")
    };
    for (const QString &directory : directories) {
        if (!QDir(staging.path()).mkpath(directory)) {
            setError(error, QStringLiteral("Unable to create route directory: %1")
                     .arg(QDir(staging.path()).filePath(directory)));
            return false;
        }
    }

    const QString stagingName = QFileInfo(staging.path()).fileName();
    {
        RouteDirectoryContext routeContext(stagingName);

        if (routeTemplate->trkFileName.isEmpty())
            routeTemplate->trkFileName = routeDirectory + QStringLiteral(".trk");
        if (QFileInfo(routeTemplate->trkFileName).fileName()
                != routeTemplate->trkFileName) {
            setError(error, QStringLiteral("Invalid TRK filename: %1")
                     .arg(routeTemplate->trkFileName));
            return false;
        }
        routeTemplate->setModified(true);
        if (!routeTemplate->save()) {
            setError(error, QStringLiteral("Unable to save the new route TRK file."));
            return false;
        }
        if (!TDB::saveEmpty(false, routeTemplate->routeName)
                || !TDB::saveEmpty(true, routeTemplate->routeName)) {
            setError(error, QStringLiteral("Unable to create the empty route databases."));
            return false;
        }

        {
            // New routes always use the quadtree terrain format. The legacy
            // recovery setting is for existing routes, not creation of damaged ones.
            std::unique_ptr<TerrainLibQt> terrain(new TerrainLibQt());
            if (!terrain->createNewRouteTerrain(routeTemplate->startTileX,
                                                routeTemplate->startTileZ)) {
                setError(error, QStringLiteral("Unable to create the initial terrain tile."));
                return false;
            }
        }
        const QDir terrainDirectory(
                QDir(staging.path()).filePath(QStringLiteral("TILES")));
        const QDir terrainIndexDirectory(
                QDir(staging.path()).filePath(QStringLiteral("TD")));
        if (!QFileInfo::exists(terrainIndexDirectory.filePath(
                                  QStringLiteral("td_idx.dat")))
                || terrainIndexDirectory.entryList(
                       QStringList() << QStringLiteral("*.td"), QDir::Files).isEmpty()
                || terrainDirectory.entryList(
                       QStringList() << QStringLiteral("*.t"), QDir::Files).isEmpty()
                || terrainDirectory.entryList(
                       QStringList() << QStringLiteral("*_y.raw"), QDir::Files).isEmpty()) {
            setError(error, QStringLiteral("Initial terrain files are incomplete."));
            return false;
        }
        if (!Tile::saveEmpty(routeTemplate->startTileX,
                             routeTemplate->startTileZ)) {
            setError(error, QStringLiteral("Unable to create the initial world tile."));
            return false;
        }

        const QDir source(templateDirectory());
        const QStringList files = {
            QStringLiteral("sigcfg.dat"), QStringLiteral("sigscr.dat"),
            QStringLiteral("ttype.dat"), QStringLiteral("carspawn.dat"),
            QStringLiteral("deer.haz"), QStringLiteral("forests.dat"),
            QStringLiteral("speedpost.dat"), QStringLiteral("spotter.haz"),
            QStringLiteral("ssource.dat"), QStringLiteral("telepole.dat")
        };
        for (const QString &file : files) {
            if (!copyFileChecked(source.filePath(file),
                                 QDir(staging.path()).filePath(file), error))
                return false;
        }
        if (!copyFileChecked(source.filePath(QStringLiteral("template.ref")),
                             QDir(staging.path()).filePath(
                                 routeTemplate->routeName + QStringLiteral(".ref")),
                             error)) {
            return false;
        }

        if (!copyDirectoryFiles(source.filePath(QStringLiteral("envfiles")),
                                QDir(staging.path()).filePath(
                                    QStringLiteral("ENVFILES")),
                                error, false)
                || !copyDirectoryFiles(
                    source.filePath(QStringLiteral("envfiles/textures")),
                    QDir(staging.path()).filePath(
                        QStringLiteral("ENVFILES/TEXTURES")),
                    error)) {
            return false;
        }

        const QPair<QString, QString> directoryCopies[] = {
            {QStringLiteral("shapes"), QStringLiteral("SHAPES")},
            {QStringLiteral("sound"), QStringLiteral("SOUND")},
            {QStringLiteral("terrtex"), QStringLiteral("TERRTEX")},
            {QStringLiteral("textures"), QStringLiteral("TEXTURES")}
        };
        for (const auto &copy : directoryCopies) {
            if (!copyDirectoryFiles(source.filePath(copy.first),
                                    QDir(staging.path()).filePath(copy.second),
                                    error)) {
                return false;
            }
        }

        Texture graphicTexture(200, 150, 24);
        if (!AceLib::save(QDir(staging.path()).filePath(
                                  QStringLiteral("graphic.ace")),
                          &graphicTexture)) {
            setError(error, QStringLiteral("Unable to create graphic.ace."));
            return false;
        }
    }

    if (!routesDirectory.rename(stagingName, routeDirectory)) {
        setError(error, QStringLiteral("Unable to move the completed route into %1")
                 .arg(finalPath));
        return false;
    }
    staging.setAutoRemove(false);
    Game::route = routeDirectory;
    return true;
}
