/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ENGLIGHTS_H
#define ENGLIGHTS_H

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <memory>

class FileBuffer;

// The lights of a car from the Lights block of its ENG or WAG file (task
// renderer 34): everything Open Rails reads (Orts.Formats.Msts/
// LightCollection.cs), and which of them a train's state turns on, by Open
// Rails' rules (Viewer3D/Lights.cs).
class EngLights {
public:
    enum Type { Glow = 0, Cone = 1 };

    // A light's conditions as the file writes them; 0 ignores one.
    struct Condition {
        // 1 off, 2 dim, 3 bright, 4 dim or bright, 5 off or bright, 6 off or dim.
        int headlight = 0;
        // 1 middle, 2 first, 3 last, 4 last reversed, 5 first reversed.
        int unit = 0;
        int penalty = 0;    // 1 no, 2 yes
        int control = 0;    // 1 AI, 2 player
        int service = 0;    // 1 no, 2 yes
        int timeOfDay = 0;  // 1 day, 2 night
        int weather = 0;    // 1 clear, 2 rain, 3 snow
        int coupling = 0;   // 1 front, 2 rear, 3 both
        int battery = 0;    // 1 on, 2 off (ORTSBattery)
        int brake = 0;      // 1 released, 2 applied
        // 1 forward, 2 reverse, 3 neutral, 4 forward or reverse, 5 forward or
        // neutral, 6 reverse or neutral.
        int reverser = 0;
        int doors = 0;      // 1 closed, 2 left, 3 right, 4 both, 5 left or right
        int horn = 0;       // 1 off, 2 sounding
        int bell = 0;       // 1 off, 2 ringing
        int mu = 0;         // 1 the lead locomotive, 2 local, 3 remote
    };

    // One of a light's states (a flashing light cycles through them).
    struct State {
        float duration = 0.0f;
        quint32 colour = 0;  // ARGB
        // In the car shape's space (MSTS: x right, y up, z forward).
        float position[3] = {0.0f, 0.0f, 0.0f};
        // A glow's width; a cone's diameter at its reach.
        float radius = 0.0f;
        // Degrees; Open Rails uses the middle values.
        float azimuth[3] = {0.0f, 0.0f, 0.0f};
        float elevation[3] = {0.0f, 0.0f, 0.0f};
        bool transition = false;
        // A cone's full angle, degrees.
        float angle = 0.0f;
        // The way it shines in the car shape's space, as Open Rails turns
        // forward by the azimuth (to the right) and the elevation (down).
        void direction(float *out) const;
        // A cone's half-angle (radians) and reach (metres).
        float halfAngle() const;
        float reach() const;
    };

    struct Light {
        // Its place in the block; a reversed cone has its cone's.
        int index = 0;
        Type type = Glow;
        bool cycle = false;
        float fadeIn = 0.0f;
        float fadeOut = 0.0f;
        QVector<State> states;
        // Any one holding turns the light on; none: always on.
        QVector<Condition> conditions;
        int shapeIndex = -1;
        QString shapeHierarchy;  // upper case
        QString graphic;         // upper case
        // The copy of a cone facing backwards that Open Rails adds, as MSTS
        // made them.
        bool reversed = false;
    };

    // What Open Rails evaluates for a car (LightViewer.UpdateState). The
    // defaults are a train in service with its headlights at Bright, the
    // player's (task renderer 34's test state until the simulation sets it).
    struct Train {
        enum Headlight { HeadlightOff = 0, HeadlightDim, HeadlightBright };
        enum Weather { Clear = 0, Rain, Snow };
        int headlight = HeadlightBright;
        bool first = true;
        bool last = true;
        bool reversed = false;
        bool penalty = false;
        bool player = true;
        bool inService = true;
        bool day = true;
        int weather = Clear;
        bool coupledFront = false;
        bool coupledRear = false;
        bool battery = true;
        bool brakeApplied = false;
        // -1 reverse, 0 neutral, 1 forward (for this car).
        int reverser = 1;
        bool leftDoorOpen = false;
        bool rightDoorOpen = false;
        bool horn = false;
        bool bell = false;
        // -1 the lead locomotive, 1 a locomotive with it, 0 otherwise.
        int mu = -1;
        bool operator==(const Train &o) const;
        bool operator!=(const Train &o) const { return !(*this == o); }
    };

    QVector<Light> lights;
    // The block's own glow texture (ORTSGraphic), upper case; empty for
    // Open Rails' default.
    QString graphic;

    // Reads the block from just inside its parenthesis (after ParserX
    // returned "lights") to its end; the reversed cones follow the others.
    void read(FileBuffer *data);
    // The Lights block of an ENG or WAG file's Wagon section, includes
    // resolved as Eng::load resolves them (in includePath, else
    // mstsIncludePath); the last block when there are several, null without
    // one. Read once per file and include path, then shared.
    static std::shared_ptr<const EngLights> fromFile(const QString &file, const QString &includePath,
                                                     const QString &mstsIncludePath);
    static bool isOn(const Light &light, const Train &train);
    static bool holds(const Condition &condition, const Train &train);

    // The scene's day (the sun up) and weather (Train::Weather) for the
    // conditions on them: the editor sets them each frame, as Open Rails
    // takes them from its sky and weather.
    static void setEnvironment(bool day, int weather);
    static bool environmentDay();
    static int environmentWeather();
};

#endif
