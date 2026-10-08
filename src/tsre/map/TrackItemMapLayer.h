/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TRACKITEMMAPLAYER_H
#define TRACKITEMMAPLAYER_H

#include <QColor>
#include <QHash>
#include <QString>
#include <memory>
#include <vector>

class GameObj;
class MapSelection;
class MapView;
class OglObj;
class RenderQueue;
class Route;
class TDB;
struct MapPalette;

// Track objects in map mode (the Map menu's Track Objects): signals, speed
// posts, platforms, sidings, car spawners, level crossings, hazards,
// pickups and sound regions, as filled circles with a border of a fixed
// screen size, in the colours the 3D view gives their objects.
//
// Zoomed out (the view spans WorldObjectExtentMetres or more) the circles
// come from the track and road databases' items. Zoomed in, they come from
// the world objects of the tiles in view, which the map loads (only the
// track objects are read, no shapes), so they can later be selected and
// edited as objects. Objects with two items (platforms, sidings, car
// spawners) also draw the line they give the map (WorldObj::getMapLine)
// along the track between them, LinePixels wide with the same border.
class TrackItemMapLayer {
public:
    enum Kind {
        Signal, SpeedPost, Platform, Siding, CarSpawner, LevelCrossing, Hazard, Pickup,
        SoundRegion, KindCount
    };
    static constexpr float WorldObjectExtentMetres = 3.0f * 2048.0f;
    static constexpr float MarkerPixels = 11.0f;
    static constexpr float LinePixels = 3.0f;
    static constexpr float BorderPixels = 1.5f;
    // Selected objects get a halo of this many pixels around the border.
    static constexpr float HaloPixels = 3.0f;
    static constexpr float HaloHeight = 595.0f;
    static constexpr float BorderHeight = 600.0f;
    static constexpr float FillHeight = 610.0f;

    TrackItemMapLayer();
    ~TrackItemMapLayer();
    void pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette,
                         Route *route, TDB *track, TDB *road);
    // The markers last drawn, with the 3D view's selection IDs: database
    // items zoomed out, world objects (and their parts) zoomed in.
    void pushSelection(MapSelection &selection, const MapView &view) const;
    // The databases or world objects changed: positions are found again.
    void invalidate();
    // Builds the geometry again on the next draw (the selection changed).
    void rebuild() { valid = false; }

    // The kind of a database item or a world object; -1 for what the map
    // does not draw. Public for tests.
    static int kindOfItemType(const QString &type);
    static int kindOfObjectType(int typeId);
    static QColor colour(const MapPalette &palette, int kind);
    // A filled circle on the ground, as triangles wound for the map's face
    // culling.
    static void appendCircle(std::vector<float> &out, float x, float y, float z, float radius);
    // Whether a view of that size draws the world objects' items.
    static bool drawsWorldObjects(const MapView &view);

private:
    struct Position {
        int tileX = 0;
        int tileZ = 0;
        float x = 0.0f;
        float z = 0.0f;
        int kind = -1;
        // What a pick selects: the database item, or the world object (its
        // tile in the view's convention, key and part) zoomed in.
        int database = 0;
        unsigned int itemId = 0;
        int objectTileX = 0;
        int objectTileZ = 0;
        int objectKey = -1;
        int part = 0;
        GameObj *object = nullptr;
    };
    struct Index {
        bool built = false;
        QHash<int, Position> items;
    };
    void buildIndex(Index &index, TDB *database, int databaseKind);
    void collectDatabaseMarkers(std::vector<Position> &markers, const int *tiles);
    // Also the objects' lines, by kind, relative to the view's tile, and the
    // selected objects' lines again in lines[KindCount].
    void collectObjectMarkers(std::vector<Position> &markers, std::vector<float> *lines,
                              const MapView &view, Route *route, const int *tiles);
    // The objects of the loaded tiles in a range, loading the tiles.
    static int objectCount(Route *route, const int *tiles);
    void build(const MapView &view, const MapPalette &palette, Route *route, const int *tiles,
               bool worldObjects);

    Index trackIndex;
    Index roadIndex;
    bool valid = false;
    int builtTileX = 0;
    int builtTileZ = 0;
    float builtMetresPerPixel = 0.0f;
    bool builtWorldObjects = false;
    int builtTiles[4] = {0, 0, 0, 0};
    int builtObjects = 0;
    QString builtPalette;
    std::vector<Position> builtMarkers;
    std::unique_ptr<OglObj> halos;
    std::unique_ptr<OglObj> borders;
    std::unique_ptr<OglObj> fills[KindCount];
};

#endif
