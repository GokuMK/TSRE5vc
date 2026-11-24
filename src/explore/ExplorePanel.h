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

// The Explore window's content (task explore 02), in pages (task explore
// 05): the route page (the routes, the chosen route's picture, facts and
// description, its Activities, Timetables and Explore the route), then the
// page of one of those with the run's details and choices, and one bar
// under them (Drive or Walk, the walk's station and time, the day, the
// weather, Start); the RmlUi data model `explore` of explore-menu.rml. Its
// lists are read from the game root (Lists), the consists and the route's
// stations on workers; its choices are remembered in the profile's
// explore.json. Start gives the run chosen to `start`.
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
    // Explore the route, the activities' names, the timetable sets' names.
    QStringList runNames() const;
    bool chooseRoute(const QString &name);
    // A run's page with it chosen: "explore", an activity's or a set's name
    // or file.
    bool chooseRun(const QString &name);
    // The page shown: "route", "explore", "activity", "timetable"; one
    // opened by name (false: the route has none of it); back to the route
    // page (false: on it already).
    QString page() const;
    bool openPage(const QString &name);
    bool back();
    // The explore page's lists (their filters, the files shown).
    void setPathFilter(const QString &text);
    void setConsistFilter(const QString &text);
    QStringList pathFiles() const;
    QStringList consistFiles() const;
    // An element's text, by its id, as the document shows it now (the
    // first shown of that id; a text field's value); empty for none.
    QString text(const QString &id) const;
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
    // Walking (task explore 04): Drive or Walk; the stations offered (a
    // train's calls, a set's rows, the route's) and one chosen; a
    // timetable's No train; exploring with its train or none. The route's
    // stations come from a worker (stationsRead).
    void setWalk(bool on);
    QStringList stationNames() const;
    bool chooseStation(const QString &name);
    void chooseNoTrain();
    void setExploreTrain(bool on);
    bool stationsRead() const;
    // Start can be pressed: the run is complete.
    bool ready() const;
    // The run chosen now.
    RunChoice choice() const;
    // Start: the run given to start.
    bool go();
    // A line under the window: why a run did not start.
    void setStatus(const QString &text);
    // The picker shown (its rows' names), or none.
    bool picking() const;
    void closePicker();

    // The element to focus when the window is entered: on the route page
    // the button of the page last used on the route (the remembered run's
    // at first), else the chosen route's (else the first route's); on a run
    // page its first column's chosen row (the first column with one).
    Rml::Element *entry() const;
    // Left or Right from the focused element across the page's columns
    // (the route page's routes and buttons; a run page's lists and the bar):
    // RmlUi's arrows stay in a list. False when it is not one to move across
    // (a text field, a button with a neighbour that way), or Left from the
    // routes (the menu's item); Left from a run page's first column goes
    // back to the route page.
    bool moveAcross(Rml::Element *focus, bool right);
    // Up from a list's first row to its filter, Down from the filter's row
    // to the list; false for any other.
    bool moveUpDown(Rml::Element *focus, bool down);
    // Page Up or Page Down: the page's text scrolled (the route's
    // description, an activity's details); false when none is shown.
    bool scrollDetails(bool down);

    // Where the choices are remembered: the profile's explore.json, unless
    // the checks name another file (empty: the profile's again).
    static void setRememberedFile(const QString &file);

private:
    struct Data;
    std::unique_ptr<Data> d;
};

}

#endif // EXPLOREPANEL_H
