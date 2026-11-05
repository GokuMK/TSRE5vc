/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <functional>
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
    // What a requester does with a file texture: Display only (its loader
    // prepares it for the GPU and keeps no CPU copy; it is never edited), or
    // it may be Editable (terrain: its CPU pixels can be had), or it is
    // displayed and its texels are Data, not colour (PBR maps, task shapes
    // 13: never bled as a cutout, mipmaps the means of its values).
    enum Use { Display, Editable, Data };
    // How addTex loads a texture it does not have yet: as the setting
    // core.rendering.threadedTextureLoading says (every type: ACE, DDS and
    // images such as PNG; a caller that reads pixels at once asks for Now),
    // on a thread of its own
    // whatever the setting (the caller, say the render thread, never waits),
    // or at once in the caller's thread. A reload loads at once.
    enum class Loading { Setting, Background, Now };
    static int addTex(QString path, QString name, bool reload = false, Use use = Display);
    static int addTex(QString pathid, bool reload = false, Use use = Display,
                      Loading loading = Loading::Setting);
    static int addTex(QString pathid, bool reload, Loading loading) {
        return addTex(pathid, reload, Display, loading);
    }
    // The size an image file says in its header (ACE, DDS, PNG, JPEG, BMP,
    // TGA), read without loading the image; empty when it cannot tell.
    static QSize imageSize(QString pathid);
    static int addTex(Texture* texture, bool reload = false);
    static bool reloadTexIfPresent(QString pathid);
    // Texture memory residency (docs/tasks/textures/04-texture-memory.md).
    // Once a frame on the render thread: stamps the clock, and twice a second
    // releases the least recently used file textures over the budget.
    static void residencyFrame();
    // Releases the least recently used file textures, unused for minAgeMs or
    // more, until those loaded are 10% under the current budget; how many it
    // released. now: TexLib's clock.
    static int enforceResidency(qint64 now, qint64 minAgeMs);
    // The budget in bytes: the setting, else an eighth of installed memory,
    // 1 to 4 GB. A test may set its own (negative: the setting again).
    static qint64 residencyBudget();
    static void setResidencyBudget(qint64 bytes);
    // The budget the residency passes keep to: residencyBudget, lowered
    // while the GPU runs short of memory (setGpuRoomSource, memoryShort).
    static qint64 currentBudget();
    // What the textures held at the last residency pass (their GPU memory,
    // and CPU pixels waiting for their first draw), in bytes: the memory log's.
    static qint64 residentTotal();
    // The GPU the textures go to (the renderer, once): with a discrete GPU
    // the automatic budget is 60% of its own memory (1 to 16 GB) instead.
    static void setGpuMemory(qint64 bytes, bool discrete);
    // The GPU memory left to TSRE (renderer task 42), in bytes: what its
    // driver allows TSRE beyond what TSRE uses (below 0 when TSRE uses
    // more), and what it allows in all (-1: not known).
    struct GpuRoom {
        qint64 room = -1, allowed = -1;
    };
    // Where the room comes from (the renderer; null: none). A residency pass
    // asks it once a second. With less than a margin left (a 16th of the
    // allowed, 256 MB to 1 GB) the budget goes down to what the textures
    // hold less what is missing, at least an eighth of the budget (512 MB
    // at most); the room grown again, it goes back up, at most to the
    // setting. What the textures could hold so stays the same while they
    // come and go, and falls when something else takes the memory (meshes,
    // other programs); lowered again only 3 s after the last time, the
    // driver telling released textures late.
    static void setGpuRoomSource(std::function<GpuRoom()> source);
    // A GPU resource could not be created (any thread): for a minute the
    // budget is three quarters of what the textures hold at the next pass.
    static void memoryShort();
    // A released texture loads again: on a worker (drawn once loaded), or at
    // once for code that needs its pixels now.
    static void reloadReleased(Texture *texture);
    static void reloadNow(Texture *texture);
    // reloadReleased for code that holds a texture's GPU handle.
    static void reloadEvicted(unsigned int handle);
    // Whether a library texture's content is loaded; a released one starts
    // loading again (for code that checks Texture::loaded itself).
    static bool request(int id);
    // A texture file that failed to load is loaded again, at once and in
    // place (every holder of the id gets it): a failure is remembered until
    // a reload asks for this. True when it loaded; false for one that had not
    // failed, or has no file to load (embedded, generated).
    static bool retryIfFailed(int id);
    static bool decodeFromBytes(Texture* texture, const QByteArray& encodedBytes, QString* outError = nullptr);
    static int getTex(QString pathid);
    static int cloneTex(int id);
    static void save(QString type, QString path, int id);
private:

};

#endif	/* TEXLIB_H */

