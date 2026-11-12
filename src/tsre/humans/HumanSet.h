/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef HUMANS_HUMANSET_H
#define HUMANS_HUMANSET_H

#include <QString>
#include <QStringList>
#include <vector>

namespace Humans {

// The humans a route's people are made of (task humans 05): the route's
// own, listed in `humans.dat` in its main folder (their files in its SHAPES
// and TEXTURES), else TSRE's own, as the player's setting
// core.content.people says. TSRE's own (the user, 2026-11-12): the full set
// in the assets folder (assets/humans, not in git nor in releases), else a
// basic set in appdata (appdata/<version>/humans, in git and in releases);
// each laid out as a route: humans.dat, SHAPES, TEXTURES.
//
// | Setting          | A route with humans.dat | A route without                 |
// | Always (default) | its own humans          | assets' humans, else appdata's  |
// | Route defined    | its own humans          | no people                       |
// | Never            | no people               | no people                       |
//
// humans.dat, named lists as Open Rails' CarSpawnerList (the first list is
// the platforms'); a human's number is how often it is picked:
//
//     HumanList (
//         Name ( "Passengers" )
//         Animations ( "people_clips.glb" )
//         Human ( "woman.glb" 2 )
//         Human ( "man.glb" 1 )
//     )
struct HumanSet {
    enum Mode { Always = 0, RouteDefined = 1, Never = 2 };
    enum class Source { None, Route, Assets, Basic };

    struct Human {
        // The human's file (full path) and how often it is picked.
        QString path;
        float weight = 1.0f;
    };
    struct List {
        QString name;
        // The shared clips file (full path), empty for none.
        QString clips;
        std::vector<Human> humans;
    };

    Source source = Source::None;
    // The humans.dat read, and the folders its files are in.
    QString file;
    QString shapes;
    QString textures;
    std::vector<List> lists;
    // What was wrong in it (files missing, empty lists), for the log.
    QStringList problems;

    // Whether there is anyone to show.
    bool empty() const { return lists.empty() || lists.front().humans.empty(); }

    // The humans for a route (its main folder) by the setting's mode; TSRE's
    // own from `assetsFolder` (assetsHumansFolder()), else `basicFolder`
    // (basicHumansFolder()).
    static HumanSet forRoute(const QString &routeFolder, int mode, const QString &assetsFolder,
                             const QString &basicFolder);
    // A humans.dat; its files looked for in `folder`'s SHAPES. False when
    // the file is not there.
    static bool read(const QString &humansDat, const QString &folder, HumanSet &out);
    // TSRE's own humans: the full set (assets/humans) and the basic one
    // (appdata/<version>/humans).
    static QString assetsHumansFolder();
    static QString basicHumansFolder();
};

}

#endif
