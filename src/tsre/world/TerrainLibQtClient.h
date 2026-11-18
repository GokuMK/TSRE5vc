/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TERRAINLIBQTCLIENT_H
#define TERRAINLIBQTCLIENT_H

#include <tsre/world/TerrainLibQt.h>

class TerrainLibQtClient : public TerrainLibQt {
public:
    TerrainLibQtClient();
    TerrainLibQtClient(const TerrainLibQtClient& orig);
    virtual ~TerrainLibQtClient();
    void updateTerrainHeightmap(Terrain *t);
    void updateTerrainTFile(Terrain *t);
    // A client writes nothing into its route folder (task server 01): these
    // are refused until the server makes them.
    bool createQuadTile(const QuadTree::Quad &quad, bool overwrite, QString &error) override;
    bool deleteQuadTile(const QuadTree::Quad &quad, QString &error) override;
    void saveEmpty(int x, int z) override;
    bool saveEmpty(int x, int z, TerrainHeightProfile profile,
                   int patches,
                   bool overwrite = false) override;
    bool createNewRouteTerrain(int x, int z) override;
    void save() override;
    // Its tiles come from the server, not from files: they stay.
    bool releasesUnusedTiles() const override { return false; }
protected:
    // A tile that asks the server for its files and loads them with the
    // local loader when they come (task server 03).
    Terrain *makeTerrain(TerrainInfo *info, bool complete) override;

};

#endif /* TERRAINLIBQTCLIENT_H */

