#include <tsre/geo/osm/OsmDirectory.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <chrono>
#include <functional>
#include <iostream>

using namespace Osm;

namespace {

// A tiny Geofabrik-style download around (lon, lat); extra nodes change the file size.
std::string download(double lon, double lat, int extraNodes, int64_t timestamp) {
    HeaderInfo h;
    h.bbox = Box::fromDegrees(lon - 0.5, lat - 0.5, lon + 0.5, lat + 0.5);
    h.requiredFeatures << "OsmSchema-V0.6" << "DenseNodes";
    h.optionalFeatures << "Sort.Type_then_ID";
    h.replicationTimestamp = timestamp;
    BlockBuilder nodes, ways;
    const Tag none[] = {{"", ""}};
    for (int i = 0; i < 3 + extraNodes; ++i) nodes.addNode(i + 1, Location::fromDegrees(lon + i * 0.001, lat), none, 0);
    const Tag road[] = {{"highway", "service"}};
    const int64_t refs[] = {1, 2, 3};
    ways.addWay(10, road, 1, refs, nullptr, 3);
    std::string a, b;
    nodes.build(a); ways.build(b);
    return encodeBlobFrame("OSMHeader", encodeHeaderBlock(h), {}, 6) + encodeBlobFrame("OSMData", a, {}, 6) + encodeBlobFrame("OSMData", b, {}, 6);
}

bool write(const QString &path, const std::string &bytes) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes.data(), qint64(bytes.size())) == qint64(bytes.size());
}

QStringList names(const std::vector<const DirectoryEntry *> &v) {
    QStringList n;
    for (const auto *e : v) n << QFileInfo(e->path).fileName();
    n.sort();
    return n;
}

}

void runDirectoryTests(const std::function<void(bool, const char *)> &check) {
    QTemporaryDir tmp;
    const QDir dir(tmp.path());
    ConvertOptions options;
    options.threads = 2;
    ConvertStats stats;
    QString error;

    check(OsmDirectory::convertedPathFor(dir.filePath("pomorskie-261006.osm.pbf")) == dir.filePath("pomorskie-261006.tsre.osm.pbf")
              && OsmDirectory::convertedPathFor(dir.filePath("Odd.OSM.PBF")) == dir.filePath("Odd.tsre.osm.pbf"),
          "converted name replaces the .osm.pbf suffix");

    // b: download with an up-to-date conversion; c: conversion whose download was deleted;
    // d: conversion made from an older download.
    write(dir.filePath("a.osm.pbf"), download(10.0, 50.0, 0, 100));
    write(dir.filePath("b.osm.pbf"), download(18.6, 54.3, 0, 100));
    write(dir.filePath("c.osm.pbf"), download(19.0, 52.0, 0, 100));
    write(dir.filePath("d.osm.pbf"), download(21.0, 52.2, 0, 100));
    check(convertPbf(dir.filePath("b.osm.pbf"), dir.filePath("b.tsre.osm.pbf"), options, stats, error)
              && convertPbf(dir.filePath("c.osm.pbf"), dir.filePath("c.tsre.osm.pbf"), options, stats, error)
              && convertPbf(dir.filePath("d.osm.pbf"), dir.filePath("d.tsre.osm.pbf"), options, stats, error),
          "prepare converted files");
    QFile::remove(dir.filePath("c.osm.pbf"));
    write(dir.filePath("d.osm.pbf"), download(21.0, 52.2, 5, 200));  // newer download replaced the old one
    write(dir.filePath("junk.osm.pbf"), "not a pbf");
    write(dir.filePath("x.osm.pbf.part"), download(0, 0, 0, 1));
    write(dir.filePath("readme.txt"), "text");
    QDir().mkpath(dir.filePath("sub"));
    write(dir.filePath("sub/e.osm.pbf"), download(5.0, 45.0, 0, 100));

    OsmDirectory od;
    check(od.scan(dir.path(), error), "scan the directory");
    check(od.entries().size() == 7, "only top-level *.osm.pbf files are listed");
    const DirectoryEntry *junk = nullptr;
    for (const auto &e : od.entries()) if (e.path.endsWith("junk.osm.pbf")) junk = &e;
    check(junk && !junk->usable() && !junk->error.isEmpty(), "unreadable files are reported, not used");
    check(names(od.convertedFiles()) == QStringList({"b.tsre.osm.pbf", "c.tsre.osm.pbf", "d.tsre.osm.pbf"}), "converted files are recognised by their header");
    check(names(od.pendingConversions()) == QStringList({"a.osm.pbf", "d.osm.pbf"}), "downloads without an up-to-date conversion are pending");
    const Box nearGdansk = Box::fromDegrees(18.0, 54.0, 19.0, 55.0), nearWarsaw = Box::fromDegrees(20.9, 52.1, 21.1, 52.3);
    check(od.pendingConversions(&nearGdansk).empty() && names(od.pendingConversions(&nearWarsaw)) == QStringList({"d.osm.pbf"}),
          "pending conversions are filtered by area");
    for (const auto &e : od.entries())
        if (e.path.endsWith("/b.osm.pbf")) check(e.convertedPath == dir.filePath("b.tsre.osm.pbf"), "a download is paired with its conversion");

    const DirectoryEntry *a = nullptr, *d = nullptr;
    for (const auto &e : od.entries()) { if (e.path.endsWith("/a.osm.pbf")) a = &e; if (e.path.endsWith("/d.osm.pbf")) d = &e; }
    check(a && OsmDirectory::convert(*a, false, options, stats, error) && QFile::exists(dir.filePath("a.osm.pbf")) && QFile::exists(dir.filePath("a.tsre.osm.pbf")),
          "keep mode leaves the download next to its conversion");
    check(d && OsmDirectory::convert(*d, true, options, stats, error) && !QFile::exists(dir.filePath("d.osm.pbf")) && QFile::exists(dir.filePath("d.tsre.osm.pbf")),
          "delete mode removes the download after converting");
    check(od.scan(dir.path(), error) && od.pendingConversions().empty() && od.convertedFiles().size() == 4, "after converting nothing is pending");
    const DirectoryEntry *converted = nullptr;
    for (const auto &e : od.entries()) if (e.path.endsWith("/d.tsre.osm.pbf")) converted = &e;
    check(converted && converted->source.timestamp == 200 && converted->source.name == "d.osm.pbf", "a reconversion records the newer download");
    check(!od.scan(dir.filePath("missing"), error) && !error.isEmpty(), "a missing directory is an error");
}

// Opt-in local data: --scan <directory>
int scanDirectory(const QString &path) {
    const auto start = std::chrono::steady_clock::now();
    OsmDirectory od;
    QString error;
    if (!od.scan(path, error)) { std::cerr << error.toStdString() << '\n'; return 1; }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    for (const auto &e : od.entries())
        std::cout << QFileInfo(e.path).fileName().toStdString() << ": " << (e.usable() ? (e.converted ? "converted" : e.convertedPath.isEmpty() ? "needs conversion" : "converted copy exists") : e.error.toStdString().c_str()) << '\n';
    std::cout << od.entries().size() << " files scanned in " << seconds << " s\n";
    return 0;
}
