/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/coords/CoordsCountryPlaces.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>
#include <utility>
#include <tsre/Game.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/geo/GeoPresetData.h>

namespace {
const QString kmlNamespace = QStringLiteral("http://www.opengis.net/kml/2.2");
const QString tsreNamespace = QStringLiteral("urn:tsre5:geo-places:1");

QString normalizedCountryCode(const QString &value) {
    const QString code = value.trimmed().toUpper();
    static const QRegularExpression valid(QStringLiteral("^[A-Z]{2}$"));
    return valid.match(code).hasMatch() ? code : QString();
}

QString coordinateText(double value) {
    return QString::number(value, 'f', 8);
}

bool setMarkerPosition(Coords::Marker &marker) {
    if (Game::GeoCoordConverter == nullptr
            || !std::isfinite(marker.lat) || !std::isfinite(marker.lon)
            || marker.lat < -90.0 || marker.lat > 90.0
            || marker.lon < -180.0 || marker.lon > 180.0)
        return false;

    IghCoordinate internal;
    PreciseTileCoordinate tile;
    if (Game::GeoCoordConverter->ConvertToInternal(
                marker.lat, marker.lon, &internal) == nullptr
            || Game::GeoCoordConverter->ConvertToTile(
                &internal, &tile) == nullptr)
        return false;
    marker.tileX.append(tile.TileX);
    marker.tileZ.append(tile.TileZ);
    marker.x.append(int(tile.X * 2048.0 - 1024.0));
    marker.y.append(0);
    marker.z.append(int(tile.Z * 2048.0 - 1024.0));
    return true;
}
}

QString CoordsCountryPlaces::fileNameForCountry(const QString &countryCode) {
    const QString code = normalizedCountryCode(countryCode);
    return code.isEmpty()
            ? QString()
            : QStringLiteral("tsre-country-%1.kml").arg(code);
}

bool CoordsCountryPlaces::isCountryPlacesFile(
        const QString &fileName, QString *countryCode) {
    static const QRegularExpression expression(
            QStringLiteral("^tsre-country-([A-Z]{2})\\.kml$"),
            QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(
            QFileInfo(fileName).fileName());
    if (!match.hasMatch()) return false;
    if (countryCode != nullptr)
        *countryCode = match.captured(1).toUpper();
    return true;
}

CoordsCountryPlaces::CoordsCountryPlaces(const QString &path) {
    isCountryPlacesFile(path, &fileCountryCode);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        loadError = QStringLiteral("Cannot open %1: %2")
                .arg(path, file.errorString());
        return;
    }

    QXmlStreamReader reader(&file);
    Marker current;
    bool inPlacemark = false;
    bool hasCoordinates = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = reader.name().toString();
            const QString namespaceUri = reader.namespaceUri().toString();
            if (namespaceUri == kmlNamespace && name == QStringLiteral("Placemark")) {
                current = Marker();
                inPlacemark = true;
                hasCoordinates = false;
            } else if (inPlacemark && namespaceUri == kmlNamespace
                       && name == QStringLiteral("name")) {
                current.name = reader.readElementText().trimmed();
            } else if (inPlacemark && namespaceUri == kmlNamespace
                       && name == QStringLiteral("coordinates")) {
                const QStringList tuples = reader.readElementText().trimmed().split(
                        QRegularExpression(QStringLiteral("\\s+")),
                        Qt::SkipEmptyParts);
                if (!tuples.isEmpty()) {
                    const QStringList values = tuples.first().split(',');
                    bool longitudeOk = false;
                    bool latitudeOk = false;
                    if (values.size() >= 2) {
                        current.lon = values[0].toDouble(&longitudeOk);
                        current.lat = values[1].toDouble(&latitudeOk);
                    }
                    hasCoordinates = longitudeOk && latitudeOk;
                }
            } else if (namespaceUri == tsreNamespace
                       && name == QStringLiteral("countryCode")) {
                const QString code = normalizedCountryCode(
                        reader.readElementText());
                if (!code.isEmpty()) fileCountryCode = code;
            } else if (inPlacemark && namespaceUri == tsreNamespace
                       && name == QStringLiteral("placeId")) {
                bool ok = false;
                const qint64 id = reader.readElementText().trimmed().toLongLong(&ok);
                if (ok) current.sourceId = id;
            } else if (inPlacemark && namespaceUri == tsreNamespace
                       && name == QStringLiteral("asciiName")) {
                current.asciiName = reader.readElementText().trimmed();
            } else if (inPlacemark && namespaceUri == tsreNamespace
                       && name == QStringLiteral("alias")) {
                const QString alias = reader.readElementText().trimmed();
                if (!alias.isEmpty()) current.aliases.append(alias);
            }
        } else if (reader.isEndElement()
                   && reader.namespaceUri() == kmlNamespace
                   && reader.name() == QStringLiteral("Placemark")) {
            inPlacemark = false;
            current.countryCode = fileCountryCode;
            if (!current.name.isEmpty() && hasCoordinates
                    && setMarkerPosition(current))
                markerList.append(std::move(current));
        }
    }

    if (reader.hasError()) {
        loadError = QStringLiteral("Invalid country places KML %1: %2")
                .arg(path, reader.errorString());
        markerList.clear();
        return;
    }
    if (markerList.isEmpty()) {
        loadError = QStringLiteral("No valid places in %1").arg(path);
        return;
    }
    for (int markerIndex = 0; markerIndex < markerList.size(); ++markerIndex) {
        const Marker &marker = markerList[markerIndex];
        QStringList names = marker.aliases;
        names.prepend(marker.asciiName);
        names.prepend(marker.name);
        for (const QString &name : names) {
            const QString key = name.simplified().toCaseFolded();
            if (key.isEmpty()) continue;
            QVector<int> &matches = indexedNames[key];
            if (matches.isEmpty() || matches.constLast() != markerIndex)
                matches.append(markerIndex);
        }
    }
    sortedNames = indexedNames.keys();
    std::sort(sortedNames.begin(), sortedNames.end());
    loaded = true;
}

QVector<int> CoordsCountryPlaces::search(
        const QString &text, int maximumResults) const {
    QVector<int> result;
    const QString query = text.simplified().toCaseFolded();
    if (query.isEmpty() || maximumResults <= 0) return result;

    QSet<int> seen;
    auto it = std::lower_bound(sortedNames.cbegin(), sortedNames.cend(), query);
    for (; it != sortedNames.cend() && it->startsWith(query)
           && result.size() < maximumResults; ++it) {
        for (int markerIndex : indexedNames.value(*it)) {
            if (seen.contains(markerIndex)) continue;
            seen.insert(markerIndex);
            result.append(markerIndex);
            if (result.size() == maximumResults) break;
        }
    }
    return result;
}

bool CoordsCountryPlaces::write(
        const QString &path, const QString &countryCode,
        const GeoPlacePresetIndex &places, QString *error) {
    const QString code = normalizedCountryCode(countryCode);
    if (code.isEmpty()) {
        if (error) *error = QStringLiteral("Invalid country code: %1")
                .arg(countryCode);
        return false;
    }
    const QVector<int> countryPlaces = places.countryPlaces(code);
    if (countryPlaces.isEmpty()) {
        if (error) *error = QStringLiteral("No place presets found for %1")
                .arg(code);
        return false;
    }
    const QFileInfo destinationInfo(path);
    if (!QDir().mkpath(destinationInfo.absolutePath())) {
        if (error) *error = QStringLiteral("Cannot create %1")
                .arg(destinationInfo.absolutePath());
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Cannot write %1: %2")
                .arg(path, file.errorString());
        return false;
    }
    QXmlStreamWriter writer(&file);
    writer.setAutoFormatting(true);
    writer.writeStartDocument(QStringLiteral("1.0"));
    writer.writeStartElement(QStringLiteral("kml"));
    writer.writeDefaultNamespace(kmlNamespace);
    writer.writeNamespace(tsreNamespace, QStringLiteral("tsre"));
    writer.writeStartElement(QStringLiteral("Document"));
    writer.writeTextElement(QStringLiteral("name"),
                            QStringLiteral("TSRE country places: %1").arg(code));
    writer.writeStartElement(QStringLiteral("ExtendedData"));
    writer.writeTextElement(QStringLiteral("tsre:formatVersion"),
                            QStringLiteral("1"));
    writer.writeTextElement(QStringLiteral("tsre:countryCode"), code);
    writer.writeEndElement();

    for (int index : countryPlaces) {
        const GeoPlacePreset &place = places.place(index);
        writer.writeStartElement(QStringLiteral("Placemark"));
        writer.writeTextElement(QStringLiteral("name"), place.name);
        writer.writeStartElement(QStringLiteral("ExtendedData"));
        writer.writeTextElement(QStringLiteral("tsre:placeId"),
                                QString::number(place.id));
        if (!place.asciiName.isEmpty()
                && place.asciiName.compare(place.name, Qt::CaseInsensitive) != 0)
            writer.writeTextElement(QStringLiteral("tsre:asciiName"),
                                    place.asciiName);
        QSet<QString> writtenNames;
        writtenNames.insert(place.name.simplified().toCaseFolded());
        writtenNames.insert(place.asciiName.simplified().toCaseFolded());
        for (const QString &alias : place.aliases) {
            const QString key = alias.simplified().toCaseFolded();
            if (key.isEmpty() || writtenNames.contains(key)) continue;
            writtenNames.insert(key);
            writer.writeTextElement(QStringLiteral("tsre:alias"), alias);
        }
        writer.writeEndElement();
        writer.writeStartElement(QStringLiteral("Point"));
        writer.writeTextElement(QStringLiteral("coordinates"),
                coordinateText(place.longitude) + QLatin1Char(',')
                + coordinateText(place.latitude) + QStringLiteral(",0"));
        writer.writeEndElement();
        writer.writeEndElement();
    }

    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndDocument();
    if (writer.hasError() || !file.commit()) {
        if (error) *error = QStringLiteral("Cannot save %1: %2")
                .arg(path, file.errorString());
        return false;
    }
    if (error) error->clear();
    return true;
}
