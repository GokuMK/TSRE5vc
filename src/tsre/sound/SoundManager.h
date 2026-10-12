/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SOUNDMANAGER_H
#define	SOUNDMANAGER_H

#include <QString>
#include <QMap>

class SoundSource;
class SoundDefinitionGroup;

class SoundManager {
public:
    static QMap<int, SoundSource*> Sources;
    static int SourcesCount;
    static int listenerX;
    static int listenerZ;

    // Starts the sound engine on the default playback device.
    static void InitAl();
    // Removes the sources, then stops the engine.
    static void CloseAl();
    static void UpdateAll();
    static void UpdateListenerPos(int x, int y, float *pos, float* target, float* up);

    static int AddSoundSource(QString resPath, QString name);
    static int AddSoundSource(SoundDefinitionGroup* g);
};

#endif	/* SOUNDMANAGER_H */
