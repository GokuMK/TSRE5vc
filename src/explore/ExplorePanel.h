/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef EXPLOREPANEL_H
#define EXPLOREPANEL_H

#include <explore/ExploreChoice.h>
#include <explore/ExploreLists.h>

#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

namespace Rml {
class Context;
class Element;
class ElementDocument;
}

namespace Explore {

// The Explore window's content (task explore 02): the routes, the chosen
// route's runs (exploring it, its activities, its timetable sets) and the
// chosen run's details and choices, as the RmlUi data model `explore` of
// explore-menu.rml. Its lists are read from the game root (Lists), the
// consists on a worker; its choices are remembered in the profile's
// explore.json. Explore gives the run chosen to `start`.
class Panel {
public:
    // One the documents use; the context outlives it. `root`: the game
    // root; `route`: the route chosen first (the Load window's), else the
    // last one.
    Panel(Rml::Context &context, const QString &root, const QString &route, std::function<void(const RunChoice &)> start);
    ~Panel();
    // False when the model could not be made.
    bool usable() const;

    // The document showing it (focus moves in it), after it loaded.
    void setDocument(Rml::ElementDocument *document);
    // Each frame: the consists' list taken when the worker is done, a focus
    // waiting for its element.
    void frame();
    // The routes read, the route chosen first; once, when the window opens.
    void open();

    // What the checks drive, as the clicks and keys do.
    QStringList routeNames() const;
    QStringList runNames() const;
    bool chooseRoute(const QString &name);
    bool chooseRun(const QString &name);
    // Exploring: a path by file, a consist by file.
    bool choosePath(const QString &file);
    bool chooseConsist(const QString &file);
    // A timetable's train, shown in the list now (filter, drivable).
    bool chooseTrain(const QString &name);
    QStringList trainNames() const;
    void setFilter(const QString &text);
    void setDrivableOnly(bool on);
    void sortTrains(const QString &column);
    void setDay(const QDate &day);
    void setWeather(int weather);
    void setTime(int seconds);
    // The consists were read (or none are there).
    bool consistsRead() const;
    // Explore can start: the run is complete.
    bool ready() const;
    // The run chosen now.
    RunChoice choice() const;
    // Explore: the run given to start.
    bool go();
    // A line under the window: why a run did not start.
    void setStatus(const QString &text);
    // The picker shown (its rows' names), or none.
    bool picking() const;
    void closePicker();

    // The element to focus when the window is entered: the chosen route's,
    // else the first route's.
    Rml::Element *entry() const;
    // Left or Right from the focused element across the window's columns
    // (routes, runs, the details, Explore): RmlUi's arrows stay in a list.
    // False when it is not one to move across (a text field, a button with
    // a neighbour that way), or Left from the routes (the menu's item).
    bool moveAcross(Rml::Element *focus, bool right);

    // Where the choices are remembered: the profile's explore.json, unless
    // the checks name another file (empty: the profile's again).
    static void setRememberedFile(const QString &file);

private:
    struct Data;
    std::unique_ptr<Data> d;
};

}

#endif // EXPLOREPANEL_H
