/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TERRAINCLIENT_H
#define TERRAINCLIENT_H

#include <tsre/world/Terrain.h>
#include <QByteArray>
#include <QHash>

// A server client's terrain tile (task server 03): its files are asked from
// the server (as its live tile would save them) and read by the local
// loader when they come (Terrain::loadDescriptor, completeLoad), then
// dropped. A descriptor-only tile (map mode) asks for the rest when it is
// to be completed. Its edits go to the server.
class TerrainClient : public Terrain {
    Q_OBJECT
public:
    TerrainClient(TerrainInfo *ti, bool complete = true);
    virtual ~TerrainClient();
    // False until the server's files are in (asked for then).
    bool completeLoad() override;
    // The server's answer: the tile's files, by path under the game root;
    // none when it has no such tile.
    void filesArrived(const QHash<QString, QByteArray> &files, bool descriptorOnly);
    // An answer still to come.
    bool waiting() const { return asked != Asked::None; }
    // Back after a disconnection: an answer still to come is asked for
    // again (the request or its answer may have been lost with the link).
    void askAgain();
    void updateTFile();
    void updateFFile();
private:
    enum class Asked { None, Descriptor, Complete };
    Asked asked = Asked::None;
    bool failed = false;
    void ask(bool complete);
};

#endif /* TERRAINCLIENT_H */
