/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TRK_H
#define	TRK_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <optional>
#include <unordered_map>

#include <tsre/world/TerrainLod.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/sound/RunningNoise.h>

class FileBuffer;
class QTextStream;

class Trk {
public:
    QString idName;
    QString displayName;
    QString description;
    QString graphic;
    QString loadingScreen;
    // Physical descriptor filename. This is separate from routeName, which is
    // the MSTS FileName token and database stem.
    QString trkFileName;
    QString routeName;
    unsigned int electrified;
    unsigned int mountains;
    int overheadWireHeight;
    int passengerRuleSet;
    int freightRuleSet;
    int signalSet;
    int gantrySet;
    // TrackGauge in metres, 0 for none (as MSTS routes write it). Open Rails
    // reads it as a length with a unit; trackGaugeText is the token as the
    // file wrote it, written back while the value is the one it gives.
    float trackGauge;
    QString trackGaugeText;
    int era;
    float speedLimit;
    std::unordered_map<std::string, QString> environment;
    int terrainErrorScale;
    int startTileX;
    int startTileZ;
    float startpX;
    float startpZ;
    float distantTerrainYOffset = 0;
    int tsreSuperelevation = 0;
    // The ambient left where the sky is hidden (task 27); negative: the
    // setting's.
    float tsreEnclosedAmbient = -1.0f;
    // TsreRunningNoise: which running noise vehicles that carry their own
    // (MaSzyna cars) play on this route (RunningNoise.h). Unset: not
    // written, their own (Vehicle).
    RunningNoise tsreRunningNoise = RunningNoise::Unset;
    bool milepostUnitsKilometers;
    float maxLineVoltage;
    QString defaultSignalSMS;
    QString defaultCrossingSMS;
    QString defaultWaterTowerSMS;
    QString defaultCoalTowerSMS;
    QString defaultDieselTowerSMS;
    float tempRestrictedSpeed;
    float gravityScale;
    float timetableTollerance;
    float forestClearDistance = 0;
    float derailScale;
    int imageLoadId = -1;
    int imageDetailsId = -1;
    std::optional<GeoProjectionParameters> geoProjection;
    GeoProjectionType geoProjectionType = GeoProjectionType::Undefined;
    int tsreMaxStaticDetailLevel = 10;
    QVector<TerrainLodLevel> terrainLodLevels;
    // Tr_RouteFile's blocks TSRE does not read (Open Rails' superelevation
    // standards, its sound and forest preferences, comments), as the file
    // wrote them, in its order; written back after the ones TSRE writes.
    QStringList kept;
    bool isModified();
    void setModified(bool val);
    Trk();
    void load();
    void loadUtf16Data(FileBuffer *data);
    void load(QString path);
    bool save();
    void saveToStream(QTextStream &out);
    static std::unique_ptr<Trk> createNewRouteTemplate(
            const QString &routeDirectoryName);
    const QVector<TerrainLodLevel> &effectiveTerrainLodLevels() const;
    QString terrainLodSummary() const;
    virtual ~Trk();
    
private:
    bool modified = false;
};

#endif	/* TRK_H */

