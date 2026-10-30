#include <tsre/sim/DrivingKeys.h>
#include <QDebug>
#include <QKeyEvent>

namespace Sim {

namespace {

using Command = DrivingKeys::Command;

// A lever moved while held: its Increase or Decrease command, true at the
// press, false at the release.
Command held(const char *name) {
    return {QString::fromLatin1(name), true, MessageValue(false)};
}

// A push button: on while held.
Command button(const char *name, const char *on, const char *off) {
    return {QString::fromLatin1(name), QString::fromLatin1(on), MessageValue(QString::fromLatin1(off))};
}

// A switch: the other way at each press.
Command toggle(const char *name) {
    return {QString::fromLatin1(name), QStringLiteral("toggle"), std::nullopt};
}

// A value at each press.
Command step(const char *name, const MessageValue &value) {
    return {QString::fromLatin1(name), value, std::nullopt};
}

struct Default {
    const char *control;
    const char *key;
    std::vector<Command> commands;
};

// Open Rails' default keys (InputSettings.cs, on a US keyboard's places)
// for what TSRE's loco scripts have. Its others are kept free for their
// parts: the sander X, bell B, alerter Z, doors Q, wiper V, cab light L,
// gear E, handbrake Shift+' and Shift+;, retainers Shift+[ and Shift+],
// brake hoses \, initialize brakes Shift+/, quick release Ctrl+/, water
// scoop Y, ejectors J, cylinder compound P, electric train supply Alt+B,
// refill at a pickup T, switches G, change cab Ctrl+E, uncouple U,
// autopilot Alt+A.
std::vector<Default> defaults() {
    return {
        {"ThrottleIncrease", "D", {held("throttleIncrease")}},
        {"ThrottleDecrease", "A", {held("throttleDecrease")}},
        {"ThrottleZero", "Ctrl+A", {step("throttle", 0.0)}},
        {"Forwards", "W", {held("directionIncrease")}},
        {"Backwards", "S", {held("directionDecrease")}},
        {"TrainBrakeIncrease", "'", {held("trainBrakeIncrease")}},
        {"TrainBrakeDecrease", ";", {held("trainBrakeDecrease")}},
        {"TrainBrakeZero", "Ctrl+;", {step("trainBrake", 0.0)}},
        {"EngineBrakeIncrease", "]", {held("engineBrakeIncrease")}},
        {"EngineBrakeDecrease", "[", {held("engineBrakeDecrease")}},
        {"DynamicBrakeIncrease", ".", {held("dynamicBrakeIncrease")}},
        {"DynamicBrakeDecrease", ",", {held("dynamicBrakeDecrease")}},
        {"BailOff", "/", {button("bailOff", "on", "off")}},
        {"EmergencyPushButton", "Backspace", {toggle("emergency")}},
        {"Horn", "Space", {button("horn", "on", "off")}},
        {"HeadlightIncrease", "H", {step("headlight", QStringLiteral("+"))}},
        {"HeadlightDecrease", "Shift+H", {step("headlight", QStringLiteral("-"))}},
        {"Pantograph1", "P", {toggle("pantograph1")}},
        {"Pantograph2", "Shift+P", {toggle("pantograph2")}},
        {"Pantograph3", "Ctrl+P", {toggle("pantograph3")}},
        {"Pantograph4", "Ctrl+Shift+P", {toggle("pantograph4")}},
        {"CircuitBreakerClosingOrder", "O",
         {button("circuitBreaker", "close", "released"), button("tractionCutOffRelay", "close", "released"),
          toggle("injector2")}},
        {"CircuitBreakerOpeningOrder", "I",
         {button("circuitBreaker", "open", "released"), button("tractionCutOffRelay", "open", "released"),
          toggle("injector1")}},
        {"CircuitBreakerClosingAuthorization", "Shift+O", {toggle("circuitBreakerAuthorization")}},
        {"BatterySwitchClose", "Insert", {step("battery", QStringLiteral("on"))}},
        {"BatterySwitchOpen", "Ctrl+Insert", {step("battery", QStringLiteral("off"))}},
        {"MasterKey", "Enter", {toggle("masterKey")}},
        {"DieselPlayer", "Shift+Y", {toggle("engine")}},
        {"DieselHelper", "Ctrl+Y", {toggle("helperEngines")}},
        {"SteamHeatIncrease", "Alt+U", {held("steamHeatIncrease")}},
        {"SteamHeatDecrease", "Alt+D", {held("steamHeatDecrease")}},
        {"ImmediateRefill", "Ctrl+T", {step("refuel", true), step("refill", true)}},
        {"FiringRateIncrease", "R", {held("firingRateIncrease")}},
        {"FiringRateDecrease", "Shift+R", {held("firingRateDecrease")}},
        {"FireShovelFull", "Ctrl+R", {step("shovel", true)}},
        {"FireboxOpen", "F", {held("fireDoorIncrease")}},
        {"FireboxClose", "Shift+F", {held("fireDoorDecrease")}},
        {"Firing", "Ctrl+F", {toggle("manualFiring")}},
        {"AIFireOn", "Alt+H", {step("fireOn", true)}},
        {"AIFireOff", "Ctrl+H", {step("fireOff", true)}},
        {"AIFireReset", "Ctrl+Alt+H", {step("fireReset", true)}},
        {"BlowerIncrease", "N", {held("blowerIncrease")}},
        {"BlowerDecrease", "Shift+N", {held("blowerDecrease")}},
        {"DamperIncrease", "M", {held("damperIncrease")}},
        {"DamperDecrease", "Shift+M", {held("damperDecrease")}},
        {"Injector1Increase", "K", {held("injector1WaterIncrease")}},
        {"Injector1Decrease", "Shift+K", {held("injector1WaterDecrease")}},
        {"Injector2Increase", "L", {held("injector2WaterIncrease")}},
        {"Injector2Decrease", "Shift+L", {held("injector2WaterDecrease")}},
        {"CylinderCocks", "C", {toggle("cylinderCocks")}},
        {"BlowdownValve", "Shift+C", {toggle("blowdown")}},
        {"GamePause", "Pause", {}},
    };
}

}

DrivingKeys::DrivingKeys() {
    for (Default &d : defaults()) {
        Binding binding;
        binding.control = QString::fromLatin1(d.control);
        binding.stroke = KeyPosition::parse(QString::fromLatin1(d.key));
        binding.commands = std::move(d.commands);
        binding.pause = binding.control == QLatin1String("GamePause");
        if (!binding.stroke.valid())
            qWarning() << "DrivingKeys: no key" << d.key << "for" << d.control;
        else
            list.push_back(std::move(binding));
    }
}

const DrivingKeys::Binding *DrivingKeys::find(const KeyPosition::Stroke &stroke) const {
    for (const Binding &binding : list)
        if (binding.stroke == stroke)
            return &binding;
    return nullptr;
}

void DrivingKeys::send(const Binding &binding, bool press) {
    if (binding.pause) {
        if (press && sink.pause)
            sink.pause();
        return;
    }
    if (!sink.command)
        return;
    for (const Command &command : binding.commands) {
        if (press)
            sink.command(command.name, command.press);
        else if (command.release.has_value())
            sink.command(command.name, *command.release);
    }
}

bool DrivingKeys::keyPress(const QKeyEvent &event) {
    const int position = KeyPosition::of(event);
    if (position == 0)
        return false;
    const auto down = held.find(position);
    const Binding *binding = find({position, KeyPosition::modifiers(event.modifiers())});
    if (event.isAutoRepeat())
        return down != held.end() || binding != nullptr;
    if (binding == nullptr)
        return false;
    // Pressed again without its release seen: that one ended first.
    if (down != held.end()) {
        send(*down->second, false);
        held.erase(down);
    }
    send(*binding, true);
    for (const Command &command : binding->commands)
        if (command.release.has_value()) {
            held[position] = binding;
            break;
        }
    return true;
}

bool DrivingKeys::keyRelease(const QKeyEvent &event) {
    const int position = KeyPosition::of(event);
    if (position == 0)
        return false;
    const auto down = held.find(position);
    if (event.isAutoRepeat())
        return down != held.end();
    if (down == held.end())
        return find({position, KeyPosition::modifiers(event.modifiers())}) != nullptr;
    const Binding *binding = down->second;
    held.erase(down);
    send(*binding, false);
    return true;
}

void DrivingKeys::releaseKeys() {
    const std::map<int, const Binding *> ending = std::move(held);
    held.clear();
    for (const auto &key : ending)
        send(*key.second, false);
}

}
