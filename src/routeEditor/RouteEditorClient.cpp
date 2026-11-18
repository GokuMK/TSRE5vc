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
#include <QEventLoop>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <memory>

RouteEditorClient::RouteEditorClient() {
    QString secretError;
    const QString login = SettingsManager::instance().resolveSecretPlaceholders(
                Settings::string("core.network.clientLogin"), &secretError);
    if (!secretError.isEmpty()) {
        qWarning() << "Cannot resolve Route Editor client login:" << secretError;
        return;
    }
    if (!parseLogin(login, url, username, password)) {
        qDebug() << "Wrong server address";
        return;
    }
    username = NetworkMessage::cleanUsername(username);
    init();
}

bool RouteEditorClient::parseLogin(const QString &login, QString &url, QString &username, QString &password) {
    // The last @: a password may hold one, an address does not.
    const int at = login.lastIndexOf('@');
    if (at < 0)
        return false;
    const QString address = login.mid(at + 1).trimmed();
    if (address.isEmpty())
        return false;
    url = address.startsWith("ws://") || address.startsWith("wss://") ? address : "ws://" + address;
    const QString account = login.left(at);
    const int colon = account.indexOf(':');
    username = colon < 0 ? account : account.left(colon);
    password = colon < 0 ? QString() : account.mid(colon + 1);
    return true;
}

RouteEditorClient::RouteEditorClient(const QString &url, const QString &username, const QString &password) {
    this->url = url;
    this->username = NetworkMessage::cleanUsername(username);
    this->password = password;
    init();
}

void RouteEditorClient::init() {
    m_webSocket = new QWebSocket();
    m_webSocket->setMaxAllowedIncomingMessageSize(quint64(NetworkMessage::ServerLimit + NetworkMessage::HeaderSize));
    m_webSocket->setMaxAllowedIncomingFrameSize(quint64(NetworkMessage::ServerLimit + NetworkMessage::HeaderSize));
    qDebug() << "WebSocket server:" << url;
#if QT_CONFIG(ssl)
    if (url.startsWith("wss://")) {
        // TSRE_SERVER_TLS_CA: a certificate trusted besides the system's (a
        // test's, a private proxy's).
        const QString authority = qEnvironmentVariable("TSRE_SERVER_TLS_CA");
        if (!authority.isEmpty()) {
            QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
            if (!configuration.addCaCertificates(authority))
                qWarning() << "Route Editor client: no certificate in" << authority;
            m_webSocket->setSslConfiguration(configuration);
        }
        // The connection fails on them; said why.
        connect(m_webSocket, &QWebSocket::sslErrors, this, [](const QList<QSslError> &errors) {
            for (const QSslError &error : errors)
                qWarning().noquote() << "Route Editor client: TLS:" << error.errorString();
        });
    }
#endif
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
    if (url.startsWith("wss://") && !QSslSocket::supportsSsl()) {
        currentState = State::Refused;
        emit loginFailed("The server " + url + " needs TLS (wss://), and this TSRE has none (Qt's TLS backend is missing).");
        return;
    }
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

// The hello; the server answers with a challenge (task server 02).
void RouteEditorClient::onConnected() {
    qDebug() << "WebSocket connected";
    currentState = State::Connected;
    editPermitted = false;
    welcomed = false;
    sendUtf16Message("hello ( protocol ( " + QString::number(NetworkMessage::Protocol) + " ) tsre ( \""
                     + NetworkMessage::quoted(Game::AppVersion) + "\" ) user ( \"" + username + "\" ) )\n");
}

quint32 RouteEditorClient::send(const QByteArray &payload, bool request) {
    if (m_webSocket == NULL)
        return 0;
    const quint32 number = request ? ++nextRequest : 0;
    m_webSocket->sendBinaryMessage(NetworkMessage::frame(payload, number));
    return number;
}

quint32 RouteEditorClient::sendUtf16Message(QString msg) {
    return send(NetworkMessage::utf16(msg));
}

// welcome ( user ( "<name>" ) session ( "<id>" ) route ( "<name>" ) tsre (
// "<version>" ) change ( <last> ) allowed ( view edit ) ): logged in.
void RouteEditorClient::readWelcome(FileBuffer *data) {
    QString route;
    QString sh;
    editPermitted = false;
    welcomed = true;
    while (!(sh = ParserX::NextTokenInside(data).toLower()).isEmpty()) {
        if (sh == "user") {
            username = ParserX::GetStringInside(data);
        } else if (sh == "session") {
            session = ParserX::GetStringInside(data);
        } else if (sh == "route") {
            route = ParserX::GetStringInside(data);
        } else if (sh == "change") {
            lastChange = quint32(ParserX::GetNumber(data));
        } else if (sh == "allowed") {
            QString action;
            while (!(action = ParserX::GetStringInside(data)).isEmpty())
                if (action == "edit")
                    editPermitted = true;
        }
        ParserX::SkipToken(data);
    }
    reconnectDelayMs = 2000;
    if (routeRequested) {
        // The server started again with another route: nothing of this one
        // is refreshed, the editor starts again for it.
        if (route.compare(Game::route, Qt::CaseInsensitive) != 0) {
            editPermitted = false;
            emit connectionChanged(true, "The server serves another route now (" + route + "); starting again for it.");
            emit restartNeeded(false);
            return;
        }
        refreshRoute();
        return;
    }
    Game::route = route;
    if (Game::checkRoot(Game::root) && Game::checkRemoteRoute(Game::route)) {
        routeRequested = true;
        emit loadRoute();
    } else {
        currentState = State::Refused;
        emit loginFailed("The route " + Game::route + " the server serves is not in this client's game root " + Game::root + ".");
    }
}

// The receive rule: what applying a received message changes is not sent
// back; only the user's own edits are. And nothing while disconnected (the
// editor refuses edits then).
bool RouteEditorClient::editAllowed() {
    if (applying) {
        suppressedEdits++;
        return false;
    }
    if (currentState != State::Connected || !welcomed || !editPermitted) {
        droppedEdits++;
        return false;
    }
    return true;
}

// id_block ( <kind> <first> <count> [<tile x> <tile z>] )
void RouteEditorClient::readIdBlock(FileBuffer *data) {
    NetworkMessage::IdKind kind;
    if (!NetworkMessage::idKindFromName(ParserX::GetStringInside(data), kind))
        return;
    const int first = int(ParserX::GetNumber(data));
    const int count = int(ParserX::GetNumber(data));
    int x = 0, z = 0;
    if (NetworkMessage::idKindPerTile(kind)) {
        x = int(ParserX::GetNumber(data));
        z = int(ParserX::GetNumber(data));
    }
    IdStock &stock = idStocks[NetworkMessage::idKey(kind, x, z)];
    for (int i = 0; i < count; i++)
        stock.ids.append(first + i);
    stock.blockSize = std::max(stock.blockSize, NetworkMessage::idBlockSize(kind));
    stock.asked = 0;
    emit idsArrived();
}

void RouteEditorClient::askIds(NetworkMessage::IdKind kind, int tileX, int tileZ) {
    IdStock &stock = idStocks[NetworkMessage::idKey(kind, tileX, tileZ)];
    if (stock.asked != 0 || currentState != State::Connected || !welcomed)
        return;
    QString record = "request_ids ( " + NetworkMessage::idKindName(kind);
    if (NetworkMessage::idKindPerTile(kind))
        record += " " + QString::number(tileX) + " " + QString::number(tileZ);
    stock.asked = sendUtf16Message(record + " )\n");
    idRequests.insert(stock.asked);
}

int RouteEditorClient::idsHeld(NetworkMessage::IdKind kind, int tileX, int tileZ) const {
    return idStocks.value(NetworkMessage::idKey(kind, tileX, tileZ)).ids.size();
}

int RouteEditorClient::takeId(NetworkMessage::IdKind kind, int tileX, int tileZ) {
    const QString key = NetworkMessage::idKey(kind, tileX, tileZ);
    if (idStocks[key].ids.isEmpty() && currentState == State::Connected && welcomed && editPermitted && !idWait) {
        // None left: the edit waits for the next block.
        askIds(kind, tileX, tileZ);
        idWait = true;
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(this, &RouteEditorClient::idsArrived, &loop, &QEventLoop::quit);
        connect(this, &RouteEditorClient::connectionChanged, &loop, &QEventLoop::quit);
        timer.start(IdWaitMs);
        while (idStocks[key].ids.isEmpty() && timer.isActive() && currentState == State::Connected)
            loop.exec(QEventLoop::ExcludeUserInputEvents);
        idWait = false;
        if (!heldMessages.isEmpty() && !flushScheduled) {
            // After the edit that waited: it returns to the event loop first.
            flushScheduled = true;
            QTimer::singleShot(0, this, [this]() {
                flushScheduled = false;
                flushHeld();
            });
        }
        if (idStocks[key].ids.isEmpty())
            notice("The server gave no new IDs in " + QString::number(IdWaitMs / 1000)
                   + " s; this edit will be refused and put right.");
    }
    IdStock &stock = idStocks[key];
    if (stock.ids.isEmpty())
        return -1;
    const int id = stock.ids.takeFirst();
    if (stock.ids.size() < stock.blockSize / 2)
        askIds(kind, tileX, tileZ);
    return id;
}

// Back after a disconnection: the route kept, what is loaded asked for
// again from the server's current state.
void RouteEditorClient::refreshRoute() {
    RouteClient *route = dynamic_cast<RouteClient*>(Game::currentRoute);
    if (route == NULL || !route->loaded) {
        emit connectionChanged(true, "Connected to the server again; the route had not loaded: starting again.");
        emit restartNeeded(true);
        return;
    }
    emit connectionChanged(true, "Connected to the server again; refreshing the route from it.");
    emit routeReplacing();
    // The last connection's blocks went back to the server.
    idStocks.clear();
    idRequests.clear();
    refreshLast = route->refreshFromServer();
    refreshing = true;
    emit reconnected();
}

void RouteEditorClient::sendEdit(const QByteArray &payload) {
    if (!editAllowed())
        return;
    sentEdits++;
    send(payload);
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
    // The server takes this connection's unused IDs back.
    idStocks.clear();
    idRequests.clear();
    welcomed = false;
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
    // While an edit waits for IDs, only their block is applied; the rest
    // after the edit, in order.
    if (idWait || !heldMessages.isEmpty()) {
        NetworkMessage::Frame peek;
        if (idWait && NetworkMessage::readFrame(message, NetworkMessage::ServerLimit, peek) == NetworkMessage::Read::Ok
                && idRequests.contains(peek.request)) {
            processMessage(message);
            return;
        }
        heldMessages.append(message);
        if (!idWait)
            flushHeld();
        return;
    }
    processMessage(message);
}

void RouteEditorClient::flushHeld() {
    while (!heldMessages.isEmpty() && !idWait)
        processMessage(heldMessages.takeFirst());
}

void RouteEditorClient::processMessage(const QByteArray &message) {
    QWebSocket *pClient = m_webSocket;
    NetworkMessage::Frame frame;
    QString why;
    const NetworkMessage::Read read = NetworkMessage::readFrame(message, NetworkMessage::ServerLimit, frame, &why);
    if (read != NetworkMessage::Read::Ok) {
        if (read == NetworkMessage::Read::NotFrame || read == NetworkMessage::Read::OtherProtocol) {
            // Another TSRE: nothing it sends can be read.
            const QString text = "The server " + url + " speaks another protocol (" + why + "); update TSRE on both.";
            if (!routeRequested) {
                currentState = State::Refused;
                emit loginFailed(text);
            } else {
                notice(text);
            }
            disconnectNow();
            return;
        }
        qWarning().noquote() << "Route Editor client: a message from the server not read:" << why;
        return;
    }
    if (frame.change != 0)
        lastChange = frame.change;
    // A copy the parser owns, freed after the message (B8).
    std::unique_ptr<FileBuffer> data(NetworkMessage::buffer(frame.payload));
    // The receive rule: applying it sends nothing back.
    const bool wasApplying = applying;
    applying = true;
    try {
        if (frame.kind == NetworkMessage::Text)
            readUtf16Message(pClient, data.get());
        else
            readBinaryMessage(pClient, data.get());
    } catch (const std::exception &e) {
        qWarning() << "Route Editor client: a malformed message from the server:" << e.what();
    }
    applying = wasApplying;
    if (refreshing && frame.request == refreshLast) {
        refreshing = false;
        if (Game::currentRoute != NULL)
            Game::currentRoute->refreshingFromServer = false;
        emit connectionChanged(true, "Connected to the server; the route refreshed from it.");
        emit routeRefreshed();
    }
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
        // The tile's files, as it loads; a loaded tile's again (a refresh)
        // are applied as an edit.
        case TS::TSRE_Requested_Terrain_tFile:
            if (!t->loaded) {
                t->loadTFile(data);
                return;
            }
            applied = t->Terrain::loadTFile(data);
            break;
        case TS::TSRE_Requested_Terrain_RawFile:
            if (!t->loaded) {
                t->loadRAWFile(data);
                return;
            }
            applied = t->Terrain::loadRAWFile(data);
            break;
        case TS::TSRE_Requested_Terrain_FtFile:
            if (!t->loaded) {
                t->loadFFile(data);
                return;
            }
            // The server's tile has no flags.
            if (data->off == data->length)
                return;
            applied = t->Terrain::loadFFile(data);
            break;
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
        if (sh == ("challenge")) {
            QByteArray nonce;
            QString field;
            while (!(field = ParserX::NextTokenInside(data).toLower()).isEmpty()) {
                if (field == "nonce")
                    nonce = QByteArray::fromHex(ParserX::GetStringInside(data).toLatin1());
                ParserX::SkipToken(data);
            }
            // The password stays here: only the answer is sent.
            sendUtf16Message("login ( answer ( \"" + NetworkMessage::loginAnswer(password, nonce, username) + "\" ) )\n");
        } else if (sh == ("welcome")) {
            readWelcome(data);
        } else if (sh == ("refused")) {
            // No retry: the server would refuse again.
            const QString text = "The server " + url + " refused this client: " + ParserX::GetStringInside(data) + ".";
            currentState = State::Refused;
            if (!routeRequested) {
                emit loginFailed(text);
            } else {
                notice(text);
                emit connectionChanged(false, text);
            }
        } else if (sh == ("id_block")) {
            readIdBlock(data);
        } else if (sh == ("done")) {
            ParserX::GetNumber(data);
            const quint32 last = quint32(ParserX::GetNumber(data));
            if (last > lastChange)
                lastChange = last;
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
        } else if (sh == ("error")) {
            const QString text = ParserX::GetStringInside(data);
            refusals++;
            notice("The server refused: " + text);
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
        } else if (sh == "update_worldobj") {
            // Not applied to a tile still loading: its data comes from the
            // server with the edit in it.
            Game::currentRoute->updateWorldObjData(data);
        } else if (sh == "update_tritem_tdb" || sh == "update_tritem_rdb") {
            QString refusal;
            // The server's: a new ID (another user's block) taken in.
            if ((sh == "update_tritem_rdb" ? Game::roadDB : Game::trackDB)->updateTrItemData(data, &refusal, [](int) { return QString(); }) < 0)
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
            if (db->updateTrNodeData(data, &refusal, [](int) { return QString(); }) < 0)
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
    if (currentState != State::Connected || !welcomed || applying)
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
    send(NetworkMessage::utf16("update_pointer_position ( " + QString::number(X) + " " + QString::number(Z) + " "
                     + QString::number(x) + " " + QString::number(y) + " " + QString::number(z) + " )\n"), false);
}
