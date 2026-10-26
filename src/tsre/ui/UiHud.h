/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef UIHUD_H
#define UIHUD_H

#include <QElapsedTimer>
#include <memory>

namespace Ui {

class Screen;

// The heads-up display as a UI document on a view's screen (task scripting
// 01, the first one): appdata/<version>/ui/hud.rml shows the ridden train's
// speed, distance, throttle and brake through the data model `train`, each
// frame's values also sent to it as the event `trainupdate` (parameters
// speed, distance, throttle, brake in SI and 0 to 1, time in seconds) for
// its Lua. Its panel can be dragged.
class Hud {
public:
    // What it shows.
    struct Values {
        float speed = 0.0f;
        float distance = 0.0f;
        float throttle = 0.0f;
        float brake = 0.0f;
    };

    // The screen outlives it.
    explicit Hud(Screen &screen);
    ~Hud();
    // Shown with this frame's values. False when the document cannot be
    // loaded: the caller draws its own.
    bool update(const Values &values);
    // Not shown (no train ridden).
    void hide();
    // False once the document failed to load.
    bool usable() const { return !failed; }

private:
    struct Model;
    Screen &screen;
    std::unique_ptr<Model> model;
    QElapsedTimer clock;
    bool failed = false;

    bool load();
};

}

#endif // UIHUD_H
