#include <tsre/fileFunctions/ContentPath.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>
#include <QHash>
#include <unordered_map>
struct Entry { QString path, key; };
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    if (argc != 2) { out << "Usage: benchmark <gameroot>\n"; return 2; }
    const auto trainset = ContentPath::join(QString::fromLocal8Bit(argv[1]), "TRAINS/TRAINSET");
    QStringList paths;
    QDir dirs(trainset);
    for (const auto &dir : dirs.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QDir vehicles(trainset + '/' + dir);
        for (const auto &name : vehicles.entryList({"*.eng", "*.wag"}, QDir::Files, QDir::Name))
            paths << ContentPath::join(vehicles.path(), name);
    }
    const qint64 n = paths.size();
    out << "Qt " << qVersion() << " files=" << n << " comparisons=" << n*(n-1)/2 << Qt::endl;
    if (!n) return 2;
    for (int mode = 0; mode < 5; ++mode) {
        std::unordered_map<int, Entry> cache;
        QHash<QString,int> index;
        qint64 comparisons=0,hits=0;
        QElapsedTimer timer;timer.start();
        for (const auto &path : paths) {
            const auto requestKey = mode >= 2 ? ContentPath::key(path) : QString();
            if (mode == 4) {
                hits += index.contains(requestKey);
                index.insert(requestKey,index.size());
                continue;
            }
            for (const auto &pair : cache) {
                ++comparisons;
                bool match=false;
                if (mode == 0) match = path == pair.second.path;
                if (mode == 1) match = ContentPath::key(path) == ContentPath::key(pair.second.path);
                if (mode == 2) match = requestKey == ContentPath::key(pair.second.path);
                if (mode == 3) match = requestKey == pair.second.key;
                if (match) { ++hits;break; }
            }
            cache.emplace(cache.size(),Entry{path,mode==3 ? requestKey : QString()});
        }
        const char *names[]={"plain QString comparison","current: both keys inside loop",
                            "request key hoisted only","both keys cached, still linear","precomputed key hash lookup"};
        out << names[mode] << ": " << timer.nsecsElapsed()/1e6 << " ms, comparisons="
            << comparisons << ", hits=" << hits << Qt::endl;
    }
}
