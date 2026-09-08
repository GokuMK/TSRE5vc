#include "UndoBuffer.h"
#include <QThreadPool>
#include <QRunnable>
#include <QVector>
#include <atomic>
#include <algorithm>

namespace {
struct Job {
    std::weak_ptr<UndoBuffer> owner;
    QByteArray input, output;
    std::atomic<bool> done{false};
};
QVector<std::weak_ptr<UndoBuffer>> pending;
std::shared_ptr<Job> active;
QThreadPool &pool() {
    static QThreadPool instance;
    static const bool configured = [] { instance.setMaxThreadCount(1); return true; }();
    Q_UNUSED(configured);
    return instance;
}
}
UndoBuffer::UndoBuffer(const QByteArray &bytes)
    : raw(bytes.constData(), bytes.size()), size(bytes.size()) {}
QByteArray UndoBuffer::bytes() const {
    if (!raw.isEmpty() || size == 0) return raw;
    QByteArray result = qUncompress(compressed);
    return result.size() == size ? result : QByteArray();
}
void UndoBuffer::compressLater() {
    if (queued || raw.isEmpty()) return;
    queued = true;
    pending.push_back(weak_from_this());
    pump();
}
void UndoBuffer::pump() {
    if (active && active->done.load(std::memory_order_acquire)) {
        if (auto owner = active->owner.lock()) {
            // Keep raw data if compression failed or would increase memory use.
            if (!active->output.isEmpty() && active->output.size() < owner->raw.size()) {
                owner->compressed = std::move(active->output);
                owner->raw.clear();
            }
        }
        active.reset();
    }
    pending.erase(std::remove_if(pending.begin(), pending.end(),
                                [](const auto &entry) { return entry.expired(); }), pending.end());
    if (active) return;
    while (!pending.isEmpty()) {
        auto owner = pending.takeFirst().lock();
        if (!owner) continue;
        auto job = std::make_shared<Job>();
        job->owner = owner;
        job->input = owner->raw; // Immutable COW copy, no second full allocation.
        active = job;
        pool().start(QRunnable::create([job] {
            try {
                job->output = qCompress(job->input, 1);
            } catch (...) {
                // Compression is optional; raw history remains usable.
                job->output.clear();
            }
            job->done.store(true, std::memory_order_release);
        }));
        break;
    }
}
bool UndoBuffer::busy() { return bool(active) || !pending.isEmpty(); }
