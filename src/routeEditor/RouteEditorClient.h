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
#include <QList>
#include <QSet>
#include <tsre/fileFunctions/NetworkMessage.h>

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
class GameObj;

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
    // core.network.clientLogin: user[:password]@[ws://|wss://]host[:port];
    // no scheme is ws://. false when it names no server.
    static bool parseLogin(const QString &login, QString &url, QString &username, QString &password);
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
    // A new ID from this client's blocks (task server 02); UiDs per tile.
    // The next block is asked for when half of one is used; with none left
    // the edit waits for it (up to IdWaitMs, user input held, the view not
    // drawn). -1 when none came (the caller takes a local one, which the
    // server refuses and puts right).
    int takeId(NetworkMessage::IdKind kind, int tileX = 0, int tileZ = 0);
    // How many IDs of a kind this client holds now.
    int idsHeld(NetworkMessage::IdKind kind, int tileX = 0, int tileZ = 0) const;
    // Waiting for a block in the middle of an edit.
    bool waitingForIds() const { return idWait; }
    // Short: an answer takes milliseconds, but a server saving or sending a
    // large route's TDB to another user is busy for a second or two.
    static const int IdWaitMs = 3000;
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
    // The server is back after a disconnection: the route is being
    // refreshed from it in place (others may have edited meanwhile).
    void reconnected();
    // The server is back with another route, or before this one had
    // loaded: the editor loads it in place (task editor 10).
    void routeSwitchNeeded(QString route);
    // Before a refresh replaces the route's data: nothing in the editor may
    // go on pointing into it (task server 02).
    void routeReplacing();
    // The refresh's last answer is in.
    void routeRefreshed();
    // Received data replaced an object, or a whole loaded tile; the old ones
    // are kept, not freed, and nothing may go on pointing at them.
    void objectReplaced(GameObj *old);
    void tileReplaced(int x, int z);
    void idsArrived();
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
    // Between the socket's connection and the welcome nothing but the login
    // is sent: the server closes a socket that sends anything else first.
    bool welcomed = false;
    quint32 nextRequest = 0;
    // A refresh: the number of its last request.
    bool refreshing = false;
    quint32 refreshLast = 0;
    // The ID blocks, by key (NetworkMessage::idKey): their IDs in order,
    // the last block's size, whether the next is asked for.
    struct IdStock {
        QList<int> ids;
        int blockSize = 0;
        quint32 asked = 0;
    };
    QHash<QString, IdStock> idStocks;
    QSet<quint32> idRequests;
    bool idWait = false;
    // Messages that came while an edit waited for IDs: applied after it.
    QList<QByteArray> heldMessages;
    bool flushScheduled = false;
    void readIdBlock(FileBuffer *data);
    void askIds(NetworkMessage::IdKind kind, int tileX, int tileZ);
    void flushHeld();
    void processMessage(const QByteArray &message);
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
    void refreshRoute();
    void scheduleReconnect();
    void connectionLost(const QString &why);
    void readUsersInfo(FileBuffer* data);
    void readUtf16Message(QWebSocket *client, FileBuffer* data);
    void readBinaryMessage(QWebSocket *client, FileBuffer* data);
};

#endif /* ROUTEEDITORCLIENT_H */

