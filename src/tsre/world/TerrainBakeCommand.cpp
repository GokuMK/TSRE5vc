#include "TerrainBakeCommand.h"
#include "TerrainSeason.h"
#include "Terrain.h"
#include "TerrainMaterialMap.h"
#include "ScopedBakeTFile.h"
#include <tsre/Game.h>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QElapsedTimer>
#include <QTextStream>

namespace {
class BatchTerrain : public Terrain {
public:
    bool bake(const QString &file, const QString &root, QString &error) {
        name=QFileInfo(file).completeBaseName();
        tfile=new ScopedBakeTFile();
        if (!tfile->readT(file)) {error="Cannot read "+file;return false;}
        if (tfile->sampleMaterialBuffer.isEmpty()) return true;
        if (!validateGridLayout(file)) {error="Unsupported terrain layout: "+file;return false;}
        texturepath=rootTexturepath=root;
        for (int i=0;i<TerrainGridLayout::SupportedPatchRecordCount;++i) {
            texid[i]=texid2[i]=-1;texModified[i]=texLocked[i]=false;
        }
        loadProceduralMaterial(QFileInfo(file).path(),false);
        QString previous;
        if (!saveProceduralBake() || !saveProceduralMap(QFileInfo(file).path(),previous)) {
            proceduralSaveFailed();error="Cannot bake "+file;return false;
        }
        if (!tfile->save(file)) {
            proceduralSaveFailed();error="Cannot save descriptor "+file;return false;
        }
        proceduralSaveCompleted();
        return true;
    }
};
}
bool TerrainBakeCommand::bakeRoute(const QString &route, const QString &season, int resolution,
                                   const std::function<void(const QString &)> &progress, QString &error) {
    if (!Game::writeEnabled || Game::serverClient) {error="Route writing is disabled";return false;}
    const QDir dir(QFileInfo(route).absoluteFilePath());
    if (!dir.exists("terrtex") || !dir.exists("tiles")) {error="Expected a route containing TERRTEX and TILES";return false;}
    if (resolution<256 || resolution>4096 || (resolution&(resolution-1))) {error="Resolution must be 256, 512, 1024, 2048 or 4096";return false;}
    QStringList seasons;
    if (season.compare("all",Qt::CaseInsensitive)==0) seasons=TerrainSeason::available(dir.filePath("terrtex"));
    else {const auto v=TerrainSeason::canonical(season);if (v.isEmpty()) {error="Unknown season: "+season;return false;}seasons<<v;}
    QLockFile lock(dir.filePath(".tsre-procedural-bake.lock"));
    lock.setStaleLockTime(0); // A large route may legitimately bake for minutes.
    if (!lock.tryLock()) {error="Another procedural bake owns this route";return false;}
    QDir routes=dir;routes.cdUp();QDir install=routes;install.cdUp();
    // Existing library lookup is root/routes/route; require the standard route location.
    if (routes.dirName().compare("routes",Qt::CaseInsensitive)!=0) {error="Route must be inside a ROUTES directory";return false;}
    Game::root=install.absolutePath();Game::route=dir.dirName();
    TerrainMaterialMap::Enabled=true;TerrainMaterialMap::BakedSide=resolution;
    bool ok=true;
    for (const auto &folder : {QString("tiles"),QString("lo_tiles")}) {
        const QDir tiles(dir.filePath(folder));
        for (const auto &entry:tiles.entryInfoList(QDir::Files,QDir::Name)) {
            if (entry.suffix().compare("t",Qt::CaseInsensitive)!=0) continue;
            for (const auto &variant:seasons) {
                Game::season=variant;QElapsedTimer timer;timer.start();
                BatchTerrain tile;QString failure;
                const bool saved=tile.bake(entry.absoluteFilePath(),dir.filePath("terrtex"),failure);
                progress(QString("%1 / %2: %3 (%4 ms)").arg(entry.fileName(),variant,saved?"OK":failure).arg(timer.elapsed()));
                if (!saved) {ok=false;error=failure;break;}
            }
        }
    }
    return ok;
}
int TerrainBakeCommand::run(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QCommandLineParser parser;parser.setApplicationDescription("Bake procedural terrain variants; no GUI or GPU required.");parser.addHelpOption();
    parser.addOption({"refreshpmaptextures","Bake route procedural textures"});
    parser.addOption({"route","Absolute route directory","directory"});
    parser.addOption({"season","Season name or all","variant","all"});
    parser.addOption({"res","Square baked texture resolution","pixels","1024"});
    parser.addOption({"patch-res","Intermediate procedural patch resolution (match editor setting)","pixels","512"});
    parser.addOption({"validate","Enable expensive full-map diagnostic validation"});
    parser.process(app);
    bool number=false;const int size=parser.value("res").toInt(&number);
    QString error;QTextStream output(stdout);
    if (!number || !parser.isSet("route")) {output<<"Supply --route and a valid --res\n";return 2;}
    // This explicit write command does not load editor startup/settings files.
    // The GUI checks its session's write protection before launching it.
    Game::writeEnabled=true;
    bool patchNumber=false;const int patchSize=parser.value("patch-res").toInt(&patchNumber);
    if (!patchNumber || patchSize<128 || patchSize>2048 || (patchSize&(patchSize-1))) {
        output<<"--patch-res must be 128, 256, 512, 1024 or 2048\n";return 2;
    }
    TerrainMaterialMap::OutputSide=patchSize;
    TerrainMaterialMap::ValidateBakeOnLoad=parser.isSet("validate");
    const bool ok=bakeRoute(parser.value("route"),parser.value("season"),size,
                           [&](const QString &s){output<<s<<Qt::endl;},error);
    if (!ok) output<<error<<Qt::endl;
    return ok?0:1;
}
