#include "QuadTree.h"
#include "TFile.h"
#include "TerrainGridLayout.h"
#include <tsre/Game.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/SimisTextReader.h>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QDataStream>
#include <QDateTime>
#include <memory>
#include <cmath>
#include <cstring>

namespace {
using Kind = SimisTextReader::Kind;
bool consume(SimisTextReader &r, Kind k) { return r.next().kind == k; }
bool writeFile(const QString &path, const QByteArray &bytes, QString &error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = path + ": " + file.errorString(); return false;
    }
    return true;
}
}

bool QuadTree::decodeTileName(const QString &input, int &x, int &y, int &level) {
    const QString name = input.toLower();
    if (name.size() < 2 || name.size() > 9 || (name[0] != '-' && name[0] != '_')) return false;
    const int shift = name[0] == '-' ? 2 : 0;
    const int steps = ((name.size() - 1) * 4 - shift) / 2;
    if (steps < 7 || steps > 15) return false; // Existing TD hierarchy: 2..512 km.
    for (auto ch : name.mid(1))
        if (!QStringLiteral("0123456789abcdef").contains(ch)) return false;
    bool ok; quint32 bits = name.mid(1).toUInt(&ok, 16);
    if (!ok || (shift && (bits & 3))) return false;
    bits >>= shift;
    x = y = -16384; level = 32768;
    for (int i = steps - 1; i >= 0; --i) {
        level /= 2;
        const int quadrant = (bits >> (i * 2)) & 3;
        if (quadrant == 1 || quadrant == 2) x += level;
        if (quadrant == 0 || quadrant == 1) y += level;
    }
    return true;
}

bool QuadTree::insertTile(int x, int y, int level, SavePolicy policy) {
    if (level < 1 || level > 256 || (level & (level - 1)) ||
        x < -16384 || x >= 16384 || y < -16384 || y >= 16384 ||
        x % level || y % level) return false;
    const int tx = int(std::floor(x / 512.0)) * 512;
    const int ty = int(std::floor(y / 512.0)) * 512;
    auto *&entry = td[tx * 100000 + ty];
    if (!entry) {
        entry = new TdFile;
        entry->x = tx; entry->y = ty;
        entry->qt = new QuadTile(256, 1, tx, ty);
    }
    entry->qt->addTile(x, y, level);
    entry->modified = modified = true;
    if (policy == SavePolicy::Immediate && immediateSaveAllowed()) save();
    return true;
}

bool QuadTree::isModified() const {
    if (temporary) return false;
    if (modified) return true;
    for (auto entry : td) if (entry && entry->modified) return true;
    return false;
}

bool QuadTree::reconstruct(const QString &directory, QStringList &issues, int &count) {
    count = 0;
    makeTemporary();
    QDir dir(ContentPath::normalize(directory));
    if (!dir.exists()) return true; // No distant terrain is a normal case.
    if (!QFileInfo(dir.absolutePath()).isReadable()) {
        issues.append(directory + ": terrain directory is not readable"); return false;
    }
    const auto files = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const auto &file : files) {
        if (file.suffix().compare("t", Qt::CaseInsensitive)) continue;
        int x, y, level;
        TFile::LayoutInfo layout;
        TerrainGridLayout checked;
        QString error;
        if (!decodeTileName(file.completeBaseName(), x, y, level) ||
            !TFile::readLayoutInfo(file.filePath(), layout) ||
            !TerrainGridLayout::tryCreate(layout.samples, layout.spacing, layout.patches, 0, checked, error) ||
            std::abs(double(layout.samples) * layout.spacing - level * 2048.0) > 0.01) {
            issues.append(file.fileName() + ": unsupported name or invalid/inconsistent layout");
            continue;
        }
        // No mesh, height or texture loading. Resource failures remain loader diagnostics.
        if (!insertTile(x, y, level)) { issues.append(file.fileName() + ": invalid placement"); continue; }
        ++count;
    }
    return true;
}

QuadTree::LoadStatus QuadTree::loadChecked(const QString &directory, QString &error) {
    error.clear();
    const QDir dir(ContentPath::normalize(directory));
    const QString index = ContentPath::normalize(dir.filePath(low ? "lo_td_idx.dat" : "td_idx.dat"));
    if (!QFileInfo::exists(index)) { error = "Missing QuadTree index: " + index; return LoadStatus::Missing; }
    QByteArray bytes;
    QString text;
    if (!TerrainFile::readDescriptorBytes(index, bytes, error) ||
        !SimisTextReader::decode(bytes, text, error)) return LoadStatus::Invalid;
    SimisTextReader r(text);
    auto fail = [&](const QString &why) { error = index + ": " + why; return LoadStatus::Invalid; };
    if (!r.next().text.startsWith("SIMISA") ||
        r.next().text.compare("terrain_desc", Qt::CaseInsensitive) || !consume(r, Kind::Open))
        return fail("Invalid terrain_desc header");
    QuadTree candidate(low);
    bool hasTiles = false;
    while (r.peekKind() == Kind::Atom) {
        const QString field = r.next().text.toLower();
        if (!consume(r, Kind::Open)) return fail("Missing block opening");
        if (field == "terrain_desc_size" || field == "depth") {
            qint32 value;
            if (!r.readInteger(value) || value <= 0 || !consume(r, Kind::Close)) return fail("Invalid " + field);
            if (field == "depth") candidate.depth = value; else candidate.terrainDescSize = value;
        } else if (field == "terrain_desc_tiles") {
            if (hasTiles) return fail("Duplicate terrain_desc_tiles");
            hasTiles = true;
            while (r.peekKind() == Kind::Atom) {
                if (r.next().text.compare("tdfile", Qt::CaseInsensitive) || !consume(r, Kind::Open))
                    return fail("Invalid TdFile entry");
                qint32 x, y;
                if (!r.readInteger(x) || !r.readInteger(y) || !consume(r, Kind::Close) ||
                    x < -32 || x >= 32 || y < -32 || y >= 32) return fail("Invalid TD coordinates");
                const int key = x * 512 * 100000 + y * 512;
                if (candidate.td.contains(key)) return fail("Duplicate TD coordinates");
                const QString path = dir.filePath(getNameXY(x) + getNameXY(y) + (low ? ".tdl" : ".td"));
                if (!TerrainFile::readDescriptorBytes(path, bytes, error)) return fail(path + ": " + error);
                auto memory = new unsigned char[bytes.size()];
                std::memcpy(memory, bytes.constData(), bytes.size());
                FileBuffer in(memory, bytes.size());
                try {
                    in.require(32);
                    if (!in.isBinarySimis()) throw FileBuffer::ParseError("Expected binary TD");
                    in.off = 32;
                    const auto root = in.readBlock();
                    if (root.id != TS::terrain_desc) throw FileBuffer::ParseError("Expected terrain_desc");
                    FileBuffer::ScopedLimit rootLimit(in, root.end);
                    in.off = root.payload;
                    const auto children = in.readBlock();
                    if (children.id != TS::terrain_desc_tiles) throw FileBuffer::ParseError("Expected terrain_desc_tiles");
                    FileBuffer::ScopedLimit childLimit(in, children.end);
                    in.off = children.payload;
                    const auto treeBytes = in.getUint();
                    if (treeBytes != quint32(children.end - in.off))
                        throw FileBuffer::ParseError("Invalid TD byte count");
                    auto entry = std::make_unique<TdFile>();
                    entry->x = x * 512; entry->y = y * 512;
                    auto tree = std::make_unique<QuadTile>(256, 1, entry->x, entry->y);
                    tree->load(&in);
                    if (in.off != children.end) throw FileBuffer::ParseError("Unexpected TD tail");
                    entry->qt = tree.release(); candidate.td.insert(key, entry.release());
                } catch (const FileBuffer::ParseError &e) { return fail(path + ": " + e.what()); }
            }
            if (!consume(r, Kind::Close)) return fail("Truncated tile list");
        } else if (!r.skipBlock()) return fail("Invalid extra index block");
    }
    if (!hasTiles || !consume(r, Kind::Close) || !consume(r, Kind::End)) return fail("Incomplete QuadTree index");
    td.swap(candidate.td); depth = candidate.depth; terrainDescSize = candidate.terrainDescSize;
    modified = temporary = recovery = false;
    return LoadStatus::Loaded;
}

bool QuadTree::saveChecked(const QString &directory, QString &error) {
    error.clear();
    if (!Game::writeEnabled) { error = "Route writing is disabled"; return false; }
    if (temporary) { error = "Temporary QuadTree has not been adopted"; return false; }
    QDir dir(ContentPath::normalize(directory));
    if (!dir.mkpath(".")) { error = "Cannot create TD directory: " + directory; return false; }
    // Preserve all old domain metadata once before a reconstructed tree replaces it.
    // A unique backup directory avoids destroying evidence from an earlier repair.
    if (recovery && backupDirectory.isEmpty()) {
        QString backup = "recovery-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + (low ? "-lo" : "-detail");
        for (int suffix = 1; dir.exists(backup); ++suffix) backup += "-" + QString::number(suffix);
        const QString index = low ? "lo_td_idx.dat" : "td_idx.dat";
        const auto old = dir.entryInfoList(QDir::Files);
        bool created = false;
        for (const auto &f : old) {
            if (f.fileName().compare(index, Qt::CaseInsensitive) &&
                f.suffix().compare(low ? "tdl" : "td", Qt::CaseInsensitive)) continue;
            if ((!created && !dir.mkdir(backup)) ||
                !QFile::copy(f.filePath(), dir.filePath(backup + "/" + f.fileName()))) {
                error = "Cannot back up QuadTree metadata: " + f.filePath(); return false;
            }
            created = true;
        }
        backupDirectory = dir.filePath(backup);
    }
    // Write TD files atomically, index last. Dirty state is cleared only after all
    // commits succeed. A process crash across files is recoverable from the backup,
    // not claimed to be a filesystem-wide atomic transaction.
    for (auto entry : td) {
        if (!entry || !entry->modified) continue;
        QByteArray bytes; QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        saveTD(entry->x, entry->y, &stream);
        const QString path = ContentPath::normalize(dir.filePath(getNameXY(entry->x / 512) + getNameXY(entry->y / 512) + (low ? ".tdl" : ".td")));
        if (stream.status() != QDataStream::Ok || !writeFile(path, bytes, error)) return false;
    }
    QByteArray bytes; QTextStream out(&bytes, QIODevice::WriteOnly);
    out.setEncoding(QStringConverter::Utf16); out.setGenerateByteOrderMark(true);
    out << "SIMISA@@@@@@@@@@JINX0D0t______\n\n";
    save(out); out.flush();
    if (out.status() != QTextStream::Ok || !writeFile(ContentPath::normalize(dir.filePath(low ? "lo_td_idx.dat" : "td_idx.dat")), bytes, error)) return false;
    for (auto entry : td) if (entry) entry->modified = false;
    modified = false;
    return true;
}
