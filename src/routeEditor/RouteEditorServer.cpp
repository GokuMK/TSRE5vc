/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <QApplication>
#include "RouteEditorServer.h"
#include <QtWebSockets/QWebSocketServer>
#include <QtWebSockets/QWebSocket>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/world/Route.h>
#include <tsre/trains/EngLib.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/world/Tile.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/world/TerrainLibQt.h>
#include <tsre/world/Terrain.h>
#include <tsre/fileFunctions/TS.h>
#include <tsre/fileFunctions/NetworkToken.h>
#include <tsre/sim/SimulationWire.h>
#include "SimulationServer.h"
#include <tsre/fileFunctions/NetworkMessage.h>
#include <iostream>
#include <climits>
#include <algorithm>
#include <memory>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/world/Trk.h>
#include <tsre/ClientInfo.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/world/QuadTree.h>
#include <tsre/world/objects/WorldObj.h>
#include <QDateTime>
#include <QDir>
#include <QTimer>
#include <QFile>
#include <QTextStream>

#define S_OUT QTextStream(stdout)

namespace {
// A record's id ( n ), its first field, without reading past it; -1 without.
int peekId(FileBuffer *data) {
    const int off = data->off;
    int id = -1;
    if (ParserX::NextTokenInside(data).toLower() == "id")
        id = int(ParserX::GetNumber(data));
    data->off = off;
    return id;
}

QString payloadText(const QByteArray &payload) {
    if (payload.size() < 2)
        return QString();
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(payload.constData() + 2), (payload.size() - 2) / 2);
}
}

#include <cerrno>
#ifdef Q_OS_WIN
#include <io.h>
#define readInput _read
#else
#include <unistd.h>
#define readInput ::read
#endif

QString RouteEditorServer::IP;
int RouteEditorServer::Port = 65535;
QString RouteEditorServer::UsersFile;

namespace {
QString timeNow() {
    return QDateTime::currentDateTime().toString("HH:mm:ss");
}

// How long a socket may take to log in; TSRE_SERVER_LOGIN_TIMEOUT_MS for
// the tests.
int loginTimeoutMs() {
    static const int timeout = qEnvironmentVariableIsSet("TSRE_SERVER_LOGIN_TIMEOUT_MS")
            ? qEnvironmentVariableIntValue("TSRE_SERVER_LOGIN_TIMEOUT_MS") : RouteEditorServer::LoginTimeoutMs;
    return timeout;
}
}

RouteEditorServer::RouteEditorServer() {
    if(!loadRoute()){
        S_OUT << "Route Editor Server: the route did not load, nothing to serve.\n";
        return;
    }

    usersAuthInit();
    session = QString::fromLatin1(NetworkMessage::newNonce().left(8).toHex());
    // The run's records go out as frames of their own, compressed whatever
    // their size (task server 05).
    simulation = new SimulationServer(route,
            [](ClientInfo *client, const QByteArray &payload, quint32 request) {
                if (client->closing || client->socket == NULL)
                    return;
                client->socket->sendBinaryMessage(NetworkMessage::frame(payload, request, 0, 64));
            },
            [this](ClientInfo *client, const QString &what, const QString &why) { refuse(client, what, why); },
            [this](ClientInfo *client) { return nameOf(client); }, this);

    m_pWebSocketServer = new QWebSocketServer(
            QStringLiteral("Route Editor Server"),
            QWebSocketServer::NonSecureMode, this);

    QHostAddress addr(QHostAddress::Any);
    if(IP.length() > 0)
        addr.setAddress(IP);

    if (m_pWebSocketServer->listen(addr, Port)) {
        S_OUT << "Route Editor Server listening on ip " << addr.toString() << " port " << m_pWebSocketServer->serverPort() << "\n";
        connect(m_pWebSocketServer, &QWebSocketServer::newConnection,
            this, &RouteEditorServer::onNewConnection);
        connect(m_pWebSocketServer, &QWebSocketServer::closed, this, &RouteEditorServer::closed);
    } else {
        S_OUT << "Route Editor Server cannot listen on ip " << addr.toString() << " port " << Port
              << ": " << m_pWebSocketServer->errorString() << "\n";
    }

    c = new ConsoleThread();
    connect(c, SIGNAL(sendCommand(QString)), this, SLOT(readCommand(QString)));
    connect(c, SIGNAL(inputClosed()), this, SLOT(consoleClosed()));
    c->start();

    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, QOverload<>::of(&RouteEditorServer::update));
    timer->start(100);
}

bool RouteEditorServer::listening() const {
    return m_pWebSocketServer != NULL && m_pWebSocketServer->isListening();
}

// The authentication setting, read one way: empty is anyone, "file" is
// users.txt (beside the route, or the --users file).
void RouteEditorServer::usersAuthInit(){
    const QString authentication = Settings::string(
                "core.network.serverAuthenticationMode", SettingType::Enum).trimmed();
    authenticateWithFile = authentication == "file";
    if(authenticateWithFile)
        loadUsersFromFile();
    S_OUT << "Authentication: " << (authenticateWithFile ? "users file" : "none, anyone may log in") << "\n";
}

void RouteEditorServer::loadUsersFromFile(){
    QString path = UsersFile;
    if(path.isEmpty())
        path = Game::root + "/ROUTES/" + Game::route + "/users.txt";
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)){
        S_OUT << "Error: the users file " << path << " failed to open; nobody can log in." << "\n";
        return;
    }

    S_OUT << "Loading the users file: " << path << "\n";

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    QString line;
    while (in.readLineInto(&line)) {
        line = line.trimmed();
        if(line.isEmpty() || line.startsWith("#"))
            continue;
        if(!line.contains('='))
            continue;
        const QString user = line.section('=', 0, 0).trimmed();
        if(user.isEmpty())
            continue;
        usersAuth[user] = line.section('=', 1).trimmed();
    }
    S_OUT << usersAuth.size() << " users\n";
}

void RouteEditorServer::listUsers(){
    int loggedIn = 0;
    foreach (ClientInfo *value, clients)
        if(value != NULL && value->loggedIn)
            loggedIn++;
    S_OUT << "Users: " << loggedIn << " logged in, " << clients.size() - loggedIn << " not logged in\n";
    foreach (ClientInfo *value, clients) {
        if(value == NULL)
            continue;
        S_OUT << "  " << (value->loggedIn ? value->username : QString("(not logged in)")) << " "
              << value->X << "_" << -value->Z << " " << value->lastAction
              << (value->tsre.isEmpty() ? QString() : " (TSRE " + value->tsre + ")") << "\n";
    }
}

bool RouteEditorServer::userAuth(const QString &user, const QByteArray &nonce, const QString &answer){
    if(!authenticateWithFile)
        return true;
    if(!usersAuth.contains(user))
        return false;
    // Compared in full, whatever differs first.
    const QByteArray expected = NetworkMessage::loginAnswer(usersAuth[user], nonce, user).toLatin1();
    const QByteArray given = answer.toLatin1();
    if(expected.size() != given.size())
        return false;
    char difference = 0;
    for(int i = 0; i < expected.size(); i++)
        difference |= expected[i] ^ given[i];
    return difference == 0;
}

QString RouteEditorServer::nameOf(ClientInfo *client) const {
    if(client == NULL)
        return QString();
    if(client->loggedIn)
        return client->username;
    return "(not logged in) " + client->socket->peerAddress().toString();
}

// Everyone logged in may view and edit, watch a run and drive in it, until
// permissions are designed; a run is controlled from the console only (task
// server 05). Every request goes through here (task server 02).
bool RouteEditorServer::allowed(ClientInfo *client, Action action) const {
    if (action == Action::RunControl)
        return false;
    return client != NULL && client->loggedIn;
}

QString RouteEditorServer::usersInfo() const {
    QString out = "users_info (\n";
    foreach (ClientInfo *value, clients) {
        if(value == NULL || !value->loggedIn)
            continue;
        out += "user ( ";
        out += value->username+" ";
        out += QString::number(value->X)+" ";
        out += QString::number(value->Z)+" ";
        out += QString::number(value->x)+" ";
        out += QString::number(value->y)+" ";
        out += QString::number(value->z)+" ";
        out += ParserX::AddComIfReq(value->lastAction) +" ";
        out += ")\n";
    }
    out += ")\n";
    return out;
}

// The users and their pointers, to the logged-in clients, when something in
// it changed (or to everyone, always).
void RouteEditorServer::sendUsersInfo(bool always){
    const QString info = usersInfo();
    if(!always && info == lastUsersInfo)
        return;
    lastUsersInfo = info;
    const QByteArray message = NetworkMessage::textFrame(info);
    foreach (ClientInfo *value, clients) {
        if(value == NULL || !value->loggedIn)
            continue;
        value->socket->sendBinaryMessage(message);
    }
}

void RouteEditorServer::update(){
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    foreach (ClientInfo *value, clients) {
        if(value == NULL || value->loggedIn || value->closing)
            continue;
        if(now - value->connectedAt < loginTimeoutMs())
            continue;
        S_OUT << timeNow() << " No login in " << loginTimeoutMs() / 1000.0 << " s, closing " << value->socket->peerAddress().toString() << "\n";
        value->closing = true;
        value->socket->close(QWebSocketProtocol::CloseCodePolicyViolated, "no login");
    }
    sendUsersInfo(false);
}

ConsoleThread::ConsoleThread(){
}

ConsoleThread::~ConsoleThread(){

}

// Reads lines until the input ends; then the server runs on without a
// console. Nothing else runs in this thread. The input is read from its
// descriptor, not through the C library's stdin: a read waiting on stdin holds
// its lock, and the exit, flushing every stream, would wait for it for ever.
void ConsoleThread::run(){
    QByteArray pending;
    char buffer[512];
    for (;;) {
        const int n = int(readInput(0, buffer, sizeof(buffer)));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        pending.append(buffer, n);
        int newline;
        while ((newline = pending.indexOf('\n')) >= 0) {
            emit sendCommand(QString::fromLocal8Bit(pending.left(newline)));
            pending.remove(0, newline + 1);
        }
    }
    if (!pending.isEmpty())
        emit sendCommand(QString::fromLocal8Bit(pending));
    emit inputClosed();
}

void RouteEditorServer::consoleClosed(){
    S_OUT << timeNow() << " The console input ended; the server runs on without it.\n";
}

void RouteEditorServer::printHelp(){
    S_OUT << "Commands:\n"
          << "  help          this list\n"
          << "  status        the route, the users, the unsaved changes\n"
          << "  users         the users, where they are and what they did last\n"
          << "  kick <user>   disconnects a user (who does not reconnect on its own)\n"
          << "  save          saves the route, and says what it wrote\n"
          << "  quit          stops the server (unsaved changes are lost)\n";
    if (simulation != NULL)
        simulation->help();
}

void RouteEditorServer::printStatus(){
    S_OUT << "Route: " << Game::route << " (" << Game::root << ")\n";
    if(m_pWebSocketServer != NULL)
        S_OUT << "Listening: " << (listening() ? "yes, port " + QString::number(m_pWebSocketServer->serverPort()) : QString("no")) << "\n";
    S_OUT << "Session: " << session << ", " << changeNumber << " change(s) since the start\n";
    listUsers();
    if(route == NULL)
        return;
    QVector<QString> unsaved;
    route->getUnsavedInfo(unsaved);
    S_OUT << "Unsaved: " << unsaved.size() << (unsaved.isEmpty() ? "" : ":") << "\n";
    foreach(const QString &item, unsaved)
        S_OUT << "  " << item << "\n";
    S_OUT << "The track and road databases are written on every save.\n";
    if (simulation != NULL)
        simulation->status();
}

void RouteEditorServer::kick(const QString &username){
    foreach (ClientInfo *value, clients) {
        if(value == NULL || !value->loggedIn || value->username != username)
            continue;
        sendUtf16Message(value->socket, "kicked ( \"kicked from the server's console\" )\n");
        value->socket->flush();
        value->closing = true;
        value->socket->close(QWebSocketProtocol::CloseCodeNormal, "kicked");
        S_OUT << "Kicked " << username << "\n";
        return;
    }
    S_OUT << "No user " << username << "\n";
}

void RouteEditorServer::save(){
    if(route == NULL){
        S_OUT << "Nothing to save: no route.\n";
        return;
    }
    if(!Game::writeEnabled){
        S_OUT << "Not saved: writing is disabled (core.route.writeEnabled).\n";
        return;
    }
    QVector<QString> before;
    route->getUnsavedInfo(before);
    route->save();
    QVector<QString> after;
    route->getUnsavedInfo(after);
    int written = 0;
    foreach(const QString &item, before)
        if(!after.contains(item))
            written++;
    S_OUT << timeNow() << " Saved: " << written << " changed file(s)";
    foreach(const QString &item, before)
        if(!after.contains(item))
            S_OUT << "\n  " << item;
    S_OUT << "\n";
    const bool tdb = Game::trackDB != NULL && Game::trackDB->lastSaveWritten;
    const bool rdb = Game::roadDB != NULL && Game::roadDB->lastSaveWritten;
    S_OUT << "Track database: " << (tdb ? "written" : "NOT written") << "; road database: " << (rdb ? "written" : "NOT written") << "\n";
    if(!after.isEmpty()){
        S_OUT << "NOT saved: " << after.size() << "\n";
        foreach(const QString &item, after)
            S_OUT << "  " << item << "\n";
    }
}

void RouteEditorServer::readCommand(QString val){
    val = val.trimmed();
    if(val.isEmpty())
        return;
    const QString command = val.section(' ', 0, 0).toLower();
    const QString argument = val.section(' ', 1).trimmed();
    if (command == "quit") {
        S_OUT << "Quitting.\n";
        qApp->quit();
    } else if (command == "save") {
        save();
    } else if (command == "users") {
        listUsers();
    } else if (command == "status") {
        printStatus();
    } else if (command == "kick") {
        kick(argument);
    } else if (command == "help" || command == "?") {
        printHelp();
    } else if (simulation != NULL && simulation->console(command, argument)) {
        // A command of the run's (task server 05).
    } else {
        S_OUT << "Unknown command " << command << "; help lists the commands.\n";
    }
}

bool RouteEditorServer::loadRoute(){
    Game::currentShapeLib = new ShapeLib();
    Game::currentEngLib = new EngLib();

    route = new Route();
    route->load();
    if (!route->loaded){
        return false;
    }
    // Clients keep the node numbers they were given: the save closes the
    // holes only in the file it writes (task server 01, B4).
    if(Game::trackDB != NULL)
        Game::trackDB->renumberNodesOnlyInFile = true;
    if(Game::roadDB != NULL)
        Game::roadDB->renumberNodesOnlyInFile = true;
    // The route's files TSRE never writes, as this load read them.
    fixedFiles = RouteBundle::gatherFixed(Game::root, Game::route);
    qint64 bytes = 0;
    for (const QByteArray &file : std::as_const(fixedFiles.files))
        bytes += file.size();
    S_OUT << timeNow() << " Route files for clients: " << fixedFiles.files.size() << " files, "
          << QString::number(bytes / 1048576.0, 'f', 1) << " MB\n";
    for (const QString &skipped : std::as_const(fixedFiles.skipped))
        S_OUT << timeNow() << " Left out of the route files (too large): " << skipped << "\n";
    return true;
}

QByteArray RouteEditorServer::routeFilesRecord(ClientInfo *client){
    RouteBundle::Bundle bundle = fixedFiles;
    RouteBundle::gatherCurrent(Game::root, Game::route, bundle);
    const QByteArray body = RouteBundle::encode(bundle);
    QByteArray outd;
    QDataStream out(&outd, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    NetworkToken::write(out, TS::TSRE_Requested_Route_Files);
    out << (qint32)0 << (qint8)0 << (qint32)0 << (qint32)0;
    out.writeRawData(body.constData(), body.size());
    out.setDevice(nullptr);
    S_OUT << timeNow() << " " << nameOf(client) << ": the route files, " << bundle.files.size() << " files and "
          << bundle.heavy.size() << " heavy files listed, " << QString::number(body.size() / 1048576.0, 'f', 1)
          << " MB before compression\n";
    for (const QString &skipped : std::as_const(bundle.skipped))
        if (!fixedFiles.skipped.contains(skipped))
            S_OUT << timeNow() << " Left out of the route files (too large): " << skipped << "\n";
    return outd;
}

RouteEditorServer::~RouteEditorServer() {
    if(m_pWebSocketServer != NULL)
        m_pWebSocketServer->close();
    qDeleteAll(clients.begin(), clients.end());
}


void RouteEditorServer::onNewConnection(){
    QWebSocket *pSocket = m_pWebSocketServer->nextPendingConnection();
    // A larger message closes the socket (S4).
    pSocket->setMaxAllowedIncomingMessageSize(quint64(NetworkMessage::ClientLimit + NetworkMessage::HeaderSize));
    pSocket->setMaxAllowedIncomingFrameSize(quint64(NetworkMessage::ClientLimit + NetworkMessage::HeaderSize));
    S_OUT << timeNow() << " New connection " << pSocket->peerAddress().toString() << "\n";
    connect(pSocket, &QWebSocket::textMessageReceived, this, &RouteEditorServer::processTextMessage);
    connect(pSocket, &QWebSocket::binaryMessageReceived, this, &RouteEditorServer::processBinaryMessage);
    connect(pSocket, &QWebSocket::disconnected, this, &RouteEditorServer::socketDisconnected);

    ClientInfo *info = new ClientInfo();
    info->socket = pSocket;
    info->username = "";
    info->lastAction = "Waiting for authentication";
    info->connectedAt = QDateTime::currentMSecsSinceEpoch();
    clients[pSocket] = info;
}

void RouteEditorServer::processTextMessage(QString message){
    Q_UNUSED(message);
    // Messages are binary frames; a text frame is not part of the protocol.
}

// Sends a refusal to the client that sent something the server did not
// apply, as the reply to its request; nothing of it is forwarded.
void RouteEditorServer::refuse(ClientInfo *client, const QString &what, const QString &why){
    S_OUT << timeNow() << " Refused " << what << " from " << nameOf(client) << ": " << why << "\n";
    replyText(client, "error ( \"" + NetworkMessage::quoted(what + ": " + why) + "\" )\n");
}

void RouteEditorServer::reply(ClientInfo *client, const QByteArray &payload){
    if(client->closing)
        return;
    client->socket->sendBinaryMessage(NetworkMessage::frame(payload, client->request));
}

void RouteEditorServer::replyText(ClientInfo *client, const QString &text){
    reply(client, NetworkMessage::utf16(text));
}

void RouteEditorServer::closeClient(ClientInfo *client, const QString &text, const QString &why){
    if(client->closing)
        return;
    replyText(client, text);
    client->socket->flush();
    client->closing = true;
    client->socket->close(QWebSocketProtocol::CloseCodePolicyViolated, why.left(100));
}

void RouteEditorServer::accept(ClientInfo *client, const QByteArray &payload){
    ++changeNumber;
    if(client->firstChange == 0)
        client->firstChange = changeNumber;
    client->lastChange = changeNumber;
    forwardToOthers(client, NetworkMessage::frame(payload, 0, changeNumber));
}

void RouteEditorServer::forwardToOthers(ClientInfo *from, const QByteArray &message){
    foreach (ClientInfo *value, clients) {
        if(value == NULL || value == from || !value->loggedIn || value->closing)
            continue;
        value->socket->sendBinaryMessage(message);
    }
}

// hello ( protocol ( 1 ) tsre ( "<version>" ) user ( "<name>" ) ): the
// challenge back, or a refusal (another protocol).
bool RouteEditorServer::hello(ClientInfo *client, FileBuffer *data){
    int protocol = 0;
    QString user;
    QString tsre;
    QString sh;
    while(!(sh = ParserX::NextTokenInside(data).toLower()).isEmpty()){
        if(sh == "protocol")
            protocol = int(ParserX::GetNumber(data));
        else if(sh == "tsre")
            tsre = ParserX::GetStringInside(data);
        else if(sh == "user")
            user = ParserX::GetStringInside(data);
        ParserX::SkipToken(data);
    }
    if(client->loggedIn){
        refuse(client, "hello", "logged in already");
        return true;
    }
    if(protocol != NetworkMessage::Protocol){
        const QString why = QString("protocol %1 here, %2 there").arg(NetworkMessage::Protocol).arg(protocol);
        S_OUT << timeNow() << " Refused " << nameOf(client) << ": " << why << "\n";
        closeClient(client, "refused ( \"" + why + "\" )\n", why);
        return false;
    }
    client->requestedUser = NetworkMessage::cleanUsername(user);
    client->tsre = tsre;
    client->nonce = NetworkMessage::newNonce();
    client->lastAction = "Waiting for the login";
    replyText(client, "challenge ( session ( \"" + session + "\" ) nonce ( \""
              + QString::fromLatin1(client->nonce.toHex()) + "\" ) )\n");
    return true;
}

// login ( answer ( "<hex>" ) ): the challenge's answer (NetworkMessage::
// loginAnswer). The welcome back, or auth_fail and the socket closed.
bool RouteEditorServer::login(ClientInfo *client, FileBuffer *data){
    QString answer;
    QString sh;
    while(!(sh = ParserX::NextTokenInside(data).toLower()).isEmpty()){
        if(sh == "answer")
            answer = ParserX::GetStringInside(data);
        ParserX::SkipToken(data);
    }
    if(client->loggedIn)
        return true;
    if(client->nonce.isEmpty()){
        S_OUT << timeNow() << " Refused " << nameOf(client) << ": a login before its hello\n";
        closeClient(client, "refused ( \"a login before its hello\" )\n", "a login before its hello");
        return false;
    }
    const QString requested = client->requestedUser;
    // One answer per challenge.
    const QByteArray nonce = client->nonce;
    client->nonce.clear();
    if(!userAuth(requested, nonce, answer)){
        client->lastAction = "Authentication failed";
        S_OUT << timeNow() << " " << requested << " authentication failed\n";
        closeClient(client, "auth_fail ( )\n", "authentication failed");
        return false;
    }
    // The name shown to others: unique among the logged-in users.
    QString name = requested;
    for(int n = 2; ; n++){
        bool taken = false;
        foreach (ClientInfo *value, clients)
            if(value != NULL && value != client && value->loggedIn && value->username == name)
                taken = true;
        if(!taken)
            break;
        name = requested + QString::number(n);
    }
    client->username = name;
    client->loggedIn = true;
    client->lastAction = "Logged in";
    S_OUT << timeNow() << " " << name << " logged in" << (client->tsre.isEmpty() ? QString() : " (TSRE " + client->tsre + ")") << "\n";
    QString allowedActions;
    if(allowed(client, Action::View))
        allowedActions += " view";
    if(allowed(client, Action::Edit))
        allowedActions += " edit";
    replyText(client, "welcome ( user ( \"" + name + "\" ) session ( \"" + session + "\" ) route ( \""
              + NetworkMessage::quoted(Game::route) + "\" ) tsre ( \"" + NetworkMessage::quoted(Game::AppVersion)
              + "\" ) change ( " + QString::number(changeNumber) + " ) allowed (" + allowedActions + " ) )\n");
    giveStartIds(client);
    sendUsersInfo(true);
    // A run on the server: its announcement (task server 05).
    if (simulation != NULL)
        simulation->loggedIn(client);
    return true;
}

void RouteEditorServer::readUtf16Message(ClientInfo *client, FileBuffer* data){
    data->skipBOM();
    for(;;){
        const int start = data->off;
        const QString name = ParserX::NextTokenInside(data).toLower();
        if(name.isEmpty())
            break;
        const int body = data->off;
        NetworkMessage::skipRecord(data);
        const int end = data->off;
        // The record's contents alone: its parser cannot read into the next
        // record (B7).
        std::unique_ptr<FileBuffer> contents(NetworkMessage::part(data, body, end));
        const QByteArray record = NetworkMessage::record(data, start, end);
        const Record result = readRecord(client, name, contents.get(), record);
        if(result == Record::Stop)
            return;
        // Each applied edit numbered and forwarded once, on its own (B9).
        if(result == Record::Forward)
            accept(client, record);
    }
}

void RouteEditorServer::putRightObject(ClientInfo *client, int x, int z, unsigned int uid, int typeID, const QByteArray &record){
    if(typeID < 0)
        return;
    Tile *tile = route->tile.value(x*10000+z, NULL);
    WorldObj *own = tile != NULL && tile->loaded == 1 ? tile->findObject(uid, typeID) : NULL;
    QString text;
    if(own != NULL && own->loaded){
        QString object;
        QTextStream objectOut(&object);
        own->save(&objectOut);
        objectOut.flush();
        text = "update_worldobj (\nx ( " + QString::number(x) + " )\nz ( " + QString::number(z) + " )\n" + object + ")\n";
    } else {
        // The server has none: the sender's own object, removed.
        const QString mine = payloadText(record);
        const int open = mine.indexOf('(');
        if(open < 0)
            return;
        text = mine.left(open + 1) + "\nremove ( )\n" + mine.mid(open + 1);
    }
    replyText(client, text);
}

void RouteEditorServer::putRightNode(ClientInfo *client, bool road, int id){
    TDB *db = road ? Game::roadDB : Game::trackDB;
    if(db == NULL || id < 0)
        return;
    auto found = db->trackNodes.find(id);
    TRnode *node = found != db->trackNodes.end() ? found->second : NULL;
    QString text;
    QTextStream out(&text);
    NetworkMessage::exactNumbers(out);
    out << (road ? "update_trnode_rdb (\n" : "update_trnode_tdb (\n") << "id ( " << id << " )\n";
    if(node != NULL)
        node->saveToStream(out, id);
    else
        out << "remove ( )\n";
    out << ")\n";
    out.flush();
    replyText(client, text);
}

void RouteEditorServer::putRightItem(ClientInfo *client, bool road, int id){
    TDB *db = road ? Game::roadDB : Game::trackDB;
    if(db == NULL || id < 0)
        return;
    auto found = db->trackItems.find(id);
    TRitem *item = found != db->trackItems.end() ? found->second : NULL;
    QString text;
    QTextStream out(&text);
    NetworkMessage::exactNumbers(out);
    out << (road ? "update_tritem_rdb (\n" : "update_tritem_tdb (\n") << "id ( " << id << " )\n";
    if(item != NULL)
        item->save(&out);
    else
        out << "remove ( )\n";
    out << ")\n";
    out.flush();
    replyText(client, text);
}

RouteEditorServer::IdPool *RouteEditorServer::pool(NetworkMessage::IdKind kind, int x, int z){
    using NetworkMessage::IdKind;
    const QString key = NetworkMessage::idKey(kind, x, z);
    auto found = idPools.find(key);
    if(found != idPools.end())
        return &found.value();
    IdPool start;
    const bool road = kind == IdKind::RdbNode || kind == IdKind::RdbItem;
    TDB *db = road ? Game::roadDB : Game::trackDB;
    switch(kind){
        case IdKind::TdbNode:
        case IdKind::RdbNode:
            if(db == NULL)
                return NULL;
            start.next = db->iTRnodes + 1;
            break;
        case IdKind::TdbItem:
        case IdKind::RdbItem:
            if(db == NULL)
                return NULL;
            // The free slots first: emptyitems (and holes).
            for(int i = 0; i < db->iTRitems; i++){
                auto item = db->trackItems.find(i);
                if(item == db->trackItems.end() || item->second == NULL || item->second->type == "emptyitem")
                    start.free.append(i);
            }
            start.next = db->iTRitems;
            break;
        case IdKind::Section:
        case IdKind::Shape: {
            TSectionDAT *sections = Game::trackDB != NULL ? Game::trackDB->tsection : NULL;
            if(sections == NULL)
                return NULL;
            // Sections two at a time: blocks start on an even ID.
            start.next = kind == IdKind::Section ? sections->routeMaxIdx + sections->routeMaxIdx % 2 : sections->routeShapes;
            break;
        }
        case IdKind::Uid:
        case IdKind::UidSound: {
            Tile *tile = route->tile.value(x*10000+z, NULL);
            if(tile == NULL || tile->loaded != 1)
                return NULL;
            start.next = tile->highestUiD(kind == IdKind::UidSound) + 1;
            break;
        }
    }
    return &idPools.insert(key, start).value();
}

QString RouteEditorServer::giveIds(ClientInfo *client, NetworkMessage::IdKind kind, int x, int z){
    IdPool *ids = pool(kind, x, z);
    if(ids == NULL || !allowed(client, Action::Edit))
        return QString();
    const int size = NetworkMessage::idBlockSize(kind);
    // UiDs stay below a million (TSRE treats higher ones apart).
    const int limit = NetworkMessage::idKindPerTile(kind) ? 1000000 : INT_MAX;
    QList<int> block;
    while(block.size() < size && !ids->free.isEmpty())
        block.append(ids->free.takeFirst());
    while(block.size() < size && ids->next < limit)
        block.append(ids->next++);
    if(block.isEmpty())
        return QString();
    std::sort(block.begin(), block.end());
    QSet<int> &given = client->givenIds[NetworkMessage::idKey(kind, x, z)];
    for(int id : block)
        given.insert(id);
    const QString tile = NetworkMessage::idKindPerTile(kind) ? " " + QString::number(x) + " " + QString::number(z) : QString();
    QString out;
    int first = block[0];
    int count = 1;
    for(int i = 1; i <= block.size(); i++){
        if(i < block.size() && block[i] == block[i-1] + 1){
            count++;
            continue;
        }
        out += "id_block ( " + NetworkMessage::idKindName(kind) + " " + QString::number(first) + " " + QString::number(count) + tile + " )\n";
        if(i < block.size()){
            first = block[i];
            count = 1;
        }
    }
    return out;
}

void RouteEditorServer::giveStartIds(ClientInfo *client){
    using NetworkMessage::IdKind;
    QString blocks;
    for(IdKind kind : {IdKind::TdbNode, IdKind::TdbItem, IdKind::RdbNode, IdKind::RdbItem, IdKind::Section, IdKind::Shape})
        blocks += giveIds(client, kind);
    if(!blocks.isEmpty())
        sendUtf16Message(client->socket, blocks);
}

QString RouteEditorServer::takeGiven(ClientInfo *client, NetworkMessage::IdKind kind, int id, int x, int z){
    auto given = client->givenIds.find(NetworkMessage::idKey(kind, x, z));
    if(given == client->givenIds.end() || !given->remove(id))
        return QString("new %1 %2 is not one of this user's").arg(NetworkMessage::idKindName(kind)).arg(id);
    return QString();
}

// An item slot emptied: given again, first.
void RouteEditorServer::freeItem(bool road, int id){
    const NetworkMessage::IdKind kind = road ? NetworkMessage::IdKind::RdbItem : NetworkMessage::IdKind::TdbItem;
    auto found = idPools.find(NetworkMessage::idKey(kind));
    if(found == idPools.end() || found->free.contains(id))
        return;
    found->free.insert(std::lower_bound(found->free.begin(), found->free.end(), id), id);
}

// A disconnected client's unused item IDs are given again; the other kinds'
// are not (their gaps cost nothing).
void RouteEditorServer::returnIds(ClientInfo *client){
    for(bool road : {false, true}){
        const NetworkMessage::IdKind kind = road ? NetworkMessage::IdKind::RdbItem : NetworkMessage::IdKind::TdbItem;
        for(int id : client->givenIds.value(NetworkMessage::idKey(kind)))
            freeItem(road, id);
    }
    client->givenIds.clear();
}

QByteArray RouteEditorServer::terrainRecord(char kind, bool requested, Terrain *t, int x, int z){
    QByteArray outd;
    QDataStream out(&outd, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    TS::TokenId token;
    if(kind == 'T')
        token = requested ? TS::TSRE_Requested_Terrain_tFile : TS::TSRE_Terrain_tFile;
    else if(kind == 'Y')
        token = requested ? TS::TSRE_Requested_Terrain_RawFile : TS::TSRE_Terrain_RawFile;
    else
        token = requested ? TS::TSRE_Requested_Terrain_FtFile : TS::TSRE_Terrain_FtFile;
    NetworkToken::write(out, token);
    out << (qint32)0; //should be size in bytes;
    out << (qint8)0;
    out << (qint32)x;
    out << (qint32)z;
    if(kind == 'T')
        t->saveTfileToStream(out);
    else if(kind == 'Y')
        t->saveRAWfileToStreamFloat(out);
    else
        t->saveFfileToStream(out); // nothing when the tile has no flags (B5)
    out.setDevice(nullptr);
    return outd;
}

RouteEditorServer::Record RouteEditorServer::readRecord(ClientInfo *client, const QString &sh, FileBuffer* data, const QByteArray &record){
    if (sh == "update_pointer_position" && client->loggedIn){
        client->X = ParserX::GetNumberInside(data);
        client->Z = ParserX::GetNumberInside(data);
        client->x = ParserX::GetNumberInside(data);
        client->y = ParserX::GetNumberInside(data);
        client->z = ParserX::GetNumberInside(data);
        return Record::Done;
    }
    // Refused: the socket is being closed, the client may be gone.
    if (sh == "hello")
        return hello(client, data) ? Record::Done : Record::Stop;
    if (sh == "login")
        return login(client, data) ? Record::Done : Record::Stop;
    // Nothing else before a login: the socket is closed.
    if(!client->loggedIn){
        S_OUT << timeNow() << " Refused " << nameOf(client) << ": " << sh << " before the login\n";
        closeClient(client, "refused ( \"" + NetworkMessage::quoted(sh) + " before the login\" )\n", "a record before the login");
        return Record::Stop;
    }
    // The one permission check: asking for data is viewing, the rest edits.
    const Action action = sh.startsWith("request_") ? Action::View : Action::Edit;
    if(!allowed(client, action)){
        refuse(client, sh, "not allowed");
        return Record::Done;
    }

    if (sh == "request_addons") {
        client->lastAction = "Requested Addons ";
        QByteArray outd;
        QTextStream out(&outd);
        NetworkMessage::exactNumbers(out);
        out.setEncoding(QStringConverter::Utf16);
        out.setGenerateByteOrderMark(true);
        out << "requested_addon (\n";
        if(route->ref != NULL)
            route->ref->saveToStream(&out);
        out << ")\n";
        out.flush();
        reply(client, outd);
        return Record::Done;
    }

    // The route's data by kind (task server 03): no path is asked for.
    if (sh == "request_route_files") {
        client->lastAction = "Requested the route's files";
        reply(client, routeFilesRecord(client));
        return Record::Done;
    }

    if (sh == "request_tile") {
        int x = ParserX::GetNumber(data);
        int z = ParserX::GetNumber(data);
        client->lastAction = "Requested world tile "+QString::number(x)+" "+QString::number(-z);
        Tile *tile = route->tile.value(x*10000+z, NULL);
        if(tile == NULL || tile->loaded != 1){
            // The client marks the tile missing (B3).
            replyText(client, "no_tile ( "+QString::number(x)+" "+QString::number(z)+" )\n");
            return Record::Done;
        }
        QByteArray outd;
        QTextStream out(&outd);
        NetworkMessage::exactNumbers(out);
        out.setEncoding(QStringConverter::Utf16);
        out.setGenerateByteOrderMark(true);
        out << "requested_tile (\n";
        out << "x ( "+QString::number(x)+" )\n";
        out << "z ( "+QString::number(z)+" )\n";
        tile->saveToStream(out);
        out << ")\n";
        // The tile's UiDs for this user, with the tile (once, while it holds
        // some).
        for(NetworkMessage::IdKind kind : {NetworkMessage::IdKind::Uid, NetworkMessage::IdKind::UidSound})
            if(client->givenIds.value(NetworkMessage::idKey(kind, x, z)).isEmpty())
                out << giveIds(client, kind, x, z);
        out.flush();
        reply(client, outd);
        return Record::Done;
    }
    if (sh == "request_ids") {
        NetworkMessage::IdKind kind;
        if(!NetworkMessage::idKindFromName(ParserX::GetStringInside(data), kind)){
            refuse(client, "ids", "no such kind");
            return Record::Done;
        }
        int x = 0, z = 0;
        if(NetworkMessage::idKindPerTile(kind)){
            x = int(ParserX::GetNumber(data));
            z = int(ParserX::GetNumber(data));
        }
        const QString blocks = giveIds(client, kind, x, z);
        if(blocks.isEmpty())
            refuse(client, "ids", "none to give of " + NetworkMessage::idKey(kind, x, z));
        else
            replyText(client, blocks);
        return Record::Done;
    }
    if (sh == "request_terrain_qt") {
        QByteArray outd;
        QTextStream out(&outd);
        NetworkMessage::exactNumbers(out);
        out.setEncoding(QStringConverter::Utf16);
        out.setGenerateByteOrderMark(true);
        out << "requested_td (\n";
        Game::terrainLib->saveQtToStream(out);
        out << ") \n";
        out << "requested_td_lo (\n";
        Game::terrainLib->saveQtLoToStream(out);
        out << ") \n";
        out.flush();
        reply(client, outd);
        // The quad tree's files.
        QuadTree *trees[2] = {Game::terrainLib->getQuadTreeDetailed(), Game::terrainLib->getQuadTreeDistant()};
        for(int k = 0; k < 2; k++){
            QuadTree *qt = trees[k];
            if(qt == NULL)
                continue;
            QHashIterator<int, QuadTree::TdFile*> i(qt->td);
            while (i.hasNext()) {
                i.next();
                QByteArray outd2;
                QDataStream out2(&outd2, QIODevice::WriteOnly);
                out2.setByteOrder(QDataStream::LittleEndian);
                out2.setFloatingPointPrecision(QDataStream::SinglePrecision);
                NetworkToken::write(out2, k == 0 ? TS::TSRE_Requested_TD_File : TS::TSRE_Requested_TD_Lo_File);
                out2 << (qint32)0; //should be size in bytes;
                out2 << (qint8)0;
                out2 << (qint32)i.value()->x;
                out2 << (qint32)i.value()->y;
                qt->saveTD(i.value()->x, i.value()->y, &out2);
                out2.setDevice(nullptr);
                reply(client, outd2);
            }
        }
        return Record::Done;
    }
    // A terrain tile's files, as the live tile would save them (task server
    // 03): detailed or distant, all or the tile file only (map mode). None
    // when there is no such tile.
    if (sh == "request_terrain_files") {
        const int x = int(ParserX::GetNumber(data));
        const int z = int(ParserX::GetNumber(data));
        const bool low = ParserX::GetNumber(data) != 0;
        const bool descriptorOnly = ParserX::GetNumber(data) != 0;
        client->lastAction = QString("Requested terrain ") + (low ? "distant " : "") + "tile " + QString::number(x) + " " + QString::number(-z);
        TerrainLibQt *terrain = dynamic_cast<TerrainLibQt*>(Game::terrainLib);
        Terrain *t = NULL;
        if (terrain != NULL) {
            if (descriptorOnly) {
                t = low ? terrain->getDistantDescriptor(x, z) : terrain->getTerrainDescriptor(x, z);
            } else {
                if (low)
                    terrain->setDistantAsCurrent();
                t = terrain->getTerrainByXY(x, z, true);
                terrain->setDetailedAsCurrent();
                if (t != NULL && !t->loaded)
                    t->completeLoad();
            }
        }
        const QHash<QString, QByteArray> files = t != NULL ? t->filesForClient(descriptorOnly) : QHash<QString, QByteArray>();
        QByteArray outd;
        QDataStream out(&outd, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out.setByteOrder(QDataStream::LittleEndian);
        NetworkToken::write(out, TS::TSRE_Requested_Terrain_Files);
        out << (qint32)0 << (qint8)0 << (qint32)x << (qint32)z;
        out << quint8(low ? 1 : 0) << quint8(descriptorOnly ? 1 : 0) << quint32(files.size());
        for (auto it = files.constBegin(); it != files.constEnd(); ++it)
            out << it.key() << it.value();
        out.setDevice(nullptr);
        reply(client, outd);
        return Record::Done;
    }
    if (sh == "request_terrain_tfile" || sh == "request_terrain_yfile" || sh == "request_terrain_ffile") {
        int x = ParserX::GetNumber(data);
        int z = ParserX::GetNumber(data);
        const char kind = sh == "request_terrain_tfile" ? 'T' : (sh == "request_terrain_yfile" ? 'Y' : 'F');
        client->lastAction = QString("Requested terrain ") + kind + " file " + QString::number(x) + " " + QString::number(-z);
        Terrain *t = Game::terrainLib->getTerrainByXY(x, z, true);
        if(t == NULL || !t->loaded){
            refuse(client, QString("terrain %1 %2").arg(x).arg(-z), "the server has no such terrain tile");
            return Record::Done;
        }
        reply(client, terrainRecord(kind, true, t, x, z));
        return Record::Done;
    }
    if (sh == "update_worldobj") {
        QString refusal;
        Route::ObjectRecord parsed;
        // A new object's UiD: the sender's (the watermark, UiD 0, is one a
        // tile).
        WorldObj *obj = route->updateWorldObjData(data, &refusal, &parsed, [&](const Route::ObjectRecord &r) {
            if(r.uid == 0)
                return QString();
            return takeGiven(client, r.sound ? NetworkMessage::IdKind::UidSound : NetworkMessage::IdKind::Uid,
                             int(r.uid), r.x, r.z);
        });
        if(obj == NULL){
            refuse(client, "object", refusal);
            putRightObject(client, parsed.x, parsed.z, parsed.uid, parsed.typeID, record);
            return Record::Done;
        }
        client->lastAction = "Modified world object "+QString::number(obj->x)+" "+QString::number(-obj->y)+" "+QString::number(obj->UiD);
        S_OUT << timeNow() << " " << client->username << ": object " << obj->x << " " << -obj->y << " " << obj->UiD << (obj->loaded ? "" : " removed") << "\n";
        return Record::Forward;
    }
    if (sh == "request_tdb" || sh == "request_rdb") {
        const bool road = sh == "request_rdb";
        client->lastAction = road ? "Requested Road Database " : "Requested Track Database ";
        QByteArray outd;
        QTextStream out(&outd);
        NetworkMessage::exactNumbers(out);
        out.setEncoding(QStringConverter::Utf16);
        out.setGenerateByteOrderMark(true);
        out << (road ? "requested_rdb (\n" : "requested_tdb (\n");
        (road ? Game::roadDB : Game::trackDB)->saveToStream(out);
        out << ")\n";
        out.flush();
        reply(client, outd);
        return Record::Done;
    }
    if (sh == "update_trnode_tdb" || sh == "update_trnode_rdb") {
        const bool road = sh == "update_trnode_rdb";
        TDB *db = road ? Game::roadDB : Game::trackDB;
        QString refusal;
        const int id = peekId(data);
        int n = db->updateTrNodeData(data, &refusal, [&](int newId) {
            return takeGiven(client, road ? NetworkMessage::IdKind::RdbNode : NetworkMessage::IdKind::TdbNode, newId);
        });
        if(n < 0){
            refuse(client, "track node", refusal);
            putRightNode(client, road, id);
            return Record::Done;
        }
        client->lastAction = "Modified TrackNode " + QString::number(n);
        return Record::Forward;
    }
    if (sh == "update_tritem_tdb" || sh == "update_tritem_rdb") {
        const bool road = sh == "update_tritem_rdb";
        TDB *db = road ? Game::roadDB : Game::trackDB;
        QString refusal;
        const int id = peekId(data);
        int n = db->updateTrItemData(data, &refusal, [&](int newId) {
            return takeGiven(client, road ? NetworkMessage::IdKind::RdbItem : NetworkMessage::IdKind::TdbItem, newId);
        });
        if(n < 0){
            refuse(client, "track item", refusal);
            putRightItem(client, road, id);
            return Record::Done;
        }
        auto item = db->trackItems.find(n);
        if(item != db->trackItems.end() && item->second != NULL && item->second->type == "emptyitem")
            freeItem(road, n);
        client->lastAction = "Modified TrackItem " + QString::number(n);
        return Record::Forward;
    }
    if (sh == "update_tracksection") {
        QString refusal;
        if(!Game::trackDB->updateTrackSectionData(data, &refusal, [&](int newId) {
                return takeGiven(client, NetworkMessage::IdKind::Section, newId); })){
            refuse(client, "track section", refusal);
            return Record::Done;
        }
        client->lastAction = "Modified TrackSection ";
        return Record::Forward;
    }
    if (sh == "update_trackshape") {
        QString refusal;
        if(!Game::trackDB->updateTrackShapeData(data, &refusal, [&](int newId) {
                return takeGiven(client, NetworkMessage::IdKind::Shape, newId); })){
            refuse(client, "track shape", refusal);
            return Record::Done;
        }
        client->lastAction = "Modified TrackShape ";
        return Record::Forward;
    }
    if (sh == "request_trk") {
        client->lastAction = "Requested TRK file ";
        QByteArray outd;
        QTextStream out(&outd);
        NetworkMessage::exactNumbers(out);
        out.setEncoding(QStringConverter::Utf16);
        out.setGenerateByteOrderMark(true);
        out << "requested_trk (\n";
        Game::currentRoute->trk->saveToStream(out);
        out << ")\n";
        out.flush();
        reply(client, outd);
        return Record::Done;
    }
    if (sh == "request_tsection") {
        client->lastAction = "Requested TrackSection Database ";
        QByteArray outd;
        QTextStream out(&outd);
        NetworkMessage::exactNumbers(out);
        out.setEncoding(QStringConverter::Utf16);
        out.setGenerateByteOrderMark(true);
        out << "requested_tsection (\n";
        Game::trackDB->tsection->saveRouteToStream(out);
        out << ")\n";
        out.flush();
        reply(client, outd);
        return Record::Done;
    }
    refuse(client, sh, "unknown record");
    return Record::Done;
}

void RouteEditorServer::readBinaryMessage(ClientInfo *client, const QByteArray &message, FileBuffer* data) {
    if(!client->loggedIn){
        closeClient(client, "refused ( \"a binary record before the login\" )\n", "a record before the login");
        return;
    }

    TS::TokenId token;
    QString tokenError;
    if (!NetworkToken::read(data, token, tokenError)) {
        refuse(client, "binary record", tokenError);
        return;
    }
    // A run's records (task server 05): watching (the input made, the
    // trains heard) and driving.
    if (token == TS::TSRE_Simulation) {
        Sim::Wire::Kind kind;
        QByteArray body;
        if (!Sim::Wire::ParseRecord(message, kind, body)) {
            refuse(client, "simulation record", "malformed");
            return;
        }
        const bool drives = kind == Sim::Wire::Kind::Drive || kind == Sim::Wire::Kind::Leave
                || kind == Sim::Wire::Kind::Command;
        if (!allowed(client, drives ? Action::Drive : Action::Watch)) {
            refuse(client, "simulation record", "not allowed");
            return;
        }
        if (simulation != NULL)
            simulation->received(client, kind, body, client->request);
        return;
    }
    if(!allowed(client, Action::Edit)){
        refuse(client, "terrain", "not allowed");
        return;
    }
    if (token != TS::TSRE_Terrain_tFile && token != TS::TSRE_Terrain_RawFile && token != TS::TSRE_Terrain_FtFile) {
        refuse(client, "binary record", "unknown token " + QString::number(token));
        return;
    }
    data->getInt();
    data->get();
    const int x = data->getInt();
    const int z = data->getInt();
    const QString where = QString::number(x) + " " + QString::number(-z);
    Terrain *t = Game::terrainLib->getTerrainByXY(x, z, true);
    if (t == NULL || !t->loaded) {
        refuse(client, "terrain " + where, "the server has no such terrain tile");
        return;
    }
    bool applied = false;
    QString kind;
    switch (token) {
        case TS::TSRE_Terrain_tFile:
            kind = "T";
            applied = t->loadTFile(data);
            break;
        case TS::TSRE_Terrain_RawFile:
            kind = "heights";
            applied = t->loadRAWFile(data);
            break;
        default:
            kind = "flags";
            applied = t->loadFFile(data);
            break;
    }
    if(!applied){
        refuse(client, "terrain " + kind + " " + where, "malformed or of another size");
        // The server's file as an edit: the sender's tile as the server has it.
        reply(client, terrainRecord(token == TS::TSRE_Terrain_tFile ? 'T' : (token == TS::TSRE_Terrain_RawFile ? 'Y' : 'F'),
                                    false, t, x, z));
        return;
    }
    t->setModified();
    client->lastAction = "Modified terrain " + kind + " " + where;
    S_OUT << timeNow() << " " << client->username << ": terrain " << kind << " " << where << "\n";
    accept(client, message);
}

void RouteEditorServer::processBinaryMessage(QByteArray message){
    QWebSocket *socket = qobject_cast<QWebSocket *>(sender());
    ClientInfo *client = clients.value(socket, NULL);
    if (message.isEmpty() || client == NULL || client->closing)
        return;
    NetworkMessage::Frame frame;
    QString why;
    if (NetworkMessage::readFrame(message, NetworkMessage::ClientLimit, frame, &why) != NetworkMessage::Read::Ok) {
        // Not this protocol, too large or malformed: nothing of it read.
        S_OUT << timeNow() << " Refused a message from " << nameOf(client) << ": " << why << "\n";
        client->request = frame.request;
        closeClient(client, "refused ( \"" + NetworkMessage::quoted(why) + "\" )\n", why);
        return;
    }
    client->request = frame.request;
    client->firstChange = 0;
    client->lastChange = 0;
    // A copy the parser owns, freed after the message (B8).
    std::unique_ptr<FileBuffer> data(NetworkMessage::buffer(frame.payload));
    try {
        if (frame.kind == NetworkMessage::Text)
            readUtf16Message(client, data.get());
        else
            readBinaryMessage(client, frame.payload, data.get());
    } catch (const std::exception &e) {
        refuse(client, "message", QString("malformed: ") + e.what());
    }
    // The sender's confirmation: the numbers its edits got.
    if (!client->closing && client->lastChange != 0 && client->request != 0)
        replyText(client, QString("done ( %1 %2 )\n").arg(client->firstChange).arg(client->lastChange));
    client->request = 0;
}

void RouteEditorServer::socketDisconnected(){
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    if (pClient == NULL)
        return;
    ClientInfo *info = clients.take(pClient);
    if (info != NULL)
        returnIds(info);
    if (info != NULL && simulation != NULL)
        simulation->disconnected(info);
    S_OUT << timeNow() << " Disconnected " << (info != NULL && info->loggedIn ? info->username : pClient->peerAddress().toString());
    // Why, when it was not a normal close (a message over the limit, S4).
    if (pClient->closeCode() != QWebSocketProtocol::CloseCodeNormal && !pClient->closeReason().isEmpty())
        S_OUT << " (" << pClient->closeReason() << ")";
    S_OUT << "\n";
    delete info;
    pClient->deleteLater();
}

void RouteEditorServer::sendUtf16Message(QWebSocket *client, QString msg) {
    client->sendBinaryMessage(NetworkMessage::textFrame(msg));
}
