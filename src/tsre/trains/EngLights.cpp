/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/trains/EngLights.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <QFile>
#include <QHash>
#include <cmath>

namespace {

float number(FileBuffer *data, float fallback = 0.0f) {
    bool ok = true;
    const float value = ParserX::GetNumberInside(data, &ok);
    return ok ? value : fallback;
}

void numbers(FileBuffer *data, float *out, int count) {
    for (int i = 0; i < count; ++i)
        out[i] = number(data);
}

void readCondition(FileBuffer *data, EngLights::Condition &c) {
    QString sh;
    while (!(sh = ParserX::NextTokenInside(data).toLower()).isEmpty()) {
        int *value = sh == "headlight" ? &c.headlight
                : sh == "unit" ? &c.unit
                : sh == "penalty" ? &c.penalty
                : sh == "control" ? &c.control
                : sh == "service" ? &c.service
                : sh == "timeofday" ? &c.timeOfDay
                : sh == "weather" ? &c.weather
                : sh == "coupling" ? &c.coupling
                : sh == "ortsbattery" ? &c.battery
                : sh == "brake" ? &c.brake
                : sh == "reverser" ? &c.reverser
                : sh == "doors" ? &c.doors
                : sh == "horn" ? &c.horn
                : sh == "bell" ? &c.bell
                : sh == "mu" ? &c.mu
                : nullptr;
        if (value != nullptr)
            *value = int(number(data));
        ParserX::SkipToken(data);
    }
}

void readState(FileBuffer *data, EngLights::State &s) {
    QString sh;
    while (!(sh = ParserX::NextTokenInside(data).toLower()).isEmpty()) {
        if (sh == "duration")
            s.duration = number(data);
        else if (sh == "lightcolour") {
            bool ok = false;
            const quint32 colour = ParserX::GetStringInside(data).toUInt(&ok, 16);
            if (ok)
                s.colour = colour;
        } else if (sh == "position")
            numbers(data, s.position, 3);
        else if (sh == "radius")
            s.radius = number(data);
        else if (sh == "azimuth")
            numbers(data, s.azimuth, 3);
        else if (sh == "elevation")
            numbers(data, s.elevation, 3);
        else if (sh == "transition")
            s.transition = number(data) >= 1.0f;
        else if (sh == "angle")
            s.angle = number(data);
        ParserX::SkipToken(data);
    }
}

void readLight(FileBuffer *data, EngLights::Light &light) {
    QString sh;
    while (!(sh = ParserX::NextTokenInside(data).toLower()).isEmpty()) {
        if (sh == "type")
            light.type = int(number(data)) == EngLights::Cone ? EngLights::Cone : EngLights::Glow;
        else if (sh == "conditions") {
            light.conditions.push_back(EngLights::Condition());
            readCondition(data, light.conditions.back());
        } else if (sh == "cycle")
            light.cycle = int(number(data)) != 0;
        else if (sh == "fadein")
            light.fadeIn = number(data);
        else if (sh == "fadeout")
            light.fadeOut = number(data);
        else if (sh == "states") {
            // The count is skipped with the rest; the states written count.
            QString state;
            while (!(state = ParserX::NextTokenInside(data).toLower()).isEmpty()) {
                if (state == "state") {
                    light.states.push_back(EngLights::State());
                    readState(data, light.states.back());
                }
                ParserX::SkipToken(data);
            }
        } else if (sh == "shapeindex")
            light.shapeIndex = int(number(data, -1.0f));
        else if (sh == "shapehierarchy")
            light.shapeHierarchy = ParserX::GetStringInside(data).toUpper();
        else if (sh == "ortsgraphic")
            light.graphic = ParserX::GetStringInside(data).toUpper();
        ParserX::SkipToken(data);
    }
}

bool sceneDay = true;
int sceneWeather = EngLights::Train::Clear;

// Open Rails' copy of a cone facing the other way (LightState and
// LightCondition with reverse).
EngLights::Light reversedCone(const EngLights::Light &cone) {
    EngLights::Light out = cone;
    out.reversed = true;
    for (EngLights::State &s : out.states) {
        for (float &a : s.azimuth)
            a = std::fmod(a + 180.0f, 360.0f);
        s.position[0] = -s.position[0];
        s.position[2] = -s.position[2];
    }
    for (EngLights::Condition &c : out.conditions) {
        if (c.unit == 2)
            c.unit = 5;
        else if (c.unit == 3)
            c.unit = 4;
    }
    return out;
}

}

void EngLights::State::direction(float *out) const {
    const float az = azimuth[1] * float(M_PI) / 180.0f;
    const float el = elevation[1] * float(M_PI) / 180.0f;
    out[0] = std::cos(el) * std::sin(az);
    out[1] = -std::sin(el);
    out[2] = std::cos(el) * std::cos(az);
}

float EngLights::State::halfAngle() const {
    return angle * float(M_PI) / 360.0f;
}

float EngLights::State::reach() const {
    const float s = std::sin(halfAngle());
    return s > 1e-4f ? radius * 0.5f / s : 0.0f;
}

bool EngLights::Train::operator==(const Train &o) const {
    return headlight == o.headlight && first == o.first && last == o.last && reversed == o.reversed
            && penalty == o.penalty && player == o.player && inService == o.inService && day == o.day
            && weather == o.weather && coupledFront == o.coupledFront && coupledRear == o.coupledRear
            && battery == o.battery && brakeApplied == o.brakeApplied && reverser == o.reverser
            && leftDoorOpen == o.leftDoorOpen && rightDoorOpen == o.rightDoorOpen && horn == o.horn
            && bell == o.bell && mu == o.mu;
}

void EngLights::read(FileBuffer *data) {
    lights.clear();
    graphic.clear();
    QString sh;
    // The count is skipped with the rest (Open Rails: it is often wrong).
    while (!(sh = ParserX::NextTokenInside(data).toLower()).isEmpty()) {
        if (sh == "light") {
            Light light;
            light.index = lights.size();
            readLight(data, light);
            lights.push_back(light);
        } else if (sh == "ortsgraphic")
            graphic = ParserX::GetStringInside(data).toUpper();
        ParserX::SkipToken(data);
    }
    const int written = lights.size();
    for (int i = 0; i < written; ++i)
        if (lights[i].type == Cone)
            lights.push_back(reversedCone(lights[i]));
}

std::shared_ptr<const EngLights> EngLights::fromFile(const QString &file, const QString &includePath,
                                                     const QString &mstsIncludePath) {
    // Cars of one file share it; variants of a locomotive often include one
    // large block (the Vectron's has 1101 lights).
    static QHash<QString, std::shared_ptr<const EngLights>> read;
    const QString key = file + QLatin1Char('|') + includePath;
    const auto found = read.constFind(key);
    if (found != read.constEnd())
        return *found;
    std::shared_ptr<const EngLights> out;
    QFile source(file);
    if (source.open(QIODevice::ReadOnly)) {
        FileBuffer *data = ReadFile::read(&source);
        source.close();
        data->toUtf16();
        data->skipBOM();
        auto include = [&]() {
            const QString path = ParserX::GetStringInside(data);
            ParserX::SkipToken(data);
            data->insertFile(includePath + "/" + path, mstsIncludePath + "/" + path);
        };
        // The walk of Eng::load, keeping only the Wagon section's lights.
        bool named = false;
        QString sh;
        while (!(sh = ParserX::NextTokenInside(data).toLower()).isEmpty()) {
            if (sh.startsWith("simisa"))
                continue;
            if (sh == "include") {
                include();
                continue;
            }
            if (sh == "wagon") {
                if (!named)
                    ParserX::GetString(data);
                named = true;
                while (!(sh = ParserX::NextTokenInside(data).toLower()).isEmpty()) {
                    if (sh == "include") {
                        include();
                        continue;
                    }
                    if (sh == "lights") {
                        // A later block replaces an earlier one; Open Rails
                        // refuses one without lights.
                        auto block = std::make_shared<EngLights>();
                        block->read(data);
                        out = block->lights.isEmpty() ? nullptr : block;
                    }
                    ParserX::SkipToken(data);
                }
            }
            ParserX::SkipToken(data);
        }
        delete data;
    }
    read.insert(key, out);
    return out;
}

bool EngLights::holds(const Condition &c, const Train &t) {
    switch (c.headlight) {
    case 0: break;
    case 1: if (t.headlight != Train::HeadlightOff) return false; break;
    case 2: if (t.headlight != Train::HeadlightDim) return false; break;
    case 3: if (t.headlight != Train::HeadlightBright) return false; break;
    case 4: if (t.headlight < Train::HeadlightDim) return false; break;
    case 5: if (t.headlight == Train::HeadlightDim) return false; break;
    case 6: if (t.headlight > Train::HeadlightDim) return false; break;
    default: return false;
    }
    switch (c.unit) {
    case 0: break;
    case 1: if (t.first || t.last) return false; break;
    case 2: if (!t.first || t.reversed) return false; break;
    case 3: if (!t.last || t.reversed) return false; break;
    case 4: if (!t.last || !t.reversed) return false; break;
    case 5: if (!t.first || !t.reversed) return false; break;
    default: return false;
    }
    // Two-valued conditions: 1 for the first answer, 2 for the second.
    auto two = [](int value, bool second) { return value == 0 || (value == 1 && !second) || (value == 2 && second); };
    if (!two(c.penalty, t.penalty) || !two(c.control, t.player) || !two(c.service, t.inService)
            || !two(c.timeOfDay, !t.day) || !two(c.battery, !t.battery) || !two(c.brake, t.brakeApplied)
            || !two(c.horn, t.horn) || !two(c.bell, t.bell))
        return false;
    switch (c.weather) {
    case 0: break;
    case 1: if (t.weather != Train::Clear) return false; break;
    case 2: if (t.weather != Train::Rain) return false; break;
    case 3: if (t.weather != Train::Snow) return false; break;
    default: return false;
    }
    switch (c.coupling) {
    case 0: break;
    case 1: if (!t.coupledFront || t.coupledRear) return false; break;
    case 2: if (t.coupledFront || !t.coupledRear) return false; break;
    case 3: if (!t.coupledFront || !t.coupledRear) return false; break;
    default: return false;
    }
    switch (c.reverser) {
    case 0: break;
    case 1: if (t.reverser != 1) return false; break;
    case 2: if (t.reverser != -1) return false; break;
    case 3: if (t.reverser != 0) return false; break;
    case 4: if (t.reverser == 0) return false; break;
    case 5: if (t.reverser < 0) return false; break;
    case 6: if (t.reverser > 0) return false; break;
    default: return false;
    }
    switch (c.doors) {
    case 0: break;
    case 1: if (t.leftDoorOpen || t.rightDoorOpen) return false; break;
    case 2: if (!t.leftDoorOpen) return false; break;
    case 3: if (!t.rightDoorOpen) return false; break;
    case 4: if (!t.leftDoorOpen || !t.rightDoorOpen) return false; break;
    case 5: if (!t.leftDoorOpen && !t.rightDoorOpen) return false; break;
    default: return false;
    }
    switch (c.mu) {
    case 0: break;
    case 1: if (t.mu != -1) return false; break;
    case 2: if (t.mu == 0) return false; break;
    case 3: if (t.mu != 0) return false; break;
    default: return false;
    }
    return true;
}

void EngLights::setEnvironment(bool day, int weather) {
    sceneDay = day;
    sceneWeather = weather;
}

bool EngLights::environmentDay() {
    return sceneDay;
}

int EngLights::environmentWeather() {
    return sceneWeather;
}

bool EngLights::isOn(const Light &light, const Train &train) {
    if (light.conditions.isEmpty())
        return true;
    for (const Condition &c : light.conditions)
        if (holds(c, train))
            return true;
    return false;
}
