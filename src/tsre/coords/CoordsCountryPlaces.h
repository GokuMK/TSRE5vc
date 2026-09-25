/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef COORDSCOUNTRYPLACES_H
#define COORDSCOUNTRYPLACES_H

#include <tsre/coords/Coords.h>
#include <QHash>

class GeoPlacePresetIndex;

class CoordsCountryPlaces : public Coords {
public:
    explicit CoordsCountryPlaces(const QString &path);

    QString countryCode() const { return fileCountryCode; }
    QString errorString() const { return loadError; }
    QVector<int> search(const QString &text,
                        int maximumResults = 20) const override;

    static QString fileNameForCountry(const QString &countryCode);
    static bool isCountryPlacesFile(const QString &fileName,
                                    QString *countryCode = nullptr);
    static bool write(const QString &path, const QString &countryCode,
                      const GeoPlacePresetIndex &places,
                      QString *error = nullptr);

private:
    QString fileCountryCode;
    QString loadError;
    QHash<QString, QVector<int>> indexedNames;
    QStringList sortedNames;
};

#endif /* COORDSCOUNTRYPLACES_H */
