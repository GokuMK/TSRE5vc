/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef CLIENTINFO_H
#define CLIENTINFO_H

#include <QByteArray>
#include <QString>

class RenderQueue;

class OglObj;
class TextObj;
class GLUU;
class QWebSocket;

class ClientInfo {
public:
    QWebSocket *socket = NULL;
    QString username;
    bool loggedIn = false;
    // The server's: when the socket connected, and whether it is being
    // closed (nothing more is sent to it or read from it).
    qint64 connectedAt = 0;
    bool closing = false;
    // The server's, for the login (task server 02): the name the hello
    // asked for, its TSRE version, the challenge's nonce until it is
    // answered.
    QString requestedUser;
    QString tsre;
    QByteArray nonce;
    // The server's, while a frame is read: its request number, and the
    // first and last change numbers its edits got.
    quint32 request = 0;
    quint32 firstChange = 0;
    quint32 lastChange = 0;
    int X = 0;
    int Z = 0;
    float x = 0, y = 0, z = 0;
    QString lastAction;
    ClientInfo();
    ClientInfo(const ClientInfo& orig);
    virtual ~ClientInfo();
    void pushRenderItem(RenderQueue &queue, float playerRot);
private:
    void initMarker();
    OglObj* stick = NULL;
    TextObj* name = NULL;
};

#endif /* CLIENTINFO_H */

