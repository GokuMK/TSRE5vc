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
#include <QString>
#include <memory>

class Renderer;

namespace Ui {

class RenderInterface;

// The heads-up display as a UI document (task scripting 01, the first one):
// appdata/<version>/ui/hud.rml shows the ridden train's speed, distance,
// throttle and brake through the data model `train`, each frame's values
// also sent to it as the event `trainupdate` (parameters speed, distance,
// throttle, brake in SI and 0 to 1, time in seconds) for its Lua. Drawn by
// the view's renderer over the finished frame.
class Hud {
public:
    // What it shows.
    struct Values {
        float speed = 0.0f;
        float distance = 0.0f;
        float throttle = 0.0f;
        float brake = 0.0f;
    };

    Hud();
    ~Hud();
    // The document over a view of width x height pixels, `scale` times its
    // own size (the display's pixel ratio and the HUD scale). False when
    // the UI or the document cannot be loaded: the caller draws its own.
    bool draw(Renderer *renderer, int width, int height, float scale, const Values &values);
    // False once the UI or the document failed to load.
    bool usable() const { return !failed; }

private:
    struct Model;
    std::unique_ptr<RenderInterface> renderInterface;
    std::unique_ptr<Model> model;
    QElapsedTimer clock;
    bool failed = false;

    bool load(int width, int height);
};

}

#endif // UIHUD_H
