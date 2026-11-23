#pragma once
#include <QByteArray>
#include <QPair>
#include <QString>
#include <QVariant>
#include <QVector>
#include <functional>
#include <memory>
#include <vector>
#include <tsre/trains/CabPicker.h>

class ComplexShape;

// The values a dragged control's stops send, in order, one list a control
// (empty: worked by clicks); its loco script's (task maszyna 04), given
// with the controls' values.
using CabStops = std::vector<std::vector<double>>;

// A control opening the cab to the outside: a window or a side door (task
// sound 04, a MaSzyna cab's universals found in its model).
struct CabOpening {
    int control = -1;
    // The driver's left (-1) or right (1) in its cab.
    int side = 0;
    // Its side's largest: the one the window keys work.
    bool main = false;
    // Open at value 0, shut at 1 (the 4e grey's and EP08's drop windows).
    bool inverted = false;
    // Its parts' centre at rest in the cab shape's space, and their size
    // across the car's side (height times length, m²).
    float place[3] = {0.0f, 0.0f, 0.0f};
    float area = 0.0f;
    // How far open a shown value is, 0 to 1.
    float share(double value) const {
        const float v = value < 0.0 ? 0.0f : value > 1.0 ? 1.0f : float(value);
        return inverted ? 1.0f - v : v;
    }
};

// One car's 3D cab shape posed by its controls (task maszyna 04): the
// shape's parts bound to the controls once, posed in the car's own state,
// and what the mouse needs to drag them.
class CabPose {
  public:
    virtual ~CabPose() = default;
    // What a pose takes besides the values: the seconds since the last one
    // (`first`: none before), the car's animated groups (CarAnimation's;
    // Open Rails' outside parts follow them), whether the cab is its rear
    // one.
    struct Moment {
        float dt = 0.0f;
        bool first = false;
        const bool *wanted = nullptr;
        bool rear = false;
    };
    // The parts posed in `state` for `values`, one a control (NaN, or none
    // given: its parts keep their pose).
    virtual void pose(ComplexShape &cab, unsigned int state, const std::vector<double> &values,
                      const Moment &moment) = 0;
    // Reads the interactive parts for the mouse: run on a worker, its
    // result taken on the main thread (CabPicker::load).
    virtual std::function<CabPicker::Loaded()> pickerJob() const = 0;
    // The loco script's stops, for controls whose stops it gives.
    virtual void setStops(const std::shared_ptr<const CabStops> &stops) { (void)stops; }
    // Dragged along its path (levers, switches of several states), not
    // clicked.
    virtual bool dragged(int control) const = 0;
    // Worked by the driver (the mouse's parts: levers, switches, handles),
    // not a display (needles, clocks, lamps): its parts at rest go into
    // the car's lamps' maps in its frame (task renderer 46 C).
    virtual bool interactive(int control) const {
        (void)control;
        return false;
    }
    // A dragged control's stops along its travel, at most `samples` of
    // them, in order, and the one it shows now; false where it has none.
    virtual bool stops(const ComplexShape &cab, int control, int part, int samples, std::vector<float> &stops,
                       float &current) const = 0;
    // A part's matrix relative to its parent at a stop of its control.
    virtual bool partAt(const ComplexShape &cab, int control, int part, float stop, float *local) const = 0;
    // The value a stop of a control sends; NaN none.
    virtual double valueAt(int control, float stop) const = 0;
    // Where a control's part is in the vehicle's model space (its sounds',
    // task maszyna 05); false where it has none.
    virtual bool controlPlace(const ComplexShape &cab, int control, float *place) const {
        (void)cab;
        (void)control;
        (void)place;
        return false;
    }
    // The value a control's parts show now, following the value given with
    // its friction (task sound 04: an opening heard as it moves); false
    // where it shows none yet.
    virtual bool shown(int control, double &value) const {
        (void)control;
        (void)value;
        return false;
    }
};

// A 3D cab's controls, whatever describes them (task maszyna 04): an Open
// Rails .cvf matched to an S file cab (CvfCabControls, task driving 08), or
// a MaSzyna .mmd's cab (MaszynaCabControls). Made once, shared by every car
// of a definition; a control is its index. Its loco script gives their
// values and turns the mouse's work into commands, in the Lua module it
// names.
class CabControls {
  public:
    virtual ~CabControls() = default;
    virtual int count() const = 0;
    // Shown by the mouse resting on it.
    virtual QString name(int control) const = 0;
    // For logs and tests' lists.
    virtual QString describe(int control) const = 0;
    // Drawn with the car's body: a MaSzyna cab model goes on top of the
    // vehicle. An Open Rails 3D cab is drawn instead: its loco's shape has
    // a low-poly cab merged in that no cab could replace.
    virtual bool drawsBody() const = 0;
    // The Lua module and each control's table for it.
    virtual QByteArray module() const = 0;
    virtual void fields(int control, QVector<QPair<QByteArray, QVariant>> &out) const = 0;
    // Its parts in a loaded cab shape, read from `shapePath` again for the
    // mouse.
    virtual std::unique_ptr<CabPose> bind(const ComplexShape &cab, const QString &shapePath) const = 0;
    // Its controls opening the cab to the outside (task sound 04); none
    // where it tells none (a .cvf's windows are the car's animations).
    virtual const std::vector<CabOpening> &openings() const {
        static const std::vector<CabOpening> none;
        return none;
    }
};
