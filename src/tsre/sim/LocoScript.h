#pragma once
#include <QHash>
#include <QString>
#include <memory>
#include <tsre/sim/VehicleModel.h>

namespace Script {
class State;
}

namespace Sim {

class LuaLoco;

// A run's loco scripts (task driving 02): one Lua state of the simulation's
// kind for the run, made on first use in the thread that steps the run,
// each script file loaded once; its functions are shared, each loco has a
// state table of its own. For now every electric engine runs TSRE's
// electric.lua (the user's, 2026-10-28); a vehicle's own script comes later.
//
// A script's functions, all optional but update:
//   init(state, vehicle)        once, `vehicle` its file's keys in SI;
//   update(dt, inputs, state, out)   every step;
//   command(name, value, state[, from])  every message for it; `from`
//       "line" for one from another unit on the control line;
//   save(state)                 what to save (else the state table).
// While they run, `send(name, value)` sends an event (to the UI, the cab,
// the sounds) and `line(name, value)` a message to the train's other units,
// also from the modules they call. `require` finds modules in the scripts'
// folder; the parts TSRE's locos share are there in tsre/ (require
// "tsre.brakes"), for any loco script to use.
//
// `vehicle` reads its file through the key catalogue, in SI:
//   vehicle.engine.maxforce, vehicle.wagon.mass  a key's first value (a
//       block of blocks gives a table to index further);
//   vehicle.number(path [, index [, default]])  ("engine(maxforce", 1-based);
//   vehicle.text(path [, index]);
//   vehicle.values(path)   the last such block's values, in order;
//   vehicle.all(path)      every such block's values, a table each;
//   vehicle.blocks(path)   the blocks in the last such block, each
//       {name, values} (curves: their throttle values are block names);
//   vehicle.has(path).
// Don't keep `vehicle` in the state table: it isn't saved.
class LocoScripts {
public:
    // `folder`: TSRE's loco scripts (appdata/<version>/scripts/loco).
    explicit LocoScripts(const QString &folder, unsigned seed = 0);
    ~LocoScripts();
    LocoScripts(const LocoScripts &) = delete;
    LocoScripts &operator=(const LocoScripts &) = delete;

    // The script a vehicle runs: its file, empty for none.
    QString scriptFor(const VehicleDefinition &definition) const;
    // A scripted vehicle's model; null for a vehicle without a script.
    std::unique_ptr<VehicleModel> make(const std::shared_ptr<const VehicleDefinition> &definition);

    Script::State &lua();
    // send() and line(): a message from the loco whose script runs, an
    // event or one for the control line.
    void deliver(const Message &message, bool toLine);

private:
    friend class LuaLoco;
    QString folder;
    unsigned seed;
    // Shared with its locos' models, which release their tables in it.
    std::shared_ptr<Script::State> state;
    // Script numbers by file (-1: it failed to load), and why.
    QHash<QString, int> loaded;
    QHash<QString, QString> errors;
    // The loco whose script runs (send() and line() write to it).
    LuaLoco *current = nullptr;

    int load(const QString &path);
};

}
