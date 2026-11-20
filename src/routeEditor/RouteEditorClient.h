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

#include <QPointer>
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
namespace Sim {
class RemoteSimulationLink;
}

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
    // The name the server shows this user by (its welcome's: another login
    // of the same name gets a number added).
    QString username;
    // The name this client logs in with, every time (the review: a renamed
    // user logging in again by its shown name was refused).
    QString loginName;
    // Another user's changes not applied: this client's own edit of the
    // record was still unanswered, and the server applies it after them.
    int skippedChanges = 0;
    // Edits sent to the server; edits not sent because they came from
    // applying a received message (the receive rule), or while disconnected.
    int sentEdits = 0;
    int suppressedEdits = 0;
    int droppedEdits = 0;
    // The request records sent, the last 500 (task server 03: a client asks
    // for kinds of data, never a path).
    QStringList requests;
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
    // The route has loaded: other users' changes held meanwhile applied.
    void routeLoaded();

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
    // Other users' changes that came while the route loaded (its data asked
    // for, not all in): applied once it has (task server 03's review: a
    // terrain edit reached a terrain not made yet). Their bytes, at most
    // MostHeldWhileLoading.
    QList<QByteArray> loadingHeld;
    qint64 loadingHeldBytes = 0;
    static constexpr qint64 MostHeldWhileLoading = 64 * 1024 * 1024;
    bool flushScheduled = false;
    void readIdBlock(FileBuffer *data);
    void askIds(NetworkMessage::IdKind kind, int tileX, int tileZ);
    void flushHeld();
    void processMessage(const QByteArray &message);
    int reconnectDelayMs = 2000;
    // The view's link to the server's run (task server 05): made at the
    // first welcome, the simulation's view's while this client exists.
    QPointer<Sim::RemoteSimulationLink> simulationLink;
    // The size of the message being read, as it came.
    qint64 messageBytes = 0;
    void linkSimulation();
    bool pointerSent = false;
    int lastPointerX = 0, lastPointerZ = 0;
    float lastPointer[3] = {0, 0, 0};
    void init();
    bool editAllowed();
    // An edit of a record (its key: NetworkMessage::recordKey's): unanswered until the server
    // answers its request or a later one.
    void sendEdit(const QByteArray &payload, const QString &key);
    // This client's edits the server has not answered yet: a record's key,
    // the request that sent it last. Another user's change to such a record
    // is not applied, so every client ends as the server does (the review:
    // two users moving one object ended on different places).
    QHash<QString, quint32> unanswered;
    // The frame being read answers this request (0: none of this client's).
    quint32 readingReply = 0;
    // Another user's change, not applied while this client's edit of the
    // same record is unanswered.
    bool othersChangeSkipped(const QString &key);
    // A payload as a frame: a request with its number (0: no reply asked).
    quint32 send(const QByteArray &payload, bool request = true);
    void readWelcome(FileBuffer *data);
    // The route's data by kind, its server's (task server 03): held for the
    // loaders (RouteFiles); the heavy folders compared with this client's.
    void readRouteFiles(const QByteArray &bytes);
    // A terrain tile's files (task server 03), to the tile that asked.
    void readTerrainFiles(int x, int z, const QByteArray &bytes);
    void refreshRoute();
    void scheduleReconnect();
    void connectionLost(const QString &why);
    void readUsersInfo(FileBuffer* data);
    void readUtf16Message(QWebSocket *client, FileBuffer* data);
    void readBinaryMessage(QWebSocket *client, FileBuffer* data);
};

#endif /* ROUTEEDITORCLIENT_H */

