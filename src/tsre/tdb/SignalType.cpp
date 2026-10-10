/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tdb/SignalType.h>

const SignalType::Light *SignalType::light(int index) const {
    for (const Light &l : lights)
        if (l.index == index)
            return &l;
    return nullptr;
}

const SignalType::DrawState *SignalType::drawState(const QString &stateName) const {
    for (const DrawState &state : drawStates)
        if (state.name == stateName)
            return &state;
    return nullptr;
}

const SignalType::DrawState *SignalType::defaultDrawState() const {
    const AspectEntry *restrictive = nullptr;
    for (const AspectEntry &entry : aspects)
        if (entry.aspect != UNKNOWN_ASPECT && (restrictive == nullptr || entry.aspect < restrictive->aspect))
            restrictive = &entry;
    if (restrictive != nullptr)
        return drawState(restrictive->drawState);
    const DrawState *lowest = nullptr;
    for (const DrawState &state : drawStates)
        if (lowest == nullptr || state.index < lowest->index)
            lowest = &state;
    return lowest;
}

SignalType::Aspect SignalType::aspectFromName(const QString &name) {
    static const char *const names[] = {"STOP", "STOP_AND_PROCEED", "RESTRICTING", "APPROACH_1",
                                        "APPROACH_2", "APPROACH_3", "CLEAR_1", "CLEAR_2"};
    for (int i = 0; i < int(sizeof(names) / sizeof(names[0])); ++i)
        if (name.compare(QLatin1String(names[i]), Qt::CaseInsensitive) == 0)
            return Aspect(i);
    return UNKNOWN_ASPECT;
}
