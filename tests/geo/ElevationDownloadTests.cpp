#include <tsre/geo/ElevationDownload.h>
#include <QElapsedTimer>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

void runDownloadTests(const std::function<void(bool,const char*)> &check) {
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost),"local download fixture listens");
    if (!server.isListening()) return;
    enum Mode { Barrier, Mixed, Cancel, Stall, Auth, Redirect, Html };
    Mode mode = Barrier;
    int received = 0;
    QByteArray receivedAuthorization;
    bool crossOrigin = false;
    QVector<QPair<QPointer<QTcpSocket>,QByteArray>> waiting;
    std::atomic_bool cancel{false};
    const auto respond = [](QTcpSocket *socket, const QByteArray &body, int status=200) {
        socket->write("HTTP/1.1 "+QByteArray::number(status)+" Result\r\nContent-Length: "
            +QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
        socket->disconnectFromHost();
    };
    QObject::connect(&server,&QTcpServer::newConnection,&server,[&] {
        while (auto *socket = server.nextPendingConnection()) {
            QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket,bytes=QByteArray(),handled=false]() mutable {
                bytes += socket->readAll();
                if (handled || !bytes.contains("\r\n\r\n")) return;
                handled = true; ++received;
                const QByteArray path = bytes.split(' ').value(1);
                receivedAuthorization.clear();
                for (const auto &line : bytes.split('\n'))
                    if (line.toLower().startsWith("authorization:")) receivedAuthorization = line.mid(14).trimmed();
                if (mode==Barrier) {
                    waiting.push_back({socket,path});
                    // A serial downloader cannot finish this barrier. Reply in
                    // reverse order to check per-request result association.
                    if (waiting.size()==4) for (auto it=waiting.crbegin(); it!=waiting.crend(); ++it)
                        if (it->first) respond(it->first,it->second);
                } else if (mode==Mixed) {
                    if (path=="/1") respond(socket,"unavailable",503);
                    else if (path=="/3") respond(socket,QByteArray(128,'x'));
                    else respond(socket,path);
                } else if (mode==Html) {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: 28\r\nConnection: close\r\n\r\n<html>Request rejected</html>");
                    socket->disconnectFromHost();
                } else if (mode==Auth || (mode==Redirect && path=="/target")) respond(socket,"ok");
                else if (mode==Redirect) {
                    const QByteArray host = crossOrigin ? "localhost" : "127.0.0.1";
                    socket->write("HTTP/1.1 302 Found\r\nContent-Length: 0\r\nLocation: http://"
                        +host+':'+QByteArray::number(server.serverPort())+"/target\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                } else if (mode==Cancel && received==4) cancel = true;
            });
        }
    });
    QVector<QUrl> urls;
    for (int i=0; i<4; ++i) urls.push_back(QUrl(QString("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(i)));
    Elevation::DownloadLimits limits;
    limits.maxBytes = 64; limits.transferTimeoutMs = 5000; limits.deadlineMs = 5000;
    QVector<int> progress;
    auto results = Elevation::downloadWave(urls,cancel,[&](int n) { progress.push_back(n); },limits);
    bool correct = received==4 && results.size()==4;
    for (int i=0; i<4; ++i) correct &= results[i].error.isEmpty() && results[i].bytes=="/"+QByteArray::number(i);
    check(correct,"four requests reach server before any response; results retain input order");
    check(progress==QVector<int>({1,2,3,4}),"download progress counts completions monotonically");
    waiting.clear(); received = 0;
    results = Elevation::downloadWave(urls,cancel,{},limits);
    check(received==4 && results[3].bytes=="/3","second wave uses fresh working connections");
    mode = Mixed; received = 0;
    results = Elevation::downloadWave(urls,cancel,{},limits);
    check(results[0].bytes=="/0" && results[2].bytes=="/2",
        "individual HTTP failures do not discard other successful replies");
    check(results[1].bytes.isEmpty() && results[1].error.contains("503"),"HTTP failure returns no cacheable body");
    check(results[3].bytes.isEmpty() && results[3].error.contains("exceeds"),"oversized reply is rejected per request");
    mode = Cancel; received = 0;
    QElapsedTimer elapsed; elapsed.start();
    results = Elevation::downloadWave(urls,cancel,{},limits);
    correct = cancel && received==4 && elapsed.elapsed()<2000;
    for (const auto &r : results) correct &= r.bytes.isEmpty();
    check(correct,"cancellation promptly aborts all four outstanding replies");
    received = 0;
    results = Elevation::downloadWave(urls,cancel,{},limits);
    check(received==0 && results[0].bytes.isEmpty(),"pre-cancelled wave starts no HTTP requests");
    cancel = false; mode = Stall; limits.deadlineMs = 100;
    results = Elevation::downloadWave({urls[0]},cancel,{},limits);
    check(results[0].bytes.isEmpty() && results[0].error.contains("timed out"),"per-reply deadline rejects a stalled response");
    auto oversizedBatch = urls; oversizedBatch.push_back(urls[0]); received = 0;
    results = Elevation::downloadWave(oversizedBatch,cancel,{},limits);
    check(received==0 && !results[0].error.isEmpty(),"transport refuses more than four simultaneous requests");
    results = Elevation::downloadWave({QUrl()},cancel,{},limits);
    check(results[0].bytes.isEmpty() && !results[0].error.isEmpty(),"immediately invalid request completes without hanging");
    mode = Auth; limits.deadlineMs = 5000;
    const QByteArray authorization = "Basic " + QByteArray("fixture-api-key:").toBase64();
    results = Elevation::downloadWave({urls[0]},cancel,{},limits,authorization);
    check(results[0].bytes=="ok" && receivedAuthorization==authorization,"API key is sent in Authorization header");
    results = Elevation::downloadWave({urls[0]},cancel,{},limits);
    check(results[0].bytes=="ok" && receivedAuthorization.isEmpty(),"next unauthenticated wave retains no credentials");
    mode = Redirect; received = 0;
    results = Elevation::downloadWave({urls[0]},cancel,{},limits,authorization);
    check(results[0].bytes=="ok" && received==2 && receivedAuthorization==authorization,
        "authenticated requests follow same-origin redirects");
    crossOrigin = true; received = 0;
    results = Elevation::downloadWave({urls[0]},cancel,{},limits,authorization);
    check(!results[0].error.isEmpty() && received==1 && !results[0].error.contains(authorization),
        "authenticated requests reject cross-origin redirects without exposing credentials");
    mode = Html;
    results = Elevation::downloadWave({urls[0]},cancel,{},limits,authorization);
    check(results[0].bytes.isEmpty() && results[0].error.contains("HTML"),
        "HTTP 200 HTML rejection is reported as a service response error, not a TIFF decode error");
}
