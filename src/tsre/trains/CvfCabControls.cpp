#include <tsre/trains/CvfCabControls.h>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tsre/shape/ComplexShape.h>
#include <tsre/trains/CabView.h>
#include <tsre/trains/CarAnimation.h>

namespace {

// A cab shape's parts matched to the .cvf's controls: each control's frame
// shown (-1 none yet), its outside parts' frames and the frames shown.
class CvfCabPose : public CabPose {
  public:
    CvfCabPose(std::shared_ptr<const CabView> view, float split, const ComplexShape &cab, QString shapePath)
        : view(std::move(view)), split(split), path(std::move(shapePath)) {
        QStringList names;
        std::vector<float> last;
        for (int i = 0; i < cab.partCount(); ++i) {
            names << cab.partName(i);
            last.push_back(cab.partLastFrame(i));
        }
        parts = std::make_shared<CabParts>(CabParts::match(this->view.get(), names, last));
        frames.assign(parts->controls.size(), -1.0f);
        outsideFrames.assign(parts->outside.size(), 0.0f);
        outsideShown.assign(parts->outside.size(), -1.0f);
        // With TSRE_CAB_LOG set: its outside parts, to their last key frame.
        static const bool log = qEnvironmentVariableIsSet("TSRE_CAB_LOG");
        if (log)
            for (const CabParts::Outside &o : parts->outside) {
                float lastFrame = -1.0f;
                for (int part : o.parts)
                    lastFrame = std::max(lastFrame, cab.partLastFrame(part));
                qInfo().noquote() << "cab: outside" << o.type << o.order << o.parts.size() << "parts to frame"
                                  << lastFrame;
            }
    }

    // Each control's parts at the frame its value shows, as Open Rails
    // poses a 3D cab (CabView::frame); a control without a value or a rule
    // keeps its frame (at rest until it has one). Its outside parts follow
    // the car's (setAnimated), as Open Rails' ThreeDimentionCabViewer moves
    // them: the wipers sweeping while on, the mirrors, doors and windows a
    // key frame a second toward open or closed (there at once when first
    // posed); from a rear cab its doors are the car's other side.
    void pose(ComplexShape &cab, unsigned int state, const std::vector<double> &values,
              const Moment &moment) override {
        for (size_t k = 0; k < parts->controls.size(); ++k) {
            const CabParts::Control &c = parts->controls[k];
            const size_t i = size_t(c.control);
            if (c.lastFrame < 0.0f || i >= values.size() || std::isnan(values[i]))
                continue;
            const float frame = CabView::frame(view->controls()[i], values[i], c.lastFrame,
                                               split >= 0.0f ? double(split)
                                                             : std::numeric_limits<double>::quiet_NaN());
            if (frame < 0.0f || frame == frames[k])
                continue;
            frames[k] = frame;
            for (int part : c.parts)
                cab.setPartFrame(state, part, frame);
        }
        static const std::pair<const char *, CarAnimation::Group> outsideGroups[] = {
                {"EXTERNALWIPERS", CarAnimation::Wipers},
                {"MIRRORS", CarAnimation::Mirrors},
                {"LEFTDOOR", CarAnimation::DoorLeft},
                {"RIGHTDOOR", CarAnimation::DoorRight},
                {"ORTS_EXTERNALLEFTWINDOWFRONT", CarAnimation::WindowLeftFront},
                {"ORTS_EXTERNALRIGHTWINDOWFRONT", CarAnimation::WindowRightFront},
                {"ORTS_EXTERNALLEFTWINDOWREAR", CarAnimation::WindowLeftRear},
                {"ORTS_EXTERNALRIGHTWINDOWREAR", CarAnimation::WindowRightRear}};
        for (size_t k = 0; k < parts->outside.size() && k < outsideFrames.size(); ++k) {
            const CabParts::Outside &o = parts->outside[k];
            int group = -1;
            for (const auto &[type, g] : outsideGroups)
                if (o.type.compare(QLatin1String(type), Qt::CaseInsensitive) == 0)
                    group = g;
            if (moment.rear && (group == CarAnimation::DoorLeft || group == CarAnimation::DoorRight))
                group = group == CarAnimation::DoorLeft ? CarAnimation::DoorRight : CarAnimation::DoorLeft;
            float last = 0.0f;
            for (int part : o.parts)
                last = std::max(last, cab.partLastFrame(part));
            if (group < 0 || last <= 0.0f)
                continue;
            const bool on = moment.wanted != nullptr && moment.wanted[group];
            float &frame = outsideFrames[k];
            if (group == CarAnimation::Wipers)
                frame = CarAnimation::sweep(frame, on, moment.dt, cab.animationRate() * 1.5f / 30.0f, last);
            else
                frame = moment.first ? (on ? last : 0.0f) : CarAnimation::move(frame, on, moment.dt, last);
            if (frame == outsideShown[k])
                continue;
            outsideShown[k] = frame;
            for (int part : o.parts)
                cab.setPartFrame(state, part, frame);
        }
    }

    std::function<CabPicker::Loaded()> pickerJob() const override {
        return [path = path, view = view, parts = std::shared_ptr<const CabParts>(parts)] {
            return CabPicker::load(path, *view, *parts);
        };
    }

    bool dragged(int control) const override {
        return control >= 0 && size_t(control) < view->controls().size()
               && CvfCabControls::dragged(view->controls()[size_t(control)].kind);
    }

    // Its key frames: to the last its control can show (its tables' frames)
    // or the part has.
    bool stops(const ComplexShape &cab, int control, int part, int samples, std::vector<float> &out,
               float &current) const override {
        out.clear();
        current = 0.0f;
        if (control < 0 || size_t(control) >= view->controls().size())
            return false;
        const float lastFrame = float(std::max(view->controls()[size_t(control)].tableFrames - 1, 0));
        const float keyed = cab.partLastFrame(part);
        const float last = lastFrame > 0.0f ? std::min(lastFrame, keyed) : keyed;
        if (last <= 0.0f)
            return false;
        for (size_t k = 0; k < parts->controls.size(); ++k)
            if (parts->controls[k].control == control && k < frames.size())
                current = std::max(frames[k], 0.0f);
        const int n = std::clamp(int(std::ceil(last)), 1, std::max(samples, 1));
        for (int k = 0; k <= n; ++k)
            out.push_back(last * float(k) / float(n));
        return true;
    }

    bool partAt(const ComplexShape &cab, int control, int part, float stop, float *local) const override {
        (void)control;
        return cab.partAtFrame(part, stop, local);
    }

    double valueAt(int control, float stop) const override {
        if (control < 0 || size_t(control) >= view->controls().size())
            return std::numeric_limits<double>::quiet_NaN();
        return CabView::valueAt(view->controls()[size_t(control)], int(std::lround(stop)));
    }

  private:
    std::shared_ptr<const CabView> view;
    float split = -1.0f;
    QString path;
    std::shared_ptr<CabParts> parts;
    std::vector<float> frames, outsideFrames, outsideShown;
};

}

CvfCabControls::CvfCabControls(std::shared_ptr<const CabView> view, float combinedSplit)
    : cabView(std::move(view)), split(combinedSplit) {}

int CvfCabControls::count() const {
    return int(cabView->controls().size());
}

QString CvfCabControls::name(int control) const {
    return control >= 0 && control < count() ? CabView::name(cabView->controls()[size_t(control)]) : QString();
}

QString CvfCabControls::describe(int control) const {
    if (control < 0 || control >= count())
        return QString();
    const CabView::Control &c = cabView->controls()[size_t(control)];
    return QString("%1 %2 %3").arg(c.type).arg(c.order).arg(c.kind);
}

void CvfCabControls::fields(int control, QVector<QPair<QByteArray, QVariant>> &out) const {
    out.clear();
    if (control < 0 || control >= count())
        return;
    const CabView::Control &c = cabView->controls()[size_t(control)];
    out = {{"type", c.type},       {"kind", c.kind},       {"units", c.units}, {"style", c.style},
           {"min", c.minValue},    {"max", c.maxValue},    {"order", c.order}};
}

std::unique_ptr<CabPose> CvfCabControls::bind(const ComplexShape &cab, const QString &shapePath) const {
    return std::make_unique<CvfCabPose>(cabView, split, cab, shapePath);
}

bool CvfCabControls::dragged(const QString &kind) {
    for (const char *k : {"Lever", "TriState", "MultiState", "CombinedControl"})
        if (kind.compare(QLatin1String(k), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}
