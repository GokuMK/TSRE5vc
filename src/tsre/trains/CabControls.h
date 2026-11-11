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
};
