/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ROUTEEDITORSERVER_H
#define ROUTEEDITORSERVER_H

#include <QtCore/QObject>
#include <QtCore/QList>
#include <QtCore/QByteArray>
#include <QHash>
#include <QThread>
#include <QMap>
#include <tsre/fileFunctions/NetworkMessage.h>

class QWebSocketServer;
class QWebSocket;
class Route;
class FileBuffer;
class QByteArray;
class ClientInfo;
class QTextStream;
class Terrain;

// The server's console: reads commands from its standard input until the
// input ends (a server started without a console runs on).
class ConsoleThread : public QThread {
    Q_OBJECT
public:
    ConsoleThread();
    virtual ~ConsoleThread();
    void run();
signals:
    void sendCommand(QString val);
    void inputClosed();
};

class RouteEditorServer : public QObject {
    Q_OBJECT
public:
    static QString IP;
    static int Port;
    // users.txt for the "file" authentication; empty: beside the route.
    static QString UsersFile;
    // A socket that has not logged in by then is closed.
    static const int LoginTimeoutMs = 10000;

    RouteEditorServer();
    virtual ~RouteEditorServer();
    bool listening() const;
    void sendUtf16Message(QWebSocket *client, QString msg);

public slots:
    void onNewConnection();
    void processTextMessage(QString message);
    void processBinaryMessage(QByteArray message);
    void socketDisconnected();
    void readCommand(QString val);
    void consoleClosed();
    void update();

signals:
    void closed();

private:
    enum class Record { Done, Forward, Stop };
    // What a request does, for the one permission check (task server 02).
    enum class Action { View, Edit };

    QWebSocketServer *m_pWebSocketServer = NULL;
    QHash<QWebSocket *, ClientInfo*> clients;
    Route *route = NULL;
    QString lastUsersInfo;
    // New at every start; the clients are told it in the welcome.
    QString session;
    // The last change accepted: every applied edit is numbered.
    quint32 changeNumber = 0;
    bool loadRoute();
    void forwardToOthers(ClientInfo *from, const QByteArray &message);
    // An edit applied: numbered, and forwarded with its number to the
    // others; the sender gets the numbers in `done`.
    void accept(ClientInfo *client, const QByteArray &payload);
    // A reply to the request being read (its number in the frame).
    void reply(ClientInfo *client, const QByteArray &payload);
    void replyText(ClientInfo *client, const QString &text);
    // Sends a last record (a refusal) and closes the socket.
    void closeClient(ClientInfo *client, const QString &text, const QString &why);
    bool allowed(ClientInfo *client, Action action) const;
    QString nameOf(ClientInfo *client) const;
    void refuse(ClientInfo *client, const QString &what, const QString &why);
    void readUtf16Message(ClientInfo *client, FileBuffer* data);
    // `record`: the whole record, as it is forwarded.
    Record readRecord(ClientInfo *client, const QString &name, FileBuffer* data, const QByteArray &record);
    // After a refusal, the sender gets the server's version of what its
    // record touched (its own record removed when the server has none),
    // applied as any received edit: it ends as the server has it.
    void putRightObject(ClientInfo *client, int x, int z, unsigned int uid, int typeID, const QByteArray &record);
    void putRightNode(ClientInfo *client, bool road, int id);
    void putRightItem(ClientInfo *client, bool road, int id);
    // A terrain tile's file as a binary record: requested (the answer to a
    // request) or as an edit (a put-right).
    QByteArray terrainRecord(char kind, bool requested, Terrain *t, int x, int z);

    // ID blocks (task server 02), by key (NetworkMessage::idKey): item IDs
    // given back (free slots: emptyitems, a disconnected user's unused
    // ones), and the next new ID. Other kinds are never given again: their
    // unused IDs cost nothing.
    struct IdPool {
        QList<int> free;
        int next = -1;
    };
    QHash<QString, IdPool> idPools;
    IdPool *pool(NetworkMessage::IdKind kind, int x, int z);
    // A block for a client, as id_block records (several when free item
    // slots are not in a row); empty when there is none to give.
    QString giveIds(ClientInfo *client, NetworkMessage::IdKind kind, int x = 0, int z = 0);
    // The blocks a client gets after its welcome (not its tiles' UiDs).
    void giveStartIds(ClientInfo *client);
    // A new ID a record uses: one of the client's blocks (then used), or why
    // it is refused.
    QString takeGiven(ClientInfo *client, NetworkMessage::IdKind kind, int id, int x = 0, int z = 0);
    void freeItem(bool road, int id);
    void returnIds(ClientInfo *client);
    void readBinaryMessage(ClientInfo *client, const QByteArray &message, FileBuffer* data);
    // Whether the client is still connected after it.
    bool hello(ClientInfo *client, FileBuffer* data);
    // Whether the client is logged in after it.
    bool login(ClientInfo *client, FileBuffer* data);
    void sendUsersInfo(bool always);
    QString usersInfo() const;
    void usersAuthInit();
    // The login's answer to the challenge, checked against users.txt.
    bool userAuth(const QString &user, const QByteArray &nonce, const QString &answer);
    void loadUsersFromFile();
    void listUsers();
    void printStatus();
    void printHelp();
    void kick(const QString &username);
    void save();
    ConsoleThread *c = NULL;
    QMap<QString, QString> usersAuth;
    bool authenticateWithFile = false;
};

#endif /* ROUTEEDITORSERVER_H */

