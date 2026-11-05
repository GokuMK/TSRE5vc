/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "FrameLog.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <QtMath>
#include <cmath>
#include <settings/SettingsAccess.h>

namespace FrameLog {

std::atomic<bool> open{false};

namespace {
struct State {
    bool checked = false;
    bool on = false;
    QFile file;
    QTextStream out;
    QElapsedTimer clock;
    qint64 started = -1, previousStart = -1, begun = 0, painted = 0, lastMark = 0;
    bool painting = false;
    double sections[SectionCount] = {};
    double counters[CounterCount] = {};
    std::atomic<int> logLines{0};
    std::atomic<qint64> logNs{0};
    double gpuMs = -1.0;
    double uploads[UploadCount] = {};
    bool hasCamera = false;
    int tileX = 0, tileZ = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f, rotX = 0.0f, rotY = 0.0f;
    double previousWorldX = 0.0, previousWorldZ = 0.0;
    qint64 previousCameraNs = -1;
    bool hasSimulation = false;
    unsigned long long simTick = 0, viewTick = 0;
    double simAgeMs = 0.0, simTime = 0.0, watchTravelled = 0.0, watchSpeed = 0.0, viewTickMs = 0.0;
    int watchTrain = -1;
    int lines = 0;
    // The last lines at exit.
    ~State() {
        if (on)
            out.flush();
    }
};

State &state() {
    static State s;
    return s;
}

double ms(qint64 ns) {
    return ns / 1e6;
}
}

bool enabled() {
    State &s = state();
    if (!s.checked) {
        s.checked = true;
        const QString path = qEnvironmentVariable("TSRE_FRAME_LOG");
        s.on = !path.isEmpty() || Settings::boolean("core.system.frameLog");
        if (s.on) {
            // A file a session: the next start would replace the last one.
            s.file.setFileName(path.isEmpty()
                    ? QStringLiteral("frames-%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"))
                    : path);
            s.on = s.file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
        }
        if (s.on) {
            s.out.setDevice(&s.file);
            s.out << "t_ms,interval_ms,wait_ms,paint_ms,submit_ms,"
                     "whole_view_ms,gather_ms,shadows_ms,enclosure_ms,environment_ms,scene_ms,"
                     "overlay_ms,finish_ms,other_ms,gpu_ms,"
                     "textures_made,texture_ms,mesh_ms,uploads_deferred,"
                     "world_tiles,world_tile_ms,terrain_tiles,terrain_tile_ms,shapes_adopted,"
                     "log_lines,log_ms,tile_x,tile_z,x,z,speed_mps,camera,"
                     "sim_tick,frame_age_ms,sim_s,watch_train,watch_m,watch_mps,view_tick,view_tick_ms,"
                     "texture_kb,mesh_kb,copy_ms,idle_ms,idle_made\n";
            s.clock.start();
            open.store(true, std::memory_order_relaxed);
        }
    }
    return s.on;
}

void frameStart() {
    State &s = state();
    s.started = s.clock.nsecsElapsed();
    s.begun = s.painted = s.started;
}

void frameBegun() {
    state().begun = state().clock.nsecsElapsed();
}

void framePainted() {
    state().painted = state().clock.nsecsElapsed();
}

void paintStart() {
    State &s = state();
    s.lastMark = s.clock.nsecsElapsed();
    s.painting = true;
}

void mark(Section section) {
    State &s = state();
    if (!s.painting)
        return;
    const qint64 now = s.clock.nsecsElapsed();
    s.sections[section] += ms(now - s.lastMark);
    s.lastMark = now;
}

void add(Counter counter, double value) {
    state().counters[counter] += value;
}

void add(Upload counter, double value) {
    state().uploads[counter] += value;
}

void countLogLine(qint64 mainThreadNs) {
    State &s = state();
    s.logLines.fetch_add(1, std::memory_order_relaxed);
    if (mainThreadNs > 0)
        s.logNs.fetch_add(mainThreadNs, std::memory_order_relaxed);
}

void setGpuMs(double value) {
    state().gpuMs = value;
}

void setCamera(int tileX, int tileZ, const float *pos, float rotX, float rotY) {
    State &s = state();
    s.hasCamera = true;
    s.tileX = tileX;
    s.tileZ = tileZ;
    s.x = pos[0];
    s.y = pos[1];
    s.z = pos[2];
    s.rotX = rotX;
    s.rotY = rotY;
}

void setSimulation(unsigned long long tick, double ageMs, double simTime, int train, double travelled,
                   double speed) {
    State &s = state();
    s.hasSimulation = true;
    s.simTick = tick;
    s.simAgeMs = ageMs;
    s.simTime = simTime;
    s.watchTrain = train;
    s.watchTravelled = travelled;
    s.watchSpeed = speed;
}

void setViewTick(unsigned long long serial, double ms) {
    State &s = state();
    s.viewTick = serial;
    s.viewTickMs = ms;
}

void frameEnd() {
    State &s = state();
    if (s.started < 0)
        return;
    const qint64 end = s.clock.nsecsElapsed();
    const double interval = s.previousStart >= 0 ? ms(s.started - s.previousStart) : 0.0;
    const double paint = ms(s.painted - s.begun);
    double sections = 0.0;
    for (double value : s.sections)
        sections += value;
    double speed = 0.0;
    const double worldX = s.tileX * 2048.0 + s.x, worldZ = s.tileZ * 2048.0 + s.z;
    if (s.hasCamera && s.previousCameraNs >= 0 && s.started > s.previousCameraNs)
        speed = std::hypot(worldX - s.previousWorldX, worldZ - s.previousWorldZ)
                / ((s.started - s.previousCameraNs) / 1e9);
    if (s.hasCamera) {
        s.previousWorldX = worldX;
        s.previousWorldZ = worldZ;
        s.previousCameraNs = s.started;
    }
    auto f = [](double value) { return QString::number(value, 'f', 2); };
    s.out << f(ms(s.started)) << ',' << f(interval) << ',' << f(ms(s.begun - s.started)) << ','
          << f(paint) << ',' << f(ms(end - s.painted));
    for (double value : s.sections)
        s.out << ',' << f(value);
    s.out << ',' << f(std::max(0.0, paint - sections)) << ',' << f(s.gpuMs);
    for (int c = 0; c < CounterCount; ++c)
        s.out << ',' << f(s.counters[c]);
    s.out << ',' << s.logLines.exchange(0, std::memory_order_relaxed) << ','
          << f(ms(s.logNs.exchange(0, std::memory_order_relaxed))) << ',' << s.tileX << ',' << s.tileZ
          << ',' << f(s.x) << ',' << f(s.z) << ',' << f(speed) << ','
          // As the navigation window shows it (tile Z and z negated).
          << s.tileX << ' ' << -s.tileZ << ' ' << f(s.x) << ' ' << f(s.y) << ' ' << f(-s.z) << ' '
          << f(qRadiansToDegrees(s.rotX)) << ' ' << f(qRadiansToDegrees(s.rotY));
    // The simulation's frame drawn (empty columns without one).
    if (s.hasSimulation)
        s.out << ',' << s.simTick << ',' << f(s.simAgeMs) << ',' << QString::number(s.simTime, 'f', 3) << ','
              << s.watchTrain << ',' << QString::number(s.watchTravelled, 'f', 3) << ',' << f(s.watchSpeed);
    else
        s.out << ",,,,,,";
    s.out << ',' << s.viewTick << ',' << f(s.viewTickMs);
    for (double value : s.uploads)
        s.out << ',' << f(value);
    s.out << '\n';
    s.hasSimulation = false;
    if (++s.lines % 60 == 0)
        s.out.flush();
    s.previousStart = s.started;
    s.started = -1;
    s.painting = false;
    s.gpuMs = -1.0;
    std::fill(std::begin(s.sections), std::end(s.sections), 0.0);
    std::fill(std::begin(s.counters), std::end(s.counters), 0.0);
    std::fill(std::begin(s.uploads), std::end(s.uploads), 0.0);
}

}
