#include <tsre/tests/VehicleLoadBench.h>

#include <QElapsedTimer>
#include <QFile>
#include <QDebug>
#include <tsre/Game.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLib.h>

namespace {

// The process's resident memory (Linux), kB; -1 elsewhere.
long residentKb() {
    QFile status("/proc/self/status");
    if (!status.open(QIODevice::ReadOnly))
        return -1;
    for (const QByteArray &line : status.readAll().split('\n'))
        if (line.startsWith("VmRSS:"))
            return line.mid(6).trimmed().split(' ').value(0).toLong();
    return -1;
}

} // namespace

int TsreTests::runVehicleLoadBench(bool verbose) {
    Q_UNUSED(verbose);
    const long before = residentKb();
    EngLib *lib = new EngLib();
    Game::currentEngLib = lib;
    QElapsedTimer timer;
    timer.start();
    lib->loadAll(Game::root);
    const qint64 ms = timer.elapsed();
    int loaded = 0;
    for (const auto &entry : lib->eng)
        loaded += entry.second != nullptr && entry.second->loaded == 1 ? 1 : 0;
    const long after = residentKb();
    qInfo().noquote() << QString("[tests:vehicle-load] %1 of %2 vehicles loaded in %3 ms; resident memory %4 MB before, "
                                 "%5 MB after (+%6 MB)")
                                 .arg(loaded)
                                 .arg(lib->eng.size())
                                 .arg(ms)
                                 .arg(before / 1024.0, 0, 'f', 1)
                                 .arg(after / 1024.0, 0, 'f', 1)
                                 .arg((after - before) / 1024.0, 0, 'f', 1);
    return loaded > 0 ? 0 : 1;
}
