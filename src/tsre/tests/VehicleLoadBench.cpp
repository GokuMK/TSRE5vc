#include <tsre/tests/VehicleLoadBench.h>

#include <QElapsedTimer>
#include <QFile>
#include <QDebug>
#include <settings/SettingsAccess.h>
#include <tsre/Game.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/VehicleDefinition.h>

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

// What a definition says, as text: the fields the editors and the
// simulation read, the files read, the problems, and the Wagon and Engine
// blocks written out whole.
QString describe(const VehicleDefinition &d) {
    QStringList out;
    out << d.path << d.folder << d.engName << d.displayName << d.searchKeywords << d.type << d.engType
        << d.typeHash << QString::number(d.wagonTypeId) << d.brakeSystemType << d.shape << d.shapePath
        << QString::number(d.size[0]) << QString::number(d.size[1]) << QString::number(d.size[2])
        << QString::number(d.massKg, 'g', 17) << QString::number(d.wheelRadius)
        << QString::number(d.driverWheelRadius) << QString::number(d.freightAnims.size())
        << QString::number(d.couplings.size()) << d.cab3DShapePath << d.cab3DViewPath
        << QString::number(d.ok()) << d.files().join('|');
    for (const SimisDocument::Problem &p : d.problems)
        out << p.file + ":" + QString::number(p.line) + " " + p.message;
    if (d.document) {
        for (const SimisDocument::Problem &p : d.document->problems())
            out << p.file + ":" + QString::number(p.line) + " " + p.message;
        out << d.document->write(d.document->last("Wagon")) << d.document->write(d.document->last("Engine"));
    }
    return out.join('\n');
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

    // The definitions read on loadAll's workers, each against the same file
    // read again here, on the main thread, as Eng::load reads it alone.
    const bool openRails = Settings::boolean("core.content.loading.preferOpenRailsEng");
    int differ = 0;
    for (const auto &entry : lib->eng) {
        const Eng *eng = entry.second;
        if (eng == nullptr)
            continue;
        const std::shared_ptr<const VehicleDefinition> alone
                = VehicleDefinition::read(eng->path, eng->name, Game::root, openRails, Game::routeFolder());
        if (describe(*alone) == describe(*eng->definition))
            continue;
        if (++differ <= 5)
            qWarning().noquote() << "[tests:vehicle-load] read on the workers, differs from a read alone:"
                                 << eng->pathid;
    }
    qInfo().noquote() << QString("[tests:vehicle-load] %1 of %2 definitions as a read alone")
                                 .arg(lib->eng.size() - differ)
                                 .arg(lib->eng.size());
    return loaded > 0 && differ == 0 ? 0 : 1;
}
