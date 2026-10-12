/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/sound/SoundManager.h>
#include <tsre/sound/SoundEngine.h>
#include <tsre/sound/SoundSource.h>
#include <tsre/sound/MstsSoundDefinition.h>
#include <tsre/math3d/GLMatrix.h>

QMap<int, SoundSource*> SoundManager::Sources;
int SoundManager::SourcesCount;
int SoundManager::listenerX = 0;
int SoundManager::listenerZ = 0;

void SoundManager::InitAl() {
    SoundEngine::get().start();
}

void SoundManager::UpdateAll(){
    if(!SoundEngine::get().running())
        return;
    QMapIterator<int, SoundSource*> i(Sources);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL) continue;
        i.value()->update();
    }
}

void SoundManager::UpdateListenerPos(int x, int y, float* pos, float* target, float* up){
    if(!SoundEngine::get().running())
        return;
    float forward[3];
    forward[0] = target[0] - pos[0];
    forward[1] = target[1] - pos[1];
    forward[2] = target[2] - pos[2];
    Vec3::normalize(forward, forward);
    SoundEngine::get().setListener(pos, forward, up);

    if(listenerX != x || listenerZ != y){
        listenerX = x;
        listenerZ = y;
        QMapIterator<int, SoundSource*> i(Sources);
        while (i.hasNext()) {
            i.next();
            if(i.value() == NULL) continue;
            i.value()->updatePosition();
        }
    }
}

int SoundManager::AddSoundSource(QString resPath, QString name){
    // simple source
    Sources[SourcesCount] = new SoundSource(resPath, name);
    return SourcesCount++;
}

int SoundManager::AddSoundSource(SoundDefinitionGroup* g){
    if(g == NULL)
        return -1;

    Sources[SourcesCount] = new SoundSource(g);
    return SourcesCount++;
}

void SoundManager::CloseAl(){
    QMapIterator<int, SoundSource*> i(Sources);
    while (i.hasNext()) {
        i.next();
        delete i.value();
    }
    Sources.clear();
    SoundEngine::get().shutdown();
}
