/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#ifndef CHARACTERS_COLLISIONWORLD_H
#define CHARACTERS_COLLISIONWORLD_H

#include <tsre/characters/CollisionMesh.h>
#include <functional>
#include <memory>
#include <vector>

namespace Characters {

// What a walking character collides with (task characters 02): collision
// meshes placed in one frame (the world's, about the character: its
// metres from an origin), each by a matrix (a turn, a move and one scale
// for all three axes), and the terrain's height. Placed meshes may move (a
// car): place() again. Queries on the thread that changes it.
class CollisionWorld {
public:
    // Adds a mesh placed by `matrix` (its space to the world's, column
    // major); its index, given again after remove(). `tag` is the caller's.
    // Not `openable`: openings leave its triangles as they are (a gangway's
    // walls inside its own door's opening, task characters 05).
    int add(std::shared_ptr<const CollisionMesh> mesh, const float *matrix, int tag = 0, bool openable = true);
    void place(int index, const float *matrix);
    void remove(int index);
    void clear();
    // How many are placed now.
    int count() const { return int(placed.size() - free.size()); }
    int tagOf(int index) const;
    // The terrain's height at (x, z) in the world's frame; false where there
    // is none. Without it, no ground. A fallback terrain (a car's assumed
    // floor) is a floor only where no mesh is found between the heights
    // asked, and no ray meets it; else the highest of it and the meshes.
    void setTerrain(std::function<bool(float x, float z, float *height)> height, bool fallback = false) {
        terrain = std::move(height);
        terrainFallback = fallback;
    }

    struct Floor {
        float height = 0.0f;
        float normal[3] = {0.0f, 1.0f, 0.0f};
        // The placed mesh, -1 for the terrain.
        int placed = -1;
    };
    // The highest floor under (x, z) between `below` and `above`.
    bool floorAt(float x, float z, float above, float below, Floor &out) const;
    // The nearest thing a ray meets (origin, unit direction) within
    // `length`: how far, its normal against the ray, the placed mesh (-1:
    // the terrain, met in steps of half a metre).
    bool ray(const float *origin, const float *direction, float length, float *distance, float *normal,
             int *placedIndex = nullptr) const;
    // Every placed triangle within `radius` of a segment (CollisionMesh::
    // contacts), in the world's frame; the terrain is not one.
    struct Contact {
        CollisionMesh::Contact contact;
        int placed = -1;
    };
    void contacts(const float *a, const float *b, float radius, std::vector<Contact> &out) const;
    // Openings (task characters 03: a partition's door drawn on its wall,
    // an .sd Passage): boxes in the world's frame (min x, y, z, max x, y,
    // z) where no triangle touches a body (contacts() leaves out those met
    // inside one); floors and rays as they are. Its index, given again
    // after removeOpening() (an end door's, open while its car is coupled
    // to a matching one: task characters 05).
    int addOpening(const float *box);
    void removeOpening(int index);
    void clearOpenings() {
        openings.clear();
        freeOpenings.clear();
    }
    int openingCount() const { return int(openings.size() / 6 - freeOpenings.size()); }

private:
    struct Placed {
        std::shared_ptr<const CollisionMesh> mesh;
        float matrix[16];
        float inverse[16];
        float scale = 1.0f;
        float box[6];
        int tag = 0;
        bool openable = true;
    };
    std::vector<Placed> placed;
    std::vector<int> free;
    std::function<bool(float, float, float *)> terrain;
    bool terrainFallback = false;
    std::vector<float> openings;
    std::vector<int> freeOpenings;
    static void placeOne(Placed &p, const float *matrix);
    void terrainFloor(float x, float z, float height, Floor &out) const;
};

}

#endif
