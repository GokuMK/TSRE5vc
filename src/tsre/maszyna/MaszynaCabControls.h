/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MASZYNACABCONTROLS_H
#define MASZYNACABCONTROLS_H

#include <tsre/maszyna/MaszynaCab.h>
#include <tsre/trains/CabControls.h>
#include <tsre/trains/VehicleDefinition.h>

struct MaszynaVehicle;

// A MaSzyna cab's controls (task maszyna 04): an end's of its vehicle's
// .mmd (MaszynaCab), posed as MaSzyna's TGauge and TButton pose them, the
// cab model drawn on top of the vehicle; values in MaSzyna's units from
// the Lua module maszyna.cab.
class MaszynaCabControls : public CabControls {
  public:
    // Cab `end` (1 or 2) of the vehicle's.
    MaszynaCabControls(std::shared_ptr<const MaszynaVehicle> vehicle, int end);
    int count() const override;
    // Its label made readable ("main_on_bt": "Main on").
    QString name(int control) const override;
    // "label submodel animation".
    QString describe(int control) const override;
    bool drawsBody() const override { return true; }
    QByteArray module() const override { return QByteArrayLiteral("maszyna.cab"); }
    // {label, type (toggle, push, delayed, pushtoggle), lamp, animation,
    // index1, index2, word}.
    void fields(int control, QVector<QPair<QByteArray, QVariant>> &out) const override;
    std::unique_ptr<CabPose> bind(const ComplexShape &cab, const QString &shapePath) const override;
    const MaszynaCab::End &end() const { return *cabEnd; }

    // A MaSzyna vehicle's 3D cab from its .mmd: a viewpoint at each end's
    // driver (driver<N>pos:, looking toward its end, turned by
    // driver<N>angle:) and one seated (driver<N>sitpos:), its cab model as
    // its shape (none for a cab without
    // a model file, or with its controls on the vehicle's own model), its
    // box (the head 0.5 to 1.8 m over its floor, as MaSzyna keeps it) and
    // its controls; null without any.
    static std::shared_ptr<const VehicleDefinition::Cab3D> cab3D(const VehicleDefinition &definition);

    // What the mouse works, as MaSzyna's control mapper has them: switches
    // (_sw), buttons (_bt), the universals, the levers (mainctrl, brakectrl
    // ...); not the needles and the lamps.
    static bool worked(const QString &label);
    // Where a control's value v shows: v · scale + offset, the scale
    // lerped toward endScale as v goes to endValue (rotvar, movvar)
    // (TGauge::GetScaledValue).
    static float scaled(const MaszynaCab::Control &control, float value);
    // A gauge's part at a scaled value: its rest turned scaled · 360°
    // about its own Y, or moved by scaled along its own Z (TGauge's
    // gt_Rotate, gt_Move).
    static void posed(const MaszynaCab::Control &control, const float *rest, float scaledValue, float *out);

  private:
    std::shared_ptr<const MaszynaVehicle> vehicle;
    const MaszynaCab::End *cabEnd = nullptr;
};

#endif // MASZYNACABCONTROLS_H
