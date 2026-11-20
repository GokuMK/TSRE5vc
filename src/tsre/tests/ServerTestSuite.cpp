/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Tasks server 01 and 02: the server and client mode, as it is used. A tiny
// route made for the test (RouteCreator's, a passing loop in its TDB, a
// signal linked to a junction, platform items), a server process started as
// a user starts one (--server), scripted clients that send the protocol's
// frames, and a real client (RouteEditorClient and RouteClient) in this
// process.

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
#include <tsre/world/Terrain.h>
#include <tsre/world/Tile.h>
#include <tsre/world/Trk.h>
#include <tsre/ErrorMessage.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/fileFunctions/RouteFiles.h>
#include <tsre/geo/TerrainOverlays.h>
#include <tsre/tdb/SigCfg.h>
#include <tsre/tdb/SpeedPostDAT.h>
#include <tsre/world/Environment.h>
#include <tsre/world/RouteSwitch.h>
#include <tsre/world/SoundList.h>
#include <tsre/world/TelepoleData.h>
#include <tsre/world/TerrainClient.h>
#include <tsre/world/TerrainLibQt.h>
#include <tsre/world/objects/CarSpawnerObj.h>
#include <tsre/world/objects/ForestObj.h>
#include <tsre/world/objects/WorldObj.h>
#include <QPushButton>
#include <routeEditor/ActivityTools.h>
#include <tsre/environment/WorldClock.h>
#include <tsre/trains/ActivityNotifier.h>
#include <tsre/sim/RemoteSimulationLink.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/SimulationSession.h>
#include <tsre/sim/SimulationTexts.h>
#include <tsre/sim/SimulationWire.h>
#include <tsre/tests/SimulationTestSuite.h>
#include <tsre/trains/Activity.h>
#include <QTime>

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QtEndian>
#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketServer>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QStandardPaths>
#include <cmath>
#include <csignal>
#include <cstring>
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif
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

bool writeBytes(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// A client's game root as task server 03 has it: the route's heavy folders
// (SHAPES, TEXTURES, TERRTEX, SOUND, ENVFILES/TEXTURES) and GLOBAL's, the
// rest of the route and GLOBAL/tsection.dat gone.
bool keepHeavyOnly(const QString &root, const QString &route) {
    const QStringList heavy{"SHAPES", "TEXTURES", "TERRTEX", "SOUND"};
    const QDir folder(root + "/ROUTES/" + route);
    for (const QFileInfo &entry : folder.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.isFile()) {
            if (!QFile::remove(entry.absoluteFilePath()))
                return false;
        } else if (entry.fileName().compare("ENVFILES", Qt::CaseInsensitive) == 0) {
            for (const QFileInfo &env : QDir(entry.absoluteFilePath()).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
                if (env.fileName().compare("TEXTURES", Qt::CaseInsensitive) != 0
                        && !(env.isDir() ? QDir(env.absoluteFilePath()).removeRecursively() : QFile::remove(env.absoluteFilePath())))
                    return false;
        } else if (!heavy.contains(entry.fileName().toUpper())
                   && !QDir(entry.absoluteFilePath()).removeRecursively()) {
            return false;
        }
    }
    for (const QFileInfo &entry : QDir(root + "/GLOBAL").entryInfoList(QDir::Files))
        if (!QFile::remove(entry.absoluteFilePath()))
            return false;
    return true;
}

// A map overlay picture: one colour, size by size.
bool writeOverlay(const QString &path, int size, Qt::GlobalColor colour) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage picture(size, size, QImage::Format_RGB888);
    picture.fill(colour);
    return picture.save(path, "PNG");
}

QString overlayPath(const QString &routeDir, int x, int z) {
    return routeDir + "/TERRAIN_MAPS/" + QString::number(TerrainOverlays::key(x, z)) + ".png";
}

// What a local load of the route reads (task server 03: a client's load
// from its server's files equals it).
struct LocalLoad {
    int globalSections = -1, sections = -1, shapes = -1;
    QString speedPosts;
    int signalTypes = -1, signalShapes = -1;
    int soundSources = -1, soundRegions = -1, forests = -1, carSpawners = -1, telepoles = -1;
    bool environment = false;
    int water = -1;
    QStringList environmentFiles;
    QByteArray heights;
    bool flags = false;
    int samples = -1;
};

// Every file of a folder with its size and time.
QStringList snapshot(const QString &dir) {
    QStringList files;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        const QString relative = QDir(dir).relativeFilePath(info.absoluteFilePath());
        // A client's own map overlays (task server 03): it saves them.
        if (relative.startsWith("TERRAIN_MAPS/", Qt::CaseInsensitive))
            continue;
        files << relative + " " + QString::number(info.size()) + " "
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
    bool start(const QString &root, int listenPort, const QStringList &extra, bool closeInput,
               const QString &route = RouteName) {
        port = listenPort;
        output.clear();
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "offscreen");
        env.insert("TSRE_SOUND_DEVICE", "none");
        env.insert("TSRE_SERVER_LOGIN_TIMEOUT_MS", "1500");
        process.setProcessEnvironment(env);
        process.setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_LINUX
        // Ends with the suite, also when the suite is killed (a timeout).
        process.setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGKILL); });
#endif
        QStringList args = {"--server", "--game-root", root, "--route", route,
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
    // Its memory's peak so far, in KB (Linux).
    long long peakKb() const {
        QFile status(QString("/proc/%1/status").arg(process.processId()));
        if (!status.open(QIODevice::ReadOnly))
            return -1;
        const QRegularExpressionMatch m = QRegularExpression("VmHWM:\\s+(\\d+)").match(QString::fromLatin1(status.readAll()));
        return m.hasMatch() ? m.captured(1).toLongLong() : -1;
    }
};

// A text payload's text.
QString payloadText(const QByteArray &payload) {
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(payload.constData() + 2), (payload.size() - 2) / 2);
}

// A client that sends the protocol's frames and keeps what comes back.
struct ScriptedClient {
    struct Received {
        NetworkMessage::Frame frame;
        QString text;
    };
    QWebSocket socket;
    QStringList texts;
    QList<QByteArray> binaries;
    QList<Received> frames;
    // Every payload sent, for the check that no password is in one.
    QByteArray sent;
    quint32 request = 0;
    bool closed = false;
    QString unread;
    // The id_block records received (not cleared with the rest).
    QStringList idTexts;

    ScriptedClient() {
        QObject::connect(&socket, &QWebSocket::binaryMessageReceived, [this](const QByteArray &m) {
            Received r;
            QString why;
            if (NetworkMessage::readFrame(m, NetworkMessage::ServerLimit, r.frame, &why) != NetworkMessage::Read::Ok) {
                unread = why;
                return;
            }
            if (r.frame.kind == NetworkMessage::Text) {
                r.text = payloadText(r.frame.payload);
                texts << r.text;
                if (r.text.contains("id_block"))
                    idTexts << r.text;
            } else {
                binaries << r.frame.payload;
            }
            frames << r;
        });
        QObject::connect(&socket, &QWebSocket::disconnected, [this] { closed = true; });
    }
    bool open(int port) {
        socket.open(QUrl(QString("ws://127.0.0.1:%1").arg(port)));
        return waitFor([this] { return socket.state() == QAbstractSocket::ConnectedState; }, 5000);
    }
    // A request; its number.
    quint32 send(const QString &text) {
        const QByteArray payload = NetworkMessage::utf16(text);
        sent += payload;
        socket.sendBinaryMessage(NetworkMessage::frame(payload, ++request));
        return request;
    }
    quint32 sendBinary(const QByteArray &record) {
        sent += record;
        socket.sendBinaryMessage(NetworkMessage::frame(record, ++request));
        return request;
    }
    void sendRaw(const QByteArray &message) {
        socket.sendBinaryMessage(message);
    }
    // The text frames of a record, and the reply to a request.
    QList<Received> framesOf(const QString &record) const {
        QList<Received> out;
        for (const Received &r : frames)
            if (r.frame.kind == NetworkMessage::Text
                    && r.text.contains(QRegularExpression("(^|\\s)" + QRegularExpression::escape(record) + "\\s*\\(")))
                out << r;
        return out;
    }
    // The IDs of a kind the server gave, in their order.
    QList<int> ids(const QString &kind) const {
        QList<int> out;
        const QRegularExpression block("id_block \\( " + QRegularExpression::escape(kind) + " (-?\\d+) (\\d+)");
        for (const QString &text : idTexts) {
            auto it = block.globalMatch(text);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                for (int i = 0; i < m.captured(2).toInt(); i++)
                    out << m.captured(1).toInt() + i;
            }
        }
        return out;
    }
    QList<Received> repliesTo(quint32 number) const {
        QList<Received> out;
        for (const Received &r : frames)
            if (r.frame.request == number)
                out << r;
        return out;
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
    // hello, then the challenge's answer: the welcome.
    bool hello(int port, const QString &user) {
        if (!open(port))
            return false;
        send("hello ( protocol ( 1 ) tsre ( \"suite\" ) user ( \"" + user + "\" ) )\n");
        return waitText("challenge");
    }
    QByteArray nonce() const {
        const QRegularExpressionMatch m = QRegularExpression("nonce \\( \"([0-9a-f]+)\" \\)").match(last("challenge"));
        return m.hasMatch() ? QByteArray::fromHex(m.captured(1).toLatin1()) : QByteArray();
    }
    void answer(const QString &text) {
        send("login ( answer ( \"" + text + "\" ) )\n");
    }
    bool login(int port, const QString &user, const QString &password = QString()) {
        if (!hello(port, user))
            return false;
        answer(NetworkMessage::loginAnswer(password, nonce(), NetworkMessage::cleanUsername(user)));
        return waitFor([this] { return count("welcome") > 0 || count("auth_fail") > 0 || closed; }, 5000)
                && count("welcome") > 0;
    }
    void clear() {
        texts.clear();
        binaries.clear();
        frames.clear();
    }
};

// A terrain tile's heights as the protocol sends them.
QByteArray terrainFlags(Terrain *t) {
    QByteArray out;
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    t->saveFfileToStream(stream);
    return out;
}

QByteArray terrainHeights(Terrain *t) {
    QByteArray out;
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    t->saveRAWfileToStreamFloat(stream);
    return out;
}

// An item's record in a TrackDB text, from its name to its closing bracket.
QString itemRecord(const QString &tdb, int id) {
    const QRegularExpressionMatch m = QRegularExpression("(\\w+Item) \\(\\s*TrItemId \\( " + QString::number(id) + " \\)").match(tdb);
    if (!m.hasMatch())
        return QString();
    int depth = 0;
    for (int i = m.capturedStart(); i < tdb.size(); i++) {
        if (tdb[i] == '(')
            depth++;
        else if (tdb[i] == ')' && --depth == 0)
            return tdb.mid(m.capturedStart(), i - m.capturedStart() + 1);
    }
    return QString();
}

QString endNodeRecord(int id) {
    return QString("update_trnode_tdb ( id ( %1 ) TrackNode ( %1 TrEndNode ( 0 ) UiD ( 0 0 0 0 0 0 0 0 0 ) "
                   "TrPins ( 1 0 TrPin ( 1 1 ) ) ) )\n").arg(id);
}

bool disjoint(const QList<int> &a, const QList<int> &b) {
    for (int id : a)
        if (b.contains(id))
            return false;
    return true;
}

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
        // The users, in the server's profile (the user, 2026-11-20): one
        // whose name could never log in; an old users.txt in the route's
        // folder, not read.
        made = made && QDir().mkpath(serverRoot + "/profile")
                && writeBytes(serverRoot + "/profile/users.json",
                              "{ \"format\": \"tsre-server-users\", \"schemaVersion\": 1, \"users\": {\n"
                              "  \"alice\": { \"password\": \"secret\" }, \"bob\": { \"password\": \"pw2\" },\n"
                              "  \"real\": { \"password\": \"realpw\", \"permissions\": [\"watch\"] },\n"
                              "  \"jan.k\": { \"password\": \"x\" } } }\n")
                && writeUtf16(routeDir + "/users.txt", "alice = fromroute\n");
        // Two heavy files the client's copy will differ in (task server 03).
        made = made && writeBytes(routeDir + "/TEXTURES/servertest_a.ace", QByteArray(100, 'a'))
                && writeBytes(routeDir + "/TEXTURES/servertest_b.ace", QByteArray(200, 'b'));
        // The client's game root: a copy before any edit, with only the
        // heavy folders (task server 03): one heavy file missing, one of
        // another size, one the server has not got.
        made = made && copyTree(serverRoot, clientRoot) && keepHeavyOnly(clientRoot, RouteName)
                && QFile::remove(clientRoot + "/ROUTES/" + RouteName + "/TEXTURES/servertest_a.ace")
                && QFile::remove(clientRoot + "/ROUTES/" + RouteName + "/TEXTURES/servertest_b.ace")
                && writeBytes(clientRoot + "/ROUTES/" + RouteName + "/TEXTURES/servertest_b.ace", QByteArray(300, 'b'))
                && writeBytes(clientRoot + "/ROUTES/" + RouteName + "/TEXTURES/servertest_c.ace", QByteArray(10, 'c'));
        // Map overlays: the server's two, the client's own one of another
        // picture (task server 03: a client's are its own).
        made = made && writeOverlay(overlayPath(routeDir, StartX, StartZ), 8, Qt::red)
                && writeOverlay(overlayPath(routeDir, StartX + 1, StartZ), 8, Qt::red)
                && writeOverlay(overlayPath(clientRoot + "/ROUTES/" + RouteName, StartX, StartZ), 16, Qt::blue);
    }
    check(made, "the route made for the test (needs assets/templateRoute_0.6)");
    if (!made) {
        Game::root = rootWas;
        Game::route = routeWas;
        Game::writeEnabled = writeWas;
        qInfo() << "[tests:server] cases=" << passed + failed << "passed=" << passed << "failed=" << failed;
        return 1;
    }

    // A local load of the route, to compare a client's with (task server 03).
    LocalLoad local;
    {
        TSectionDAT global(false, false);
        local.globalSections = global.tsectionMaxIdx;
        TSectionDAT sections(false, true);
        local.sections = int(sections.sekcja.size());
        local.shapes = int(sections.shape.size());
        SpeedPostDAT speedPosts;
        local.speedPosts = speedPosts.speed_Warning_Sign_Shape + "|" + speedPosts.restricted_Shape;
        SigCfg signalConfig;
        local.signalTypes = signalConfig.signalType.size();
        local.signalShapes = signalConfig.signalShape.size();
        SoundList sounds;
        sounds.loadSoundSources(routeDir + "/ssource.dat");
        sounds.loadSoundRegions(routeDir + "/ttype.dat");
        local.soundSources = sounds.sources.size();
        local.soundRegions = sounds.regions.size();
        ForestObj::LoadForestList();
        local.forests = ForestObj::forestList.size();
        CarSpawnerObj::LoadCarSpawnerList();
        local.carSpawners = CarSpawnerObj::carSpawnerList.size();
        local.telepoles = TelepoleData::routeData(routeDir, true).configs().size();
        Environment environment(routeDir + "/ENVFILES/editor.env");
        local.environment = bool(environment.file.world);
        local.water = environment.waterCount;
        local.environmentFiles = QDir(routeDir + "/ENVFILES").entryList({"*.env"}, QDir::Files, QDir::Name | QDir::IgnoreCase);
        TerrainLibQt *terrain = new TerrainLibQt();
        terrain->loadQuadTree();
        if (Terrain *t = terrain->getTerrainByXY(StartX, StartZ, true)) {
            local.heights = terrainHeights(t);
            local.flags = t->hasFlags();
            local.samples = t->getSampleCount();
        }
    }
    check(local.globalSections > 0 && local.signalTypes > 0 && local.soundSources > 0 && local.forests > 0
                  && local.telepoles > 0 && local.environment && !local.heights.isEmpty(),
          "a local load of the route reads its files (the template's)");

    const int port = freePort();
    const QString url = QString("ws://127.0.0.1:%1").arg(port);
    ServerProcess server;
    check(server.start(serverRoot, port, {}, false), "the server starts and listens");

    // The frames themselves.
    {
        NetworkMessage::Frame f;
        const QByteArray small = NetworkMessage::textFrame("a ( 1 )\n", 7, 9);
        bool ok = NetworkMessage::readFrame(small, 1000, f) == NetworkMessage::Read::Ok && f.request == 7
                && f.change == 9 && f.kind == NetworkMessage::Text && !(f.flags & NetworkMessage::Compressed)
                && payloadText(f.payload) == "a ( 1 )\n";
        QByteArray big = QByteArray("B") + QByteArray(100000, 'x');
        const QByteArray bigFrame = NetworkMessage::frame(big, 3);
        ok = ok && bigFrame.size() < 20000 && NetworkMessage::readFrame(bigFrame, 200000, f) == NetworkMessage::Read::Ok
                && (f.flags & NetworkMessage::Compressed) && f.kind == NetworkMessage::Binary && f.payload == big;
        ok = ok && NetworkMessage::readFrame(bigFrame, 50000, f) == NetworkMessage::Read::TooLarge;
        ok = ok && NetworkMessage::readFrame(small.left(small.size() - 1), 1000, f) == NetworkMessage::Read::Malformed;
        ok = ok && NetworkMessage::readFrame(NetworkMessage::utf16("login ( a )"), 1000, f) == NetworkMessage::Read::NotFrame;
        check(ok, "frames: numbers kept, large payloads compressed, the stated size checked before uncompressing, "
                  "a wrong length and a message without the mark not read");
    }

    // The login setting's forms, and wss:// (TLS): a client reaches a
    // secure server with a certificate it trusts (TSRE_SERVER_TLS_CA), and
    // not one with a certificate it does not.
    {
        QString u, n, p;
        bool parsed = RouteEditorClient::parseLogin("ann:pw@host:7", u, n, p) && u == "ws://host:7" && n == "ann" && p == "pw";
        parsed = parsed && RouteEditorClient::parseLogin("ann:p@ss:x@wss://host:8", u, n, p) && u == "wss://host:8"
                && n == "ann" && p == "p@ss:x";
        parsed = parsed && RouteEditorClient::parseLogin("ann@ws://host", u, n, p) && u == "ws://host" && p.isEmpty();
        parsed = parsed && !RouteEditorClient::parseLogin("host:7", u, n, p);
        check(parsed, "the login setting: user[:password]@[ws://|wss://]host[:port], the last @ ends the password");
        const QString openssl = QStandardPaths::findExecutable("openssl");
        if (openssl.isEmpty() || !QSslSocket::supportsSsl()) {
            qInfo().noquote() << "[tests:server] wss:// skipped:" << (openssl.isEmpty() ? "no openssl on the PATH" : "no TLS in Qt");
        } else {
            const QString cert = workspace.filePath("tls-cert.pem"), key = workspace.filePath("tls-key.pem");
            QProcess make;
            make.start(openssl, {"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", cert,
                                 "-days", "1", "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=IP:127.0.0.1"});
            make.waitForFinished(30000);
            QFile keyFile(key);
            const QList<QSslCertificate> certs = QSslCertificate::fromPath(cert);
            const bool made = make.exitCode() == 0 && !certs.isEmpty() && keyFile.open(QIODevice::ReadOnly);
            check(made, "a certificate for the secure test server (openssl)");
            if (made) {
                QWebSocketServer secure(QStringLiteral("suite"), QWebSocketServer::SecureMode);
                QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
                configuration.setLocalCertificate(certs.first());
                configuration.setPrivateKey(QSslKey(&keyFile, QSsl::Rsa));
                configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
                secure.setSslConfiguration(configuration);
                QStringList hellos;
                QList<QWebSocket *> accepted;
                QObject::connect(&secure, &QWebSocketServer::newConnection, [&] {
                    QWebSocket *socket = secure.nextPendingConnection();
                    accepted << socket;
                    QObject::connect(socket, &QWebSocket::binaryMessageReceived, [&](const QByteArray &m) {
                        NetworkMessage::Frame f;
                        if (NetworkMessage::readFrame(m, NetworkMessage::ClientLimit, f) == NetworkMessage::Read::Ok)
                            hellos << payloadText(f.payload);
                    });
                });
                check(secure.listen(QHostAddress::LocalHost, 0), "a secure test server listens");
                const QString secureUrl = QString("wss://127.0.0.1:%1").arg(secure.serverPort());
                qputenv("TSRE_SERVER_TLS_CA", cert.toLocal8Bit());
                RouteEditorClient *trusting = new RouteEditorClient(secureUrl, "tls", "pw");
                qunsetenv("TSRE_SERVER_TLS_CA");
                trusting->connectNow();
                check(waitFor([&] { return !hellos.isEmpty(); }, 10000) && hellos.value(0).contains("hello ( protocol ( 1 )"),
                      "wss://: the client reaches the secure server and says hello");
                trusting->disconnectNow();
                QString failure;
                RouteEditorClient *doubting = new RouteEditorClient(secureUrl, "tls", "pw");
                QObject::connect(doubting, &RouteEditorClient::loginFailed, [&](QString why) { failure = why; });
                doubting->connectNow();
                check(waitFor([&] { return !failure.isEmpty(); }, 10000) && hellos.size() == 1,
                      "a certificate the client does not trust: no connection, the user told");
                doubting->disconnectNow();
                settle(200);
                delete trusting;
                delete doubting;
                qDeleteAll(accepted);
            }
        }
    }

    // Logging in.
    ScriptedClient alice, alice2, stranger, silent;
    check(alice.login(port, "alice"), "a login: hello, challenge, answer, welcome");
    check(alice.last("welcome").contains("route ( \"" + RouteName + "\" )")
                  && alice.last("welcome").contains("allowed ( view edit )"),
          "the welcome names the route and what the user may do");
    check(alice.waitText("users_info") && alice.last("users_info").contains("user ( alice "),
          "the logged-in user gets the users");
    // al.ice is cleaned to alice, taken: alice2.
    check(alice2.login(port, "al.ice") && alice2.last("welcome").contains("user ( \"alice2\" )"),
          "a name cleaned before the duplicate check, and a duplicate gets a number, not a random name");
    {
        ScriptedClient any;
        check(any.hello(port, "anyone") && (any.answer("not an answer"), any.waitText("welcome")),
              "authentication off: any answer is taken");
    }
    check(stranger.open(port) && silent.open(port), "sockets that do not log in connect");
    stranger.send("request_tdb ( )\n");
    check(stranger.waitText("refused") && waitFor([&] { return stranger.closed; }, 5000)
                  && stranger.count("requested_tdb") == 0,
          "nothing before a login: refused, and the socket closed");
    {
        ScriptedClient old, newer, early, wrongHello;
        check(old.open(port) && (old.sendRaw(NetworkMessage::utf16("login ( alice  )\n")), waitFor([&] { return old.closed; }, 5000))
                      && old.count("welcome") == 0 && old.count("challenge") == 0,
              "a message without the mark (an older TSRE): closed, nothing sent but the refusal");
        QByteArray other = NetworkMessage::textFrame("hello ( protocol ( 2 ) )\n", 1);
        other[4] = char(2);
        check(newer.open(port) && (newer.sendRaw(other), newer.waitText("refused"))
                      && newer.last("refused").contains("protocol 1 here, 2 there") && waitFor([&] { return newer.closed; }, 5000),
              "a frame of another protocol: refused before any route data");
        check(early.open(port) && (early.answer("x"), early.waitText("refused")) && waitFor([&] { return early.closed; }, 5000),
              "a login before its hello: refused");
        check(wrongHello.open(port) && (wrongHello.send("hello ( protocol ( 2 ) user ( \"x\" ) )\n"), wrongHello.waitText("refused"))
                      && wrongHello.count("challenge") == 0,
              "a hello of another protocol: refused");
    }

    // ID blocks (task 02): after the welcome the TDB's and tsection's, with
    // a tile its UiDs; two users' never overlap.
    check(!alice.ids("tdb_node").isEmpty() && !alice.ids("tdb_item").isEmpty() && !alice.ids("section").isEmpty()
                  && !alice.ids("shape").isEmpty(),
          "after the welcome: blocks of TDB node, item, track section and shape IDs");
    check(alice.ids("tdb_item").size() == 32 && alice.ids("tdb_node").size() == 256 && alice.ids("section").size() == 256
                  && alice.ids("tdb_node").value(0) == 23 && alice.ids("tdb_item").value(0) == 3
                  && alice.ids("section").value(0) % 2 == 0,
          "32 items, 256 of the others; new nodes after the last (22), items after the last (2), sections from an even ID");
    alice.send(QString("request_tile ( %1 %2 )\n").arg(StartX).arg(StartZ));
    alice2.send(QString("request_tile ( %1 %2 )\n").arg(StartX).arg(StartZ));
    check(alice.waitText("requested_tile") && alice2.waitText("requested_tile")
                  && alice.ids("uid").size() == 256 && alice2.ids("uid").size() == 256,
          "with a tile, a block of its UiDs");
    check(disjoint(alice.ids("uid"), alice2.ids("uid")) && disjoint(alice.ids("tdb_node"), alice2.ids("tdb_node"))
                  && disjoint(alice.ids("tdb_item"), alice2.ids("tdb_item")) && disjoint(alice.ids("section"), alice2.ids("section"))
                  && disjoint(alice.ids("shape"), alice2.ids("shape")),
          "two users' blocks never overlap");
    alice.send(QString("request_tile ( %1 %2 )\n").arg(StartX).arg(StartZ));
    check(alice.waitText("requested_tile", 2) && alice.ids("uid").size() == 256, "the tile again: no second block while it holds some");
    const quint32 moreIds = alice.send("request_ids ( tdb_node )\n");
    check(waitFor([&] { return alice.ids("tdb_node").size() == 512; }, 5000)
                  && alice.repliesTo(moreIds).value(0).text.contains("id_block ( tdb_node"),
          "request_ids: the next block, as the reply");
    const int uidA = alice.ids("uid").value(0), uidB = alice.ids("uid").value(1), uidC = alice.ids("uid").value(2);
    auto uidText = [](int uid) { return QString("UiD ( %1 )").arg(uid); };

    // Edits: numbered, forwarded once, to the logged-in others only; the
    // sender gets the numbers.
    alice.clear();
    alice2.clear();
    const quint32 firstEdit = alice.send(objectRecord(StartX, StartZ, uidA, 10));
    check(alice2.waitText("update_worldobj") && alice2.last("update_worldobj").contains(uidText(uidA)),
          "an object forwarded to another user");
    check(alice.waitText("done"), "the sender's reply: done");
    settle();
    const quint32 firstChange = alice2.framesOf("update_worldobj").value(0).frame.change;
    check(alice2.count("update_worldobj") == 1 && alice.count("update_worldobj") == 0
                  && silent.count("update_worldobj") == 0 && silent.count("users_info") == 0,
          "once, not back to its sender, not to a socket not logged in (no users either)");
    check(firstChange > 0 && alice.repliesTo(firstEdit).size() == 1
                  && alice.repliesTo(firstEdit).value(0).text.contains(QString("done ( %1 %1 )").arg(firstChange)),
          "the forwarded record has its change number; the sender's done, with its request's number, has the same");
    check(waitFor([&] { return silent.closed; }, 5000),
          "a socket that does not log in is closed (TSRE_SERVER_LOGIN_TIMEOUT_MS 1.5 s here, 10 s by default)");

    alice.clear();
    alice2.clear();
    const quint32 twoEdits = alice.send(objectRecord(StartX, StartZ, uidB, 12) + objectRecord(StartX, StartZ, uidC, 14));
    check(alice2.waitText("update_worldobj", 2), "two objects in one message both forwarded");
    check(alice.waitText("done") && alice2.framesOf("update_worldobj").size() == 2
                  && alice2.framesOf("update_worldobj").value(0).frame.change == firstChange + 1
                  && alice2.framesOf("update_worldobj").value(1).frame.change == firstChange + 2
                  && alice.repliesTo(twoEdits).value(0).text.contains(QString("done ( %1 %2 )").arg(firstChange + 1).arg(firstChange + 2)),
          "numbered without gaps, the sender's done with both numbers");
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
    const quint32 refusedEdit = alice.send(objectRecord(StartX + 7, StartZ, 10, 5));
    check(alice.waitText("error") && alice.last("error").contains("not here")
                  && !alice.repliesTo(refusedEdit).isEmpty() && alice.count("done") == 0,
          "an object on a tile the server has not got: refused, the error the reply to its request");
    check(alice.waitText("update_worldobj") && alice.repliesTo(refusedEdit).size() == 2
                  && alice.last("update_worldobj").contains("remove ( )") && alice.last("update_worldobj").contains("UiD ( 10 )"),
          "put right: the sender's own object back, removed (the server has none)");
    settle();
    check(alice2.count("update_worldobj") == 0 && alice2.count("error") == 0, "and not forwarded, the error to its sender only");
    const quint32 missingTile = alice.send(QString("request_tile ( %1 %2 )\n").arg(StartX + 7).arg(StartZ));
    check(alice.waitText("no_tile") && alice.last("no_tile").contains(QString::number(StartX + 7))
                  && alice.repliesTo(missingTile).size() == 1,
          "a request for a tile the server has not got: answered no_tile");
    const quint32 tileRequest = alice.send(QString("request_tile ( %1 %2 )\n").arg(StartX).arg(StartZ));
    check(alice.waitText("requested_tile") && alice.last("requested_tile").contains(uidText(uidC))
                  && alice.repliesTo(tileRequest).size() == 1 && alice.repliesTo(tileRequest).value(0).text.contains("requested_tile"),
          "a tile with the edits in it, as the reply to its request");
    {
        alice.clear();
        alice2.clear();
        const quint32 other = alice2.send(objectRecord(StartX, StartZ, alice.ids("uid").value(10), 3));
        check(alice2.waitText("error") && alice2.last("error").contains("not one of this user's")
                      && alice2.waitText("update_worldobj") && alice2.repliesTo(other).size() == 2
                      && alice2.last("update_worldobj").contains("remove ( )"),
              "a new object with another user's UiD: refused, and put right (removed)");
        settle();
        check(alice.count("update_worldobj") == 0, "and not forwarded");
    }
    const quint32 unknown = alice.send("frobnicate ( )\n");
    check(waitFor([&] { return !alice.repliesTo(unknown).isEmpty(); }, 5000)
                  && alice.repliesTo(unknown).value(0).text.contains("unknown record"),
          "an unknown record: an error, the reply to its request");

    // Malformed and out of range records: refused, nothing changed or sent.
    alice.clear();
    alice2.clear();
    // A node no user's block reaches.
    alice.send(endNodeRecord(99999));
    alice.send("update_tritem_tdb ( id ( 50 ) remove ( ) )\n");
    alice.send("update_trnode_tdb ( id ( 3 ) )\n");
    alice.send("update_tracksection ( TrackSection ( SectionCurve ( 0 ) 1 20 1.5 ) )\n");
    check(alice.waitText("error", 4), "a node out of range, an item out of range, a record of nothing, "
                                             "a global track section: refused");
    {
        bool node999 = false, node3 = false, item50 = false;
        waitFor([&] { return alice.count("update_trnode_tdb") >= 2 && alice.count("update_tritem_tdb") >= 1; }, 5000);
        for (const ScriptedClient::Received &r : alice.framesOf("update_trnode_tdb")) {
            node999 = node999 || (r.text.contains("id ( 99999 )") && r.text.contains("remove ( )"));
            node3 = node3 || (r.text.contains("id ( 3 )") && r.text.contains("TrackNode ( 3"));
        }
        for (const ScriptedClient::Received &r : alice.framesOf("update_tritem_tdb"))
            item50 = item50 || (r.text.contains("id ( 50 )") && r.text.contains("remove ( )"));
        check(node999 && node3 && item50, "put right: the server's node 3, and removals of node 99999 and item 50 it has not got");
    }
    if (alice.count("error") != 4)
        qWarning().noquote() << "[tests:server] refusals:" << alice.texts;
    settle();
    check(alice2.count("update_trnode_tdb") == 0 && alice2.count("update_tritem_tdb") == 0
                  && alice2.count("update_tracksection") == 0,
          "none of them forwarded");

    // Removing the isolated track and a platform item.
    for (const FixtureNode &n : Isolated)
        alice.send(QString("update_trnode_tdb ( id ( %1 ) remove ( ) )\n").arg(n.id));
    // Item 0: first along its track either way, so the save's item order does
    // not depend on the place an emptyitem keeps in memory.
    alice.send("update_tritem_tdb ( id ( 0 ) remove ( ) )\n");
    check(alice2.waitText("update_trnode_tdb", 3) && alice2.waitText("update_tritem_tdb"),
          "node and item removals forwarded");

    // Terrain: the files, an edit of each, a malformed one.
    alice.clear();
    alice2.clear();
    alice.send(QString("request_terrain_tfile( %1 %2 )\nrequest_terrain_yfile( %1 %2 )\nrequest_terrain_ffile( %1 %2 )\n")
                       .arg(StartX).arg(StartZ));
    check(waitFor([&] { return alice.binaries.size() >= 3; }, 5000), "the terrain's T file, heights and flags");
    {
        bool compressed = false;
        for (const ScriptedClient::Received &r : alice.frames)
            if (r.frame.kind == NetworkMessage::Binary && binaryToken(r.frame.payload) == quint32(TS::TSRE_Requested_Terrain_RawFile))
                compressed = (r.frame.flags & NetworkMessage::Compressed) && r.frame.payload.size() > NetworkMessage::CompressAbove;
        check(compressed, "the heights (a large payload) arrive compressed");
    }
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
    alice.clear();
    alice.sendBinary(terrainEdit(TS::TSRE_Terrain_RawFile, StartX, StartZ, heights.left(heights.size() - 4)));
    check(alice.waitText("error"), "heights of another size: refused");
    check(waitFor([&] { return !alice.lastBinary(TS::TSRE_Terrain_RawFile).isEmpty(); }, 5000)
                  && alice.lastBinary(TS::TSRE_Terrain_RawFile).mid(18) == heights,
          "put right: the server's heights back to the sender, as an edit");
    alice.sendBinary(terrainEdit(TS::TSRE_Terrain_tFile, StartX, StartZ, tFile.left(tFile.size() / 2)));
    check(alice.waitText("error", 2), "half a T file: refused");
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

    // A tile's files for the local loader (task server 03): the tile file
    // only (map mode), all of them, none for a tile the server has not got.
    {
        const auto tileFiles = [&](int x, int part) {
            alice.binaries.clear();
            alice.send(QString("request_terrain_files ( %1 %2 0 %3 )\n").arg(x).arg(StartZ).arg(part));
            QHash<QString, QByteArray> files;
            if (!waitFor([&] { return !alice.lastBinary(TS::TSRE_Requested_Terrain_Files).isEmpty(); }, 5000))
                return files;
            QDataStream in(alice.lastBinary(TS::TSRE_Requested_Terrain_Files).mid(18));
            in.setVersion(QDataStream::Qt_6_0);
            in.setByteOrder(QDataStream::LittleEndian);
            quint8 low = 0, descriptor = 0;
            quint32 count = 0;
            in >> low >> descriptor >> count;
            for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; i++) {
                QString name;
                QByteArray bytes;
                in >> name >> bytes;
                files.insert(name, bytes);
            }
            files.insert(":parts", QByteArray::number(low) + " " + QByteArray::number(descriptor));
            return files;
        };
        const QHash<QString, QByteArray> descriptorOnly = tileFiles(StartX, 1);
        const QHash<QString, QByteArray> all = tileFiles(StartX, 0);
        const QHash<QString, QByteArray> none = tileFiles(StartX + 7, 0);
        const QString tiles = "ROUTES/" + RouteName + "/TILES/";
        int dotT = 0, rawHeights = 0, rawFlags = 0;
        for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
            dotT += it.key().startsWith(tiles) && it.key().endsWith(".t");
            rawHeights += it.key().startsWith(tiles) && it.key().endsWith("_y.raw") && it.value() == heights;
            rawFlags += it.key().startsWith(tiles) && it.key().endsWith("_f.raw") && it.value() == flags;
        }
        check(descriptorOnly.size() == 2 && descriptorOnly.value(":parts") == "0 1"
                      && std::any_of(descriptorOnly.keyBegin(), descriptorOnly.keyEnd(),
                                     [&](const QString &k) { return k.startsWith(tiles) && k.endsWith(".t"); }),
              "a tile's files: the tile file only, for map mode");
        check(dotT == 1 && rawHeights == 1 && rawFlags == 1,
              "all its files: the tile file, the server's heights (as floats) and its edited flags, named as its save names them");
        check(none.size() == 1 && none.value(":parts") == "0 0", "none for a tile the server has not got");
    }

    // Limits: a message over 8 MB closes its socket; a compressed one stating
    // more is not uncompressed. The server serves on.
    {
        ScriptedClient large, bomb;
        check(large.login(port, "large"), "a user for the limits logs in");
        // Random, so that compressing it does not make it small: the
        // WebSocket's own limit.
        QByteArray noise(9 * 1024 * 1024, char(0));
        QRandomGenerator(7).fillRange(reinterpret_cast<quint32 *>(noise.data()), noise.size() / 4);
        noise[0] = 'B';
        large.sendBinary(noise);
        check(waitFor([&] { return large.closed; }, 10000), "a 9 MB message closes its socket");
        ScriptedClient packed;
        check(packed.login(port, "packed"), "a user logs in");
        packed.sendBinary(QByteArray("B") + QByteArray(9 * 1024 * 1024, 'x'));
        check(packed.waitText("refused") && waitFor([&] { return packed.closed; }, 10000),
              "9 MB compressed small: refused, its stated size over the limit");
        check(bomb.login(port, "bomb"), "another logs in");
        QByteArray stated(4 + 64, char(0));
        qToBigEndian<quint32>(300u * 1024 * 1024, stated.data());
        QByteArray header = NetworkMessage::frame(QByteArray("B"), 9).left(NetworkMessage::HeaderSize);
        header[7] = char(NetworkMessage::Compressed);
        qToLittleEndian<quint32>(quint32(stated.size()), header.data() + 16);
        bomb.sendRaw(header + stated);
        check(bomb.waitText("refused") && bomb.last("refused").contains("at most") && waitFor([&] { return bomb.closed; }, 5000),
              "a compressed payload stating 300 MB: refused unread");
        // Before any login: one stating 100 bytes that would grow to 300 MB
        // is not grown past what it states (the review's zlib bomb).
        ScriptedClient grower;
        const long long peakBefore = server.peakKb();
        QByteArray grown = qCompress(QByteArray("B") + QByteArray(300 * 1024 * 1024, 'x'), 9);
        qToBigEndian<quint32>(100u, grown.data());
        QByteArray growing = NetworkMessage::frame(QByteArray("B"), 1).left(NetworkMessage::HeaderSize);
        growing[7] = char(NetworkMessage::Compressed);
        qToLittleEndian<quint32>(quint32(grown.size()), growing.data() + 16);
        check(grower.open(port) && (grower.sendRaw(growing + grown), grower.waitText("refused"))
                      && grower.last("refused").contains("uncompress to its size") && waitFor([&] { return grower.closed; }, 5000),
              "before a login, a compressed payload growing past the size it states: refused");
        const long long peakAfter = server.peakKb();
        check(peakBefore > 0 && peakAfter - peakBefore < 20 * 1024,
              QString("and not grown in the server's memory (its peak %1 MB more)").arg((peakAfter - peakBefore) / 1024));
        // A hello whose text ends inside a string: refused, nothing read past
        // the record (the review's over-read: heap bytes in the name).
        ScriptedClient cut;
        check(cut.open(port) && (cut.send("hello ( protocol ( 1 ) user ( \"AB"), cut.waitText("error"))
                      && cut.last("error").contains("malformed") && cut.count("challenge") == 0,
              "a hello cut inside its user's name: refused as malformed, no challenge");
        alice.clear();
        alice.send(QString("request_tile ( %1 %2 )\n").arg(StartX).arg(StartZ));
        check(alice.waitText("requested_tile"), "and the server serves on");
    }

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
    check(saved.contains(QRegularExpression("EmptyItem \\(\\s*TrItemId \\( 0 \\)")) && saved.contains("TrItemTable ( 3"),
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
    // The local editor's save of the same database (renumbering in memory)
    // writes the same file.
    {
        QTemporaryDir local;
        const QString localRoute = local.path() + "/ROUTES/" + RouteName;
        QDir().mkpath(localRoute);
        QDir().mkpath(local.path() + "/GLOBAL");
        QFile::copy(serverRoot + "/GLOBAL/tsection.dat", local.path() + "/GLOBAL/tsection.dat");
        const QString routeNameWas = Game::routeName;
        Game::root = local.path();
        Game::route = RouteName;
        Game::routeName = RouteName;
        // With the route's part, as the editor loads it (its save writes it).
        TSectionDAT sections(false, true);
        TDB db(&sections, false);
        const bool read = readTdb(alice.last("requested_tdb"), db);
        // As TDB::loadTdb completes it (no .tit here).
        db.checkTrSignalRDirs();
        db.loaded = true;
        db.save();
        const QString localSaved = readUtf16(localRoute + "/" + RouteName + ".tdb");
        check(read && db.lastSaveWritten && localSaved == saved,
              "the server's file is what the local editor's save writes of the same database");
        if (localSaved != saved) {
            const QStringList a = saved.split('\n'), b = localSaved.split('\n');
            for (int i = 0; i < std::max(a.size(), b.size()); i++)
                if (a.value(i) != b.value(i)) {
                    qWarning().noquote() << "[tests:server] line" << i + 1 << "server:" << a.value(i).trimmed()
                                         << "| local:" << b.value(i).trimmed();
                    break;
                }
        }
        Game::routeName = routeNameWas;
        Game::root = serverRoot;
    }

    // A real client: it loads, applies others' edits without sending them
    // back, sends its user's own, refuses tiles the server has not got, and
    // writes nothing into its route folder.
    const QStringList clientFilesBefore = snapshot(clientRoot + "/ROUTES/" + RouteName);
    Game::root = clientRoot;
    Game::route.clear();
    Game::currentShapeLib = new ShapeLib();
    Game::currentEngLib = new EngLib();
    Undo::UndoEnabled = false;
    RouteEditorClient *client = new RouteEditorClient(url, "real", "realpw");
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
    QList<GameObj *> replacedObjects;
    QList<int> replacedTiles;
    int routeReplacing = 0, routeRefreshed = 0, switches = 0;
    QString switchedTo;
    QObject::connect(client, &RouteEditorClient::objectReplaced, [&](GameObj *old) { replacedObjects << old; });
    QObject::connect(client, &RouteEditorClient::tileReplaced, [&](int x, int z) { replacedTiles << x * 10000 + z; });
    QObject::connect(client, &RouteEditorClient::routeReplacing, [&] { routeReplacing++; });
    QObject::connect(client, &RouteEditorClient::routeRefreshed, [&] { routeRefreshed++; });
    // While a refresh is on its way nothing is editable: the window is told
    // so, and an edit is not sent (task server 02's review: an edit then was
    // replaced by answers to requests sent before it).
    bool lastConnected = false;
    int refreshEditsTried = 0, refreshEditsDropped = 0, refreshesShownEditable = 0;
    QObject::connect(client, &RouteEditorClient::connectionChanged, [&](bool connected, QString) { lastConnected = connected; });
    QObject::connect(client, &RouteEditorClient::reconnected, [&] {
        if (lastConnected)
            refreshesShownEditable++;
        // A user's edit, after the message that began the refresh, before
        // its answers.
        const int refreshedBefore = routeRefreshed;
        QTimer::singleShot(0, [&, refreshedBefore] {
            if (routeRefreshed != refreshedBefore)
                return;
            const int before = client->droppedEdits;
            refreshEditsTried++;
            client->sendEditMessage("update_trnode_tdb ( id ( 3 ) )\n");
            if (client->droppedEdits == before + 1)
                refreshEditsDropped++;
        });
    });
    QObject::connect(client, &RouteEditorClient::routeSwitchNeeded, [&](QString routeName) {
        switches++;
        switchedTo = routeName;
    });
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
        // Task server 03: from the server's files, as a local load reads
        // its own.
        const QString clientRouteDir = clientRoot + "/ROUTES/" + RouteName;
        check(RouteFiles::serving() && RouteFiles::exists(clientRouteDir + "/sigcfg.dat")
                      && !QFileInfo::exists(clientRouteDir + "/sigcfg.dat")
                      && !QFileInfo::exists(clientRoot + "/GLOBAL/tsection.dat"),
              "its root has only the heavy folders: the route's files are the server's, in memory");
        TSectionDAT *sections = Game::trackDB->tsection;
        check(sections != NULL && sections->tsectionMaxIdx == local.globalSections
                      && int(sections->sekcja.size()) == local.sections && int(sections->shape.size()) == local.shapes,
              "its track sections: the global ones and the route's numbered as a local load's");
        check(Game::trackDB->speedPostDAT != NULL && Game::trackDB->sigCfg != NULL
                      && Game::trackDB->speedPostDAT->speed_Warning_Sign_Shape + "|" + Game::trackDB->speedPostDAT->restricted_Shape == local.speedPosts
                      && Game::trackDB->sigCfg->signalType.size() == local.signalTypes
                      && Game::trackDB->sigCfg->signalShape.size() == local.signalShapes,
              "its speed posts and signal configuration as a local load's");
        check(Game::soundList != NULL && Game::soundList->sources.size() == local.soundSources
                      && Game::soundList->regions.size() == local.soundRegions
                      && ForestObj::forestList.size() == local.forests
                      && CarSpawnerObj::carSpawnerList.size() == local.carSpawners
                      && TelepoleData::routeData(clientRouteDir).configs().size() == local.telepoles,
              "its sound sources and regions, forests, car spawners and telepoles as a local load's");
        check(route->env != NULL && bool(route->env->file.world) == local.environment && route->env->waterCount == local.water
                      && RouteFiles::entries(clientRouteDir + "/ENVFILES", {"*.env"}) == local.environmentFiles,
              "its environment and env files as a local load's");
        // The server's tile was edited above (heights and flags, unsaved):
        // the client has the server's, read as a local load reads a tile.
        Terrain *startTerrain = Game::terrainLib->getTerrainByXY(StartX, StartZ);
        if (startTerrain != NULL && (terrainHeights(startTerrain) != heightsBack || terrainFlags(startTerrain) != flagsBack
                                     || startTerrain->getSampleCount() != local.samples))
            qWarning().noquote() << "[tests:server] terrain: client" << (dynamic_cast<TerrainClient *>(startTerrain) != NULL)
                                 << startTerrain->loaded << (terrainHeights(startTerrain) == heightsBack)
                                 << startTerrain->hasFlags() << (terrainFlags(startTerrain) == flagsBack)
                                 << startTerrain->getSampleCount() << "| local" << local.samples;
        check(dynamic_cast<TerrainClient *>(startTerrain) != NULL && startTerrain->loaded
                      && terrainHeights(startTerrain) == heightsBack && terrainFlags(startTerrain) == flagsBack
                      && startTerrain->hasFlags() && startTerrain->getSampleCount() == local.samples
                      && terrainHeights(startTerrain).size() == local.heights.size()
                      && !RouteFiles::exists(clientRouteDir + "/TILES/" + startTerrain->name + ".t"),
              "its terrain tile through the local loader: the server's, edited heights and flags too (its files dropped after)");
        bool kindsOnly = !client->requests.isEmpty();
        for (const QString &request : std::as_const(client->requests))
            kindsOnly = kindsOnly && !request.contains('/') && !request.contains('\\')
                    && !request.contains(QRegularExpression("\\.(dat|t|raw|ace|env|png)\\b", QRegularExpression::CaseInsensitiveOption));
        check(kindsOnly && client->requests.first().startsWith("request_route_files"),
              "it asks for kinds of data only: no request names a path or a file");
        const auto told = [](const QString &text, const QString &file) {
            for (ErrorMessage *message : std::as_const(ErrorMessagesLib::ErrorMessages))
                if (message != NULL && message->description.contains(text) && message->action.contains(file))
                    return true;
            return false;
        };
        check(told("are not in this client's game root", "servertest_a.ace")
                      && told("differ in size from the server's", "servertest_b.ace")
                      && told("are not on the server", "servertest_c.ace"),
              "its heavy files against the server's: one missing, one of another size, one not there, each told");
        // Map overlays are the client's own: its user's pictures, made with
        // their own OSM and imagery access; nothing about them goes over
        // the network.
        TerrainOverlays::clear();
        const bool ownLoaded = TerrainOverlays::loadFromDisk(StartX, StartZ);
        const QImage *ownOverlay = TerrainOverlays::image(TerrainOverlays::key(StartX, StartZ));
        const bool serversLoaded = TerrainOverlays::loadFromDisk(StartX + 1, StartZ);
        check(ownLoaded && ownOverlay != NULL && ownOverlay->width() == 16
                      && ownOverlay->pixelColor(0, 0) == QColor(Qt::blue)
                      && !serversLoaded,
              "its map overlays are read from its own folder: its own picture, none of the server's");
        QImage drawn(4, 4, QImage::Format_RGB888);
        drawn.fill(Qt::green);
        TerrainOverlays::set(StartX + 2, StartZ, drawn);
        QString overlayError;
        check(TerrainOverlays::saveToDisk(StartX + 2, StartZ, overlayError)
                      && QImage(overlayPath(clientRouteDir, StartX + 2, StartZ)).width() == 4
                      && !QFileInfo::exists(overlayPath(routeDir, StartX + 2, StartZ)),
              "and one it makes is saved into its own folder (" + overlayError + ")");
        bool overlaysCompared = false;
        for (ErrorMessage *message : std::as_const(ErrorMessagesLib::ErrorMessages))
            overlaysCompared = overlaysCompared
                    || (message != NULL && message->action.contains("TERRAIN_MAPS", Qt::CaseInsensitive));
        check(!overlaysCompared, "they are not compared with the server's");
        TerrainOverlays::clear();
        Tile *tile = route->tile.value(key, NULL);
        auto object = [&](unsigned int uid) -> WorldObj * {
            if (tile == NULL)
                return NULL;
            for (auto &entry : tile->obiekty)
                if (entry.second != NULL && entry.second->UiD == uid && entry.second->loaded)
                    return entry.second;
            return NULL;
        };
        check(object(uidA) != NULL && object(uidB) != NULL && object(uidC) != NULL, "with the objects edited before");
        // Its ID blocks: taken in order, the next asked for at half, waited
        // for when none is left.
        const int heldNodes = client->idsHeld(NetworkMessage::IdKind::TdbNode);
        check(heldNodes == 256 && client->idsHeld(NetworkMessage::IdKind::Uid, StartX, StartZ) == 256,
              "the client holds a block of node IDs, and its tile's UiDs");
        QSet<int> takenIds;
        for (int i = 0; i < heldNodes; i++)
            takenIds.insert(client->takeId(NetworkMessage::IdKind::TdbNode));
        check(takenIds.size() == heldNodes && !takenIds.contains(-1), "taken, each once");
        const int afterBlock = client->takeId(NetworkMessage::IdKind::TdbNode);
        check(afterBlock > 0 && !takenIds.contains(afterBlock) && !client->waitingForIds()
                      && client->idsHeld(NetworkMessage::IdKind::TdbNode) == 255,
              "none left: the next comes from the next block (asked for at half, waited for)");
        const int sentBefore = client->sentEdits;
        WorldObj *seven = object(uidA);
        alice.clear();
        alice.send(objectRecord(StartX, StartZ, uidA, 33));
        alice.send("update_trnode_tdb ( id ( 2 ) remove ( ) )\n");
        check(waitFor([&] { WorldObj *o = object(uidA); return o != NULL && std::fabs(o->position[0] - 33) < 0.01f; }, 5000)
                      && waitFor([&] { return Game::trackDB->trackNodes[2] == NULL; }, 5000),
              "another user's object and node applied");
        check(seven != NULL && replacedObjects.contains(seven) && object(uidA) != seven,
              "the object it replaced is reported (the editor drops a selection holding it)");
        settle();
        check(client->sentEdits == sentBefore && alice.count("update_worldobj") == 0 && alice.count("update_trnode_tdb") == 0,
              "the receive rule: applying them sends nothing back");
        // A new node of another user's block: above this client's count.
        const int othersNode = alice.ids("tdb_node").value(5);
        alice.send(endNodeRecord(othersNode));
        check(waitFor([&] {
                  auto found = Game::trackDB->trackNodes.find(othersNode);
                  return found != Game::trackDB->trackNodes.end() && found->second != NULL;
              }, 5000) && Game::trackDB->iTRnodes >= othersNode,
              "another user's new node, above this client's count, taken in");
        WorldObj *own = object(uidB);
        if (own != NULL)
            own->setModified();
        check(alice.waitText("update_worldobj") && alice.last("update_worldobj").contains(uidText(uidB))
                      && client->sentEdits == sentBefore + 1,
              "the user's own edit is sent, and forwarded");
        // Two users editing one object at once (task server 02's review):
        // another user's change to it read while this client's own edit of
        // it is unanswered is not applied; the server applies this
        // client's after it, so both end as the server has it.
        settle();
        if (WorldObj *mine = object(uidB)) {
            const int skippedBefore = client->skippedChanges;
            mine->position[0] = 21;
            mine->setModified();
            client->processBinaryMessage(NetworkMessage::frame(NetworkMessage::utf16(objectRecord(StartX, StartZ, uidB, 44)), 0, 0));
            WorldObj *now = object(uidB);
            check(client->skippedChanges == skippedBefore + 1 && now != NULL && std::fabs(now->position[0] - 21) < 0.01f,
                  "another user's change to an object this client's unanswered edit changes: not applied");
            alice.clear();
            check(alice.waitText("update_worldobj") && alice.last("update_worldobj").contains("Position ( 21 "),
                  "the server has this client's, as the client does");
            settle();
            client->processBinaryMessage(NetworkMessage::frame(NetworkMessage::utf16(objectRecord(StartX, StartZ, uidB, 55)), 0, 0));
            now = object(uidB);
            check(client->skippedChanges == skippedBefore + 1 && now != NULL && std::fabs(now->position[0] - 55) < 0.01f,
                  "once answered, the next change to it is applied");
        }
        // Put right: the client's node 3 lost here only, then a refused
        // record of it: the server's node comes back with the refusal.
        Game::trackDB->trackNodes[3] = NULL;
        const int refusalsBefore = client->refusals;
        client->sendEditMessage("update_trnode_tdb ( id ( 3 ) )\n");
        check(waitFor([&] { return client->refusals > refusalsBefore && Game::trackDB->trackNodes[3] != NULL; }, 5000)
                      && Game::trackDB->trackNodes[3]->typ == 1,
              "a refused edit put right on the client: it has the server's node again");
        route->requestTile(StartX + 3, StartZ, false);
        check(waitFor([&] { return route->tileMissing(StartX + 3, StartZ); }, 5000), "a tile the server has not got: marked");
        const int noticesBefore = notices.size();
        check(route->requestTile(StartX + 3, StartZ, true) == NULL && notices.size() == noticesBefore + 1
                      && notices.last().contains("cannot make tiles"),
              "placing on it refused, and the user told");
        check(route->newTile(StartX + 4, StartZ) == 0 && notices.size() == noticesBefore + 2, "a new tile refused on a client");
    }

    // An edit nobody saves: the client has it, the next server will not.
    QByteArray savedHeights;
    if (initDone) {
        Terrain *t = Game::terrainLib->getTerrainByXY(StartX, StartZ);
        QByteArray unsaved = heights;
        float first;
        std::memcpy(&first, unsaved.constData(), 4);
        first += 7.0f;
        std::memcpy(unsaved.data(), &first, 4);
        savedHeights = heights;
        alice.sendBinary(terrainEdit(TS::TSRE_Terrain_RawFile, StartX, StartZ, unsaved));
        check(t != NULL && waitFor([&] { return terrainHeights(t) == unsaved; }, 5000),
              "another user's unsaved height edit reaches the client");
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
            if (entry.second != NULL && int(entry.second->UiD) == uidC) {
                entry.second->setModified();
                break;
            }
    check(client->droppedEdits == droppedBefore + 1 && client->sentEdits >= 0, "an edit while disconnected is not sent");

    ServerProcess second;
    check(second.start(serverRoot, port, {"--set", "core.network.serverAuthenticationMode=file",
                                         "--settings", serverRoot + "/profile/settings.json"}, true),
          "a server starts on the saved route");
    check(second.waitOutput("Loading the users file: " + serverRoot + "/profile/users.json", 0, 5000)
                  && second.waitOutput("user \"jan.k\" cannot log in", 0, 5000) && second.waitOutput("3 users", 0, 5000),
          "its users from users.json in its profile's folder (a name that could never log in said)");
    check(second.waitOutput("console input ended", 0, 5000), "with its input closed, it runs on");
    // The 3D view sends its pointer every frame: nothing before the welcome.
    int pointerStep = 0;
    check(waitFor([&] {
              client->updatePointerPosition(StartX, StartZ, float(++pointerStep), 1.0f, 2.0f);
              return reconnects > 0;
          }, 40000),
          "the client reconnects on its own and logs in again (its pointer sent only after the welcome)");
    check(waitFor([&] { return routeRefreshed > 0; }, 20000) && routeReplacing == 1 && switches == 0,
          "it refreshes its route in place (no restart, no switch)");
    check(refreshEditsTried > 0 && refreshEditsDropped == refreshEditsTried && refreshesShownEditable == 0 && lastConnected,
          "while it refreshes nothing is editable (an edit not sent), then it is again");
    if (routeRefreshed > 0 && route != NULL) {
        const int key = StartX * 10000 + StartZ;
        QList<int> ids;
        for (const auto &entry : Game::trackDB->trackNodes)
            if (entry.second != NULL)
                ids << entry.first;
        std::sort(ids.begin(), ids.end());
        check(Game::trackDB->iTRnodes == 8 && ids == QList<int>({1, 2, 3, 4, 5, 6, 7, 8})
                      && !route->refreshingFromServer,
              "its TDB is the restarted server's: the saved file's numbers, node 2's unsaved removal gone");
        Tile *tile = route->tile.value(key, NULL);
        WorldObj *seven = NULL;
        if (tile != NULL)
            for (auto &entry : tile->obiekty)
                if (entry.second != NULL && int(entry.second->UiD) == uidA && entry.second->loaded)
                    seven = entry.second;
        check(replacedTiles.contains(key) && seven != NULL && std::fabs(seven->position[0] - 10) < 0.01f,
              "its tile is the server's: the unsaved move of object 7 gone");
        Terrain *t = Game::terrainLib->getTerrainByXY(StartX, StartZ);
        check(t != NULL && terrainHeights(t) == savedHeights, "its terrain is the server's: the unsaved height edit gone");
    }
    client->disconnectNow();
    settle(500);
    const long long ticksBefore = second.cpuTicks();
    settle(3000);
    const long long ticksAfter = second.cpuTicks();
    qInfo().noquote() << "[tests:server] idle server, input closed:" << (ticksAfter - ticksBefore) << "ticks of 300";
    check(ticksBefore >= 0 && ticksAfter - ticksBefore <= 3, "and idles under 1 % of a processor");

    ScriptedClient wrong, bob, first, replay;
    check(!wrong.login(port, "alice", "nothing") && wrong.count("auth_fail") == 1
                  && waitFor([&] { return wrong.closed; }, 5000) && wrong.count("welcome") == 0,
          "a wrong password is refused and closed");
    ScriptedClient fromRoute;
    check(!fromRoute.login(port, "alice", "fromroute") && fromRoute.count("auth_fail") == 1,
          "the route folder's old users.txt is not read");
    // An answer seen on the wire, given to another challenge.
    check(first.hello(port, "bob") && replay.hello(port, "bob"), "two hellos, two challenges");
    replay.answer(NetworkMessage::loginAnswer("pw2", first.nonce(), "bob"));
    check(first.nonce() != replay.nonce() && replay.waitText("auth_fail") && replay.count("welcome") == 0,
          "an answer to another challenge is refused (no replay)");
    check(bob.login(second.port, "bob", "pw2"), "a user in the users file logs in");
    check(!bob.sent.contains("pw2") && !bob.sent.contains(NetworkMessage::utf16("pw2").mid(2)),
          "its password is in nothing it sent");
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

    // ID ranges on the restarted server: alice and bob editing at once.
    {
        check(bob.ids("tdb_item").value(0) == 0 && bob.ids("tdb_item").value(1) == 3,
              "free item slots first: the saved emptyitem 0, then the reconnected client's returned ones");
        ScriptedClient a2;
        check(a2.login(second.port, "alice", "secret"), "another user logs in");
        a2.send(QString("request_tile ( %1 %2 )\n").arg(StartX).arg(StartZ));
        bob.send(QString("request_tile ( %1 %2 )\n").arg(StartX).arg(StartZ));
        check(a2.waitText("requested_tile") && bob.waitText("requested_tile"), "both have the tile");
        check(disjoint(a2.ids("tdb_item"), bob.ids("tdb_item")) && disjoint(a2.ids("tdb_node"), bob.ids("tdb_node"))
                      && disjoint(a2.ids("uid"), bob.ids("uid")) && disjoint(a2.ids("section"), bob.ids("section")),
              "their blocks never overlap");
        const QString platform = itemRecord(loadedTdb, 1);
        auto newItem = [&](int id) {
            QString item = platform;
            item.replace("TrItemId ( 1 )", QString("TrItemId ( %1 )").arg(id));
            return QString("update_tritem_tdb ( id ( %1 ) %2 )\n").arg(id).arg(item);
        };
        check(!platform.isEmpty(), "an item's record to copy");
        a2.clear();
        bob.clear();
        // Bob takes the free slot 0; alice the first of hers.
        bob.send(newItem(0));
        const int aliceItem = a2.ids("tdb_item").value(0);
        a2.send(newItem(aliceItem));
        check(bob.waitText("done") && a2.waitText("done") && a2.waitText("update_tritem_tdb") && bob.waitText("update_tritem_tdb"),
              "new items with their senders' IDs: applied, forwarded");
        bob.send("request_tdb ( )\n");
        check(bob.waitText("requested_tdb"), "the TDB with them");
        const QString withItems = bob.last("requested_tdb");
        bool gapsEmpty = withItems.contains(QString("TrItemTable ( %1").arg(aliceItem + 1));
        for (int id = 3; id < aliceItem; id++)
            gapsEmpty = gapsEmpty && QRegularExpression(QString("EmptyItem \\(\\s*TrItemId \\( %1 \\)").arg(id)).match(withItems).hasMatch();
        check(gapsEmpty && itemRecord(withItems, 0).startsWith("PlatformItem") && itemRecord(withItems, aliceItem + 1).isEmpty(),
              "the table ends at the last used item; the unused IDs between are emptyitems, slot 0 is the new item");
        bob.clear();
        bob.send(newItem(a2.ids("tdb_item").value(1)));
        check(bob.waitText("error") && bob.last("error").contains("not one of this user's") && bob.waitText("update_tritem_tdb")
                      && bob.last("update_tritem_tdb").contains("remove ( )"),
              "a new item with another user's ID: refused, put right");
        const int aliceNode = a2.ids("tdb_node").value(0);
        a2.clear();
        bob.clear();
        a2.send(endNodeRecord(aliceNode));
        check(a2.waitText("done") && bob.waitText("update_trnode_tdb"), "a new node with its sender's ID: applied, forwarded");
        bob.send(endNodeRecord(a2.ids("tdb_node").value(1)));
        check(bob.waitText("error") && bob.last("error").contains("not one of this user's"), "with another user's: refused");
        const int aliceSection = a2.ids("section").value(0);
        a2.clear();
        bob.clear();
        a2.send(QString("update_tracksection ( TrackSection ( SectionCurve ( 0 ) %1 20 1.5 ) )\n").arg(aliceSection));
        check(a2.waitText("done") && bob.waitText("update_tracksection"), "a new track section with its sender's ID: applied");
        bob.send(QString("update_tracksection ( TrackSection ( SectionCurve ( 0 ) %1 20 1.5 ) )\n").arg(aliceSection + 2));
        check(bob.waitText("error") && bob.last("error").contains("not one of this user's"), "with another user's: refused");
        check(bob.waitText("remove_tracksection") && bob.last("remove_tracksection").contains(QString::number(aliceSection + 2)),
              "and put right: the server has none, its removal back (the review)");
        // At most 8 blocks of a kind unused (the review: one client took
        // every UiD of a tile): asked for 10 more at once, 7 given.
        bob.clear();
        const int bobBlocks = bob.ids("tdb_node").size() / 256;
        bob.send(QString("request_ids ( tdb_node )\n").repeated(10));
        const bool refusedMore = bob.waitText("error") && bob.last("error").contains("unused");
        settle(300);
        check(refusedMore && bob.ids("tdb_node").size() == 8 * 256 && bobBlocks == 1 && bob.count("error") == 3,
              "IDs asked for while 8 blocks are unused: refused");
        // A vector node on a route section the server has not got: refused
        // and put right (the review: a node on sections refused was taken).
        bob.clear();
        const int bobNode = bob.ids("tdb_node").value(0);
        bob.send(QString("update_trnode_tdb ( id ( %1 ) TrackNode ( %1 TrVectorNode ( TrVectorSections ( 1 %2 0 %3 %4 1 0 1 00 %3 %4 0 0 0 0 0 0 ) ) "
                         "TrPins ( 1 1 TrPin ( 1 1 ) TrPin ( 1 1 ) ) ) )\n").arg(bobNode).arg(aliceSection + 4).arg(StartX).arg(StartZ));
        check(bob.waitText("error") && bob.last("error").contains("is not in the route's tsection")
                      && bob.waitText("update_trnode_tdb") && bob.last("update_trnode_tdb").contains("remove ( )"),
              "a vector node on a route section the server has not got: refused, put right");
        // A user renamed at its login (its name in use) logs in again by
        // its own name (the review: it logged in by the shown name and was
        // refused).
        {
            ScriptedClient holder;
            check(holder.login(second.port, "real", "realpw"), "the client's user logged in elsewhere");
            QString failed;
            const QMetaObject::Connection watch =
                    QObject::connect(client, &RouteEditorClient::loginFailed, [&](QString why) { failed = why; });
            int refreshed = routeRefreshed;
            client->connectNow();
            check(waitFor([&] { return routeRefreshed > refreshed; }, 20000) && client->username == "real2",
                  "the client logs in: shown as real2");
            client->disconnectNow();
            settle(300);
            // A tile asked for while the link was down (its answer lost):
            // asked for again with the refresh (the review: it stayed empty).
            const int key = StartX * 10000 + StartZ;
            if (route != NULL) {
                route->tile.remove(key);
                route->requestTile(StartX, StartZ, false);
            }
            const Tile *waiting = route != NULL ? route->tile.value(key, NULL) : NULL;
            const bool asked = waiting != NULL && waiting->loaded == -2;
            refreshed = routeRefreshed;
            client->connectNow();
            check(waitFor([&] { return routeRefreshed > refreshed; }, 20000) && failed.isEmpty()
                          && client->state() == RouteEditorClient::State::Connected,
                  "and again, by its own name");
            check(asked && waitFor([&] { const Tile *t = route->tile.value(key, NULL); return t != NULL && t->loaded == 1; }, 5000),
                  "a tile asked for while disconnected: asked for again, and loaded");
            QObject::disconnect(watch);
            client->disconnectNow();
            settle(300);
        }
        a2.clear();
        bob.clear();
        a2.send(objectRecord(StartX, StartZ, a2.ids("uid").value(0), 40));
        check(a2.waitText("done") && bob.waitText("update_worldobj"), "a new object with its sender's UiD: applied, forwarded");
        // Bob goes: his unused item IDs are given again, first.
        const QList<int> bobItems = bob.ids("tdb_item");
        bob.socket.close();
        check(waitFor([&] { return bob.closed; }, 5000), "a user disconnects");
        settle(300);
        ScriptedClient next;
        check(next.login(second.port, "alice", "secret") && next.ids("tdb_item").value(0) == bobItems.value(1),
              "the next user gets his unused item IDs first");
    }
    second.stop();

    // The server started again with another route: the client asks for it
    // to be loaded in place (task editor 10), refreshing nothing.
    {
        const QString otherRoute = RouteName + "B";
        check(copyTree(routeDir, serverRoot + "/ROUTES/" + otherRoute), "another route on the server");
        ServerProcess third;
        check(third.start(serverRoot, port, {}, true, otherRoute), "a server starts with it");
        const int replacingBefore = routeReplacing;
        switches = 0;
        client->connectNow();
        check(waitFor([&] { return switches > 0; }, 20000) && switchedTo == otherRoute
                      && routeReplacing == replacingBefore,
              "the client sees another route served: it asks for that route to be loaded in place, refreshing nothing");
        // Loaded, as the editor's window does (task editor 10): the other
        // route's files from the server (task server 03).
        if (switches > 0) {
            QDir().mkpath(clientRoot + "/ROUTES/" + otherRoute + "/TEXTURES");
            RouteSwitch::leave();
            check(RouteFiles::serving() && RouteFiles::servedNames().isEmpty(), "leaving: the old route's files dropped");
            Game::route = otherRoute;
            initDone = false;
            route = new RouteClient();
            QObject::connect(route, &RouteClient::initDone, [&] { initDone = true; });
            route->load();
            check(waitFor([&] { return initDone; }, 30000)
                          && RouteFiles::exists(clientRoot + "/ROUTES/" + otherRoute + "/sigcfg.dat")
                          && !RouteFiles::servedNames().filter("ROUTES/" + RouteName + "/").size()
                          && Game::trackDB != NULL && Game::trackDB->sigCfg != NULL
                          && Game::trackDB->sigCfg->signalType.size() == local.signalTypes,
                  "the other route loads from its own files from the server");
        }
        client->disconnectNow();
        third.stop();
    }

    const QStringList clientFilesAfter = snapshot(clientRoot + "/ROUTES/" + RouteName);
    check(!clientFilesBefore.isEmpty() && clientFilesAfter == clientFilesBefore,
          "the client wrote nothing into its route folder but its own map overlays (same files, sizes, times)");

    if (keep) {
        QFile first(workspace.filePath("server-1.txt"));
        if (first.open(QIODevice::WriteOnly))
            first.write(server.output.toUtf8());
        QFile next(workspace.filePath("server-2.txt"));
        if (next.open(QIODevice::WriteOnly))
            next.write(second.output.toUtf8());
    }
    Game::serverClient = NULL;
    // A local route again for the suites after this one.
    RouteFiles::clearServed();
    Game::root = rootWas;
    Game::route = routeWas;
    Game::writeEnabled = writeWas;
    qInfo() << "[tests:server] cases=" << passed + failed << "passed=" << passed << "failed=" << failed;
    return failed ? 1 : 0;
}


int TsreTests::runServerSimulationSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:server-simulation] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:server-simulation] FAIL" << name; }
    };
    const QString root = Game::root, routeName = Game::route;
    const QString activityFile = qEnvironmentVariable("TSRE_SIM_BASELINE_ACTIVITY").section(',', 0, 0).trimmed();
    if (!Game::checkRoot(root) || routeName.isEmpty() || activityFile.isEmpty()) {
        qWarning() << "server-simulation needs --game-root, --route and TSRE_SIM_BASELINE_ACTIVITY";
        return 1;
    }
    // The server: the route read, nothing written (no edits, no save).
    ServerProcess server;
    const int port = freePort();
    check(server.start(root, port, {}, false, routeName), "the server starts on " + routeName);
    int from = server.output.size();
    server.command("run " + activityFile);
    const bool started = server.waitOutput("Run of " + activityFile, from, 120000);
    const QString runLine = server.output.mid(server.output.indexOf("Run of " + activityFile, from)).section('\n', 0, 0);
    check(started, "console: " + runLine);

    // A real client.
    Game::root = root;
    Game::route.clear();
    Game::gui = false;
    Game::currentShapeLib = new ShapeLib();
    Game::currentEngLib = new EngLib();
    Undo::UndoEnabled = false;
    RouteEditorClient *client = new RouteEditorClient(QString("ws://127.0.0.1:%1").arg(port), "real", "realpw");
    Game::serverClient = client;
    RouteClient *route = nullptr;
    bool initDone = false;
    QStringList notices;
    QObject::connect(client, &RouteEditorClient::loadRoute, [&] {
        route = new RouteClient();
        QObject::connect(route, &RouteClient::initDone, [&] { initDone = true; });
        route->load();
    });
    QObject::connect(client, &RouteEditorClient::noticeShown, [&](QString text) { notices << text; });
    int reconnects = 0;
    QObject::connect(client, &RouteEditorClient::reconnected, [&] { reconnects++; });
    client->connectNow();
    check(waitFor([&] { return initDone; }, 120000), "the client loads the route from the server");
    Sim::SimulationController *view = Sim::SimulationController::instance();
    const bool shown = waitFor([&] { return view->exists(); }, 180000);
    auto *link = dynamic_cast<Sim::RemoteSimulationLink *>(view->linkInUse());
    check(shown, QString("the client makes its own input from the run's data (%1 ms), the same digest: the run "
                         "shown%2").arg(link != nullptr ? link->inputMilliseconds() : -1)
                         .arg(notices.isEmpty() ? QString() : ": " + notices.join("; ")));
    check(link != nullptr && !view->controlsRun() && view->input() != nullptr,
          "the view's link is the server's; the client does not control the run");
    if (!view->exists() || link == nullptr) {
        server.stop();
        qInfo().noquote() << QString("server-simulation: %1 passed, %2 failed").arg(passed).arg(failed + 1);
        return 1;
    }
    QString error;
    const bool refused = !view->runTo(view->activity(), 12 * 3600, &error);
    check(refused && !error.isEmpty(), "a client's Run to is refused: " + error);
    // Activity Tools on the client: the run's activity or timetable set
    // listed and chosen (its kind with it), as the map and the windows
    // follow the activity chosen; run control off, Drive on.
    {
        ActivityTools tools("ActivityTools");
        tools.routeLoaded(route);
        emit view->stateChanged();
        QPushButton *play = tools.findChild<QPushButton *>("simulationPlay");
        QPushButton *drive = tools.findChild<QPushButton *>("simulationDrive");
        QPushButton *runTo = tools.findChild<QPushButton *>("simulationRunTo");
        check(route->getCurrentActivity() == view->activity()
                  && ActivityNotifier::timetables() == view->activity()->isTimetables() && play != nullptr
                  && drive != nullptr && runTo != nullptr && !play->isEnabled() && !runTo->isEnabled()
                  && drive->isEnabled(),
              "Activity Tools on the client: the run's activity chosen, run control off, Drive on");
    }

    // Frames at the run's paused times, against a local run of the same
    // activity with the run's options (the test's process: the same TDB).
    const double start = view->input()->startTime;
    const auto at = [](double seconds) {
        return QTime::fromMSecsSinceStartOfDay(int(std::fmod(seconds, 86400.0) * 1000.0)).toString("HH:mm:ss");
    };
    const auto paused = [&](double target) {
        return waitFor([&] {
            const std::shared_ptr<const Sim::SimulationFrame> f = view->frame();
            return f != nullptr && f->mode == Sim::RunMode::Paused && f->time >= target - 1e-6;
        }, 120000);
    };
    const std::vector<double> after = {60.0, 300.0, 900.0};
    QStringList remote;
    for (double seconds : after) {
        server.command("runto " + at(start + seconds));
        if (paused(start + seconds))
            remote << TsreTests::SimulationFrameText(*view->frame());
        else
            remote << QString();
    }
    {
        Sim::SimulationSession local;
        QStringList localText;
        if (local.start(view->activity(), link->runOptions(), &error)) {
            for (double seconds : after) {
                local.runTo(start + seconds, false, -1, &error);
                waitFor([&] {
                    local.update();
                    const std::shared_ptr<const Sim::SimulationFrame> f = local.frame();
                    return f != nullptr && f->acknowledged >= local.lastCommand() && f->mode == Sim::RunMode::Paused;
                }, 120000);
                localText << TsreTests::SimulationFrameText(*local.frame());
            }
            local.stop();
        }
        int same = 0;
        for (int i = 0; i < remote.size() && i < localText.size(); ++i)
            same += !remote[i].isEmpty() && remote[i] == localText[i] ? 1 : 0;
        check(same == int(after.size()),
              QString("the client's frames at +60, +300 and +900 s (run to from the console) are a local run's, number "
                      "for number (%1 of %2)").arg(same).arg(after.size()));
    }

    // A second client joining: the announcement, then a whole frame once it
    // says its input is the same.
    ScriptedClient second;
    check(second.login(port, "second"), "a second client logs in during the run");
    const auto records = [&](Sim::Wire::Kind wanted) {
        QList<QByteArray> out;
        for (const QByteArray &payload : second.binaries) {
            Sim::Wire::Kind kind;
            QByteArray body;
            if (Sim::Wire::ParseRecord(payload, kind, body) && kind == wanted)
                out << body;
        }
        return out;
    };
    check(waitFor([&] { return !records(Sim::Wire::Kind::Run).isEmpty(); }, 10000),
          "it gets the run's announcement");
    Sim::Wire::Announcement announced;
    if (!records(Sim::Wire::Kind::Run).isEmpty())
        Sim::Wire::DecodeAnnouncement(records(Sim::Wire::Kind::Run).last(), announced);
    check(announced.runId == link->runId() && announced.digest == view->input()->digest()
              && !announced.data.empty(),
          QString("its run, digest and data (%1 files) are the real client's").arg(announced.data.files.size()));
    second.sendBinary(Sim::Wire::Record(Sim::Wire::Kind::Ready, Sim::Wire::EncodeReady(announced.runId, true, QString())));
    check(waitFor([&] { return !records(Sim::Wire::Kind::Frame).isEmpty(); }, 10000),
          "joining a paused run, it gets the run's frame");
    server.command("play");
    check(waitFor([&] { return records(Sim::Wire::Kind::Frame).size() >= 10; }, 10000), "it gets frames once ready");
    {
        Sim::Wire::FrameDecoder decoder;
        decoder.setInput(view->input());
        const QList<QByteArray> frames = records(Sim::Wire::Kind::Frame);
        int decoded = 0;
        for (const QByteArray &frame : frames)
            decoded += decoder.decode(frame, Sim::SteadyNs()) != nullptr ? 1 : 0;
        check(!frames.isEmpty() && decoded == frames.size(),
              QString("its first frame whole, then changes: %1 of %2 decoded").arg(decoded).arg(frames.size()));
    }
    server.command("pause");
    // Asking again for a whole frame (one that did not follow on): the
    // frame, not the run's events again (the review: they came twice).
    {
        const auto eventsGot = [&] {
            size_t count = 0;
            for (const QByteArray &body : records(Sim::Wire::Kind::Events)) {
                std::vector<Sim::TrainEvent> events;
                if (Sim::Wire::DecodeEvents(body, events))
                    count += events.size();
            }
            return count;
        };
        settle(500);
        const size_t eventsBefore = eventsGot();
        const int framesBefore = records(Sim::Wire::Kind::Frame).size();
        second.sendBinary(Sim::Wire::Record(Sim::Wire::Kind::Ready, Sim::Wire::EncodeReady(announced.runId, true, QString())));
        const bool wholeAgain = waitFor([&] { return records(Sim::Wire::Kind::Frame).size() > framesBefore; }, 5000);
        settle(300);
        check(wholeAgain && eventsBefore > 0 && eventsGot() == eventsBefore,
              QString("asked again, a whole frame; its %1 events not sent again").arg(eventsBefore));
    }

    // A client whose input differs (a vehicle file of its own, an older
    // timetable): its link refuses the run, naming the train, and the
    // server's console says why. The announcement's digests changed here.
    {
        Sim::Wire::Announcement other = announced;
        other.digest.fill('x');
        if (!other.trainDigests.empty())
            other.trainDigests.front().fill('x');
        QList<QByteArray> sent;
        QStringList notices;
        Sim::RemoteSimulationLink refusing([&](const QByteArray &payload, bool) { sent << payload; },
                                           [&](const QString &text) { notices << text; });
        refusing.received(Sim::Wire::Kind::Run, Sim::Wire::EncodeAnnouncement(other));
        unsigned int refusedRun = 0;
        bool same = true;
        QString why;
        Sim::Wire::Kind kind = Sim::Wire::Kind::Run;
        QByteArray body;
        const bool replied = sent.size() == 1 && Sim::Wire::ParseRecord(sent.front(), kind, body)
                && kind == Sim::Wire::Kind::Ready && Sim::Wire::DecodeReady(body, refusedRun, same, why);
        const QString first = !view->input()->trains.empty() ? Sim::TrainName(*view->input(), 0) : QString("-");
        check(replied && !same && refusedRun == announced.runId && !refusing.exists() && why.startsWith("1 of its")
                  && why.contains(first) && notices.size() == 1 && notices.front().contains(why),
              "a client whose input differs refuses the run, naming the train: " + why);
        ScriptedClient third;
        const int outputFrom = server.output.size();
        const bool in = third.login(port, "third");
        if (in && !sent.isEmpty())
            third.sendBinary(sent.front());
        check(in && server.waitOutput("third cannot show the run: " + why, outputFrom, 10000),
              "the server's console says why");
    }

    // A run announced while a client's route still loads, ended before it is
    // taken on: not taken on (the review: a dead run was shown, editing
    // locked, until the server's next run).
    {
        QList<QByteArray> sent;
        Sim::RemoteSimulationLink loading([&](const QByteArray &payload, bool) { sent << payload; }, [](const QString &) {});
        Game::currentRoute->loaded = false;
        loading.received(Sim::Wire::Kind::Run, Sim::Wire::EncodeAnnouncement(announced));
        loading.received(Sim::Wire::Kind::End, Sim::Wire::EncodeEnd(announced.runId, "stopped"));
        Game::currentRoute->loaded = true;
        settle(800);
        check(!loading.exists() && sent.isEmpty(), "a run ended while a client's route still loads: not taken on");
    }

    // Driving: the player's train, else the first on the track an engine
    // drives (a timetable's).
    int train = view->trainOf(true, -1);
    const std::shared_ptr<const Sim::SimulationFrame> now = view->frame();
    for (int t = 0; (train < 0 || !view->drivable(train)) && now != nullptr && t < int(now->trains.size()); ++t)
        if (view->drivable(t) && now->trains[size_t(t)].state.status != Sim::TrainStatus::Waiting
                && now->trains[size_t(t)].state.status != Sim::TrainStatus::Finished)
            train = t;
    error.clear();
    const bool drives = train >= 0 && view->drive(train, &error);
    check(drives, "the client drives a train" + (error.isEmpty() ? QString() : ": " + error));
    check(waitFor([&] { return view->frame() != nullptr && view->frame()->driven == train; }, 15000),
          QString("the server's frames show it driven (train %1)").arg(train));
    check(waitFor([&] { return view->driven() == train; }, 5000), "this client drives it (the server told it so)");
    // A train no engine drives: refused, nobody named its driver (the
    // review: the sender became the driver all the same).
    {
        int engineless = -1;
        for (int t = 0; engineless < 0 && t < int(view->input()->trains.size()); ++t)
            if (!view->drivable(t))
                engineless = t;
        if (engineless >= 0) {
            ScriptedClient tries;
            check(tries.login(port, "tries"), "another user logs in");
            tries.sendBinary(Sim::Wire::Record(Sim::Wire::Kind::Drive, Sim::Wire::EncodeTrains(announced.runId, {engineless})));
            check(tries.waitText("error") && tries.last("error").contains("no engine drives"),
                  "a Drive of a train no engine drives: refused");
        } else {
            qInfo().noquote() << "[tests:server] every train of the run has an engine: no Drive of one without";
        }
    }
    from = second.texts.size();
    second.sendBinary(Sim::Wire::Record(Sim::Wire::Kind::Drive, Sim::Wire::EncodeTrains(announced.runId, {train})));
    check(waitFor([&] {
              for (int i = from; i < second.texts.size(); ++i)
                  if (second.texts[i].contains("driven by real"))
                      return true;
              return false;
          }, 5000),
          "a second Drive is refused: driven by real");
    server.command("play");
    const unsigned long long driveTaken = view->lastCommand();
    view->command("throttle", 0.5);
    // Its effect: the vehicles' throttle moved (to a notch near a half on a
    // notched one; the review: the check passed on the Drive's sequence
    // alone).
    const auto throttled = [&] {
        const std::shared_ptr<const Sim::SimulationFrame> f = view->frame();
        if (f == nullptr || f->drivenValues == nullptr)
            return false;
        for (const auto &values : f->drivenValues->values) {
            const auto found = values.constFind(QStringLiteral("throttle"));
            if (found != values.constEnd() && std::holds_alternative<double>(found.value())
                    && std::get<double>(found.value()) > 0.25 && std::get<double>(found.value()) < 0.75)
                return true;
        }
        return false;
    };
    const bool applied = waitFor([&] { return view->lastCommand() > driveTaken && throttled(); }, 10000);
    check(applied, QString("a command taken (its sequence %1, after the Drive's %2) and applied: the driven train's throttle moved")
                           .arg(view->lastCommand()).arg(driveTaken));
    view->leave();
    check(waitFor([&] { return view->frame() != nullptr && view->frame()->driven == -1; }, 10000),
          "Leave: the train given back to the AI");

    // Hearing: the second client hears a train, every frame carries its
    // shadow's values.
    second.sendBinary(Sim::Wire::Record(Sim::Wire::Kind::Hear, Sim::Wire::EncodeTrains(announced.runId, {train})));
    check(waitFor([&] {
              const std::shared_ptr<const Sim::SimulationFrame> f = view->frame();
              return f != nullptr && !f->heard.empty() && f->heard.front().train == train;
          }, 10000),
          "a train another client hears runs its shadow on the server (its values in the frames)");
    // The second client drives, then goes: the train back to the AI.
    second.sendBinary(Sim::Wire::Record(Sim::Wire::Kind::Drive, Sim::Wire::EncodeTrains(announced.runId, {train})));
    check(waitFor([&] { return view->frame() != nullptr && view->frame()->driven == train; }, 10000),
          "the second client drives it");
    settle(500);
    check(view->driven() == -1 && view->frame()->driven == train,
          "this client, watching, does not: no keys, no cab, Drive offered (the review: every watcher was taken into its cab)");
    from = server.output.size();
    second.socket.close();
    check(server.waitOutput("gone: its train given back to the AI", from, 10000)
              && waitFor([&] { return view->frame() != nullptr && view->frame()->driven == -1; }, 10000),
          "its disconnect gives the train back");

    // The bytes a second while it plays in real time.
    server.command("speed 1");
    server.command("play");
    settle(1000);
    const qint64 bytes = link->bytesReceived(), wire = link->wireBytes();
    const int frames = link->framesDecoded();
    settle(5000);
    const double kbs = (link->bytesReceived() - bytes) / 1024.0 / 5.0;
    const double wireKbs = (link->wireBytes() - wire) / 1024.0 / 5.0;
    const double fps = (link->framesDecoded() - frames) / 5.0;
    check(fps > 15.0, QString("playing in real time: %1 frames a second, %2 KB/s as sent (%3 KB/s uncompressed) for "
                              "the client").arg(fps, 0, 'f', 1).arg(wireKbs, 0, 'f', 1).arg(kbs, 0, 'f', 1));

    // Reconnecting: the same run, its input kept, frames again.
    const unsigned int runBefore = link->runId();
    // It hears a train: heard again after the reconnect (the review: the
    // server's new connection heard nothing).
    link->hear({train});
    check(waitFor([&] { return view->frame() != nullptr && !view->frame()->heard.empty(); }, 10000), "the client hears a train");
    client->disconnectNow();
    settle(500);
    client->connectNow();
    check(waitFor([&] { return reconnects > 0 || client->isConnected(); }, 30000)
              && waitFor([&] { return view->exists() && link->runId() == runBefore; }, 30000),
          "a reconnect: the same run shown again, its input kept");
    const double before = view->frame() != nullptr ? view->frame()->time : 0.0;
    check(waitFor([&] { return view->frame() != nullptr && view->frame()->time > before + 1.0; }, 15000),
          "its frames go on");
    check(waitFor([&] { const auto f = view->frame(); return f != nullptr && !f->heard.empty() && f->heard.front().train == train; }, 10000),
          "and the train it heard is heard again");
    // As the editor's window does on a reconnect (its refresh replaces the
    // route): the view stops, and takes the run on again when the link
    // shows it (the review: the suite had no window, so this path was not
    // run).
    {
        const QMetaObject::Connection asWindow = QObject::connect(client, &RouteEditorClient::routeReplacing,
                [] { Sim::SimulationController::instance()->stop(); });
        bool stopped = false;
        const QMetaObject::Connection watchStop = QObject::connect(client, &RouteEditorClient::routeReplacing,
                [&] { stopped = true; });
        client->disconnectNow();
        settle(500);
        client->connectNow();
        check(waitFor([&] { return stopped; }, 30000)
                  && waitFor([&] { return view->exists() && link->runId() == runBefore && view->frame()->runId == runBefore; }, 30000)
                  && ActivityNotifier::editingLocked() && WorldClock::instance()->source() == view,
              "a reconnect as the window has it: the view stops, then takes the same run on again (its frames, clock, lock)");
        QObject::disconnect(asWindow);
        QObject::disconnect(watchStop);
    }

    // The run's end, and a new one.
    from = server.output.size();
    server.command("stop");
    check(waitFor([&] { return !view->exists(); }, 10000) && view->input() == nullptr && !ActivityNotifier::editingLocked()
              && WorldClock::instance()->source() != view,
          "console: stop, the client's view lets the run go (its input, the world's clock, the editing lock)");
    server.command("run " + activityFile);
    check(waitFor([&] {
              return view->exists() && view->input() != nullptr && link->runId() != 0 && view->frame()->runId == link->runId();
          }, 180000) && ActivityNotifier::editingLocked() && WorldClock::instance()->source() == view,
          "a new run shown (its frames, the world's clock, the editing lock)");
    from = server.output.size();
    server.command("trains");
    check(server.waitOutput(" 0  ", from, 5000), "console: trains");
    server.command("status");
    check(server.waitOutput("Run: " + activityFile, from, 5000) && server.waitOutput("real: watching", from, 5000),
          "console: status shows the run and its watcher");
    // The view lets the run go: the server sends it no more (the review).
    from = server.output.size();
    view->stop();
    check(server.waitOutput("real cannot show the run: this view let the run go", from, 5000),
          "the view lets the run go: the server is told, and sends it no more of it");

    client->disconnectNow();
    // Quit while the run plays: its thread ends first (the review).
    server.command("play");
    settle(500);
    server.command("quit");
    check(server.process.waitForFinished(10000) && server.process.exitStatus() == QProcess::NormalExit
              && server.process.exitCode() == 0,
          "console: quit while the run plays, the server ends cleanly");
    Game::serverClient = nullptr;
    delete client;
    qInfo().noquote() << QString("server-simulation: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
