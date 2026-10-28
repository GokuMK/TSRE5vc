/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef UILOCOCONTROLS_H
#define UILOCOCONTROLS_H

#include <QHash>
#include <QString>
#include <functional>
#include <memory>
#include <vector>
#include <tsre/sim/VehicleModel.h>

struct lua_State;

namespace Ui {

class Screen;

// The driven loco's controls (task driving 02): appdata/<version>/ui/
// loco.rml, a panel dragged by its title over the view while a train is
// driven, with buttons for its controls and its values. Its Lua gets a
// global `train`:
//   train.command(name [, value])  a message to the driven train's cab, any
//       name, queued to the simulation;
//   train.leave()   the train given back to the AI;
//   train.values    the cab's values, SI, as its model publishes them (and
//       speed);
//   train.texts     each of them as shown: the known ones in the user's
//       units, the others as text;
// and the events `trainupdate` each frame and `trainmessage` for each
// message the train's models send (parameters name, value as text,
// vehicle).
class LocoControls {
public:
    using Values = QHash<QString, Sim::MessageValue>;
    // Where train.command and train.leave go (the simulation's controller
    // by default; the tests' own).
    struct Sink {
        std::function<void(const QString &, const Sim::MessageValue &)> command;
        std::function<void()> leave;
    };

    // The screen outlives it.
    explicit LocoControls(Screen &screen);
    ~LocoControls();
    void setSink(Sink sink) { this->sink = std::move(sink); }
    // Shown with the cab's values, the train's speed (m/s) and the messages
    // since the last frame. False when the document cannot be loaded.
    bool update(const Values &values, double speed, const std::vector<std::pair<int, Sim::Message>> &messages);
    void hide();
    bool visible() const;
    bool usable() const { return !failed; }

private:
    Screen &screen;
    struct Document;
    std::unique_ptr<Document> doc;
    Sink sink;
    bool failed = false;

    bool load();
    static int command(lua_State *L);
    static int leave(lua_State *L);
};

}

#endif // UILOCOCONTROLS_H
