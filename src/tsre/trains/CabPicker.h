#pragma once
#include <QString>
#include <functional>
#include <memory>
#include <vector>

class CabView;
struct CabParts;

// A 3D cab's interactive parts for the mouse (task driving 08): the parts
// of the controls the mouse works (levers and switches), their triangles
// read from a copy of the cab's shape made for this (the drawn shape keeps
// none on the CPU), a bounding sphere each. A ray is tested against them,
// the spheres first, the nearest triangle met kept. Made once, not changed.
class CabPicker {
  public:
    struct Part {
        // The shape's part (its matrix) and its control in the .cvf.
        int part = -1;
        int control = -1;
        // Its triangles in its own space, three points each (x y z), and
        // their texture coordinates (u v).
        std::vector<float> points;
        std::vector<float> uvs;
        float center[3] = {0.0f, 0.0f, 0.0f};
        float radius = 0.0f;
    };
    struct Hit {
        // -1: nothing met.
        int control = -1;
        int part = -1;
        // How far along the ray, in its direction's lengths; the point met,
        // in the ray's space; its texture coordinates.
        float distance = 0.0f;
        float point[3] = {0.0f, 0.0f, 0.0f};
        float uv[2] = {0.0f, 0.0f};
    };

    // The controls the mouse works: levers and switches (Open Rails' discrete
    // controls, not its displays).
    static bool interactive(const QString &kind);
    // From the cab's shape file, read again on the CPU; null when it cannot be
    // read.
    static std::shared_ptr<const CabPicker> make(const QString &shapePath, const CabView &view, const CabParts &parts);
    // An empty one, parts added by hand (tests).
    CabPicker() = default;
    void add(Part part);

    // The nearest interactive part the ray (origin, direction) meets, each
    // part placed by `place`: its space to the ray's, a column-major matrix
    // (false: left out, as a part not drawn).
    Hit pick(const float *origin, const float *direction,
             const std::function<bool(int part, float *matrix)> &place) const;
    const std::vector<Part> &parts() const { return list; }

  private:
    std::vector<Part> list;
};
