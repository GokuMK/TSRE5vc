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

#include <QString>
#include <functional>
#include <memory>

namespace Ui {
class Screen;
}

namespace Explore {

// Explore mode's menus on its view's UI screen (task explore 01): the main
// menu on the left (Explore, Multiplayer, Options, Exit) and the window its
// Explore opens (empty until task 02): appdata/<version>/ui/explore-menu.rml
// and explore-window.rml.
// Each is shown as a menu (modal: the keys are its own): the arrows move,
// Enter or a click opens, the mouse over an item chooses it. Esc does not
// close the main menu; in the Explore window it goes back to it.
class Menu {
public:
    enum class Page { Main, Explore };

    // The screen outlives it. `exit` quits explore mode.
    Menu(Ui::Screen &screen, std::function<void()> exit);
    ~Menu();
    // Each frame before the screen's update: the documents loaded the first
    // time, the page shown when nothing shows it.
    void frame();
    // False once a document did not load.
    bool usable() const { return !failed; }
    Page page() const { return current; }
    void open(Page page);
    // The element with this id in the page shown clicked (the checks); false
    // when there is none.
    bool click(const QString &id);
    // The id of the element with the keys' focus, empty for none.
    QString focused() const;

private:
    struct Data;
    Ui::Screen &screen;
    std::unique_ptr<Data> d;
    std::function<void()> exitHandler;
    Page current = Page::Main;
    bool loaded = false;
    bool failed = false;

    bool load();
};

}

#endif // EXPLOREMENU_H
