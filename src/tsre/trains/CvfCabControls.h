#pragma once
#include <tsre/trains/CabControls.h>

class CabView;

// An Open Rails 3D cab's controls (task driving 08): its .cvf's, matched to
// the cab shape's parts by their names (CabParts) and posed by key frame,
// as Open Rails does; values in the .cvf's units from tsre.cabview.
class CvfCabControls : public CabControls {
  public:
    // `combinedSplit`: its loco's combined handle's split (a share of its
    // travel; below 0 none).
    CvfCabControls(std::shared_ptr<const CabView> view, float combinedSplit);
    int count() const override;
    QString name(int control) const override;
    // "TYPE order kind".
    QString describe(int control) const override;
    bool drawsBody() const override { return false; }
    QByteArray module() const override { return QByteArrayLiteral("tsre.cabview"); }
    // {type, kind, units, style, min, max, order}.
    void fields(int control, QVector<QPair<QByteArray, QVariant>> &out) const override;
    std::unique_ptr<CabPose> bind(const ComplexShape &cab, const QString &shapePath) const override;
    const CabView &view() const { return *cabView; }
    // Dragged along its path (levers, switches of several states), not
    // clicked (two state switches, buttons).
    static bool dragged(const QString &kind);

  private:
    std::shared_ptr<const CabView> cabView;
    float split = -1.0f;
};
