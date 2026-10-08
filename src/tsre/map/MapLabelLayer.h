/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MAPLABELLAYER_H
#define MAPLABELLAYER_H

#include <QColor>
#include <QFont>
#include <QSize>
#include <QHash>
#include <QImage>
#include <QRect>
#include <QString>
#include <memory>
#include <vector>

class MapView;
class OglObj;
class RenderQueue;
class Texture;
struct MapPalette;

// What a label names: its dot takes the palette colour of the kind.
enum class MapLabelKind : unsigned char { Marker, Station, Platform, Siding, Event, Place, OsmStation, Count };

// A named point for the map (task editor 04, labels; docs/tasks/geo/osm-rendering-design.md).
struct MapLabel {
    // Position in the editor's tile convention (the camera's), metres in the tile.
    int tileX = 0;
    int tileZ = 0;
    float x = 0.0f;
    float z = 0.0f;
    QString text;
    // Higher is placed first; equal ones keep their order.
    double priority = 0.0;
    // Drawn larger and bold (capitals, region seats, stations).
    bool major = false;
    MapLabelKind kind = MapLabelKind::Marker;
    // Shown at this resolution and finer (metres per pixel); 0: at every one.
    float maxMetresPerPixel = 0.0f;
};

// Rank of a place name from its GeoNames feature code and population: capitals,
// region seats and lower seats first, then by population; major sets the larger name.
double placeLabelPriority(const QString &featureCode, qint64 population, bool *major = nullptr);

// Label strings painted once by QPainter (fonts, shaping, a halo) into shared
// texture pages, packed in shelves. Pages are uploaded whole the first time and
// by changed rows after; when every page is full the atlas starts again.
class MapLabelAtlas {
public:
    static constexpr int PageSize = 1024;
    static constexpr int MaxPages = 4;
    struct Entry {
        int page = -1;
        QRect rect;  // in the page, pixels
    };

    MapLabelAtlas();
    ~MapLabelAtlas();
    // The size a label is painted at (pixels), without painting it.
    QSize measure(const QString &text, bool major) const;
    // The label's place in a page, painted on first use; null when it cannot fit at all.
    const Entry *get(const QString &text, bool major);
    // Colours and pixel ratio: a change empties the atlas.
    void setStyle(const QColor &text, const QColor &halo, float pixelRatio);
    // Sends painted rows to the textures; texture ids per page after.
    void upload();
    int textureId(int page) const;
    int pageCount() const { return int(pages.size()); }
    // Empties the pages (labels are painted again when asked for).
    void clear();
    // Bumped by clear(): entries taken before it are no longer valid.
    int generation() const { return clears; }

private:
    struct Page;
    QFont font(bool major) const;
    const Entry *paint(const QString &key, const QString &text, bool major);
    std::vector<std::unique_ptr<Page>> pages;
    QHash<QString, Entry> entries;
    QColor textColor = Qt::black;
    QColor haloColor = Qt::white;
    float ratio = 1.0f;
    int clears = 0;
};

// Map labels: each label's dot and name at a fixed screen size and upright,
// whatever the zoom and heading, with names that would overlap left out (by
// priority) and each name tried above, right of, below and left of its dot.
class MapLabelLayer {
public:
    // Above the route's data (TrackItemMapLayer 600, ActivityMapLayer 700), under the
    // pointer (900).
    static constexpr float DotHaloHeight = 805.0f;
    static constexpr float DotHeight = 810.0f;
    static constexpr float TextHeight = 820.0f;
    static constexpr float DotPixels = 5.0f;
    static constexpr float GapPixels = 2.0f;

    MapLabelLayer();
    ~MapLabelLayer();
    // Replaces the labels (sorted by priority here).
    void setLabels(std::vector<MapLabel> labels);
    // The palette colour of a kind's dots.
    static QColor dotColour(const MapPalette &palette, MapLabelKind kind);
    size_t labelCount() const { return labels.size(); }
    void pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette, float pixelRatio);
    // Labels placed in the last build.
    size_t placedCount() const { return placed; }

    // Placement, public for tests. A candidate: its dot's screen centre and its name's
    // size (pixels), in priority order. Returns the accepted names' top-left corners,
    // or -1 for a left out one.
    // Candidates with the same non-zero key (the same name) within duplicatePixels of
    // one already placed are left out: a town named by two sources shows once.
    struct Candidate {
        float x = 0.0f, y = 0.0f;
        int width = 0, height = 0;
        unsigned int key = 0;
    };
    static std::vector<QPoint> place(const std::vector<Candidate> &candidates, int screenWidth, int screenHeight,
                                     float dotPixels, float gapPixels, float duplicatePixels = 0.0f);
    static constexpr float DuplicatePixels = 120.0f;

private:
    void build(const MapView &view, const MapPalette &palette, float pixelRatio);

    std::vector<MapLabel> labels;
    MapLabelAtlas atlas;
    bool dirty = true;
    size_t placed = 0;
    // What the last build was for.
    int builtTile[2] = {0, 0};
    float builtView[6] = {0, 0, 0, 0, 0, 0};
    QString builtPalette;
    float builtRatio = 0.0f;
    std::vector<std::unique_ptr<OglObj>> textObjects;  // one per atlas page
    std::unique_ptr<OglObj> dots[int(MapLabelKind::Count)];
    std::unique_ptr<OglObj> dotHalos;
};

#endif
