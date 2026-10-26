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
};
}
