/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Task server 01: the server and client mode, as it is used. A tiny route
// made for the test (RouteCreator's, a passing loop in its TDB, a signal
// linked to a junction, platform items), a server process started as a user
// starts one (--server), scripted clients that send today's messages, and a
// real client (RouteEditorClient and RouteClient) in this process.

#include <tsre/tests/ServerTestSuite.h>

#include <routeEditor/RouteEditorClient.h>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/NetworkMessage.h>
#include <tsre/fileFunctions/NetworkToken.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/TS.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/trains/EngLib.h>
#include <tsre/world/RouteClient.h>
#include <tsre/world/RouteCreator.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/world/Tile.h>
#include <tsre/world/Trk.h>
#include <tsre/world/objects/WorldObj.h>

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QtEndian>
#include <QtWebSockets/QWebSocket>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>

namespace {

const QString RouteName = QStringLiteral("SERVERTEST");
// The route template's start tile, as the route keys its tiles (x, -file z).
const int StartX = -5000;
const int StartZ = -15000;

bool waitFor(const std::function<bool()> &done, int ms) {
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

// Runs the event loop for a while: what was to come has come.
void settle(int ms = 400) {
    waitFor([] { return false; }, ms);
}

int freePort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    const int port = probe.serverPort();
    probe.close();
    return port;
}

bool writeUtf16(const QString &path, const QString &text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << text;
    out.flush();
    return file.error() == QFile::NoError;
}

QString readUtf16(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf16);
    return in.readAll();
}

bool copyTree(const QString &from, const QString &to) {
    QDir source(from);
    if (!QDir().mkpath(to))
        return false;
    for (const QFileInfo &entry : source.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString target = to + "/" + entry.fileName();
        if (entry.isDir()) {
            if (!copyTree(entry.absoluteFilePath(), target))
                return false;
        } else if (!QFile::copy(entry.absoluteFilePath(), target)) {
            return false;
        }
    }
    return true;
}

// Every file of a folder with its size and time.
QStringList snapshot(const QString &dir) {
    QStringList files;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        files << QDir(dir).relativeFilePath(info.absoluteFilePath()) + " " + QString::number(info.size()) + " "
                         + QString::number(info.lastModified().toMSecsSinceEpoch());
    }
    files.sort();
    return files;
}

// The node numbers of a TrackDB text, in their order.
QList<int> nodeIds(const QString &text) {
    QList<int> ids;
    QRegularExpression node(QStringLiteral("TrackNode \\( (\\d+)"));
    auto it = node.globalMatch(text);
    while (it.hasNext())
        ids << it.next().captured(1).toInt();
    return ids;
}

int nodeCount(const QString &text) {
    const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("TrackNodes \\( (\\d+)")).match(text);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

// A binary record's token, and its payload after the 18 byte envelope.
quint32 binaryToken(const QByteArray &message) {
    if (message.size() < 5)
        return 0;
    quint32 token = 0;
    std::memcpy(&token, message.constData() + 1, 4);
    return qFromLittleEndian(token);
}

QByteArray terrainEdit(TS::TokenId token, int x, int z, const QByteArray &payload) {
    QByteArray message;
    QDataStream out(&message, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    NetworkToken::write(out, token);
    out << (qint32) 0 << (qint8) 0 << (qint32) x << (qint32) z;
    out.writeRawData(payload.constData(), payload.size());
    return message;
}

// The server as a user starts it: this program with --server.
struct ServerProcess {
    QProcess process;
    QString output;
    int port = 0;

    ServerProcess() {
        QObject::connect(&process, &QProcess::readyReadStandardOutput, [this] {
            output += QString::fromLocal8Bit(process.readAllStandardOutput());
        });
    }
    ~ServerProcess() {
        stop();
    }
    bool start(const QString &root, int listenPort, const QStringList &extra, bool closeInput) {
        port = listenPort;
        output.clear();
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "offscreen");
        env.insert("TSRE_SOUND_DEVICE", "none");
        env.insert("TSRE_SERVER_LOGIN_TIMEOUT_MS", "1500");
        process.setProcessEnvironment(env);
        process.setStandardErrorFile(QProcess::nullDevice());
        QStringList args = {"--server", "--game-root", root, "--route", RouteName,
                            "--port", QString::number(port),
                            "--set", "core.route.saving.enabled=true",
                            "--set", "core.route.saving.trackDatabase=true"};
        args << extra;
        process.start(QCoreApplication::applicationFilePath(), args);
        if (!process.waitForStarted(10000))
            return false;
        if (closeInput)
            process.closeWriteChannel();
        waitFor([this] { return output.contains("listening on") || process.state() == QProcess::NotRunning; }, 90000);
        return output.contains("listening on");
    }
    void command(const QString &line) {
        process.write((line + "\n").toUtf8());
    }
    bool waitOutput(const QString &text, int from, int ms = 10000) {
        return waitFor([&] { return output.indexOf(text, from) >= 0; }, ms);
    }
    void stop() {
        if (process.state() == QProcess::NotRunning)
            return;
        process.kill();
        process.waitForFinished(5000);
    }
    // Its processor time so far, in clock ticks (Linux).
    long long cpuTicks() const {
        QFile stat(QString("/proc/%1/stat").arg(process.processId()));
        if (!stat.open(QIODevice::ReadOnly))
            return -1;
        const QString text = QString::fromLatin1(stat.readAll());
        const QStringList fields = text.mid(text.lastIndexOf(')') + 2).split(' ');
        if (fields.size() < 13)
            return -1;
        return fields[11].toLongLong() + fields[12].toLongLong();
    }
};

// A client that sends today's messages and keeps what comes back.
struct ScriptedClient {
    QWebSocket socket;
    QStringList texts;
    QList<QByteArray> binaries;
    bool closed = false;

    ScriptedClient() {
        QObject::connect(&socket, &QWebSocket::binaryMessageReceived, [this](const QByteArray &m) {
            if (m.size() >= 2 && uchar(m[0]) == 0xFF && uchar(m[1]) == 0xFE)
                texts << QString::fromUtf16(reinterpret_cast<const char16_t *>(m.constData() + 2), (m.size() - 2) / 2);
            else
                binaries << m;
        });
        QObject::connect(&socket, &QWebSocket::disconnected, [this] { closed = true; });
    }
    bool open(int port) {
        socket.open(QUrl(QString("ws://127.0.0.1:%1").arg(port)));
        return waitFor([this] { return socket.state() == QAbstractSocket::ConnectedState; }, 5000);
    }
    void send(const QString &text) {
        socket.sendBinaryMessage(NetworkMessage::utf16(text));
    }
    void sendBinary(const QByteArray &message) {
        socket.sendBinaryMessage(message);
    }
    int count(const QString &record) const {
        int n = 0;
        for (const QString &text : texts)
            n += text.count(QRegularExpression("(^|\\s)" + QRegularExpression::escape(record) + "\\s*\\("));
        return n;
    }
    bool waitText(const QString &record, int atLeast = 1, int ms = 5000) {
        return waitFor([&] { return count(record) >= atLeast; }, ms);
    }
    QString last(const QString &record) const {
        for (int i = texts.size() - 1; i >= 0; --i)
            if (texts[i].contains(QRegularExpression("(^|\\s)" + QRegularExpression::escape(record) + "\\s*\\(")))
                return texts[i];
        return QString();
    }
    QByteArray lastBinary(TS::TokenId token) const {
        for (int i = binaries.size() - 1; i >= 0; --i)
            if (binaryToken(binaries[i]) == quint32(token))
                return binaries[i];
        return QByteArray();
    }
    bool login(int port, const QString &user, const QString &password = QString()) {
        if (!open(port))
            return false;
        send("login ( " + user + " " + password + " )\n");
        return waitText("load_route");
    }
    void clear() {
        texts.clear();
        binaries.clear();
    }
};

QString objectRecord(int x, int z, int uid, float px) {
    return QString("update_worldobj (\nx ( %1 )\nz ( %2 )\nStatic (\n UiD ( %3 )\n FileName ( ServerTest.s )\n"
                   " StaticFlags ( 00000000 )\n Position ( %4 1 20 )\n QDirection ( 0 0 0 1 )\n VDbId ( 0 )\n)\n)\n")
            .arg(x).arg(z).arg(uid).arg(px);
}

// The fixture's nodes: a passing loop and an isolated track.
struct FixtureNode {
    int id;
    int type;
    int pins[3];
};

const FixtureNode Loop[] = {
    {1, 0, {2, 0, 0}},
    {2, 1, {1, 11, 0}},
    {11, 2, {2, 3, 4}},
    {3, 1, {11, 12, 0}},
    {4, 1, {12, 11, 0}},
    {12, 2, {5, 3, 4}},
    {5, 1, {12, 6, 0}},
    {6, 0, {5, 0, 0}},
};
const FixtureNode Isolated[] = {
    {20, 0, {21, 0, 0}},
    {21, 1, {20, 22, 0}},
    {22, 0, {21, 0, 0}},
};
// What the save makes of them once the isolated track is removed:
// deleteNulls() moves the highest node into the lowest hole.
int compacted(int id) {
    return id == 12 ? 7 : (id == 11 ? 8 : id);
}

bool writeFixtureTdb(const QString &path) {
    TSectionDAT sections(false, false);
    TDB db(&sections, false);
    db.loaded = true;
    float nextX = 0;
    auto add = [&](const FixtureNode &n) {
        TRnode *node = new TRnode();
        node->typ = n.type;
        node->inputPinCount = 1;
        node->outputPinCount = n.type == 2 ? 2 : (n.type == 1 ? 1 : 0);
        for (int i = 0; i < 3; i++)
            node->pins[i].link = n.pins[i];
        if (n.type == 2)
            node->junction.shapeIndex = 2;
        if (n.type == 1) {
            node->iTrv = 2;
            node->trVectorSection = new TrackVectorSection[2];
            for (int i = 0; i < 2; i++) {
                node->trVectorSection[i].sectionIndex = 1;
                node->trVectorSection[i].shapeIndex = 1;
                node->trVectorSection[i].worldTileX = StartX;
                node->trVectorSection[i].worldTileZ = -StartZ;
                node->trVectorSection[i].tileX = StartX;
                node->trVectorSection[i].tileZ = -StartZ;
                node->trVectorSection[i].x = nextX;
                nextX += 10;
            }
        }
        db.trackNodes[n.id] = node;
        db.iTRnodes = std::max(db.iTRnodes, n.id);
    };
    for (const FixtureNode &n : Loop)
        add(n);
    for (const FixtureNode &n : Isolated)
        add(n);
    // Platform items 0 and 1 on track 3, a signal (2) on track 5 linked to
    // junction 12's first output.
    auto item = [&](int track, TRitem *it) {
        TRnode *node = db.trackNodes[track];
        int *refs = new int[node->iTri + 1];
        for (int i = 0; i < node->iTri; i++)
            refs[i] = node->trItemRef[i];
        refs[node->iTri] = int(it->trItemId);
        delete[] node->trItemRef;
        node->trItemRef = refs;
        node->iTri++;
        db.trackItems[int(it->trItemId)] = it;
        db.iTRitems = std::max(db.iTRitems, int(it->trItemId) + 1);
    };
    for (int k = 0; k < 2; k++) {
        TRitem *platform = TRitem::newPlatformItem(k, k == 0 ? 2.0f : 8.0f);
        platform->platformTrItemData[1] = 1 - k;
        platform->platformName = "Test";
        platform->stationName = "Test";
        item(3, platform);
    }
    TRitem *signal = TRitem::newSignalItem(2, 5.0f, 0, 1, "TestSignal");
    signal->trSignalDirs = 1;
    signal->trSignalDir = new int[4]{12, 1, 0, 0};
    item(5, signal);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);
    out.setRealNumberPrecision(6);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << "SIMISA@@@@@@@@@@JINX0T0t______\n\n";
    db.saveToStream(out);
    out.flush();
    return file.error() == QFile::NoError;
}

const char *GlobalTSection =
        "SIMISA@@@@@@@@@@JINX0F0t______\n\n"
        "TrackSections ( 2\n"
        "\tTrackSection ( 1\n\t\tSectionSize ( 1.5 10 )\n\t)\n"
        ")\n"
        "TrackShapes ( 3\n"
        "\tTrackShape ( 1\n\t\tFileName ( ServerTestStraight.s )\n\t\tNumPaths ( 1 )\n"
        "\t\tSectionIdx ( 1 0 0 0 0 1 )\n\t)\n"
        "\tTrackShape ( 2\n\t\tFileName ( ServerTestJunction.s )\n\t\tNumPaths ( 2 )\n"
        "\t\tSectionIdx ( 1 0 0 0 0 1 )\n\t\tSectionIdx ( 1 0 0 0 0 1 )\n\t\tMainRoute ( 0 )\n\t)\n"
        ")\n";

// A TrackDB text (a request_tdb answer) read into a database.
bool readTdb(const QString &text, TDB &db) {
    std::unique_ptr<FileBuffer> data(NetworkMessage::buffer(NetworkMessage::utf16(text)));
    data->skipBOM();
    QString sh;
    bool read = false;
    while (!((sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty())) {
        if (sh == "requested_tdb") {
            while (!((sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty())) {
                if (sh == "trackdb") {
                    db.loadUtf16Data(data.get());
                    read = true;
                }
                ParserX::SkipToken(data.get());
            }
        }
        ParserX::SkipToken(data.get());
    }
    return read;
}

}

int TsreTests::runServerSuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) {
            ++passed;
            if (verbose) qInfo().noquote() << "[tests:server] PASS" << name;
        } else {
            ++failed;
            qWarning().noquote() << "[tests:server] FAIL" << name;
        }
    };
    const QString rootWas = Game::root;
    const QString routeWas = Game::route;
    const bool writeWas = Game::writeEnabled;

    QTemporaryDir workspace;
    // TSRE_SERVER_SUITE_KEEP: the route and the servers' output stay, for a
    // look at a failure.
    const bool keep = qEnvironmentVariableIsSet("TSRE_SERVER_SUITE_KEEP");
    workspace.setAutoRemove(!keep);
    if (keep)
        qInfo().noquote() << "[tests:server] workspace kept:" << workspace.path();
    const QString serverRoot = workspace.filePath("server");
    const QString clientRoot = workspace.filePath("client");
    const QString routeDir = serverRoot + "/ROUTES/" + RouteName;

    // The route: RouteCreator's (a TRK, empty databases, a terrain tile, a
    // world tile), a small global tsection.dat, a TDB with holes in its node
    // numbers (7 to 10, 13 to 19), items.
    bool made = workspace.isValid() && RouteCreator::templateResourcesAvailable();
    if (made) {
        QDir().mkpath(serverRoot + "/ROUTES");
        QDir().mkpath(serverRoot + "/GLOBAL");
        made = writeUtf16(serverRoot + "/GLOBAL/tsection.dat", QString::fromLatin1(GlobalTSection));
        Game::root = serverRoot;
        Game::route = RouteName;
        Game::writeEnabled = true;
        QString error;
        made = made && RouteCreator::create(RouteName, Trk::createNewRouteTemplate(RouteName), &error);
        if (!error.isEmpty())
            qWarning() << "[tests:server] route:" << error;
        made = made && writeFixtureTdb(routeDir + "/" + RouteName + ".tdb");
        made = made && writeUtf16(serverRoot + "/users-test.txt", "# the suite's users\nalice = secret\nbob=pw2\n");
        // The client's game root: a copy before any edit.
        made = made && copyTree(serverRoot, clientRoot);
    }
    check(made, "the route made for the test (needs assets/templateRoute_0.6)");
    if (!made) {
        Game::root = rootWas;
        Game::route = routeWas;
        Game::writeEnabled = writeWas;
        qInfo() << "[tests:server] cases=" << passed + failed << "passed=" << passed << "failed=" << failed;
        return 1;
    }

    const int port = freePort();
    const QString url = QString("ws://127.0.0.1:%1").arg(port);
    ServerProcess server;
    check(server.start(serverRoot, port, {}, false), "the server starts and listens");

    // Logging in.
    ScriptedClient alice, alice2, stranger;
    check(alice.login(port, "alice"), "a login: the route to load");
    check(alice.waitText("users_info") && alice.last("users_info").contains("user ( alice "),
          "the logged-in user gets the users");
    // al.ice is cleaned to alice, taken: alice2.
    check(alice2.login(port, "al.ice") && alice2.last("set_username").contains("alice2"),
          "a name cleaned before the duplicate check, and a duplicate gets a number, not a random name");
    check(stranger.open(port), "a socket that does not log in connects");
    stranger.send("request_tdb ( )\n");
    check(stranger.waitText("edit_refused") && stranger.count("requested_tdb") == 0,
          "nothing before a login (refused)");

    // Edits: forwarded once, to the logged-in others only.
    alice.clear();
    alice2.clear();
    alice.send(objectRecord(StartX, StartZ, 7, 10));
    check(alice2.waitText("update_worldobj") && alice2.last("update_worldobj").contains("UiD ( 7 )"),
          "an object forwarded to another user");
    settle();
    check(alice2.count("update_worldobj") == 1 && alice.count("update_worldobj") == 0
                  && stranger.count("update_worldobj") == 0 && stranger.count("users_info") == 0,
          "once, not back to its sender, not to a socket not logged in (no users either)");
    check(waitFor([&] { return stranger.closed; }, 5000),
          "a socket that does not log in is closed (TSRE_SERVER_LOGIN_TIMEOUT_MS 1.5 s here, 10 s by default)");

    alice2.clear();
    alice.send(objectRecord(StartX, StartZ, 8, 12) + objectRecord(StartX, StartZ, 9, 14));
    check(alice2.waitText("update_worldobj", 2), "two objects in one message both forwarded");
    settle();
    int objectMessages = 0;
    bool eachAlone = true;
    for (const QString &text : alice2.texts)
        if (text.contains("update_worldobj")) {
            objectMessages++;
            eachAlone = eachAlone && text.count("update_worldobj") == 1;
        }
    eachAlone = eachAlone && objectMessages == 2;
    check(eachAlone, "each edit forwarded on its own (not the whole message twice)");

    alice.clear();
    alice2.clear();
    alice.send(objectRecord(StartX + 7, StartZ, 10, 5));
    check(alice.waitText("edit_refused") && alice.last("edit_refused").contains("not here"),
          "an object on a tile the server has not got: refused");
    settle();
    check(alice2.count("update_worldobj") == 0, "and not forwarded");
    alice.send(QString("request_tile ( %1 %2 )\n").arg(StartX + 7).arg(StartZ));
    check(alice.waitText("no_tile") && alice.last("no_tile").contains(QString::number(StartX + 7)),
          "a request for a tile the server has not got: answered no_tile");
    alice.send(QString("request_tile ( %1 %2 )\n").arg(StartX).arg(StartZ));
    check(alice.waitText("requested_tile") && alice.last("requested_tile").contains("UiD ( 9 )"),
          "a tile with the edits in it");

    // Malformed and out of range records: refused, nothing changed or sent.
    alice.clear();
    alice2.clear();
    alice.send("update_trnode_tdb ( id ( 999 ) TrackNode ( 999 TrEndNode ( 0 ) UiD ( 0 0 0 0 0 0 0 0 0 ) TrPins ( 1 0 TrPin ( 1 1 ) ) ) )\n");
    alice.send("update_tritem_tdb ( id ( 50 ) remove ( ) )\n");
    alice.send("update_trnode_tdb ( id ( 3 ) )\n");
    alice.send("update_tracksection ( TrackSection ( SectionCurve ( 0 ) 1 20 1.5 ) )\n");
    check(alice.waitText("edit_refused", 4), "a node out of range, an item out of range, a record of nothing, "
                                             "a global track section: refused");
    if (alice.count("edit_refused") != 4)
        qWarning().noquote() << "[tests:server] refusals:" << alice.texts;
    settle();
    check(alice2.count("update_trnode_tdb") == 0 && alice2.count("update_tritem_tdb") == 0
                  && alice2.count("update_tracksection") == 0,
          "none of them forwarded");

    // Removing the isolated track and a platform item.
    for (const FixtureNode &n : Isolated)
        alice.send(QString("update_trnode_tdb ( id ( %1 ) remove ( ) )\n").arg(n.id));
    alice.send("update_tritem_tdb ( id ( 1 ) remove ( ) )\n");
    check(alice2.waitText("update_trnode_tdb", 3) && alice2.waitText("update_tritem_tdb"),
          "node and item removals forwarded");

    // Terrain: the files, an edit of each, a malformed one.
    alice.clear();
    alice2.clear();
    alice.send(QString("request_terrain_tfile( %1 %2 )\nrequest_terrain_yfile( %1 %2 )\nrequest_terrain_ffile( %1 %2 )\n")
                       .arg(StartX).arg(StartZ));
    check(waitFor([&] { return alice.binaries.size() >= 3; }, 5000), "the terrain's T file, heights and flags");
    const QByteArray tFile = alice.lastBinary(TS::TSRE_Requested_Terrain_tFile).mid(18);
    QByteArray heights = alice.lastBinary(TS::TSRE_Requested_Terrain_RawFile).mid(18);
    const QByteArray flagsBefore = alice.lastBinary(TS::TSRE_Requested_Terrain_FtFile).mid(18);
    check(!tFile.isEmpty() && heights.size() > 4 && heights.size() % 4 == 0 && flagsBefore.isEmpty(),
          "a T file, heights, and no flags for a new tile");
    if (heights.size() > 4) {
        float first;
        std::memcpy(&first, heights.constData(), 4);
        first += 5.0f;
        std::memcpy(heights.data(), &first, 4);
    }
    const QByteArray heightEdit = terrainEdit(TS::TSRE_Terrain_RawFile, StartX, StartZ, heights);
    alice.sendBinary(heightEdit);
    check(waitFor([&] { return !alice2.binaries.isEmpty(); }, 5000) && alice2.binaries.value(0) == heightEdit,
          "a height edit forwarded as it was sent");
    alice.sendBinary(terrainEdit(TS::TSRE_Terrain_RawFile, StartX, StartZ, heights.left(heights.size() - 4)));
    check(alice.waitText("edit_refused"), "heights of another size: refused");
    alice.sendBinary(terrainEdit(TS::TSRE_Terrain_tFile, StartX, StartZ, tFile.left(tFile.size() / 2)));
    check(alice.waitText("edit_refused", 2), "half a T file: refused");
    QByteArray flags(heights.size() / 4, char(0));
    flags[0] = 0x04;
    alice.sendBinary(terrainEdit(TS::TSRE_Terrain_FtFile, StartX, StartZ, flags));
    alice.sendBinary(terrainEdit(TS::TSRE_Terrain_tFile, StartX, StartZ, tFile));
    check(waitFor([&] { return alice2.binaries.size() >= 3; }, 5000), "flags and a whole T file forwarded");
    alice.binaries.clear();
    alice.send(QString("request_terrain_yfile( %1 %2 )\nrequest_terrain_ffile( %1 %2 )\n").arg(StartX).arg(StartZ));
    const bool bothBack = waitFor([&] { return alice.binaries.size() >= 2; }, 5000);
    const QByteArray heightsBack = alice.lastBinary(TS::TSRE_Requested_Terrain_RawFile).mid(18);
    const QByteArray flagsBack = alice.lastBinary(TS::TSRE_Requested_Terrain_FtFile).mid(18);
    check(bothBack && heightsBack == heights && flagsBack == flags,
          "the server has them: its heights and flags are the edited ones (flags are sent now)");
    if (heightsBack != heights || flagsBack != flags)
        qWarning() << "[tests:server] heights back" << heightsBack.size() << (heightsBack == heights)
                   << "flags back" << flagsBack.size() << "of" << flags.size() << (flagsBack == flags);

    // The console.
    int from = server.output.size();
    server.command("help");
    check(server.waitOutput("kick <user>", from), "console: help");
    from = server.output.size();
    server.command("status");
    check(server.waitOutput("Unsaved:", from) && server.output.indexOf("Route: " + RouteName, from) >= 0,
          "console: status");
    from = server.output.size();
    server.command("kick alice2");
    check(alice2.waitText("kicked") && waitFor([&] { return alice2.closed; }, 5000)
                  && server.waitOutput("Kicked alice2", from),
          "console: kick (the user is told)");

    // The save: holes closed in the file only.
    from = server.output.size();
    server.command("save");
    check(server.waitOutput("Track database: written", from), "console: save says what it wrote");
    const QString saved = readUtf16(routeDir + "/" + RouteName + ".tdb");
    QList<int> savedIds = nodeIds(saved);
    bool noHoles = nodeCount(saved) == 8 && savedIds.size() == 8;
    std::sort(savedIds.begin(), savedIds.end());
    for (int i = 0; i < savedIds.size(); i++)
        noHoles = noHoles && savedIds[i] == i + 1;
    check(noHoles, "the saved TDB's nodes are numbered 1 to 8, without holes");
    check(saved.contains(QRegularExpression("TrSignalDir \\( 7 1 0 0 \\)")),
          "the signal's junction renumbered with it (12 to 7)");
    check(saved.contains(QRegularExpression("EmptyItem \\(\\s*TrItemId \\( 1 \\)")) && saved.contains("TrItemTable ( 3"),
          "a removed item saved as an empty item, every slot written");
    alice.clear();
    alice.send("request_tdb ( )\n");
    check(alice.waitText("requested_tdb") && nodeCount(alice.last("requested_tdb")) == 22
                  && nodeIds(alice.last("requested_tdb")).contains(12),
          "the server's numbers in memory are unchanged (22 nodes, junction 12): nothing to resend");
    from = server.output.size();
    server.command("save");
    check(server.waitOutput("Track database: written", from) && readUtf16(routeDir + "/" + RouteName + ".tdb") == saved,
          "saving twice writes the same file");

    // A real client: it loads, applies others' edits without sending them
    // back, sends its user's own, refuses tiles the server has not got, and
    // writes nothing into its route folder.
    const QStringList clientFilesBefore = snapshot(clientRoot + "/ROUTES/" + RouteName);
    Game::root = clientRoot;
    Game::route.clear();
    Game::currentShapeLib = new ShapeLib();
    Game::currentEngLib = new EngLib();
    Undo::UndoEnabled = false;
    RouteEditorClient *client = new RouteEditorClient(url, "real", "");
    Game::serverClient = client;
    RouteClient *route = NULL;
    bool initDone = false;
    int reconnects = 0;
    QStringList notices;
    QObject::connect(client, &RouteEditorClient::loadRoute, [&] {
        route = new RouteClient();
        QObject::connect(route, &RouteClient::initDone, [&] { initDone = true; });
        route->load();
    });
    QObject::connect(client, &RouteEditorClient::reconnected, [&] { reconnects++; });
    QObject::connect(client, &RouteEditorClient::noticeShown, [&](QString text) { notices << text; });
    client->connectNow();
    check(waitFor([&] { return initDone; }, 30000), "a client loads the route from the server");
    if (initDone) {
        const int key = StartX * 10000 + StartZ;
        route->requestTile(StartX, StartZ, false);
        Game::terrainLib->getTerrainByXY(StartX, StartZ, true);
        check(waitFor([&] {
                  Tile *t = route->tile.value(key, NULL);
                  return t != NULL && t->loaded == 1 && Game::terrainLib->isLoaded(StartX, StartZ);
              }, 15000),
              "its start tile and terrain arrive");
        check(Game::trackDB != NULL && Game::trackDB->iTRnodes == 22 && Game::trackDB->trackNodes[12] != NULL
                      && Game::trackDB->trackNodes[12]->typ == 2,
              "its TDB has the server's numbers");
        Tile *tile = route->tile.value(key, NULL);
        auto object = [&](unsigned int uid) -> WorldObj * {
            if (tile == NULL)
                return NULL;
            for (auto &entry : tile->obiekty)
                if (entry.second != NULL && entry.second->UiD == uid && entry.second->loaded)
                    return entry.second;
            return NULL;
        };
        check(object(7) != NULL && object(8) != NULL && object(9) != NULL, "with the objects edited before");
        const int sentBefore = client->sentEdits;
        alice.clear();
        alice.send(objectRecord(StartX, StartZ, 7, 33));
        alice.send("update_trnode_tdb ( id ( 2 ) remove ( ) )\n");
        check(waitFor([&] { WorldObj *o = object(7); return o != NULL && std::fabs(o->position[0] - 33) < 0.01f; }, 5000)
                      && waitFor([&] { return Game::trackDB->trackNodes[2] == NULL; }, 5000),
              "another user's object and node applied");
        settle();
        check(client->sentEdits == sentBefore && alice.count("update_worldobj") == 0 && alice.count("update_trnode_tdb") == 0,
              "the receive rule: applying them sends nothing back");
        WorldObj *own = object(8);
        if (own != NULL)
            own->setModified();
        check(alice.waitText("update_worldobj") && alice.last("update_worldobj").contains("UiD ( 8 )")
                      && client->sentEdits == sentBefore + 1,
              "the user's own edit is sent, and forwarded");
        route->requestTile(StartX + 3, StartZ, false);
        check(waitFor([&] { return route->tileMissing(StartX + 3, StartZ); }, 5000), "a tile the server has not got: marked");
        check(route->requestTile(StartX + 3, StartZ, true) == NULL && notices.size() == 1
                      && notices.value(0).contains("cannot make tiles"),
              "placing on it refused, and the user told");
        check(route->newTile(StartX + 4, StartZ) == 0 && notices.size() == 2, "a new tile refused on a client");
    }

    // The server goes; another starts (file authentication, no console).
    server.command("quit");
    check(server.process.waitForFinished(10000), "console: quit");
    if (server.process.state() != QProcess::NotRunning) {
        qWarning().noquote() << "[tests:server] the server's last output:" << server.output.right(600);
        server.stop();
    }
    check(waitFor([&] { return client->state() == RouteEditorClient::State::Disconnected; }, 5000),
          "the client shows the disconnection and does not quit");
    const int droppedBefore = client->droppedEdits;
    if (route != NULL && route->tile.value(StartX * 10000 + StartZ, NULL) != NULL)
        for (auto &entry : route->tile.value(StartX * 10000 + StartZ)->obiekty)
            if (entry.second != NULL && entry.second->UiD == 9) {
                entry.second->setModified();
                break;
            }
    check(client->droppedEdits == droppedBefore + 1 && client->sentEdits >= 0, "an edit while disconnected is not sent");

    ServerProcess second;
    check(second.start(serverRoot, port, {"--set", "core.network.serverAuthenticationMode=file",
                                         "--users", serverRoot + "/users-test.txt"}, true),
          "a server starts on the saved route");
    check(second.waitOutput("console input ended", 0, 5000), "with its input closed, it runs on");
    check(waitFor([&] { return reconnects > 0; }, 40000), "the client reconnects on its own and loads the route again");
    client->disconnectNow();
    settle(500);
    const long long ticksBefore = second.cpuTicks();
    settle(3000);
    const long long ticksAfter = second.cpuTicks();
    qInfo().noquote() << "[tests:server] idle server, input closed:" << (ticksAfter - ticksBefore) << "ticks of 300";
    check(ticksBefore >= 0 && ticksAfter - ticksBefore <= 3, "and idles under 1 % of a processor");

    ScriptedClient wrong, bob;
    check(wrong.open(port), "a user with a wrong password connects");
    wrong.send("login ( alice nothing )\n");
    check(wrong.waitText("auth_fail") && waitFor([&] { return wrong.closed; }, 5000) && wrong.count("load_route") == 0,
          "and is refused and closed (users file beside the route, or --users)");
    check(bob.login(second.port, "bob", "pw2"), "a user in the users file logs in");
    bob.send("request_tdb ( )\n");
    check(bob.waitText("requested_tdb"), "the saved TDB");
    const QString loadedTdb = bob.last("requested_tdb");
    QList<int> loadedIds = nodeIds(loadedTdb);
    std::sort(loadedIds.begin(), loadedIds.end());
    check(nodeCount(loadedTdb) == 8 && loadedIds == QList<int>({1, 2, 3, 4, 5, 6, 7, 8}),
          "clients connecting after a restart get the file's numbers, without holes");
    {
        TSectionDAT sections(false, false);
        TDB db(&sections, false);
        bool same = readTdb(loadedTdb, db);
        for (const FixtureNode &n : Loop) {
            auto found = db.trackNodes.find(compacted(n.id));
            const TRnode *node = found != db.trackNodes.end() ? found->second : NULL;
            same = same && node != NULL && node->typ == n.type;
            for (int i = 0; same && i < (n.type == 2 ? 3 : (n.type == 1 ? 2 : 1)); i++)
                same = node->pins[i].link == compacted(n.pins[i]);
        }
        check(same, "the same track: every node's type and links, renumbered");
    }
    second.stop();

    const QStringList clientFilesAfter = snapshot(clientRoot + "/ROUTES/" + RouteName);
    check(!clientFilesBefore.isEmpty() && clientFilesAfter == clientFilesBefore,
          "the client wrote nothing into its route folder (same files, sizes, times)");

    if (keep) {
        QFile first(workspace.filePath("server-1.txt"));
        if (first.open(QIODevice::WriteOnly))
            first.write(server.output.toUtf8());
        QFile next(workspace.filePath("server-2.txt"));
        if (next.open(QIODevice::WriteOnly))
            next.write(second.output.toUtf8());
    }
    Game::serverClient = NULL;
    Game::root = rootWas;
    Game::route = routeWas;
    Game::writeEnabled = writeWas;
    qInfo() << "[tests:server] cases=" << passed + failed << "passed=" << passed << "failed=" << failed;
    return failed ? 1 : 0;
}
