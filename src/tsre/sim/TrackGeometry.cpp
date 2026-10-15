#include <tsre/sim/TrackGeometry.h>

#include <algorithm>
#include <cmath>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/math3d/Vector3f.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TSectionDAT.h>

namespace Sim {

TrackGeometry::TrackGeometry(TDB *tdb) {
    if (tdb == nullptr || tdb->tsection == nullptr)
        return;
    nodes.resize(size_t(std::max(0, tdb->iTRnodes)) + 1);
    std::unordered_map<int, int> shapeOf;
    for (const auto &entry : tdb->trackNodes) {
        const int id = entry.first;
        const TRnode *n = entry.second;
        if (n == nullptr || n->typ != 1 || id <= 0 || size_t(id) >= nodes.size())
            continue;
        Node &node = nodes[size_t(id)];
        node.first = int(placed.size());
        node.complete = true;
        float total = 0.0f;
        for (int i = 0; i < n->iTrv; ++i) {
            const TrackVectorSection &section = n->trVectorSection[i];
            const int index = int(section.sectionIndex);
            auto known = shapeOf.find(index);
            if (known == shapeOf.end()) {
                auto found = tdb->tsection->sekcja.find(index);
                if (found == tdb->tsection->sekcja.end() || found->second == nullptr) {
                    node.complete = false;
                    break;
                }
                shapes.push_back(*found->second);
                known = shapeOf.emplace(index, int(shapes.size()) - 1).first;
            }
            Placed p;
            p.shape = known->second;
            p.length = shapes[size_t(p.shape)].getDlugosc();
            total += p.length;
            p.end = total;
            p.x = section.x;
            p.y = section.y;
            p.z = section.z;
            p.ax = section.ax;
            p.ay = section.ay;
            p.az = section.az;
            p.nextAz = i + 1 < n->iTrv ? n->trVectorSection[i + 1].az : 0.0f;
            p.tileX = section.tileX;
            p.tileZ = section.tileZ;
            placed.push_back(p);
        }
        node.count = int(placed.size()) - node.first;
        node.length = total;
        ++copiedNodes;
    }
}

float TrackGeometry::length(int node) const {
    if (node <= 0 || size_t(node) >= nodes.size())
        return -1.0f;
    return nodes[size_t(node)].length;
}

bool TrackGeometry::pose(int nodeId, float distance, TrackPose &out) const {
    if (nodeId <= 0 || size_t(nodeId) >= nodes.size())
        return false;
    const Node &node = nodes[size_t(nodeId)];
    if (node.count == 0)
        return false;
    // As getDrawPositionOnTrNode: the first section whose end reaches the
    // distance; a node with a missing section answers only before it.
    const auto first = placed.begin() + node.first;
    const auto last = first + node.count;
    const auto it = std::lower_bound(first, last, distance,
                                     [](const Placed &p, float d) { return p.end < d; });
    if (it == last)
        return false;
    const Placed &p = *it;
    const TSection &shape = shapes[size_t(p.shape)];
    const float sDistance = distance - p.end + p.length;
    Vector3f position;
    shape.getDrawPosition(&position, sDistance);
    if (p.az != 0.0f)
        position.rotateZ(p.az, 0.0f);
    float matrix[16];
    float q[4] = {0, 0, 0, 1};
    float rot[3] = {float(M_PI), p.ay, 0};
    float pos[3] = {p.x, p.y, p.z};
    Quat::fromRotationXYZ(q, rot);
    Mat4::fromRotationTranslation(matrix, q, pos);
    Mat4::rotate(matrix, matrix, -p.ax, 1, 0, 0);
    pos[0] = position.x;
    pos[1] = position.y;
    pos[2] = -position.z;
    Vec3::transformMat4(pos, pos, matrix);
    out.x = pos[0];
    out.y = pos[1];
    out.z = pos[2];
    out.yaw = -p.ay - shape.getDrawAngle(sDistance);
    out.pitch = p.ax;
    out.tileX = p.tileX;
    out.tileZ = p.tileZ;
    const float along = p.length > 0.0f ? sDistance / p.length : 0.0f;
    out.roll = -(p.az * (1.0f - along) + p.nextAz * along);
    return true;
}

size_t TrackGeometry::memory() const {
    return nodes.size() * sizeof(Node) + placed.size() * sizeof(Placed) + shapes.size() * sizeof(TSection);
}

}
