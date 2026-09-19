#include <tsre/geo/ElevationDownload.h>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <array>

namespace Elevation {
QVector<DownloadResult> downloadWave(const QVector<QUrl> &urls, std::atomic_bool &cancel,
        const std::function<void(int)> &progress, const DownloadLimits &limits,
        const QByteArray &authorization) {
    QVector<DownloadResult> results(urls.size());
    if (urls.isEmpty()) return results;
    if (urls.size()>4 || limits.maxBytes<=0 || limits.transferTimeoutMs<=0 || limits.deadlineMs<=0) {
        for (auto &r : results) r.error = QStringLiteral("Invalid elevation download batch");
        return results;
    }
    if (cancel) return results;
    // Discard idle connections after each wave; do not carry a pooled socket
    // across a slow reply or raster decoding/cache work into the next wave.
    QNetworkAccessManager network;
    QEventLoop loop;
    struct Pending { QNetworkReply *reply = nullptr; QByteArray bytes; bool tooLarge=false, timedOut=false, done=false; };
    std::array<Pending,4> pending;
    int completed = 0;
    QTimer cancellation;
    QObject::connect(&cancellation,&QTimer::timeout,&loop,[&] {
        if (cancel) for (auto &p : pending) if (p.reply && !p.reply->isFinished()) p.reply->abort();
    });
    for (int i=0; i<urls.size(); ++i) {
        QNetworkRequest request(urls[i]);
        request.setRawHeader("User-Agent","TSRE5vc terrain elevation");
        if (!authorization.isEmpty()) request.setRawHeader("Authorization",authorization);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,authorization.isEmpty()
            ? QNetworkRequest::NoLessSafeRedirectPolicy : QNetworkRequest::SameOriginRedirectPolicy);
        request.setTransferTimeout(limits.transferTimeoutMs);
        auto *reply = pending[i].reply = network.get(request);
        reply->setReadBufferSize(1024*1024);
        auto *deadline = new QTimer(reply);
        deadline->setSingleShot(true);
        QObject::connect(deadline,&QTimer::timeout,&loop,[&,i,reply] {
            pending[i].timedOut = true; reply->abort();
        });
        QObject::connect(reply,&QNetworkReply::readyRead,&loop,[&,i,reply] {
            auto &p = pending[i];
            p.bytes += reply->readAll();
            if (p.bytes.size()>limits.maxBytes) { p.tooLarge = true; reply->abort(); }
        });
        const auto finish = [&,i,reply,deadline] {
            deadline->stop();
            auto &p = pending[i]; auto &r = results[i];
            if (p.done) return;
            p.done = true;
            p.bytes += reply->readAll();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (cancel) r.error = QStringLiteral("Elevation download cancelled");
            else if (p.tooLarge || p.bytes.size()>limits.maxBytes)
                r.error = QStringLiteral("Elevation response exceeds %1 bytes").arg(limits.maxBytes);
            else if (p.timedOut) r.error = QStringLiteral("Elevation request timed out");
            else if (reply->error()!=QNetworkReply::NoError || status!=200)
                r.error = QStringLiteral("Elevation request failed (HTTP %1): %2").arg(status).arg(reply->errorString());
            else if (reply->header(QNetworkRequest::ContentTypeHeader).toString().startsWith("text/html",Qt::CaseInsensitive))
                r.error = QStringLiteral("Elevation service returned an HTML page instead of raster data (request may have been rejected)");
            else r.bytes = std::move(p.bytes);
            p.bytes.clear();
            ++completed;
            if (progress) progress(completed);
            if (completed==urls.size()) loop.quit();
        };
        QObject::connect(reply,&QNetworkReply::finished,&loop,finish);
        deadline->start(limits.deadlineMs);
        if (reply->isFinished()) QTimer::singleShot(0,&loop,finish);
    }
    cancellation.start(50);
    if (completed<urls.size()) loop.exec();
    return results;
}
}
