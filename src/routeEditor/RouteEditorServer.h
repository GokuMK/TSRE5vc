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

class QWebSocketServer;
class QWebSocket;
class Route;
class FileBuffer;
class QByteArray;
class ClientInfo;
class QTextStream;

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

    QWebSocketServer *m_pWebSocketServer = NULL;
    QHash<QWebSocket *, ClientInfo*> clients;
    Route *route = NULL;
    QString lastUsersInfo;
    bool loadRoute();
    void forwardToOthers(ClientInfo *from, const QByteArray &message);
    void refuse(ClientInfo *client, const QString &what, const QString &why);
    void readUtf16Message(ClientInfo *client, FileBuffer* data);
    Record readRecord(ClientInfo *client, const QString &name, FileBuffer* data);
    void readBinaryMessage(ClientInfo *client, const QByteArray &message, FileBuffer* data);
    // Whether the client is logged in after it.
    bool login(ClientInfo *client, FileBuffer* data);
    void sendUsersInfo(bool always);
    QString usersInfo() const;
    void usersAuthInit();
    bool userAuth(QString user, QString pass);
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

