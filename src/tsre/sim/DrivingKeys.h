#pragma once
#include <functional>
#include <map>
#include <optional>
#include <vector>
#include <QString>
#include <tsre/KeyPosition.h>
#include <tsre/KeyTaker.h>
#include <tsre/sim/VehicleModel.h>

namespace Sim {

// The driven train's keys (task driving 07): Open Rails' default keys for
// the controls TSRE's loco scripts have, on the keys' places
// (KeyPosition), sent to the driven cab as the commands the driving panel
// sends. A key sends all its commands: a loco without a part ignores that
// part's, as in Open Rails, where O closes an electric's circuit breaker, a
// diesel's traction cut-off relay and a steam loco's injector 2.
//
// The driven train's consist gives it to the view (GameObj::keyTaker)
// while the camera rides it; the simulation controller holds the one there
// is.
class DrivingKeys : public KeyTaker {
public:
    // A command with the value a press sends and, for a key held (a lever
    // moved while held, a push button), the value its release sends.
    struct Command {
        QString name;
        MessageValue press;
        std::optional<MessageValue> release;
    };
    struct Binding {
        // Open Rails' name for it (its UserCommand without "Control").
        QString control;
        KeyPosition::Stroke stroke;
        std::vector<Command> commands;
        // The run paused or run on (GamePause) instead.
        bool pause = false;
    };
    // Where they go: the driven cab's commands, the run's pause.
    struct Sink {
        std::function<void(const QString &, const MessageValue &)> command;
        std::function<void()> pause;
    } sink;

    // With Open Rails' defaults.
    DrivingKeys();
    const std::vector<Binding> &bindings() const { return list; }
    // A key's binding (exact modifiers), or nullptr.
    const Binding *find(const KeyPosition::Stroke &stroke) const;

    // A bound key is used, its auto-repeats too (they send nothing). Its
    // release ends what its press started, whatever the modifiers are by
    // then (Shift let go before R: the firing rate still stops falling).
    bool keyPress(const QKeyEvent &event) override;
    bool keyRelease(const QKeyEvent &event) override;
    void releaseKeys() override;

private:
    void send(const Binding &binding, bool press);
    std::vector<Binding> list;
    // Keys held: by their place, the binding their press started.
    std::map<int, const Binding *> held;
};

}
