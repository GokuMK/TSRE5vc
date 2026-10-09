/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SIGNALTYPE_H
#define	SIGNALTYPE_H

#include <QString>
#include <QVector>

// A signal type of sigcfg.dat: its lights, draw states and aspects. Names of
// types, lights, light textures and draw states are lower case, as Open Rails
// matches them. Values the editor does not use yet (flashing, semaphores,
// speeds, Open Rails extensions) are kept for later work.
class SignalType {
public:
    // MSTS aspects from the most restrictive; the order ranks them.
    enum Aspect {
        STOP = 0, STOP_AND_PROCEED, RESTRICTING, APPROACH_1, APPROACH_2, APPROACH_3,
        CLEAR_1, CLEAR_2, UNKNOWN_ASPECT
    };

    struct Light {
        int index = 0;
        // LightsTab entry, lower case.
        QString name;
        float position[3] = {0.0f, 0.0f, 0.0f};
        float radius = 0.0f;
        // Dark while a semaphore arm moves.
        bool semaphoreChange = false;
        // ORTSSignalLightTex, lower case; empty uses the type's.
        QString lightTexture;
    };

    struct DrawLight {
        int light = 0;
        bool flashing = false;
    };

    struct DrawState {
        int index = 0;
        // Lower case.
        QString name;
        QVector<DrawLight> lights;
        float semaphorePos = 0.0f;
    };

    struct AspectEntry {
        Aspect aspect = UNKNOWN_ASPECT;
        // Draw state name, lower case.
        QString drawState;
        // Negative when not given.
        float speedMpS = -1.0f;
        bool asap = false;
        bool speedReset = false;
        bool noSpeedReduction = false;
    };

    // Lower case.
    QString name;
    // SignalFnType in upper case: an MSTS function or an Open Rails one
    // declared in ORTSSignalFunctions.
    QString function = "NORMAL";
    // ORTSNormalSubtype, upper case.
    QString normalSubtype;
    // SignalLightTex, lower case.
    QString lightTexture;
    // ORTSScript, lower case.
    QString script;
    bool abs = false;
    bool noGantry = false;
    bool semaphore = false;
    float flashTimeOn = 1.0f;
    float flashTimeOff = 1.0f;
    // ORTSOnOffTimes: seconds a light takes to turn on or off.
    float onOffTime = 0.2f;
    // SemaphoreInfo: seconds a semaphore arm takes to move.
    float semaphoreInfo = 1.0f;
    // SignalNumClearAhead values in file order (MSTS, then Open Rails).
    QVector<int> numClearAhead;
    // ORTSDayGlow and ORTSNightGlow; negative when not given.
    float dayGlow = -1.0f;
    float nightGlow = -1.0f;
    // ORTSDayLight: false hides the lights by day.
    bool dayLight = true;
    // ORTSReqStopVisDistance, ORTSReqStopAnnDistance; negative when not given.
    float reqStopVisDistance = -1.0f;
    float reqStopAnnDistance = -1.0f;
    // ApproachControlSettings in metres and metres per second; negative when
    // not given.
    float approachControlPositionM = -1.0f;
    float approachControlSpeedMpS = -1.0f;
    // Sorted by index.
    QVector<Light> lights;
    // In file order; duplicate names are kept under a generated name.
    QVector<DrawState> drawStates;
    QVector<AspectEntry> aspects;

    const Light *light(int index) const;
    const DrawState *drawState(const QString &name) const;
    // The draw state of the most restrictive aspect; without aspects the
    // draw state with the lowest index. Null when there is none.
    const DrawState *defaultDrawState() const;
    static Aspect aspectFromName(const QString &name);
};

#endif	/* SIGNALTYPE_H */
