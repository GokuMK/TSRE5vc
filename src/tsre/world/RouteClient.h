/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ROUTECLIENT_H
#define ROUTECLIENT_H

#include <tsre/world/Route.h>
#include <QSet>
#include <QHash>

class RouteClient : public Route {
    Q_OBJECT
public:
    RouteClient();
    RouteClient(const RouteClient& orig);
    virtual ~RouteClient();
    // A tile the server has: when it has arrived, else NULL (requested once).
    // A tile the server has not got cannot be placed on (allowNew: the
    // user is told; tiles cannot be made from a client yet).
    Tile * requestTile(int x, int z, bool allowNew = true);
    // The server answered that it has no such tile (task server 01, B3).
    void markTileMissing(int x, int z);
    bool tileMissing(int x, int z) const;
    // The server is back (task server 02): what this client has loaded is
    // asked for again (the route's tsection, the TDB and RDB, the loaded
    // world tiles and terrain) and replaced in place as it arrives. The
    // last request's number: when its reply is in, the refresh is done.
    quint32 refreshFromServer();
    void save();
    void load();
    void getUnsavedInfo(QVector<QString> &items);
    
signals:
    void initDone();
    
private:
    QSet<int> missingTiles;
    QHash<int, qint64> missingTileNoticeAt;

};

#endif /* ROUTECLIENT_H */

