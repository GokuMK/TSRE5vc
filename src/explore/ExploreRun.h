/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef EXPLORERUN_H
#define EXPLORERUN_H

#include <explore/ExploreChoice.h>

#include <QObject>
#include <QString>
#include <functional>
#include <memory>

class Activity;
class RouteEditorGLWidget;

namespace Ui {
class Screen;
}

namespace Explore {

// A run of explore mode (task explore 02): the route chosen loaded behind a
// loading screen, its activity (an activity, a timetable set with the
// player's train, or a new one in memory for exploring), the day, season
// and weather, and the player's train driven from its start, the camera in
// its cab (SimulationController::playDriving). Esc in it shows the game
// menu (appdata/<version>/ui/explore-game.rml): Resume, Main menu, Exit.
class Run : public QObject {
public:
    enum class State { None, Loading, Running };

    // `menu`: the main menu back (a run left, or one that did not start,
    // with why); `exit` quits explore mode. The view outlives it.
    Run(RouteEditorGLWidget &view, std::function<void(const QString &why)> menu, std::function<void()> exit);
    ~Run() override;

    // Each frame, before the screen's update: the documents, the load once
    // the loading screen was drawn.
    void frame(Ui::Screen &screen);
    // The run chosen: the loading screen now, the route at the next frames.
    void start(const RunChoice &choice);
    // The run left (Main menu): the simulation stopped, the route left.
    void leave();
    State state() const { return now; }
    // Running, its train on the track (the loading screen gone).
    bool onTrack() const;
    const RunChoice &choice() const { return chosen; }
    // The run's activity, null without a run.
    Activity *activity() const { return running; }

    // The game menu (the screen's menu handler while a run is on).
    bool showGameMenu();
    bool gameMenuShown() const;
    // An element of the game menu clicked (the checks).
    bool click(const QString &id);

private:
    struct Data;
    RouteEditorGLWidget &view;
    std::unique_ptr<Data> d;
    std::function<void(const QString &)> menuBack;
    std::function<void()> exitHandler;
    State now = State::None;
    RunChoice chosen;
    Activity *running = nullptr;

    void load();
    bool loadDocuments();
    void showLoading();
    // The line under the route's name: the seconds the run has been
    // prepared (-1: none).
    void setLoadingStatus(int seconds);
    void hideLoading();
    void fail(const QString &why);
    Activity *prepareActivity(QString &why);
};

}

#endif // EXPLORERUN_H
