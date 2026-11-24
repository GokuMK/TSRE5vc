/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef EXPLOREMENU_H
#define EXPLOREMENU_H

#include <explore/ExploreChoice.h>

#include <QRectF>
#include <QString>
#include <functional>
#include <memory>

namespace Ui {
class Screen;
}

namespace Explore {

class Panel;

// Explore mode's main menu on its view's UI screen (task explore 01) and
// its windows as tabs (task explore 02): appdata/<version>/ui/
// explore-menu.rml, the menu on the left (Explore, Multiplayer, Options,
// Exit) and the window its item opened beside it, the item marked. Shown as
// a menu (modal: the keys are its own). In the menu Up and Down move, Enter
// or a click opens; Right or Enter on the marked item goes into its window;
// Left from the window's first column, or Esc in it, back to the item. Esc
// on the menu does nothing. A view of 3:2 and narrower has the window over
// the menu (task explore 05, explore.rcss): back on the item, the window is
// hidden, shown again as it opens.
class Menu {
public:
    enum class Page { Main, Explore };

    // The screen outlives it. `root`, `route`: the game root, the route
    // chosen first. `start`: a run chosen; `exit` quits explore mode.
    Menu(Ui::Screen &screen, const QString &root, const QString &route, std::function<void(const RunChoice &)> start,
         std::function<void()> exit);
    ~Menu();
    // Each frame before the screen's update: the document loaded the first
    // time, shown while the menu is (not during a run).
    void frame();
    // False once the document did not load.
    bool usable() const { return !failed; }
    Page page() const { return current; }
    // Main: no window; Explore: its window shown and the keys in it.
    void open(Page page);
    // Shown, or hidden for a run.
    void setShown(bool shown);
    bool shown() const { return showing; }
    // An element with this id clicked (the checks); false when there is
    // none.
    bool click(const QString &id);
    // The id of the element with the keys' focus: its own, else its list's
    // ("routes:item"); empty for none.
    QString focused() const;
    // The Explore window's content, null before the document loaded.
    Panel *panel() const;
    // The window over the menu (a view of 3:2 and narrower): the menu then
    // hides it while the keys are on the menu.
    bool covering() const;
    // The window shown (its page may be Explore with the window hidden).
    bool windowShown() const;
    // The checks: an element's border box in dp (the view's for an empty
    // id), an empty box for none; whether it is shown.
    QRectF box(const QString &id) const;
    bool shows(const QString &id) const;

private:
    struct Data;
    Ui::Screen &screen;
    std::unique_ptr<Data> d;
    std::function<void()> exitHandler;
    Page current = Page::Main;
    bool loaded = false;
    bool failed = false;
    bool showing = true;

    bool load();
    // The keys into the window: its entry once the model made it.
    void enterWindow();
    // The window over the menu hidden while the keys are on the menu, and
    // shown again when the view no longer puts it over the menu.
    void placeWindow();
    // The keys back to the menu's Explore item.
    void toMenu();
};

}

#endif // EXPLOREMENU_H
