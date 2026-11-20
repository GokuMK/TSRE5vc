/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef EXPLOREWINDOW_H
#define EXPLOREWINDOW_H

#include <QSize>
#include <QString>
#include <QWidget>
#include <functional>
#include <memory>

class RouteEditorGLWidget;

namespace Explore {

class Menu;

// Explore mode's window (task explore 01): the 3D view alone, in its
// Explore role, with no Qt widget for its interface: the menus are RmlUi
// documents over the view. Its size is core.explore.resolution in physical
// pixels; with core.explore.fillScreen it covers the whole screen, the
// taskbar too, without borders. F11 and Alt+Enter switch that for the
// session. Closing it quits TSRE.
class Window : public QWidget {
    Q_OBJECT
public:
    // `route`: chosen first in the Explore window (the Load window's
    // choice, or --route).
    explicit Window(const QString &route = QString());
    ~Window() override;
    // Shown as the settings say.
    void open();

    RouteEditorGLWidget *view() const { return glView; }
    // Null until the view's UI has drawn its first frame.
    Menu *menu() const { return mainMenu.get(); }
    const QString &chosenRoute() const { return route; }
    bool fillsScreen() const { return filling; }
    // Fill screen on or off for the session (F11, Alt+Enter).
    void setFillScreen(bool on);
    // What Exit does: TSRE quits (the checks give their own).
    void setExitHandler(std::function<void()> handler) { exitHandler = std::move(handler); }

    // The game root explore mode starts with: Game::root (this launch's or
    // the settings') when it is one, else the Load window's most recent;
    // empty for none (the Load window opens).
    static QString startRoot();
    // A resolution setting's value ("1280x720") as a size; empty for
    // another text.
    static QSize resolution(const QString &value);
    // The window's size in logical pixels for `pixels` on a screen with
    // that pixel ratio, kept within `available` (logical).
    static QSize logicalSize(const QSize &pixels, qreal ratio, const QSize &available);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    RouteEditorGLWidget *glView = nullptr;
    std::unique_ptr<Menu> mainMenu;
    QString route;
    bool filling = false;
    bool exiting = false;
    std::function<void()> exitHandler;

    // The window's size or full screen as `filling` says.
    void applySize();
    void exit();
};

}

#endif // EXPLOREWINDOW_H
