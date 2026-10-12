/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/sound/TrackSound.h>
#include <tsre/sound/SoundSource.h>
#include <tsre/sound/SoundSystem.h>
#include <tsre/world/SoundList.h>
#include <tsre/Game.h>

#include <QDebug>
#include <cmath>

TrackSound::TrackSound(const void *car, bool inside) : car(car), hasInside(inside) {
}

TrackSound::~TrackSound() {
}

QVector<QPair<QString, QString>> TrackSound::types() {
    QVector<QPair<QString, QString>> list;
    if (Game::soundList == NULL)
        return list;
    for (const SoundListItem *item : Game::soundList->regions) {
        if (item->id >= list.size())
            list.resize(item->id + 1);
        list[item->id] = {item->file1, item->file2};
    }
    return list;
}

int TrackSound::regionType(int current, float behind, int behindType, float ahead, int aheadType) {
    if (behind >= 0.0f && ahead >= 0.0f) {
        if (behindType == aheadType)
            return behindType;
        return ahead < 10.0f ? current : 0;
    }
    if (behind >= 0.0f)
        return behindType;
    if (ahead >= 0.0f)
        return aheadType;
    return current;
}

void TrackSound::setType(int type) {
    const QVector<QPair<QString, QString>> list = types();
    if (type < 0 || type >= list.size())
        type = 0;
    if (type == current)
        return;
    current = type;
    outside.reset();
    inside.reset();
    if (type >= list.size())
        return;
    const QString outsidePath = SoundSystem::findSms(list[type].second);
    if (!outsidePath.isEmpty())
        outside = std::make_unique<SoundSource>(outsidePath, SoundSource::Kind::Car, car);
    const QString insidePath = hasInside ? SoundSystem::findSms(list[type].first) : QString();
    if (!insidePath.isEmpty())
        inside = std::make_unique<SoundSource>(insidePath, SoundSource::Kind::Car, car);
}

void TrackSound::update(float speed, double travelled, int tileX, int tileZ, const float *position) {
    for (SoundSource *source : {outside.get(), inside.get()}) {
        if (source == nullptr)
            continue;
        source->values().set(Sms::Control::Speed, std::fabs(speed));
        source->values().distanceTravelled = travelled;
        source->setLocation(tileX, tileZ, position);
    }
}
