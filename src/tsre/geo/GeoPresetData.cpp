/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/geo/GeoPresetData.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <mzip/miniz/miniz.h>

namespace {
constexpr qint64 maximumPlacesArchiveSize = 32LL * 1024LL * 1024LL;
constexpr quint64 maximumPlacesFileSize = 128ULL * 1024ULL * 1024ULL;

bool extractPlacesFile(const QString &archivePath, const QString &destinationPath,
                       QString *error) {
    QFile archiveFile(archivePath);
    if (!archiveFile.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot open %1").arg(archivePath);
        return false;
    }
    if (archiveFile.size() <= 0 || archiveFile.size() > maximumPlacesArchiveSize) {
        if (error) *error = QStringLiteral("Invalid city preset archive size in %1")
                .arg(archivePath);
        return false;
    }
    const QByteArray archiveData = archiveFile.readAll();
    if (archiveData.isEmpty()) {
        if (error) *error = QStringLiteral("Empty city preset archive %1")
                .arg(archivePath);
        return false;
    }

    mz_zip_archive archive = {};
    if (!mz_zip_reader_init_mem(&archive, archiveData.constData(),
                                size_t(archiveData.size()), 0)) {
        if (error) *error = QStringLiteral("Invalid city preset archive %1")
                .arg(archivePath);
        return false;
    }

    const QByteArray entryName("geo_cities_presets.txt");
    const int entry = mz_zip_reader_locate_file(
            &archive, entryName.constData(), nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
    mz_zip_archive_file_stat stat = {};
    const bool validEntry = entry >= 0
            && mz_zip_reader_file_stat(&archive, mz_uint(entry), &stat)
            && !mz_zip_reader_is_file_a_directory(&archive, mz_uint(entry))
            && stat.m_uncomp_size > 0
            && stat.m_uncomp_size <= maximumPlacesFileSize;
    if (!validEntry) {
        mz_zip_reader_end(&archive);
        if (error) *error = QStringLiteral(
                "City preset archive %1 has no valid geo_cities_presets.txt")
                .arg(archivePath);
        return false;
    }

    QByteArray contents(qsizetype(stat.m_uncomp_size), Qt::Uninitialized);
    const bool extracted = mz_zip_reader_extract_to_mem(
            &archive, mz_uint(entry), contents.data(), size_t(contents.size()), 0);
    mz_zip_reader_end(&archive);
    if (!extracted) {
        if (error) *error = QStringLiteral("Cannot extract city presets from %1")
                .arg(archivePath);
        return false;
    }

    const QFileInfo destinationInfo(destinationPath);
    if (!QDir().mkpath(destinationInfo.absolutePath())) {
        if (error) *error = QStringLiteral("Cannot create %1")
                .arg(destinationInfo.absolutePath());
        return false;
    }
    QSaveFile destination(destinationPath);
    if (!destination.open(QIODevice::WriteOnly)
            || destination.write(contents) != contents.size()
            || !destination.commit()) {
        if (error) *error = QStringLiteral("Cannot write %1").arg(destinationPath);
        return false;
    }
    return true;
}
}

QString GeoPlacePresetIndex::normalized(const QString &text) {
    return text.simplified().toCaseFolded();
}

void GeoPlacePresetIndex::addName(const QString &name, int placeIndex) {
    const QString key = normalized(name);
    if (key.isEmpty()) return;
    QVector<int> &matches = names[key];
    if (matches.isEmpty() || matches.constLast() != placeIndex)
        matches.append(placeIndex);
}

bool GeoPlacePresetIndex::load(const QString &path, QString *error) {
    places.clear();
    names.clear();
    sortedNames.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot open %1").arg(path);
        return false;
    }

    int lineNumber = 0;
    while (!file.atEnd()) {
        ++lineNumber;
        QByteArray line = file.readLine();
        if (line.endsWith('\n')) line.chop(1);
        if (line.endsWith('\r')) line.chop(1);
        const QList<QByteArray> columns = line.split('\t');
        if (columns.size() < 9) continue;

        bool idOk = false;
        bool latitudeOk = false;
        bool longitudeOk = false;
        GeoPlacePreset value;
        value.id = columns[0].toLongLong(&idOk);
        value.name = QString::fromUtf8(columns[1]);
        value.asciiName = QString::fromUtf8(columns[2]);
        value.latitude = columns[4].toDouble(&latitudeOk);
        value.longitude = columns[5].toDouble(&longitudeOk);
        value.countryCode = QString::fromLatin1(columns[8]).trimmed().toUpper();
        if (!idOk || !latitudeOk || !longitudeOk || value.name.isEmpty())
            continue;

        const int placeIndex = places.size();
        places.append(value);
        addName(value.name, placeIndex);
        addName(value.asciiName, placeIndex);
        const QList<QByteArray> aliases = columns[3].split(',');
        for (const QByteArray &alias : aliases)
            addName(QString::fromUtf8(alias), placeIndex);
    }

    sortedNames = names.keys();
    std::sort(sortedNames.begin(), sortedNames.end());
    if (places.isEmpty()) {
        if (error) *error = QStringLiteral("No valid places in %1").arg(path);
        return false;
    }
    if (error) error->clear();
    return true;
}

bool GeoPlacePresetIndex::load(const QString &path,
                               const QString &fallbackZipPath, QString *error) {
    if (!QFileInfo(path).isFile()
            && !extractPlacesFile(fallbackZipPath, path, error))
        return false;
    return load(path, error);
}

QVector<int> GeoPlacePresetIndex::search(const QString &text,
                                         int maximumResults) const {
    QVector<int> result;
    const QString query = normalized(text);
    if (query.isEmpty() || maximumResults <= 0) return result;

    QSet<int> seen;
    auto it = std::lower_bound(sortedNames.cbegin(), sortedNames.cend(), query);
    for (; it != sortedNames.cend() && it->startsWith(query)
           && result.size() < maximumResults; ++it) {
        const QVector<int> matches = names.value(*it);
        for (int placeIndex : matches) {
            if (!seen.contains(placeIndex)) {
                seen.insert(placeIndex);
                result.append(placeIndex);
                if (result.size() == maximumResults) break;
            }
        }
    }
    return result;
}

const GeoPlacePreset &GeoPlacePresetIndex::place(int index) const {
    return places.at(index);
}

bool GeoProjectionPresetList::load(const QString &path, QString *error) {
    presets.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot open %1").arg(path);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) *error = QStringLiteral("Invalid projection preset JSON in %1: %2")
                .arg(path, parseError.errorString());
        return false;
    }

    for (const QJsonValue &entry : document.array()) {
        if (!entry.isObject()) continue;
        const QJsonObject object = entry.toObject();
        GeoProjectionPreset value;
        value.countryCode = object.value(QStringLiteral("countryCode"))
                .toString().trimmed().toUpper();
        value.countryName = object.value(QStringLiteral("countryName")).toString();
        value.name = object.value(QStringLiteral("name")).toString();
        value.latitude = object.value(QStringLiteral("lat")).toDouble(
                std::numeric_limits<double>::quiet_NaN());
        value.longitude = object.value(QStringLiteral("lon")).toDouble(
                std::numeric_limits<double>::quiet_NaN());
        value.scaleFactor = object.value(QStringLiteral("k0")).toDouble(1.0);
        if (value.name.isEmpty() || !std::isfinite(value.latitude)
                || !std::isfinite(value.longitude)
                || !std::isfinite(value.scaleFactor) || value.scaleFactor <= 0.0)
            continue;
        presets.append(value);
    }

    if (presets.isEmpty()) {
        if (error) *error = QStringLiteral("No valid projection presets in %1").arg(path);
        return false;
    }
    if (error) error->clear();
    return true;
}

QVector<int> GeoProjectionPresetList::search(const QString &text,
                                             int maximumResults) const {
    QVector<int> result;
    const QString query = text.simplified().toCaseFolded();
    if (query.isEmpty() || maximumResults <= 0) return result;
    for (int i = 0; i < presets.size() && result.size() < maximumResults; ++i) {
        const GeoProjectionPreset &value = presets[i];
        if (value.name.toCaseFolded().contains(query)
                || value.countryName.toCaseFolded().contains(query)
                || value.countryCode.toCaseFolded() == query)
            result.append(i);
    }
    return result;
}

int GeoProjectionPresetList::nearest(double latitude, double longitude,
                                     const QString &countryCode) const {
    constexpr double pi = 3.14159265358979323846;
    int best = -1;
    double bestDistance = std::numeric_limits<double>::infinity();
    const QString wantedCountry = countryCode.trimmed().toUpper();
    for (int i = 0; i < presets.size(); ++i) {
        const GeoProjectionPreset &value = presets[i];
        if (!wantedCountry.isEmpty() && value.countryCode != wantedCountry)
            continue;
        const double latitudeDelta = latitude - value.latitude;
        const double longitudeDelta = std::remainder(longitude - value.longitude, 360.0)
                * std::cos((latitude + value.latitude) * pi / 360.0);
        const double distance = latitudeDelta * latitudeDelta
                + longitudeDelta * longitudeDelta;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

const GeoProjectionPreset &GeoProjectionPresetList::preset(int index) const {
    return presets.at(index);
}
