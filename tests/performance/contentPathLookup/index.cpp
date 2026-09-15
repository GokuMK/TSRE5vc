#include <tsre/fileFunctions/ContentPath.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QTextStream>
#include <algorithm>
#include <memory>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <vector>

// Mirror EngLib's numeric ID map, pointer indirection, loaded flag and stored key.
// Both alternatives retain exactly the same entries; only Indexed adds a QHash.
struct Entry { QString pathid, hashid; int loaded = 1; };
struct Request { QString path, key; int expected; };
template<bool Indexed> struct Cache {
    std::unordered_map<int, std::unique_ptr<Entry>> entries;
    QHash<QString, int> index;
    int find(const QString &key) const {
        if constexpr (Indexed) {
            const auto hit = index.constFind(key);
            if (hit == index.cend()) return -1;
            const auto &entry = entries.at(hit.value());
            return entry && entry->loaded == 1 ? hit.value() : -1;
        } else {
            for (const auto &pair : entries)
                if (pair.second && pair.second->loaded == 1
                    && key == pair.second->hashid) return pair.first;
            return -1;
        }
    }
    void insert(int id, const QString &path, const QString &key) {
        entries.emplace(id, std::make_unique<Entry>(Entry{path, key, 1}));
        if constexpr (Indexed) index.insert(key, id);
    }
};
volatile qint64 consumed = 0;
void require(bool ok) { if (!ok) throw std::runtime_error("Lookup/ID validation failed"); }

template<bool Indexed> double cold(const QStringList &paths) {
    Cache<Indexed> cache;
    QElapsedTimer timer;
    timer.start();
    for (int id = 0; id < paths.size(); ++id) {
        const QString key = ContentPath::key(paths[id]);
        require(cache.find(key) == -1);
        cache.insert(id, paths[id], key);
    }
    const double ms = timer.nsecsElapsed() / 1e6;
    require(cache.entries.size() == size_t(paths.size()));
    consumed = cache.entries.size();
    return ms; // Destruction is outside the timed region.
}

template<bool Indexed> double queries(const Cache<Indexed> &cache,
                                     const std::vector<Request> &requests, bool prepare) {
    qint64 checksum = 0;
    QElapsedTimer timer;
    timer.start();
    for (const auto &request : requests) {
        const int id = prepare ? cache.find(ContentPath::key(request.path))
                               : cache.find(request.key);
        require(id == request.expected);
        checksum += id;
    }
    const double ms = timer.nsecsElapsed() / 1e6;
    consumed = checksum;
    return ms;
}

template<class Linear, class Indexed> void measure(QTextStream &out, const QString &dataset,
        const QString &workload, int n, int count, Linear linear, Indexed indexed) {
    // Warm both paths once, then alternate ordering across three measured rounds.
    linear(); indexed();
    std::vector<double> a, b;
    for (int round = 0; round < 3; ++round) {
        if (round % 2) { b.push_back(indexed()); a.push_back(linear()); }
        else { a.push_back(linear()); b.push_back(indexed()); }
    }
    std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
    out << dataset << ',' << workload << ',' << n << ',' << count << ','
        << a[1] << ',' << b[1] << ',' << a[1]/b[1] << ','
        << a[0] << ',' << a[2] << ',' << b[0] << ',' << b[2] << Qt::endl;
}

void run(QTextStream &out, const QString &dataset, const QStringList &paths, int count, bool build) {
    const int n = paths.size();
    if (build) measure(out, dataset, "cold-add", n, n,
        [&]{return cold<false>(paths);}, [&]{return cold<true>(paths);});
    Cache<false> linear; Cache<true> indexed;
    for (int id = 0; id < n; ++id) {
        const QString key = ContentPath::key(paths[id]);
        linear.insert(id, paths[id], key); indexed.insert(id, paths[id], key);
    }
    std::vector<int> ids(n);
    for (int id = 0; id < n; ++id) ids[id] = id;
    std::mt19937 rng(20260915);
    std::shuffle(ids.begin(), ids.end(), rng);
    std::vector<Request> hits, misses;
    for (int i = 0; i < count; ++i) {
        const int id = ids[i % n];
        const QString hit = paths[id].toUpper();
        const QString miss = paths[id] + ".missing";
        hits.push_back({hit, ContentPath::key(hit), id});
        misses.push_back({miss, ContentPath::key(miss), -1});
    }
    for (bool prepare : {false, true}) {
        const QString suffix = prepare ? "-with-key" : "-prepared-key";
        measure(out, dataset, "hits" + suffix, n, count,
            [&]{return queries(linear, hits, prepare);},
            [&]{return queries(indexed, hits, prepare);});
        measure(out, dataset, "misses" + suffix, n, count,
            [&]{return queries(linear, misses, prepare);},
            [&]{return queries(indexed, misses, prepare);});
    }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    if (argc != 2) { out << "Usage: content_path_index_benchmark <gameroot>\n"; return 2; }
    try {
        const QString trainset = ContentPath::join(QString::fromLocal8Bit(argv[1]), "TRAINS/TRAINSET");
        QStringList paths;
        QSet<QString> seen;
        int duplicateKeys = 0;
        for (const auto &dir : QDir(trainset).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            QDir vehicles(trainset + '/' + dir);
            for (const auto &name : vehicles.entryList({"*.eng", "*.wag"}, QDir::Files, QDir::Name)) {
                const QString path = ContentPath::join(vehicles.path(), name);
                const QString key = ContentPath::key(path);
                if (seen.contains(key)) { ++duplicateKeys; continue; }
                seen.insert(key); paths.append(path);
            }
        }
        require(!paths.isEmpty());
        out << "# Qt " << qVersion() << ", compiler " << __VERSION__
            << ", unique real paths " << paths.size() << ", duplicate keys " << duplicateKeys << Qt::endl;
        out << "# Median of 3 rounds after 1 warmup, times in ms. All returned IDs validated." << Qt::endl;
        out << "dataset,workload,assets,requests,linear_ms,index_ms,speedup,linear_min,linear_max,index_min,index_max" << Qt::endl;
        run(out, "real-eng-wag", paths, 10000, true);
        for (int n : {10000, 100000}) {
            QStringList synthetic;
            for (int id = 0; id < n; ++id)
                synthetic.append(QString("/benchmark/TRAINS/TRAINSET/Vehicle_%1/Vehicle_%1.eng")
                                 .arg(id, 6, 10, QChar('0')));
            run(out, "synthetic", synthetic, n == 10000 ? 2000 : 1000, n <= 10000);
        }
        out << "# PASS: every expected hit, miss, and insertion ID matched." << Qt::endl;
    } catch (const std::exception &e) { out << e.what() << Qt::endl; return 1; }
}
