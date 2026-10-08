/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/ShapeLoader.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>
#include <QCoreApplication>
#include <QHash>
#include <QThreadPool>
#include <QVector>
#include <algorithm>
#include <atomic>
#include <memory>

namespace {

struct Job {
    std::unique_ptr<ComplexShape> copy;
    std::atomic<bool> done{false};
};

// Everything but the workers' part of a job is main-thread only. A job is
// deleted on the main thread once done, so the copy's data and meshes are
// released there.
struct Loader {
    bool threaded = false;
    int parallel = 1;
    QThreadPool *pool = nullptr;
    std::atomic<int> running{0};
    std::atomic<unsigned> finished{0};
    QHash<ComplexShape *, Job *> jobs;
    // Jobs cancelled while running, deleted once done.
    QVector<Job *> dropped;
    int firstView = 0;

    void collectDropped() {
        dropped.erase(std::remove_if(dropped.begin(), dropped.end(), [](Job *job) {
            if (!job->done.load(std::memory_order_acquire))
                return false;
            delete job;
            return true;
        }), dropped.end());
    }
};

Loader *existing = nullptr;

Loader &loader() {
    if (existing == nullptr) {
        existing = new Loader;
        existing->threaded = Settings::boolean("core.rendering.threadedShapeLoading");
        existing->parallel = std::max(1, Settings::integer("core.rendering.objectLoading.parallelShapes"));
        if (existing->threaded) {
            // Owned by the application, whose end waits for running jobs.
            existing->pool = new QThreadPool(QCoreApplication::instance());
            existing->pool->setMaxThreadCount(existing->parallel);
        }
    }
    return *existing;
}

}

ShapeLoader::Request ShapeLoader::request(ComplexShape *shape) {
    Loader &l = loader();
    l.collectDropped();
    auto found = l.jobs.find(shape);
    if (found != l.jobs.end()) {
        Job *job = found.value();
        if (!job->done.load(std::memory_order_acquire))
            return Request::Wait;
        l.jobs.erase(found);
        shape->adopt(*job->copy);
        delete job;
        return Request::Adopted;
    }
    const bool limited = l.firstView == 0;
    if (l.threaded) {
        if (limited && l.running.load() >= l.parallel)
            return Request::Wait;
        if (ComplexShape *copy = shape->detachedCopy()) {
            Job *job = new Job;
            job->copy.reset(copy);
            l.jobs.insert(shape, job);
            l.running.fetch_add(1);
            Loader *owner = &l;
            l.pool->start([job, owner] {
                job->copy->loadDetached();
                // The job may be deleted as soon as it is done.
                job->done.store(true, std::memory_order_release);
                owner->finished.fetch_add(1);
                owner->running.fetch_sub(1);
            });
            return Request::Wait;
        }
    }
    if (limited) {
        if (Game::objectLoadingTokens < 1)
            return Request::Wait;
        Game::objectLoadingTokens -= 2;
    }
    return Request::LoadHere;
}

void ShapeLoader::cancel(ComplexShape *shape) {
    if (existing == nullptr)
        return;
    auto found = existing->jobs.find(shape);
    if (found == existing->jobs.end())
        return;
    Job *job = found.value();
    existing->jobs.erase(found);
    if (job->done.load(std::memory_order_acquire))
        delete job;
    else
        existing->dropped.push_back(job);
}

bool ShapeLoader::busy() {
    return existing != nullptr && existing->running.load() > 0;
}

unsigned ShapeLoader::progress() {
    return existing != nullptr ? existing->finished.load() : 0;
}

void ShapeLoader::waitForAll() {
    if (existing != nullptr && existing->pool != nullptr)
        existing->pool->waitForDone();
}

ShapeLoader::FirstView::FirstView() {
    loader().firstView++;
}

ShapeLoader::FirstView::~FirstView() {
    loader().firstView--;
}
