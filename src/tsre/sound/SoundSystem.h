/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SOUNDSYSTEM_H
#define SOUNDSYSTEM_H

#include <tsre/sound/SoundSource.h>

#include <QString>
#include <QStringList>
#include <random>

// The sounds of a session: the engine, the listener (the camera), the clock
// random triggers count on, and every SoundSource, updated once a frame.
// Sound sources belong to what makes them (world objects, trains) and
// register here themselves.
namespace SoundSystem {

// Open Rails' default sound detail level: the scalability group played.
constexpr int DetailLevel = 5;

// Starts the engine on the default device (device false: no device, for
// tests). False when there is none: everything stays silent.
bool start(bool device = true);
// Lets the sources go of their voices and stops the engine.
void shutdown();
bool running();

void setListener(const SoundListener &listener);
const SoundListener &listener();
// Advances the clock and updates every source.
void update(float seconds);
double clock();
std::mt19937 &random();
float uniform(float min, float max);

// Where a sound's waves may be: its .sms folder, the route's SOUND, the
// content root's SOUND (Open Rails' order).
QStringList waveFolders(const QString &smsFolder);
// A .sms by name: the route's SOUND, then the content root's.
QString findSms(const QString &name);

// A wave played once at a place (a train passing a junction).
void playOnce(const QString &wavePath, int tileX, int tileZ, const float *position);

// For SoundSource.
void add(SoundSource *source);
void remove(SoundSource *source);
int sourceCount();

}

#endif
