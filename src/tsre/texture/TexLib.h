/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <unordered_map>
#include <QString>
#include <QHash>
#include <QSize>
#include <tsre/texture/Texture.h>

#ifndef TEXLIB_H
#define	TEXLIB_H

class TexLib {
public:
    TexLib();
    TexLib(const TexLib& orig);
    virtual ~TexLib();
    static int jesttextur;
    static std::unordered_map<int, Texture*> mtex;
    static QHash<int, int> disabledTextures;
    static void reset();
    static void dumpStats(QString label = "");
    static void enableTexture(int id);
    static void disableTexture(int id);
    static void delRef(int texx);
    static void addRef(int texx);
    static int addTex(QString path, QString name, bool reload = false);
    // How addTex loads a texture it does not have yet: as the setting
    // core.rendering.threadedTextureLoading says, on a thread of its own
    // whatever the setting (the caller, say the render thread, never waits),
    // or at once in the caller's thread. A reload loads at once.
    enum class Loading { Setting, Background, Now };
    static int addTex(QString pathid, bool reload = false, Loading loading = Loading::Setting);
    // The size an image file says in its header (ACE, DDS, PNG, JPEG, BMP,
    // TGA), read without loading the image; empty when it cannot tell.
    static QSize imageSize(QString pathid);
    static int addTex(Texture* texture, bool reload = false);
    static bool reloadTexIfPresent(QString pathid);
    static bool decodeFromBytes(Texture* texture, const QByteArray& encodedBytes, QString* outError = nullptr);
    static int getTex(QString pathid);
    static int cloneTex(int id);
    static void save(QString type, QString path, int id);
private:

};

#endif	/* TEXLIB_H */

