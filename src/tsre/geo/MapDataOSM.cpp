/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/geo/MapDataOSM.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/geo/MapWindow.h>
#include <tsre/geo/osm/OsmClasses.h>
#include <tsre/geo/osm/OsmConversionUi.h>
#include <tsre/geo/osm/OsmDirectory.h>
#include <tsre/geo/osm/OsmMultipolygon.h>
#include <tsre/geo/osm/SortedPbfStore.h>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>
#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>
#include <algorithm>
#include <memory>
#include <thread>

namespace {

// Legacy draw order: classes with a higher layer first, bridges last.
int slotOf(const Osm::Classification &c) { return c.bridge ? 9 : 9 - std::min<int>(c.layer, 9); }

// Shoelace sign of a ring in lon/lat: fills use one winding so that overlapping areas never cancel.
bool counterClockwise(const Osm::Location *p, uint32_t n) {
    double a = 0;
    for (uint32_t i = 0; i + 1 < n; ++i) a += double(p[i].x) * p[i + 1].y - double(p[i + 1].x) * p[i].y;
    return a >= 0;
}

QPen pen(const Osm::Stroke &s) {
    // Widths are metres; painting happens in tile units of 2048 m. Width 0 stays a one-pixel cosmetic pen.
    return QPen(QColor::fromRgba(s.color), s.width / 2048.0, Qt::SolidLine, s.round ? Qt::RoundCap : Qt::FlatCap, Qt::RoundJoin);
}

const double MaxStrokeUnits = 8.0 / 2048.0;  // widest legacy casing is 7 m

}

MapDataOSM::MapDataOSM() {
}

MapDataOSM::~MapDataOSM() {
}

QPointF MapDataOSM::project(double lat, double lon) const {
    IghCoordinate igh;
    PreciseTileCoordinate tile;
    Game::GeoCoordConverter->ConvertToInternal(lat, lon, &igh);
    Game::GeoCoordConverter->ConvertToTile(&igh, &tile);
    return QPointF(tile.X + double(tile.TileX - this->tileX), tile.Z - double(tile.TileZ - this->tileZ));
}

void MapDataOSM::addItem(Item item) {
    double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
    for (const auto &ring : item.rings)
        for (const QPointF &p : ring) { x0 = std::min(x0, p.x()); y0 = std::min(y0, p.y()); x1 = std::max(x1, p.x()); y1 = std::max(y1, p.y()); }
    item.bounds = QRectF(QPointF(x0, y0), QPointF(x1, y1));
    items.push_back(std::move(item));
}

size_t MapDataOSM::loadFrom(const Osm::OsmStore &store) {
    using namespace Osm;
    items.clear();
    const FeatureClasses &classes = FeatureClasses::standard();
    const double margin = 0.0005;  // degrees, keeps wide strokes at the tile edge
    const Box area = Box::fromDegrees(minlon - margin, minlat - margin, maxlon + margin, maxlat + margin);
    std::vector<RelationData> relations;
    std::vector<Classification> relationClasses;
    Filter filter;
    filter.types = Ways | Relations;
    QString error;
    const bool ok = store.forEach(area, filter, [&](const Feature &f) {
        if (f.type == ItemType::Relation) {
            if (f.value("type") != "multipolygon") return;
            const Classification c = classes.classify(f);
            if (!c.cls || !classes.style(c).hasFill) return;
            relations.push_back(RelationData::from(f));
            relationClasses.push_back(c);
            return;
        }
        // Untagged ways are relation members; their relation draws them.
        if (!f.tagCount() || f.refCount < 2) return;
        const Classification c = classes.classify(f);
        Item item;
        item.style = &classes.style(c);
        item.slot = slotOf(c);
        item.area = item.style->hasFill && f.refCount >= 4 && f.refs[0] == f.refs[f.refCount - 1];
        std::vector<QPointF> points;
        points.reserve(f.refCount);
        for (uint32_t i = 0; i < f.refCount; ++i) points.push_back(project(f.locations[i].lat(), f.locations[i].lon()));
        if (item.area && !counterClockwise(f.locations, f.refCount)) std::reverse(points.begin(), points.end());
        item.rings.push_back(std::move(points));
        addItem(std::move(item));
    }, error);
    if (!ok) qWarning().noquote() << "OSM tile query:" << error;
    std::vector<MultipolygonResult> polygons;
    if (!relations.empty() && !assembleMultipolygons(store, relations, polygons, error)) qWarning().noquote() << "OSM multipolygons:" << error;
    for (size_t r = 0; r < polygons.size(); ++r) {
        for (const Polygon &p : polygons[r].polygons) {
            Item item;
            item.style = &classes.style(relationClasses[r]);
            item.slot = slotOf(relationClasses[r]);
            item.area = true;
            auto ring = [&](const std::vector<Location> &locations) {
                std::vector<QPointF> points;
                points.reserve(locations.size());
                for (const Location &l : locations) points.push_back(project(l.lat(), l.lon()));
                return points;
            };
            item.rings.push_back(ring(p.outer));
            for (const auto &inner : p.inners) item.rings.push_back(ring(inner));
            addItem(std::move(item));
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.slot < b.slot; });
    hasData = true;
    return items.size();
}

bool MapDataOSM::loadLocal() {
    const QString dir = Settings::string("core.paths.osmData", SettingType::Directory);
    if (dir.trimmed().isEmpty()) return false;
    const Osm::Box area = Osm::Box::fromDegrees(minlon, minlat, maxlon, maxlat);
    Osm::ensureConverted(QApplication::activeWindow(), area);
    Osm::OsmDirectory directory;
    QString error;
    if (!directory.scan(dir, error)) return false;
    // One store over every converted file keeps its block cache across tiles.
    QStringList files;
    QString signature;
    bool covered = false;
    for (const Osm::DirectoryEntry *e : directory.convertedFiles()) {
        files << e->path;
        signature += e->path + QLatin1Char('|') + QString::number(e->size) + QLatin1Char('|')
                     + QString::number(QFileInfo(e->path).lastModified().toMSecsSinceEpoch()) + QLatin1Char(';');
        covered |= !e->header.bbox.valid() || e->header.bbox.intersects(area);
    }
    if (!covered) return false;
    static std::unique_ptr<Osm::SortedPbfStore> store;
    static QString storeSignature;
    if (!store || signature != storeSignature) {
        store = std::make_unique<Osm::SortedPbfStore>();
        if (!store->open(files, error)) {
            qWarning().noquote() << "OSM data:" << error;
            store.reset();
            return false;
        }
        storeSignature = signature;
    }
    loadFrom(*store);
    return true;
}

void MapDataOSM::load() {
    items.clear();
    hasData = false;
    apiNodes.clear();
    apiWays.clear();
    if (loadLocal()) {
        //% "Load"
        emit statusInfo(qtTrId("map.network.status.load"));
        emit loaded();
        return;
    }

    LatitudeLongitudeCoordinate p00;
    p00.Latitude = (maxlat + minlat)/2.0;
    p00.Longitude = (maxlon + minlon)/2.0;
    LatitudeLongitudeCoordinate p01;
    p01.Latitude = (maxlat + minlat)/2.0;
    p01.Longitude = maxlon;
    LatitudeLongitudeCoordinate p10;
    p10.Latitude = maxlat;
    p10.Longitude = (maxlon + minlon)/2.0;
    LatitudeLongitudeCoordinate pm10;
    pm10.Latitude = minlat;
    pm10.Longitude = (maxlon + minlon)/2.0;
    LatitudeLongitudeCoordinate p0m1;
    p0m1.Latitude = (maxlat + minlat)/2.0;
    p0m1.Longitude = minlon;
    LatitudeLongitudeCoordinate minLatlon;
    LatitudeLongitudeCoordinate maxLatlon;
    minLatlon.Latitude = minlat;
    minLatlon.Longitude = minlon;
    maxLatlon.Latitude = maxlat;
    maxLatlon.Longitude = maxlon;

    // The OSM API limits the request area: four quarter-tile requests.
    loadCount = 0;
    totalLoadCount = 4;
    get(&minLatlon, &p00);
    get(&p00, &maxLatlon);
    get(&pm10, &p01);
    get(&p0m1, &p10);
}

void MapDataOSM::get(LatitudeLongitudeCoordinate* min, LatitudeLongitudeCoordinate* max){
    if (!network) {
        network = new QNetworkAccessManager(this);
        connect(network, SIGNAL(finished(QNetworkReply*)), this, SLOT(isData(QNetworkReply*)));
    }
    QNetworkRequest req(QUrl(QString("https://www.openstreetmap.org/api/0.6/map?bbox=%1,%2,%3,%4")
            .arg(min->Longitude).arg(min->Latitude).arg(max->Longitude).arg(max->Latitude)));
    network->get(req);
}

void MapDataOSM::isData(QNetworkReply* r){
    const QByteArray data = r->readAll();
    r->deleteLater();
    if(data.length() < 100){
        //"No data from the network..." label
        //% "No data from the network..."
        emit statusInfo(qtTrId("map.network.status.no.data"));
        // 5 seconds, warning time.
        QTimer::singleShot(5000, this, [this] {
            //% "Load"
            emit statusInfo(qtTrId("map.network.status.load"));
        });
        return;
    }
    parseApi(data);
    loadCount++;
    if(loadCount == totalLoadCount){
        buildApiItems();
        //% "Load"
        emit statusInfo(qtTrId("map.network.status.load"));
        emit loaded();
    } else {
        //% "Wait [%1/%2] ..."
        emit statusInfo(qtTrId("map.network.status.wait")
                        .arg(loadCount).arg(totalLoadCount));
    }
}

void MapDataOSM::parseApi(const QByteArray &data){
    QXmlStreamReader reader(data);
    ApiWay *way = nullptr;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QStringView name = reader.name();
            const QXmlStreamAttributes attr = reader.attributes();
            if (name == u"node") {
                apiNodes[attr.value("id").toLongLong()] = {attr.value("lat").toDouble(), attr.value("lon").toDouble()};
            } else if (name == u"way") {
                // The four requests overlap; a way already read is read again in full.
                way = &apiWays[attr.value("id").toLongLong()];
                way->refs.clear();
                way->tags.clear();
            } else if (name == u"nd" && way) {
                way->refs.push_back(attr.value("ref").toLongLong());
            } else if (name == u"tag" && way) {
                way->tags.emplace_back(attr.value("k").toString().toStdString(), attr.value("v").toString().toStdString());
            }
        } else if (reader.isEndElement() && reader.name() == u"way") {
            way = nullptr;
        }
    }
}

size_t MapDataOSM::loadFromApiXml(const QList<QByteArray> &responses){
    items.clear();
    apiNodes.clear();
    apiWays.clear();
    for (const QByteArray &r : responses) parseApi(r);
    buildApiItems();
    return items.size();
}

void MapDataOSM::buildApiItems(){
    using namespace Osm;
    const FeatureClasses &classes = FeatureClasses::standard();
    std::vector<int64_t> ids;
    for (const auto &w : apiWays) ids.push_back(w.first);
    std::sort(ids.begin(), ids.end());
    for (int64_t id : ids) {
        const ApiWay &w = apiWays[id];
        if (w.tags.empty()) continue;
        const Classification c = classes.classify(uint32_t(w.tags.size()), [&](uint32_t i) { return Tag{w.tags[i].first, w.tags[i].second}; });
        std::vector<Location> locations;
        for (int64_t ref : w.refs) {
            auto it = apiNodes.find(ref);
            if (it != apiNodes.end()) locations.push_back(Location::fromDegrees(it->second.second, it->second.first));
        }
        if (locations.size() < 2) continue;
        Item item;
        item.style = &classes.style(c);
        item.slot = slotOf(c);
        item.area = item.style->hasFill && locations.size() >= 4 && w.refs.front() == w.refs.back();
        std::vector<QPointF> points;
        for (const Location &l : locations) points.push_back(project(l.lat(), l.lon()));
        if (item.area && !counterClockwise(locations.data(), uint32_t(locations.size()))) std::reverse(points.begin(), points.end());
        item.rings.push_back(std::move(points));
        addItem(std::move(item));
    }
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.slot < b.slot; });
    hasData = true;
}

void MapDataOSM::paint(QPainter &painter, const QRectF &unitArea) const {
    struct Group {
        const Osm::Style *style;
        QPainterPath fill, outline, line;
        std::vector<QPainterPath> casings;
    };
    auto addRing = [](QPainterPath &path, const std::vector<QPointF> &ring) {
        path.moveTo(ring.front());
        for (size_t i = 1; i < ring.size(); ++i) path.lineTo(ring[i]);
    };
    // Per slot: all fills, then outlines, casings and lines, each batched per style.
    for (size_t i = 0; i < items.size();) {
        const int slot = items[i].slot;
        size_t end = i;
        while (end < items.size() && items[end].slot == slot) ++end;
        std::vector<Group> groups;
        size_t maxCasings = 0;
        for (size_t k = i; k < end; ++k) {
            const Item &item = items[k];
            // Margin: strokes reach past the geometry, and straight lines have empty bounds.
            if (!item.bounds.adjusted(-MaxStrokeUnits, -MaxStrokeUnits, MaxStrokeUnits, MaxStrokeUnits).intersects(unitArea)) continue;
            auto g = std::find_if(groups.begin(), groups.end(), [&](const Group &x) { return x.style == item.style; });
            if (g == groups.end()) {
                groups.push_back({item.style, {}, {}, {}, std::vector<QPainterPath>(item.style->casings.size())});
                g = groups.end() - 1;
                g->fill.setFillRule(Qt::WindingFill);
            }
            const Osm::Style &s = *item.style;
            maxCasings = std::max(maxCasings, s.casings.size());
            for (const auto &ring : item.rings) {
                if (item.area && s.hasFill) addRing(g->fill, ring);
                if (s.hasOutline) addRing(g->outline, ring);
                for (auto &c : g->casings) addRing(c, ring);
                if (s.hasLine) addRing(g->line, ring);
            }
        }
        for (const Group &g : groups) if (!g.fill.isEmpty()) painter.fillPath(g.fill, QColor::fromRgba(g.style->fill));
        for (const Group &g : groups) if (!g.outline.isEmpty()) painter.strokePath(g.outline, pen(g.style->outline));
        for (size_t c = 0; c < maxCasings; ++c)
            for (const Group &g : groups) if (c < g.casings.size() && !g.casings[c].isEmpty()) painter.strokePath(g.casings[c], pen(g.style->casings[c]));
        for (const Group &g : groups) if (!g.line.isEmpty()) painter.strokePath(g.line, pen(g.style->line));
        i = end;
    }
}

bool MapDataOSM::draw(QImage* myImage) {
    if (!hasData) return false;
    const QColor background = QColor::fromRgba(Osm::FeatureClasses::standard().background());
    const double scale = myImage->height() / level;  // pixels per tile unit, as in the legacy drawing
    const double opacity = (255.0 - MapWindow::isAlpha) / 255.0;
    const int w = myImage->width(), h = myImage->height(), hw = w / 2, hh = h / 2;
    const QRect quadrants[4] = {QRect(0, 0, hw, hh), QRect(hw, 0, w - hw, hh), QRect(0, hh, hw, h - hh), QRect(hw, hh, w - hw, h - hh)};
    const int bytesPerPixel = myImage->depth() / 8;
    uchar *bits = myImage->bits();
    const qsizetype bytesPerLine = myImage->bytesPerLine();
    const QImage::Format format = myImage->format();
    // Four painters on four parts of the same buffer: the raster engine is CPU-bound.
    auto paintQuadrant = [&](const QRect &q) {
        if (q.isEmpty()) return;
        QImage part(bits + q.y() * bytesPerLine + q.x() * bytesPerPixel, q.width(), q.height(), bytesPerLine, format);
        QPainter painter(&part);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setOpacity(opacity);
        painter.fillRect(part.rect(), background);
        painter.translate(-q.x(), -q.y());
        painter.scale(scale, scale);
        paint(painter, QRectF(q.x() / scale, q.y() / scale, q.width() / scale, q.height() / scale));
    };
    std::vector<std::thread> workers;
    for (int q = 1; q < 4; ++q) workers.emplace_back(paintQuadrant, quadrants[q]);
    paintQuadrant(quadrants[0]);
    for (auto &t : workers) t.join();
    return true;
}
