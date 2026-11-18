/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ROUTEEDITORCLIENT_H
#define ROUTEEDITORCLIENT_H

#include <QtCore/QObject>
#include <QtCore/QList>
#include <QtCore/QByteArray>
#include <QHash>

class QWebSocket;
class QTimer;
class FileBuffer;
class WorldObj;
class TRnode;
class TRitem;
class Terrain;
class TrackShape;
class TSection;
class ClientInfo;

class RouteEditorClient : public QObject {
    Q_OBJECT

public:
    enum class State {
        Connecting,
        Connected,
        // Lost; reconnecting on its own.
        Disconnected,
        // The server refused the login or disconnected the user: no retry.
        Refused
    };

    QHash<QString, ClientInfo*> clientUsersList;
    QString username;
    // Edits sent to the server; edits not sent because they came from
    // applying a received message (the receive rule), or while disconnected.
    int sentEdits = 0;
    int suppressedEdits = 0;
    int droppedEdits = 0;
    // The server's session (new at every start of it), the last change
    // number seen (others' edits and this user's confirmed ones), refusals
    // received (task server 02).
    QString session;
    quint32 lastChange = 0;
    int refusals = 0;

    RouteEditorClient();
    // A client of a given server and user (the server suite's).
    RouteEditorClient(const QString &url, const QString &username, const QString &password);
    virtual ~RouteEditorClient();
    State state() const { return currentState; }
    bool isConnected() const { return currentState == State::Connected; }
    // A received message is being applied: what applying it changes is not
    // sent back (the receive rule).
    bool applyingReceived() const { return applying; }
    // Whether the server lets this user edit (its welcome).
    bool mayEdit() const { return editPermitted; }
    // A request; its number, which the server's reply carries.
    quint32 sendUtf16Message(QString msg);
    // An edit's message (the TDB's counters): sent only when edits are.
    void sendEditMessage(QString msg);
    void updateWorldObjData(WorldObj *o);
    void updateTrackNodeData(int nid, int tdbType, TRnode *n);
    void updateTrackItemData(int iid, int tdbType, TRitem *n);
    void updateTrackSectionData(TSection *s);
    void updateTrackShapeData(TrackShape *s);
    void updateTerrainHeightmap(Terrain *t);
    void updateTerrainTFile(Terrain *t);
    void updateTerrainFFile(Terrain *t);
    void updatePointerPosition(int X, int Z, float x, float y, float z);
    void connectNow();
    // Closes the connection for good: no reconnecting.
    void disconnectNow();
    // Something the user is told (the status bar, and the Errors and
    // Messages window): an action a client cannot do, a refusal.
    void notice(const QString &text);

public slots:
    void close();
    void onConnected();
    void processTextMessage(QString message);
    void processBinaryMessage(QByteArray message);

signals:
    void loadRoute();
    void refreshObjLists();
    // Connected or not, and what to tell the user.
    void connectionChanged(bool connected, QString message);
    // The server is back after a disconnection: the route is loaded again
    // (others may have edited meanwhile).
    void reconnected();
    // The server refused the login, or the first connection failed.
    void loginFailed(QString message);
    void noticeShown(QString message);

private:
    QWebSocket * m_webSocket = NULL;
    QTimer *reconnectTimer = NULL;
    QString password;
    QString url;
    State currentState = State::Connecting;
    bool routeRequested = false;
    bool applying = false;
    bool editPermitted = false;
    quint32 nextRequest = 0;
    int reconnectDelayMs = 2000;
    bool pointerSent = false;
    int lastPointerX = 0, lastPointerZ = 0;
    float lastPointer[3] = {0, 0, 0};
    void init();
    bool editAllowed();
    void sendEdit(const QByteArray &payload);
    // A payload as a frame: a request with its number (0: no reply asked).
    quint32 send(const QByteArray &payload, bool request = true);
    void readWelcome(FileBuffer *data);
    void scheduleReconnect();
    void connectionLost(const QString &why);
    void readUsersInfo(FileBuffer* data);
    void readUtf16Message(QWebSocket *client, FileBuffer* data);
    void readBinaryMessage(QWebSocket *client, FileBuffer* data);
};

#endif /* ROUTEEDITORCLIENT_H */

