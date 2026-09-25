/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef GEOPRESETDATA_H
#define GEOPRESETDATA_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

struct GeoPlacePreset {
    qint64 id = 0;
    QString name;
    QString asciiName;
    QStringList aliases;
    QString countryCode;
    double latitude = 0.0;
    double longitude = 0.0;
};

struct GeoProjectionPreset {
    QString countryCode;
    QString countryName;
    QString name;
    double latitude = 0.0;
    double longitude = 0.0;
    // Parsed now even though the first dialog implementation deliberately
    // keeps the projection converter's current k0=1.0 behavior.
    double scaleFactor = 1.0;
};

class GeoPlacePresetIndex {
public:
    bool load(const QString &path, QString *error = nullptr);
    bool load(const QString &path, const QString &fallbackZipPath,
              QString *error = nullptr);
    bool loadDefault(QString *error = nullptr);
    QVector<int> search(const QString &text, int maximumResults = 20) const;
    QVector<int> countryPlaces(const QString &countryCode) const;
    QStringList countryCodes() const;
    QString nearestCountry(double latitude, double longitude) const;
    const GeoPlacePreset &place(int index) const;
    int placeCount() const { return places.size(); }

    static QString defaultPlacesPath();
    static QString defaultArchivePath();

private:
    static QString normalized(const QString &text);
    void addName(const QString &name, int placeIndex);

    QVector<GeoPlacePreset> places;
    QHash<QString, QVector<int>> names;
    QHash<QString, QVector<int>> countries;
    QStringList sortedNames;
};

class GeoProjectionPresetList {
public:
    bool load(const QString &path, QString *error = nullptr);
    QVector<int> search(const QString &text, int maximumResults = 20) const;
    int nearest(double latitude, double longitude,
                const QString &countryCode = QString()) const;
    const GeoProjectionPreset &preset(int index) const;
    int count() const { return presets.size(); }

private:
    QVector<GeoProjectionPreset> presets;
};

#endif /* GEOPRESETDATA_H */
