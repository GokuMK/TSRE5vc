/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MAPDATAOSM_H
#define	MAPDATAOSM_H

#include <tsre/geo/MapData.h>
#include <QPointF>
#include <QList>
#include <QRectF>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

class QByteArray;
class QImage;
class QNetworkAccessManager;
class QNetworkReply;
class QPainter;
class LatitudeLongitudeCoordinate;

namespace Osm {
class FeatureClasses;
class OsmStore;
struct Style;
}

// OSM map image of one terrain tile. Data comes from the local OSM directory
// (core.paths.osmData, converted on first use) and, when no local file covers the
// tile, from the OSM API. Classes and styles: Osm::FeatureClasses.
class MapDataOSM : public MapData {
    Q_OBJECT
public:
    MapDataOSM();
    virtual ~MapDataOSM();
    bool draw(QImage* myImage);
    void load();
    // The styles drawn with (light by default; the dark ones for the dark map
    // palette), taken by the next load.
    void setClasses(const Osm::FeatureClasses &classes);
    // Fills the tile from a store; used by load() and by tests. Returns the number of drawable items.
    size_t loadFrom(const Osm::OsmStore &store);
    // Fills the tile from OSM API responses (api/0.6/map XML); used by the network path and by tests.
    size_t loadFromApiXml(const QList<QByteArray> &responses);

signals:
    void loaded();
    void statusInfo(QString val);
    // The web data could not be had (no answer, an error, too little data).
    void failed(QString message);

public slots:
    void isData(QNetworkReply* r);

private:
    // One drawable feature, coordinates in tile units (pixel = unit * image height / level).
    struct Item {
        const Osm::Style *style = nullptr;
        int slot = 0;        // legacy draw order: 0 first, bridges last
        bool area = false;   // rings are filled (closed ways, multipolygons)
        std::vector<std::vector<QPointF>> rings;  // a way: one polyline; a multipolygon: outer, inners
        QRectF bounds;
    };
    struct ApiWay { std::vector<int64_t> refs; std::vector<std::pair<std::string, std::string>> tags; };

    bool loadLocal();
    void get(LatitudeLongitudeCoordinate* min, LatitudeLongitudeCoordinate* max);
    void parseApi(const QByteArray &data);
    void buildApiItems();
    QPointF project(double lat, double lon) const;
    void addItem(Item item);
    void paint(QPainter &painter, const QRectF &unitArea) const;

    std::vector<Item> items;
    bool hasData = false;
    QNetworkAccessManager *network = nullptr;
    int loadCount = 0;
    int totalLoadCount = 0;
    bool requestFailed = false;
    const Osm::FeatureClasses *classes = nullptr;  // null: FeatureClasses::standard()
    const Osm::FeatureClasses &styles() const;  // one of the web requests failed: the others are ignored
    std::unordered_map<int64_t, std::pair<double, double>> apiNodes;
    std::unordered_map<int64_t, ApiWay> apiWays;
};

#endif	/* MAPDATAOSM_H */
