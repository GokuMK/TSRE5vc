#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVector>
#include <atomic>
#include <functional>

namespace Elevation {
struct DownloadResult { QByteArray bytes; QString error; };
struct RangeRequest { QUrl url; quint64 first = 0, last = 0; };
struct DownloadQueryKey { QString parameter, value; };
struct DownloadLimits {
    qint64 maxBytes = 32*1024*1024;
    int transferTimeoutMs = 30000, deadlineMs = 45000;
};
// Worker-thread operation. At most four replies, fresh connections per wave.
// Results retain input order; completion callbacks may arrive in any order.
QVector<DownloadResult> downloadWave(const QVector<QUrl> &urls, std::atomic_bool &cancel,
    const std::function<void(int completed)> &progress = {}, const DownloadLimits &limits = {},
    const QByteArray &authorization = {}, const DownloadQueryKey &queryKey = {});
// Strict HTTP byte-range transport for large COG assets. A successful response
// must be 206 and describe exactly the requested interval.
QVector<DownloadResult> downloadRangeWave(const QVector<RangeRequest> &ranges,
    std::atomic_bool &cancel, const std::function<void(int completed)> &progress = {},
    const DownloadLimits &limits = {}, const QByteArray &authorization = {});
}
