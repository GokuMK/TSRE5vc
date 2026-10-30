#include <tsre/trains/CabPicker.h>
#include <QFileInfo>
#include <QMatrix4x4>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tsre/math3d/Intersections.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/trains/CabView.h>

bool CabPicker::interactive(const QString &kind) {
    for (const char *k : {"Lever", "TwoState", "TriState", "MultiState", "CombinedControl"})
        if (kind.compare(QLatin1String(k), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

void CabPicker::add(Part part) {
    // Its sphere: round the middle of its box, through its farthest point.
    const std::vector<float> &p = part.points;
    if (p.size() < 9)
        return;
    float low[3] = {p[0], p[1], p[2]}, high[3] = {p[0], p[1], p[2]};
    for (size_t i = 0; i + 2 < p.size(); i += 3)
        for (int k = 0; k < 3; ++k) {
            low[k] = std::min(low[k], p[i + size_t(k)]);
            high[k] = std::max(high[k], p[i + size_t(k)]);
        }
    float farthest = 0.0f;
    for (int k = 0; k < 3; ++k)
        part.center[k] = 0.5f * (low[k] + high[k]);
    for (size_t i = 0; i + 2 < p.size(); i += 3) {
        const float dx = p[i] - part.center[0], dy = p[i + 1] - part.center[1], dz = p[i + 2] - part.center[2];
        farthest = std::max(farthest, dx * dx + dy * dy + dz * dz);
    }
    part.radius = std::sqrt(farthest);
    list.push_back(std::move(part));
}

std::shared_ptr<const CabPicker> CabPicker::make(const QString &shapePath, const CabView &view,
                                                 const CabParts &parts) {
    const QFileInfo file(shapePath);
    SFileComplex shape(shapePath, file.fileName(), file.path());
    if (!shape.loadData())
        return nullptr;
    auto picker = std::make_shared<CabPicker>();
    for (const CabParts::Control &c : parts.controls) {
        if (c.control < 0 || size_t(c.control) >= view.controls().size()
                || !interactive(view.controls()[size_t(c.control)].kind))
            continue;
        for (int index : c.parts) {
            Part part;
            part.part = index;
            part.control = c.control;
            if (shape.partTriangles(index, part.points, part.uvs))
                picker->add(std::move(part));
        }
    }
    return picker;
}

CabPicker::Hit CabPicker::pick(const float *origin, const float *direction,
                               const std::function<bool(int part, float *matrix)> &place) const {
    Hit best;
    float bestDistance = std::numeric_limits<float>::max();
    const QVector3D from(origin[0], origin[1], origin[2]), way(direction[0], direction[1], direction[2]);
    const float wayLength = way.length();
    if (wayLength <= 0.0f)
        return best;
    for (const Part &part : list) {
        QMatrix4x4 m;
        if (!place(part.part, m.data()))
            continue;
        bool invertible = false;
        const QMatrix4x4 inverse = m.inverted(&invertible);
        if (!invertible)
            continue;
        // The ray in the part's space.
        const QVector3D o = inverse.map(from), d = inverse.mapVector(way);
        const float local[3] = {o.x(), o.y(), o.z()}, along[3] = {d.x(), d.y(), d.z()};
        float t = 0.0f;
        if (!Intersections::raySphere(local, along, part.center, part.radius, t))
            continue;
        const std::vector<float> &p = part.points;
        for (size_t i = 0; i + 8 < p.size(); i += 9) {
            float u = 0.0f, v = 0.0f;
            if (!Intersections::rayTriangle(local, along, &p[i], &p[i + 3], &p[i + 6], t, u, v))
                continue;
            // Back in the ray's space, where the parts compare.
            const QVector3D at = m.map(QVector3D(o + d * t));
            const float distance = (at - from).length() / wayLength;
            if (distance >= bestDistance)
                continue;
            bestDistance = distance;
            best.control = part.control;
            best.part = part.part;
            best.distance = distance;
            best.point[0] = at.x();
            best.point[1] = at.y();
            best.point[2] = at.z();
            const size_t triangle = i / 9 * 6;
            if (triangle + 5 < part.uvs.size()) {
                const float *uv = &part.uvs[triangle];
                best.uv[0] = uv[0] + u * (uv[2] - uv[0]) + v * (uv[4] - uv[0]);
                best.uv[1] = uv[1] + u * (uv[3] - uv[1]) + v * (uv[5] - uv[1]);
            }
        }
    }
    return best;
}
