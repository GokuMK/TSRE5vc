/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#pragma once

// The OSM workers' thread, used in place of std::thread. With Qt 6.10 built by
// MinGW, a std::thread that used a QObject (a QFile, a QPainter on an image)
// crashes in Qt's per-thread cleanup when it ends. Windows writes a crash report
// for each (about a second) and lets the program go on, so a conversion crawled
// and the heap ended up corrupted. A QThread ends cleanly. Same use as
// std::thread, except that destroying a running thread waits for it.

#include <QThread>
#include <utility>

namespace Osm {

class Thread {
public:
    Thread() = default;
    template <typename Function, typename... Args>
    explicit Thread(Function &&f, Args &&...args)
        : thread_(QThread::create(std::forward<Function>(f), std::forward<Args>(args)...)) {
        thread_->start();
    }
    Thread(Thread &&other) noexcept : thread_(std::exchange(other.thread_, nullptr)) {}
    Thread &operator=(Thread &&other) noexcept {
        if (this != &other) {
            join();
            thread_ = std::exchange(other.thread_, nullptr);
        }
        return *this;
    }
    Thread(const Thread &) = delete;
    Thread &operator=(const Thread &) = delete;
    ~Thread() { join(); }

    bool joinable() const { return thread_ != nullptr; }
    void join() {
        if (!thread_) return;
        thread_->wait();
        delete thread_;
        thread_ = nullptr;
    }

private:
    QThread *thread_ = nullptr;
};

}
