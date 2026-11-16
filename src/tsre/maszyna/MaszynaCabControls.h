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
#include <mutex>

struct E3dModel;

struct MaszynaVehicle;

// A MaSzyna cab's controls (task maszyna 04): an end's of its vehicle's
// .mmd (MaszynaCab), posed as MaSzyna's TGauge and TButton pose them, the
// cab model drawn on top of the vehicle; values in MaSzyna's units from
// the Lua module maszyna.cab.
class MaszynaCabControls : public CabControls {
  public:
    // Cab `end` (1 or 2) of the vehicle's, its model the file `model` (its
    // openings found there).
    MaszynaCabControls(std::shared_ptr<const MaszynaVehicle> vehicle, int end, QString model = QString());
    int count() const override;
    // Its label made readable ("main_on_bt": "Main on").
    QString name(int control) const override;
    // "label submodel animation".
    QString describe(int control) const override;
    bool drawsBody() const override { return true; }
    QByteArray module() const override { return QByteArrayLiteral("maszyna.cab"); }
    // {label, type (toggle, push, delayed, pushtoggle), lamp, animation,
    // index1, index2, word}; an opening's window ("left", "right"),
    // windowMain, windowInverted.
    void fields(int control, QVector<QPair<QByteArray, QVariant>> &out) const override;
    std::unique_ptr<CabPose> bind(const ComplexShape &cab, const QString &shapePath) const override;
    const MaszynaCab::End &end() const { return *cabEnd; }
    // Its windows and side doors, found in its model once asked for.
    const std::vector<CabOpening> &openings() const override;

    // The windows and side doors among cab `end`'s universals (task sound
    // 04, rule R5 of docs/tasks/sound/04-maszyna-cab-windows.md): a
    // universal moving its part (bound as the cab binds it) whose parts at
    // rest are centred at the side wall (at least 0.8 of the model's half
    // width, its narrower side, and 0.75 of the vehicle's `width`, where
    // known) and hold glass (a glass texture's name, a blended part) or are
    // at least 1.2 m tall (a door). The side from x (cab 1's left +x, cab
    // 2's -x); inverted where it moves up at value 1; each side's largest
    // its main. `model` with its vertices (read fresh).
    static std::vector<CabOpening> findOpenings(const MaszynaCab::End &end, int endNumber, const E3dModel &model,
                                                float vehicleWidth);

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
    int endNumber = 1;
    QString modelPath;
    mutable std::once_flag openingsFound;
    mutable std::vector<CabOpening> found;
};

#endif // MASZYNACABCONTROLS_H
