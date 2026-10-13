/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/objects/SoundSourceObj.h>
#include <tsre/ogl/PoleObj.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/fileFunctions/TS.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/world/SoundList.h>
#include <tsre/Game.h>
#include <tsre/sound/SoundSource.h>
#include <tsre/sound/SoundSystem.h>
#include <tsre/renderer/Renderer.h>

SoundSourceObj::SoundSourceObj() {
}

SoundSourceObj::SoundSourceObj(const SoundSourceObj& o) : WorldObj(o) {

}

WorldObj* SoundSourceObj::clone(){
    return new SoundSourceObj(*this);
}

SoundSourceObj::~SoundSourceObj() {
}

bool SoundSourceObj::allowNew(){
    return true;
}

bool SoundSourceObj::isSoundItem(){
    return true;
}

void SoundSourceObj::load(int x, int y) {
    this->x = x;
    this->y = y;
    this->position[2] = -this->position[2];
    Quat::fill(this->qDirection);
    this->loaded = true;
    syncSound();

    setMartix();
}

// A sound for the object's file: made when sound plays, again when the file
// changes; it follows the object and falls silent when it is deleted.
void SoundSourceObj::syncSound(){
    if(!Game::soundEnabled || !SoundSystem::running()){
        sound.reset();
        return;
    }
    if(sound != NULL && sound->path().endsWith("/" + fileName, Qt::CaseInsensitive))
        return;
    sound.reset();
    const QString sms = SoundSystem::findSms(fileName);
    if(sms.isEmpty())
        return;
    sound = std::make_unique<SoundSource>(sms, SoundSource::Kind::World);
    sound->locator = [this](int &tileX, int &tileZ, float *at){
        if(!loaded)
            return false;
        tileX = x;
        tileZ = y;
        at[0] = position[0];
        at[1] = position[1];
        at[2] = position[2];
        return true;
    };
}

void SoundSourceObj::set(QString sh, long long int val){
    if (sh == ("ref_value")) {
        this->fileName = "";
        foreach (SoundListItem* it, Game::soundList->sources){
        //for (auto it = Game::soundList->sources.begin(); it != Game::soundList->sources.end(); ++it ){
            if(it->id == val){
                this->fileName = it->file1;
            }
        }
        return;
    }
    WorldObj::set(sh, val);
    return;
}

void SoundSourceObj::set(QString sh, QString val){
    if (sh == ("filename")) {
        fileName = val;
        if (loaded)
            syncSound();
        return;
    }
    if (sh == ("ref_filename")) {
        fileName = val;
        return;
    }
    WorldObj::set(sh, val);
    return;
}

void SoundSourceObj::set(TS::TokenId sh, FileBuffer* data) {
    if (sh == TS::FileName) {
        data->skipLabel();
        int slen = data->getShort()*2;
        fileName = *data->getString(data->off, data->off + slen);
        data->off += slen;
        return;
    }
    WorldObj::set(sh, data);
    return;
}

void SoundSourceObj::set(QString sh, FileBuffer* data) {
    if (sh == ("filename")) {
        fileName = ParserX::GetString(data);
        return;
    }
    WorldObj::set(sh, data);
    return;
}

void SoundSourceObj::pushRenderItems(RenderQueue &queue, float lod, float posx, float posz, float* pos, float* target, float fov, quint32 selectionId){
    if (!loaded) return;
    if(!Game::viewInteractives)
        return;

    Mat4::multiply(queue.transform(), queue.transform(), matrix);

    if(pointer3d == NULL)
        pointer3d = new PoleObj();
    if(pointer3dSelected == NULL){
        pointer3dSelected = new PoleObj();
        pointer3dSelected->setMaterial(1.0,1.0,0.5);
    }

    if(this->selected)
        pointer3dSelected->pushRenderItem(queue, selectionId);
    else
        pointer3d->pushRenderItem(queue, selectionId);
}

int SoundSourceObj::getDefaultDetailLevel(){
    return -4;
}

void SoundSourceObj::save(QTextStream* out){
    if (!loaded) return;
    if(WorldObj::positiveQuaternionSerialization())
        Quat::makePositive(this->qDirection);
    
*(out) << "	Soundsource (\n";
*(out) << "		Position ( "<<this->position[0]<<" "<<this->position[1]<<" "<<-this->position[2]<<" )\n";
*(out) << "		FileName ( "<<ParserX::AddComIfReq(this->fileName)<<" )\n";
*(out) << "		UiD ( "<<this->UiD<<" )\n";
*(out) << "	)\n";
}
