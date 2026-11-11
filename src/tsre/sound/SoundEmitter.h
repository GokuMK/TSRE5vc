/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SOUNDEMITTER_H
#define SOUNDEMITTER_H

struct SoundListener;

// What SoundSystem updates every frame: a SoundSource (an MSTS .sms) or
// another kind of sound (MaSzyna's, task maszyna 05). It registers itself
// with SoundSystem::add and goes with SoundSystem::remove.
class SoundEmitter {
public:
    virtual ~SoundEmitter() = default;
    virtual void update(const SoundListener &listener, double clock) = 0;
    // Lets go of the voices (the engine stops); they come back with the
    // next update when it runs again.
    virtual void release() = 0;
    // For TSRE_SOUND_LOG: heard now, its voices, those playing.
    virtual bool heard() const = 0;
    virtual int voiceCount() const = 0;
    virtual int voicesPlaying() const = 0;
};

#endif
