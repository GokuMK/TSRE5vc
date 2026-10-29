/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <QtGlobal>

// Texts of TSRE's own UI documents (appdata/<version>/ui) by the translation
// ids the documents name (task scripting 01): listed here so lupdate puts
// them in the catalogues. The rmlui suite checks every id a document uses is
// in them.
namespace {
[[maybe_unused]] const char *const UiTexts[] = {
    //% "Speed"
    QT_TRID_NOOP("ui.hud.speed"),
    //% "Distance"
    QT_TRID_NOOP("ui.hud.distance"),
    //% "Throttle"
    QT_TRID_NOOP("ui.hud.throttle"),
    //% "Brake"
    QT_TRID_NOOP("ui.hud.brake"),
    // The driven loco's controls (loco.rml, loco.lua; task driving 02).
    //% "Driving"
    QT_TRID_NOOP("ui.loco.title"),
    //% "Leave"
    QT_TRID_NOOP("ui.loco.leave"),
    //% "Battery"
    QT_TRID_NOOP("ui.loco.battery"),
    //% "Key"
    QT_TRID_NOOP("ui.loco.master.key"),
    //% "On"
    QT_TRID_NOOP("ui.loco.on"),
    //% "Off"
    QT_TRID_NOOP("ui.loco.off"),
    //% "Pantograph 1"
    QT_TRID_NOOP("ui.loco.pantograph1"),
    //% "2"
    QT_TRID_NOOP("ui.loco.pantograph2"),
    //% "Up"
    QT_TRID_NOOP("ui.loco.up"),
    //% "Down"
    QT_TRID_NOOP("ui.loco.down"),
    //% "Breaker"
    QT_TRID_NOOP("ui.loco.breaker"),
    //% "Close"
    QT_TRID_NOOP("ui.loco.close"),
    //% "Open"
    QT_TRID_NOOP("ui.loco.open"),
    //% "Direction"
    QT_TRID_NOOP("ui.loco.direction"),
    //% "R"
    QT_TRID_NOOP("ui.loco.reverse"),
    //% "N"
    QT_TRID_NOOP("ui.loco.neutral"),
    //% "F"
    QT_TRID_NOOP("ui.loco.forward"),
    //% "Throttle"
    QT_TRID_NOOP("ui.loco.throttle"),
    //% "Train brake"
    QT_TRID_NOOP("ui.loco.train.brake"),
    //% "Engine brake"
    QT_TRID_NOOP("ui.loco.engine.brake"),
    //% "Dynamic brake"
    QT_TRID_NOOP("ui.loco.dynamic.brake"),
    //% "Emergency"
    QT_TRID_NOOP("ui.loco.emergency"),
    //% "Horn"
    QT_TRID_NOOP("ui.loco.horn"),
    //% "Lights"
    QT_TRID_NOOP("ui.loco.headlight"),
    //% "All values"
    QT_TRID_NOOP("ui.loco.all.values"),
    //% "Speed"
    QT_TRID_NOOP("ui.loco.speed"),
    //% "Line voltage"
    QT_TRID_NOOP("ui.loco.line.voltage"),
    //% "Current"
    QT_TRID_NOOP("ui.loco.ammeter"),
    //% "Force"
    QT_TRID_NOOP("ui.loco.force"),
    //% "Brake pipe"
    QT_TRID_NOOP("ui.loco.brake.pipe"),
    //% "Main reservoir"
    QT_TRID_NOOP("ui.loco.main.reservoir"),
    //% "Brake cylinder"
    QT_TRID_NOOP("ui.loco.brake.cylinder"),
    //% "Pantographs"
    QT_TRID_NOOP("ui.loco.pantographs"),
    //% "Power"
    QT_TRID_NOOP("ui.loco.power"),
    // The diesel's (task driving 03).
    //% "Engine"
    QT_TRID_NOOP("ui.loco.engine"),
    //% "Helpers"
    QT_TRID_NOOP("ui.loco.helpers"),
    //% "Traction relay"
    QT_TRID_NOOP("ui.loco.relay"),
    //% "Engine RPM"
    QT_TRID_NOOP("ui.loco.engine.rpm"),
    //% "Fuel"
    QT_TRID_NOOP("ui.loco.fuel"),
    //% "Oil pressure"
    QT_TRID_NOOP("ui.loco.oil.pressure"),
    //% "Engine temperature"
    QT_TRID_NOOP("ui.loco.engine.temperature"),
};
}
