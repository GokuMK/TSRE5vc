#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/world/TerrainSeason.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir temp;
    int passed=0,failed=0;
    auto check=[&](bool ok,const char *what) {
        if(ok)++passed;else{++failed;qCritical()<<what;}
    };
    check(temp.isValid(),"temporary directory");
    if(!temp.isValid())return 1;
    auto put=[&](const QString &name) {
        const QString path=temp.filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);check(f.open(QIODevice::WriteOnly) && f.write("fixture")==7,"fixture write");
        return path;
    };
    const auto upper=put("MixedRoot/Tree.S");
    const auto lower=temp.filePath("MixedRoot/tree.s");
    const bool sensitive=!QFileInfo::exists(lower);
    qInfo()<<"Filesystem case-sensitive:"<<sensitive;
#ifndef Q_OS_WIN
    check(sensitive,"Unix temporary filesystem must distinguish case for this test");
#endif
    check(ContentPath::key(upper)==ContentPath::key(lower),"logical identities fold case");
    const QString storedKey=ContentPath::key(upper);
    check(ContentPath::normalize(upper)==upper,"I/O path retains its authored spelling");
    if(sensitive) {
        check(ContentPath::key(lower)==storedKey && !QFileInfo::exists(lower),
                "cache key matches case variants without changing filesystem lookup");
        put("MixedRoot/tree.s");
        check(ContentPath::key(lower)==storedKey,
                "case-only files share one logical runtime identity");
    }
    check(ContentPath::withExtension(upper,"sd")==temp.filePath("MixedRoot/Tree.sd"),"implicit companion preserves stem");
    const auto dds=put("MixedRoot/Leaf.dds");
    const auto ace=temp.filePath("MixedRoot/Leaf.ACE");
    check(ContentPath::textureSource(ace)==dds,"ACE absent selects fixed DDS suffix");
    put("MixedRoot/Leaf.ACE");
    check(ContentPath::textureSource(ace)==ace,"ACE present selected before cache");
    check(ContentPath::normalize("//Server/Share//Mixed\\Tree.S")=="//Server/Share/Mixed/Tree.S","UNC prefix survives");
    const auto spring=put("ROUTES/ROUTE/TERRTEX/SPRING/Soil.ace");
    const auto base=temp.filePath("ROUTES/ROUTE/TERRTEX");
    check(TerrainSeason::resolve(base,"SpringRain","Soil.ace",false)==spring,"semantic seasonal fallback");
    if(sensitive) {
        check(TerrainSeason::resolve(base,"SpringRain","soil.ace",false).isEmpty(),"no texture case-search fallback");
        put("ROUTES/ROUTE/TERRTEX/snow/Winter.ace");
        check(TerrainSeason::resolve(base,"Snow","Winter.ace",false).isEmpty(),"no seasonal directory case-search fallback");
    }
    check(ContentPath::key("terrain-proc:ABC")=="terrain-proc:ABC","synthetic case-sensitive identity");
    qInfo()<<"Content paths:"<<passed<<"passed,"<<failed<<"failed";
    return failed?1:0;
}
