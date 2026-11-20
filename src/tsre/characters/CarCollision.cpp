/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#include "CarCollision.h"

#include <tsre/characters/ShapeCollision.h>
#include <tsre/humans/StationPeople.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/trains/CarPlacement.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/VehicleDefinition.h>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace Characters {

void CarCollision::clear() {
    collisions.clear();
    collisions.clearOpenings();
    passagesRead = false;
    collisions.setTerrain({});
    of = nullptr;
    half[0] = half[1] = tall = 0.0f;
}

void CarCollision::build(const Eng *eng) {
    clear();
    if (eng == nullptr || !eng->definition)
        return;
    of = eng;
    const VehicleDefinition &definition = *eng->definition;
    float identity[16];
    Mat4::identity(identity);
    float bounds[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    bool bounded = false;
    auto add = [&](const QString &path, const float *matrix) {
        if (path.isEmpty())
            return;
        const auto mesh = shapeCollision(path, QFileInfo(path).path(), Collision::Default);
        if (!mesh || mesh->triangleCount() == 0)
            return;
        collisions.add(mesh, matrix);
        if (!bounded) {
            std::copy(mesh->bounds(), mesh->bounds() + 6, bounds);
            bounded = true;
        }
    };
    add(definition.shapePath, identity);
    for (const VehicleDefinition::FreightAnim &anim : definition.freightAnims) {
        if (anim.shape.length() <= 1)
            continue;
        float matrix[16];
        Mat4::identity(matrix);
        Mat4::translate(matrix, matrix, -anim.offset[0], anim.offset[1], -anim.offset[2]);
        add(anim.path, matrix);
    }
    QSet<QString> cabs;
    const std::shared_ptr<const VehicleDefinition::Cab3D> cab = definition.cab3DOf();
    const int viewpoints = cab ? int(cab->viewpoints.size()) : 0;
    for (int i = 0; i < std::max(1, viewpoints); ++i) {
        const QString path = eng->cab3DShapePath(viewpoints > 0 ? i : -1);
        if (path.isEmpty() || cabs.contains(path) || !QFileInfo::exists(path))
            continue;
        cabs.insert(path);
        add(path, identity);
    }
    // Its footprint: the definition's, else the body's bounds.
    half[0] = definition.size[0] > 0.1f ? definition.size[0] / 2.0f
                                        : std::max(std::fabs(bounds[0]), std::fabs(bounds[3]));
    half[1] = definition.size[2] > 0.1f ? definition.size[2] / 2.0f
                                        : std::max(std::fabs(bounds[2]), std::fabs(bounds[5]));
    tall = definition.size[1] > 0.1f ? definition.size[1] : bounds[4];
    floor = AssumedFloor - CarPlacement::RailHeight;
    const float width = half[0], length = half[1], level = floor;
    collisions.setTerrain([width, length, level](float x, float z, float *height) {
        if (std::fabs(x) > width || std::fabs(z) > length)
            return false;
        *height = level;
        return true;
    });
}

void CarCollision::readPassages() {
    if (passagesRead || of == nullptr)
        return;
    const std::shared_ptr<const ShapeSeats::Seats> seats = Humans::StationPeople::seatsOfCar(*of);
    if (!seats)
        return;
    passagesRead = true;
    constexpr float Radius = 0.3f, Height = 2.1f, Wall = 0.35f;
    for (const ShapeSeats::Passage &passage : seats->passages) {
        const float *p = passage.position;
        const float box[6] = {p[0] - passage.width / 2.0f - Radius, p[1] + 0.05f, p[2] - Wall,
                              p[0] + passage.width / 2.0f + Radius, p[1] + Height, p[2] + Wall};
        collisions.addOpening(box);
    }
}

bool CarCollision::within(float x, float z, float margin) const {
    return of != nullptr && std::fabs(x) <= half[0] - margin && std::fabs(z) <= half[1] - margin;
}

}
