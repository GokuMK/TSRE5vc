/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/maszyna/MaszynaCabControls.h>
#include <tsre/maszyna/E3dFile.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/shape/ComplexShape.h>
#include <QFileInfo>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace {

const char *typeName(MaszynaCab::Type type) {
    switch (type) {
    case MaszynaCab::Type::Push:
        return "push";
    case MaszynaCab::Type::Delayed:
        return "delayed";
    case MaszynaCab::Type::PushToggle:
        return "pushtoggle";
    case MaszynaCab::Type::Toggle:
        break;
    }
    return "toggle";
}

// A cab model's parts bound to an end's controls, as TGauge::Load and
// TButton::Load find them (the cab model only: the low-poly interior's and
// the outside model's later), each control's value shown as TGauge::Update
// moves it.
class MaszynaCabPose : public CabPose {
  public:
    MaszynaCabPose(const MaszynaCab::End &end, const ComplexShape &cab, QString shapePath)
        : end(end), path(std::move(shapePath)) {
        QHash<QString, int> byName;
        for (int i = cab.partCount() - 1; i >= 0; --i)
            byName.insert(cab.partName(i).toLower(), i);
        firstChild.assign(size_t(cab.partCount()), -1);
        nextSibling.assign(size_t(cab.partCount()), -1);
        std::vector<int> lastChild(size_t(cab.partCount()), -1);
        for (int i = 0; i < cab.partCount(); ++i) {
            const int parent = cab.partParent(i);
            if (parent < 0 || parent >= cab.partCount())
                continue;
            if (lastChild[size_t(parent)] < 0)
                firstChild[size_t(parent)] = i;
            else
                nextSibling[size_t(lastChild[size_t(parent)])] = i;
            lastChild[size_t(parent)] = i;
        }
        bound.resize(size_t(end.controls.size()));
        for (int k = 0; k < end.controls.size(); ++k) {
            const MaszynaCab::Control &c = end.controls[k];
            Bound &b = bound[size_t(k)];
            const QString name = c.submodel.toLower();
            if (c.lamp) {
                b.off = byName.value(name + QLatin1String("_off"), -1);
            } else {
                b.part = byName.value(name, -1);
                if (b.part < 0)
                    b.part = byName.value(name + QLatin1String("_off"), -1);
            }
            b.on = byName.value(name + QLatin1String("_on"), -1);
            if (!c.lamp && MaszynaCabControls::worked(c.label))
                for (int part : {b.part, b.on})
                    if (part >= 0)
                        pickable.push_back({part, k});
        }
    }

    void pose(ComplexShape &cab, unsigned int state, const std::vector<double> &values,
              const Moment &moment) override {
        for (size_t k = 0; k < bound.size() && k < values.size(); ++k) {
            if (std::isnan(values[k]))
                continue;
            const MaszynaCab::Control &c = end.controls[int(k)];
            Bound &b = bound[k];
            if (c.lamp) {
                // TButton::Update: <submodel>_on shown while lit, _off while not.
                const bool lit = values[k] > 0.5;
                if (b.posed && lit == b.lit)
                    continue;
                b.lit = lit;
                b.posed = true;
                if (b.on >= 0)
                    cab.enableSubObjByName(state, cab.partName(b.on), lit);
                if (b.off >= 0)
                    cab.enableSubObjByName(state, cab.partName(b.off), !lit);
                continue;
            }
            if (b.part < 0 && b.on < 0)
                continue;
            // TGauge::UpdateValue: a new target where it differs by a
            // hundredth; TGauge::Update: followed with its friction (s), at
            // once where that is under twice the step.
            const float desired = float(values[k]);
            if (!b.posed || std::lround(100.0f * desired) != std::lround(100.0f * b.target))
                b.target = desired;
            if (!b.posed || moment.first) {
                b.value = b.target;
            } else if (b.value != b.target) {
                if (c.friction > 0.0f && moment.dt < 0.5f * c.friction) {
                    b.value += moment.dt * (b.target - b.value) / c.friction;
                    if (std::fabs(b.target - b.value) <= 0.0001f)
                        b.value = b.target;
                } else {
                    b.value = b.target;
                }
            }
            if (b.posed && b.value == b.shown)
                continue;
            b.shown = b.value;
            b.posed = true;
            const float at = MaszynaCabControls::scaled(c, b.value);
            for (int part : {b.part, b.on})
                if (part >= 0)
                    poseGauge(cab, state, c, part, at);
        }
    }

    // The worked controls' parts' triangles, from the cab model read again
    // (the drawn one keeps none on the CPU).
    std::function<CabPicker::Loaded()> pickerJob() const override {
        return [path = path, parts = pickable] {
            CabPicker::Loaded loaded;
            QString error;
            const std::shared_ptr<const E3dModel> model = E3dFile::read(path, &error, true);
            if (model == nullptr)
                return loaded;
            auto picker = std::make_shared<CabPicker>();
            const int stride = E3dModel::VertexFloats;
            for (const auto &[part, control] : parts) {
                if (part < 0 || size_t(part) >= model->parts.size())
                    continue;
                const E3dModel::Part &p = model->parts[size_t(part)];
                CabPicker::Part picked;
                picked.part = part;
                picked.control = control;
                const auto take = [&](unsigned vertex) {
                    const size_t at = size_t(p.firstVertex + vertex) * size_t(stride);
                    if (at + 8 > model->vertices.size())
                        return;
                    picked.points.insert(picked.points.end(), model->vertices.begin() + qsizetype(at),
                                         model->vertices.begin() + qsizetype(at) + 3);
                    picked.uvs.insert(picked.uvs.end(), model->vertices.begin() + qsizetype(at) + 6,
                                      model->vertices.begin() + qsizetype(at) + 8);
                };
                if (p.indexCount > 0) {
                    for (unsigned i = 0; i + 2 < p.indexCount; i += 3)
                        for (unsigned k = 0; k < 3; ++k) {
                            const qsizetype at = qsizetype(p.firstIndex + i + k) * model->indexBytes;
                            if (at + model->indexBytes > model->indices.size())
                                continue;
                            const uchar *b = reinterpret_cast<const uchar *>(model->indices.constData()) + at;
                            take(model->indexBytes == 4 ? unsigned(b[0] | b[1] << 8 | b[2] << 16 | uint(b[3]) << 24)
                                                        : unsigned(b[0] | b[1] << 8));
                        }
                } else {
                    for (unsigned v = 0; v + 2 < p.vertexCount; v += 3)
                        for (unsigned k = 0; k < 3; ++k)
                            take(v + k);
                }
                if (picked.points.size() % 9 == 0)
                    picker->add(std::move(picked));
            }
            loaded.picker = picker;
            return loaded;
        };
    }

    void setStops(const std::shared_ptr<const CabStops> &stops) override { given = stops; }

    bool dragged(int control) const override { return stopsOf(control) != nullptr && stopsOf(control)->size() > 2; }

    // Its stops' indices, at most `samples` of them, and the one nearest
    // the value it shows.
    bool stops(const ComplexShape &cab, int control, int part, int samples, std::vector<float> &out,
               float &current) const override {
        (void)cab;
        (void)part;
        out.clear();
        current = 0.0f;
        const std::vector<double> *list = stopsOf(control);
        if (list == nullptr || list->size() < 2)
            return false;
        const int n = int(list->size()) - 1;
        const int steps = std::clamp(n, 1, std::max(samples, 1));
        for (int k = 0; k <= steps; ++k)
            out.push_back(float(n) * float(k) / float(steps));
        const float shown = bound[size_t(control)].target;
        for (int i = 0; i <= n; ++i)
            if (std::fabs((*list)[size_t(i)] - shown) < std::fabs((*list)[size_t(std::lround(current))] - shown))
                current = float(i);
        return true;
    }

    bool partAt(const ComplexShape &cab, int control, int part, float stop, float *local) const override {
        const std::vector<double> *list = stopsOf(control);
        if (list == nullptr || list->empty())
            return false;
        const MaszynaCab::Control &c = end.controls[control];
        float rest[16];
        if (!cab.partRest(part, rest))
            return false;
        const size_t i = size_t(std::clamp(int(std::lround(stop)), 0, int(list->size()) - 1));
        MaszynaCabControls::posed(c, rest, MaszynaCabControls::scaled(c, float((*list)[i])), local);
        return true;
    }

    double valueAt(int control, float stop) const override {
        const std::vector<double> *list = stopsOf(control);
        if (list == nullptr || list->empty())
            return std::numeric_limits<double>::quiet_NaN();
        return (*list)[size_t(std::clamp(int(std::lround(stop)), 0, int(list->size()) - 1))];
    }

  private:
    struct Bound {
        // A gauge's part (its submodel, else <submodel>_off), a lamp's off
        // part, and their _on part; -1 none.
        int part = -1, off = -1, on = -1;
        float target = 0.0f, value = 0.0f, shown = 0.0f;
        bool lit = false, posed = false;
    };
    const MaszynaCab::End &end;
    QString path;
    std::vector<Bound> bound;
    // The parts the mouse works and their controls.
    std::vector<std::pair<int, int>> pickable;
    std::vector<int> firstChild, nextSibling;
    std::shared_ptr<const CabStops> given;

    const std::vector<double> *stopsOf(int control) const {
        if (given == nullptr || control < 0 || size_t(control) >= given->size() || size_t(control) >= bound.size())
            return nullptr;
        return &(*given)[size_t(control)];
    }

    void turn(ComplexShape &cab, unsigned int state, int part, float degrees) {
        float rest[16], out[16];
        if (part < 0 || !cab.partRest(part, rest))
            return;
        Mat4::rotate(out, rest, degrees * float(M_PI) / 180.0f, 0.0f, 1.0f, 0.0f);
        cab.setPartTransform(state, part, out);
    }

    // TGauge::UpdateAnimation.
    void poseGauge(ComplexShape &cab, unsigned int state, const MaszynaCab::Control &c, int part, float at) {
        switch (c.animation) {
        case MaszynaCab::Animation::Rotate:
        case MaszynaCab::Animation::Move: {
            float rest[16], out[16];
            if (!cab.partRest(part, rest))
                return;
            MaszynaCabControls::posed(c, rest, at, out);
            cab.setPartTransform(state, part, out);
            return;
        }
        case MaszynaCab::Animation::Wiper: {
            // The submodel, its child and that one's child, each turned.
            turn(cab, state, part, at * 360.0f);
            const int child = firstChild[size_t(part)];
            turn(cab, state, child, at * 360.0f);
            if (child >= 0)
                turn(cab, state, firstChild[size_t(child)], at * 360.0f);
            return;
        }
        case MaszynaCab::Animation::Digital: {
            // Its children named by a digit, each turned -36° a digit of the
            // value's whole number: 0 the units, 1 the tens ...
            std::string digits = "000000000" + std::to_string(int(std::floor(at)));
            if (digits.size() > 10)
                digits.erase(0, digits.size() - 10);
            for (int child = firstChild[size_t(part)]; child >= 0; child = nextSibling[size_t(child)]) {
                const QString name = cab.partName(child);
                if (name.isEmpty() || !name[0].isDigit())
                    continue;
                const int place = 9 - name[0].digitValue();
                if (place >= 0 && size_t(place) < digits.size())
                    turn(cab, state, child, -36.0f * float(digits[size_t(place)] - '0'));
            }
            return;
        }
        case MaszynaCab::Animation::None:
            return;
        }
    }
};

// "main_on_bt": "Main on"; "i-mainbreaker": "Mainbreaker".
QString readable(QString label) {
    if (label.startsWith(QLatin1String("i-")))
        label.remove(0, 2);
    for (const char *suffix : {"_bt", "_sw"})
        if (label.endsWith(QLatin1String(suffix)))
            label.chop(3);
    label.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!label.isEmpty())
        label[0] = label[0].toUpper();
    return label;
}

}

MaszynaCabControls::MaszynaCabControls(std::shared_ptr<const MaszynaVehicle> vehicle, int end)
    : vehicle(std::move(vehicle)) {
    cabEnd = &this->vehicle->cab().ends[std::clamp(end, 0, 2)];
}

int MaszynaCabControls::count() const {
    return int(cabEnd->controls.size());
}

QString MaszynaCabControls::name(int control) const {
    return control >= 0 && control < count() ? readable(cabEnd->controls[control].label) : QString();
}

QString MaszynaCabControls::describe(int control) const {
    if (control < 0 || control >= count())
        return QString();
    const MaszynaCab::Control &c = cabEnd->controls[control];
    return QString("%1 %2 %3").arg(c.label, c.submodel, c.lamp ? QStringLiteral("lamp") : c.animationName);
}

void MaszynaCabControls::fields(int control, QVector<QPair<QByteArray, QVariant>> &out) const {
    out.clear();
    if (control < 0 || control >= count())
        return;
    const MaszynaCab::Control &c = cabEnd->controls[control];
    out = {{"label", c.label},
           {"type", QString::fromLatin1(typeName(c.type))},
           {"lamp", c.lamp},
           {"animation", c.animationName},
           {"index1", c.index[0]},
           {"index2", c.index[1]}};
    if (!c.word.isEmpty())
        out.push_back({"word", c.word});
}

std::unique_ptr<CabPose> MaszynaCabControls::bind(const ComplexShape &cab, const QString &shapePath) const {
    return std::make_unique<MaszynaCabPose>(*cabEnd, cab, shapePath);
}

bool MaszynaCabControls::worked(const QString &label) {
    static const char *const levers[] = {"mainctrl", "scndctrl", "jointctrl", "dirkey", "brakectrl", "localbrake",
                                         "alarmchain", "manualbrake", "shuntmodepower"};
    if (label.endsWith(QLatin1String("_sw")) || label.endsWith(QLatin1String("_bt")))
        return true;
    if (label.startsWith(QLatin1String("universal")) && label.size() > 9 && label[9].isDigit())
        return true;
    for (const char *name : levers)
        if (label == QLatin1String(name))
            return true;
    return false;
}

float MaszynaCabControls::scaled(const MaszynaCab::Control &control, float value) {
    if (!control.interpolated)
        return value * control.scale + control.offset;
    const float share = control.endValue != 0.0f ? std::clamp(value / control.endValue, 0.0f, 1.0f) : 1.0f;
    return value * (control.scale + (control.endScale - control.scale) * share) + control.offset;
}

void MaszynaCabControls::posed(const MaszynaCab::Control &control, const float *rest, float scaledValue, float *out) {
    float from[16];
    std::copy(rest, rest + 16, from);
    if (control.animation == MaszynaCab::Animation::Move)
        Mat4::translate(out, from, 0.0f, 0.0f, scaledValue);
    else if (control.animation == MaszynaCab::Animation::Rotate)
        Mat4::rotate(out, from, scaledValue * 2.0f * float(M_PI), 0.0f, 1.0f, 0.0f);
    else
        std::copy(rest, rest + 16, out);
}

std::shared_ptr<const VehicleDefinition::Cab3D> MaszynaCabControls::cab3D(const VehicleDefinition &definition) {
    if (definition.maszyna == nullptr)
        return nullptr;
    const MaszynaVehicle &vehicle = *definition.maszyna;
    const MaszynaCab &cab = vehicle.cab();
    auto made = std::make_shared<VehicleDefinition::Cab3D>();
    made->mirrored = false;
    for (int n : {1, 2}) {
        const MaszynaCab::End &end = cab.ends[n];
        if (!end.defined || !end.hasPosition || end.model.isEmpty() || end.model == QLatin1String("none"))
            continue;
        const QString path = MaszynaVehicle::modelPath(vehicle.folder, end.model, definition.skin);
        if (!QFileInfo(path).isFile())
            continue;
        VehicleDefinition::Cab3D::Viewpoint viewpoint;
        // MaSzyna hides its low-poly interior's cab<N> from cab N's detailed
        // model (TDynamicObject's LowPolyIntCabs), all three with jointcabs.
        viewpoint.hiddenParts = vehicle.jointCabs ? QStringList{"cab0", "cab1", "cab2"}
                                                  : QStringList{QString("cab%1").arg(n)};
        std::copy(end.position, end.position + 3, viewpoint.position);
        // Looking toward its end (cab 1 the model's +Z, its front), turned
        // by its angle (yaw, pitch).
        viewpoint.start[1] = (n == 2 ? 180.0f : 0.0f) + (end.hasAngle ? end.angle[0] : 0.0f);
        viewpoint.start[0] = end.hasAngle ? end.angle[1] : 0.0f;
        viewpoint.shape = path;
        // MaSzyna keeps the head within its box, 0.5 to 1.8 m over the
        // floor its y is (TTrain's clamp_inside, Train.cpp:10862-10875).
        viewpoint.hasBox = true;
        const float box[6] = {std::min(end.box[0], end.box[3]), std::min(end.box[1], end.box[4]) + 0.5f,
                              std::min(end.box[2], end.box[5]), std::max(end.box[0], end.box[3]),
                              std::max(end.box[1], end.box[4]) + 1.8f, std::max(end.box[2], end.box[5])};
        std::copy(box, box + 6, viewpoint.box);
        made->shapes << end.model;
        made->viewpoints << viewpoint;
        // Seated at the desk (MaSzyna's driver<N>sitpos:), the next place
        // [1] goes to.
        if (end.hasSitting && !std::equal(end.sitting, end.sitting + 3, end.position)) {
            std::copy(end.sitting, end.sitting + 3, viewpoint.position);
            made->viewpoints << viewpoint;
        }
        made->controls[n - 1] = std::make_shared<MaszynaCabControls>(definition.maszyna, n);
        // Its light lights the low-poly interior's cab<N> (set_cab_lights).
        made->lightClass[n - 1] = QString("cab%1").arg(n);
    }
    return made->viewpoints.isEmpty() ? nullptr : made;
}
