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
#include <routeEditor/RouteEditorClient.h>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>
#include <QtWebSockets/QWebSocket>
#include <QDebug>
#include <QTimer>
#include <tsre/world/Route.h>
#include <tsre/world/RouteClient.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/NetworkMessage.h>
#include <tsre/world/objects/WorldObj.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/fileFunctions/TS.h>
#include <tsre/fileFunctions/NetworkToken.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/world/Terrain.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TrackShape.h>
#include <tsre/ClientInfo.h>
#include <tsre/world/QuadTree.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/ErrorMessage.h>
#include <QDateTime>
#include <memory>

RouteEditorClient::RouteEditorClient() {
    QString secretError;
    const QString login = SettingsManager::instance().resolveSecretPlaceholders(
                Settings::string("core.network.clientLogin"), &secretError);
    if (!secretError.isEmpty()) {
        qWarning() << "Cannot resolve Route Editor client login:" << secretError;
        return;
    }
    QStringList args = login.split("@");
    if(args.size() < 2){
        qDebug() << "Wrong server address";
        return;
    }
    url = "ws://"+args[1];
    args = args[0].split(":");
    username = args[0];
    if(args.size() > 1)
        password = args[1];
    username = NetworkMessage::cleanUsername(username);
    init();
}

RouteEditorClient::RouteEditorClient(const QString &url, const QString &username, const QString &password) {
    this->url = url;
    this->username = NetworkMessage::cleanUsername(username);
    this->password = password;
    init();
}

void RouteEditorClient::init() {
    m_webSocket = new QWebSocket();
    qDebug() << "WebSocket server:" << url;
    connect(m_webSocket, &QWebSocket::connected, this, &RouteEditorClient::onConnected);
    connect(m_webSocket, &QWebSocket::disconnected, this, &RouteEditorClient::close);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &RouteEditorClient::processTextMessage);
    connect(m_webSocket, &QWebSocket::binaryMessageReceived, this, &RouteEditorClient::processBinaryMessage);
    connect(m_webSocket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        // A connection attempt that failed; a lost connection is handled
        // when it is closed.
        if (currentState != State::Connecting)
            return;
        if (!routeRequested) {
            currentState = State::Refused;
            emit loginFailed("Cannot connect to the server " + url + ": " + m_webSocket->errorString());
            return;
        }
        scheduleReconnect();
    });
    reconnectTimer = new QTimer(this);
    reconnectTimer->setSingleShot(true);
    connect(reconnectTimer, &QTimer::timeout, this, &RouteEditorClient::connectNow);
}

RouteEditorClient::~RouteEditorClient() {
}

void RouteEditorClient::connectNow() {
    if (m_webSocket == NULL)
        return;
    reconnectTimer->stop();
    if (m_webSocket->state() != QAbstractSocket::UnconnectedState)
        return;
    currentState = State::Connecting;
    m_webSocket->open(QUrl(url));
}

void RouteEditorClient::disconnectNow() {
    currentState = State::Refused;
    reconnectTimer->stop();
    if (m_webSocket != NULL)
        m_webSocket->close();
}

void RouteEditorClient::onConnected() {
    qDebug() << "WebSocket connected";
    if (routeRequested) {
        // Back after a disconnection: the route is loaded again, from the
        // server's current state.
        currentState = State::Connected;
        emit connectionChanged(true, "Connected to the server again; loading the route again.");
        emit reconnected();
        return;
    }
    currentState = State::Connected;
    sendUtf16Message(QString("login ( ")+username+" "+password+" )\n");
}

void RouteEditorClient::sendUtf16Message(QString msg) {
    if (m_webSocket == NULL)
        return;
    m_webSocket->sendBinaryMessage(NetworkMessage::utf16(msg));
}

// The receive rule: what applying a received message changes is not sent
// back; only the user's own edits are. And nothing while disconnected (the
// editor refuses edits then).
bool RouteEditorClient::editAllowed() {
    if (applying) {
        suppressedEdits++;
        return false;
    }
    if (currentState != State::Connected) {
        droppedEdits++;
        return false;
    }
    return true;
}

void RouteEditorClient::sendEdit(const QByteArray &message) {
    if (!editAllowed())
        return;
    sentEdits++;
    m_webSocket->sendBinaryMessage(message);
}

void RouteEditorClient::sendEditMessage(QString msg) {
    sendEdit(NetworkMessage::utf16(msg));
}

void RouteEditorClient::notice(const QString &text) {
    qWarning().noquote() << "Route Editor client:" << text;
    ErrorMessagesLib::PushErrorMessage(new ErrorMessage(
            ErrorMessage::Type_Warning, ErrorMessage::Source_Editor, text));
    emit noticeShown(text);
}

void RouteEditorClient::processTextMessage(QString message) {
    Q_UNUSED(message);
}

void RouteEditorClient::scheduleReconnect() {
    if (currentState == State::Refused || reconnectTimer->isActive())
        return;
    currentState = State::Disconnected;
    const int delay = reconnectDelayMs;
    reconnectDelayMs = std::min(reconnectDelayMs * 2, 30000);
    reconnectTimer->start(delay);
    emit connectionChanged(false, QString("Disconnected from the server %1; edits are refused. Trying again in %2 s.")
                           .arg(url).arg(delay / 1000));
}

void RouteEditorClient::connectionLost(const QString &why) {
    qWarning() << "Route Editor client:" << why;
    // The others' pointers are not shown while there is no news of them.
    qDeleteAll(clientUsersList);
    clientUsersList.clear();
    ErrorMessagesLib::PushErrorMessage(new ErrorMessage(
            ErrorMessage::Type_Warning, ErrorMessage::Source_Editor,
            "Disconnected from the server " + url + ": " + why
            + ". Edits are refused until it is back; the route is then loaded again."));
    reconnectDelayMs = 2000;
    scheduleReconnect();
}

// The socket closed: a lost connection reconnects on its own; a refused
// login or a kick does not.
void RouteEditorClient::close() {
    qDebug() << "WebSocket disconnected";
    switch (currentState) {
        case State::Refused:
            return;
        case State::Connected:
            if (!routeRequested) {
                currentState = State::Refused;
                emit loginFailed("The server " + url + " closed the connection before the login was done.");
                return;
            }
            connectionLost("the connection closed");
            return;
        case State::Connecting:
        case State::Disconnected:
            if (!routeRequested) {
                currentState = State::Refused;
                emit loginFailed("Cannot connect to the server " + url + ": " + m_webSocket->errorString());
                return;
            }
            scheduleReconnect();
            return;
    }
}

void RouteEditorClient::processBinaryMessage(QByteArray message) {
    if (message.isEmpty()) return;
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    // A copy the parser owns, freed after the message (B8).
    std::unique_ptr<FileBuffer> data(NetworkMessage::buffer(message));
    // The receive rule: applying it sends nothing back.
    const bool wasApplying = applying;
    applying = true;
    try {
        if (data->isBOM())
            readUtf16Message(pClient, data.get());
        else if (data->data[0] == 'B')
            readBinaryMessage(pClient, data.get());
    } catch (const std::exception &e) {
        qWarning() << "Route Editor client: a malformed message from the server:" << e.what();
    }
    applying = wasApplying;
}

void RouteEditorClient::readBinaryMessage(QWebSocket *client, FileBuffer* data) {
    Q_UNUSED(client);
    TS::TokenId token;
    QString tokenError;
    if (!NetworkToken::read(data, token, tokenError)) {
        qWarning().noquote() << tokenError;
        return;
    }
    if (token != TS::TSRE_Requested_TD_File && token != TS::TSRE_Requested_TD_Lo_File
            && token != TS::TSRE_Requested_Terrain_tFile && token != TS::TSRE_Requested_Terrain_RawFile
            && token != TS::TSRE_Requested_Terrain_FtFile && token != TS::TSRE_Terrain_tFile
            && token != TS::TSRE_Terrain_RawFile && token != TS::TSRE_Terrain_FtFile) {
        qWarning() << "Route Editor client: undefined token" << token;
        return;
    }
    data->getInt();
    data->get();
    const int x = data->getInt();
    const int z = data->getInt();
    if (token == TS::TSRE_Requested_TD_File || token == TS::TSRE_Requested_TD_Lo_File) {
        QuadTree *qt = token == TS::TSRE_Requested_TD_File
                ? Game::terrainLib->getQuadTreeDetailed() : Game::terrainLib->getQuadTreeDistant();
        if (qt != NULL)
            qt->loadTD(x, z, data);
        return;
    }
    Terrain *t = Game::terrainLib->getTerrainByXY(x, z);
    if (t == NULL) {
        qDebug() << "Route Editor client: no terrain" << x << z << "for" << TS::name(token);
        return;
    }
    bool applied = true;
    switch (token) {
        // The tile's files, as it loads.
        case TS::TSRE_Requested_Terrain_tFile:
            t->loadTFile(data);
            return;
        case TS::TSRE_Requested_Terrain_RawFile:
            t->loadRAWFile(data);
            return;
        case TS::TSRE_Requested_Terrain_FtFile:
            t->loadFFile(data);
            return;
        // Another user's edits.
        case TS::TSRE_Terrain_tFile:
            applied = t->Terrain::loadTFile(data);
            break;
        case TS::TSRE_Terrain_RawFile:
            applied = t->Terrain::loadRAWFile(data);
            break;
        default:
            applied = t->Terrain::loadFFile(data);
            break;
    }
    if (applied)
        t->refresh();
    else
        qWarning() << "Route Editor client: a terrain edit of" << x << -z << "not applied";
}

void RouteEditorClient::readUsersInfo(FileBuffer* data) {
    QSet<QString> present;
    QString sh;
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        if (sh == ("user")) {
            const QString name = ParserX::GetStringInside(data);
            ClientInfo *n = clientUsersList.value(name, NULL);
            if (n == NULL) {
                n = new ClientInfo();
                clientUsersList[name] = n;
            }
            present.insert(name);
            n->username = name;
            n->X = ParserX::GetNumberInside(data);
            n->Z = ParserX::GetNumberInside(data);
            n->x = ParserX::GetNumberInside(data);
            n->y = ParserX::GetNumberInside(data);
            n->z = ParserX::GetNumberInside(data);
            n->lastAction = ParserX::GetStringInside(data);
        }
        ParserX::SkipToken(data);
    }
    // Users who left.
    for (auto it = clientUsersList.begin(); it != clientUsersList.end(); ) {
        if (present.contains(it.key())) {
            ++it;
            continue;
        }
        delete it.value();
        it = clientUsersList.erase(it);
    }
}

void RouteEditorClient::readUtf16Message(QWebSocket *client, FileBuffer* data) {
    Q_UNUSED(client);
    data->skipBOM();
    QString sh;
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        if (sh == ("set_username")) {
            username = ParserX::GetStringInside(data);
        } else if (sh == ("auth_fail")) {
            currentState = State::Refused;
            emit loginFailed("The server refused the login: wrong user name or password.");
        } else if (sh == ("kicked")) {
            currentState = State::Refused;
            const QString text = "The server disconnected you (" + ParserX::GetStringInside(data)
                    + "). Edits are refused; the client does not reconnect.";
            ErrorMessagesLib::PushErrorMessage(new ErrorMessage(
                    ErrorMessage::Type_Warning, ErrorMessage::Source_Editor, text));
            emit connectionChanged(false, text);
        } else if (sh == ("edit_refused")) {
            const QString text = ParserX::GetStringInside(data);
            notice("The server refused an edit: " + text);
        } else if (sh == ("users_info")) {
            readUsersInfo(data);
        } else if (sh == ("no_tile")) {
            const int x = ParserX::GetNumber(data);
            const int z = ParserX::GetNumber(data);
            RouteClient *route = dynamic_cast<RouteClient*>(Game::currentRoute);
            if (route != NULL)
                route->markTileMissing(x, z);
        } else if (sh == ("requested_tile")) {
            Game::currentRoute->updateTileData(data);
        } else if (sh == ("requested_tdb")) {
            Game::currentRoute->loadTdbData(data, "tdb");
        } else if (sh == ("requested_rdb")) {
            Game::currentRoute->loadTdbData(data, "rdb");
        } else if (sh == ("requested_trk")) {
            Game::currentRoute->loadTrkData(data);
        } else if (sh == ("requested_td")) {
            Game::currentRoute->loadQuadTreeDetailed(data);
        } else if (sh == ("requested_td_lo")) {
            Game::currentRoute->loadQuadTreeDistant(data);
        } else if (sh == ("requested_addon")) {
            // The old one is freed: nothing points at its items yet (B10).
            delete Game::currentRoute->ref;
            Game::currentRoute->ref = new Ref("");
            Game::currentRoute->ref->loadUtf16Data(data, "");
            Game::currentRoute->ref->ensureDynTrackItems();
            emit refreshObjLists();
        } else if (sh == ("requested_tsection")) {
            Game::currentRoute->loadTSectionData(data);
        } else if (sh == ("load_route")) {
            Game::route = ParserX::GetString(data);
            if (Game::checkRoot(Game::root) && (Game::checkRemoteRoute(Game::route))) {
                routeRequested = true;
                emit loadRoute();
            } else {
                currentState = State::Refused;
                emit loginFailed("The route " + Game::route + " the server serves is not in this client's game root " + Game::root + ".");
            }
        } else if (sh == "update_worldobj") {
            // Not applied to a tile still loading: its data comes from the
            // server with the edit in it.
            Game::currentRoute->updateWorldObjData(data);
        } else if (sh == "increase_tdb_itrnode") {
            Game::trackDB->TDB::getNextItrNode();
        } else if (sh == "increase_rdb_itrnode") {
            Game::roadDB->TDB::getNextItrNode();
        } else if (sh == "increase_tdb_itritem") {
            ++Game::trackDB->iTRitems;
        } else if (sh == "increase_rdb_itritem") {
            ++Game::roadDB->iTRitems;
        } else if (sh == "update_tritem_tdb" || sh == "update_tritem_rdb") {
            QString refusal;
            if ((sh == "update_tritem_rdb" ? Game::roadDB : Game::trackDB)->updateTrItemData(data, &refusal) < 0)
                qWarning() << "Route Editor client: a track item from the server not applied:" << refusal;
        } else if (sh == "update_tracksection") {
            QString refusal;
            if (!Game::trackDB->updateTrackSectionData(data, &refusal))
                qWarning() << "Route Editor client: a track section from the server not applied:" << refusal;
        } else if (sh == "update_trackshape") {
            QString refusal;
            if (!Game::trackDB->updateTrackShapeData(data, &refusal))
                qWarning() << "Route Editor client: a track shape from the server not applied:" << refusal;
        } else if (sh == "update_trnode_tdb" || sh == "update_trnode_rdb") {
            TDB *db = sh == "update_trnode_rdb" ? Game::roadDB : Game::trackDB;
            QString refusal;
            if (db->updateTrNodeData(data, &refusal) < 0)
                qWarning() << "Route Editor client: a track node from the server not applied:" << refusal;
            else
                db->refresh();
        }
        NetworkMessage::skipRecord(data);
    }
}

void RouteEditorClient::updateTrackSectionData(TSection *s){
    if(s == NULL)
        return;
    QByteArray outd;
    QTextStream out(&outd);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << "update_tracksection ( \n";
    s->saveToStream(out);
    out << ")\n";
    out.flush();
    sendEdit(outd);
}

void RouteEditorClient::updateTrackShapeData(TrackShape *s){
    if(s == NULL)
        return;
    QByteArray outd;
    QTextStream out(&outd);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << "update_trackshape ( \n";
    s->saveToStream(out);
    out << ")\n";
    out.flush();
    sendEdit(outd);
}

void RouteEditorClient::updateTrackItemData(int iid, int tdbType, TRitem *n) {
    QByteArray outd;
    QTextStream out(&outd);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    if (tdbType == 0)
        out << "update_tritem_tdb ( \n";
    else
        out << "update_tritem_rdb ( \n";
    out << "id ( " + QString::number(iid) + " )\n";

    if (n != NULL) {
        if (iid != (int)n->trItemId) {
            qDebug() << "fail: Item id doesn't match " << iid << n->trItemId;
        }
        n->save(&out);
    } else {
        out << "remove ( )\n";
    }
    out << ")\n";
    out.flush();
    sendEdit(outd);
}

void RouteEditorClient::updateTrackNodeData(int nid, int tdbType, TRnode *n) {
    QByteArray outd;
    QTextStream out(&outd);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    if (tdbType == 0)
        out << "update_trnode_tdb ( \n";
    else
        out << "update_trnode_rdb ( \n";
    out << "id ( " + QString::number(nid) + " )\n";
    if (n != NULL)
        n->saveToStream(out, nid);
    else
        out << "remove ( )\n";
    out << ")\n";
    out.flush();
    sendEdit(outd);
}

void RouteEditorClient::updateWorldObjData(WorldObj *o) {
    // The object as its W file has it; nothing while it writes nothing yet
    // (a track object before its place is set, as it is being placed).
    QString object;
    QTextStream objectOut(&object);
    bool tl = o->loaded;
    o->loaded = true;
    o->save(&objectOut);
    o->loaded = tl;
    objectOut.flush();
    if (object.trimmed().isEmpty())
        return;
    QString record = "update_worldobj (\n";
    record += "x ( " + QString::number(o->x) + " )\n";
    record += "z ( " + QString::number(o->y) + " )\n";
    if (o->loaded == false)
        record += "remove ( ) \n";
    record += object;
    record += ")\n";
    sendEdit(NetworkMessage::utf16(record));
}

namespace {
QByteArray terrainMessage(TS::TokenId token, Terrain *t, void (Terrain::*write)(QDataStream &)) {
    QByteArray outd;
    QDataStream out(&outd, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    NetworkToken::write(out, token);
    out << (qint32) 0; //should be size in bytes;
    out << (qint8) 0;
    out << (qint32) t->mojex;
    out << (qint32) t->mojez;
    (t->*write)(out);
    out.setDevice(nullptr);
    return outd;
}
}

void RouteEditorClient::updateTerrainHeightmap(Terrain *t) {
    if (t != NULL)
        sendEdit(terrainMessage(TS::TSRE_Terrain_RawFile, t, &Terrain::saveRAWfileToStreamFloat));
}

void RouteEditorClient::updateTerrainTFile(Terrain *t) {
    if (t != NULL)
        sendEdit(terrainMessage(TS::TSRE_Terrain_tFile, t, &Terrain::saveTfileToStream));
}

void RouteEditorClient::updateTerrainFFile(Terrain *t) {
    if (t != NULL && t->hasFlags())
        sendEdit(terrainMessage(TS::TSRE_Terrain_FtFile, t, &Terrain::saveFfileToStream));
}

// Where this user's pointer is, for the others: only when it moved (B10).
void RouteEditorClient::updatePointerPosition(int X, int Z, float x, float y, float z){
    if (currentState != State::Connected || applying)
        return;
    if (pointerSent && X == lastPointerX && Z == lastPointerZ
            && x == lastPointer[0] && y == lastPointer[1] && z == lastPointer[2])
        return;
    pointerSent = true;
    lastPointerX = X;
    lastPointerZ = Z;
    lastPointer[0] = x;
    lastPointer[1] = y;
    lastPointer[2] = z;
    sendUtf16Message("update_pointer_position ( " + QString::number(X) + " " + QString::number(Z) + " "
                     + QString::number(x) + " " + QString::number(y) + " " + QString::number(z) + " )\n");
}
