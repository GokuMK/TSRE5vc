#pragma once
#include <QString>
#include <vector>

struct VehicleDefinition;

namespace Sim {

// The lines a joint between two vehicles carries (task driving 10), as
// MaSzyna's coupling bits (MOVER.h coupling::): the coupler itself, the
// brake hose, multiple-unit control, high voltage, the gangway, the main
// reservoir hose, heating, a permanent coupling, the 24 V, 110 V and
// 3×400 V lines.
namespace Lines {
enum : int {
    Coupler = 0x1,
    BrakeHose = 0x2,
    Control = 0x4,
    HighVoltage = 0x8,
    Gangway = 0x10,
    MainHose = 0x20,
    Heating = 0x40,
    Permanent = 0x80,
    LowVoltage24 = 0x100,
    LowVoltage110 = 0x200,
    ThreePhase = 0x400,
};
}

// The `.con` extension's words (TSRECoupling ( "brakehose mainhose control"
// )): brakehose, control, highvoltage, gangway, mainhose, heating,
// permanent, 24v, 110v, 400v; the coupler always. Unknown words are
// ignored (`unknown` gets them).
int ParseLines(const QString &words, QString *unknown = nullptr);
QString LinesText(int lines);

// A `.fiz` vehicle's coupler end (0 its front, 1 its rear): the lines it
// allows (AllowedFlag: BuffCoupl. for both, BuffCoupl1. the front,
// BuffCoupl2. the rear; MaSzyna's default 3; negative a permanent
// coupling) and its control type (ControlType).
int AllowedLines(const VehicleDefinition &vehicle, int end);
QString ControlType(const VehicleDefinition &vehicle, int end);

// A joint's lines by its two ends where the `.con` says nothing: `front`'s
// end `frontEnd` against `rear`'s end `rearEnd`. Two `.fiz` vehicles: what
// both ends allow, control only with equal control types (MaSzyna's
// coupling by the player). Two ENG/WAG vehicles: the brake hose and
// control (Open Rails: every loco follows the lead), the main hose between
// two twin pipe vehicles (`twin` both). Mixed: the brake hose, the main
// hose between twin pipes, never control.
int DefaultLines(const VehicleDefinition &front, int frontEnd, const VehicleDefinition &rear, int rearEnd, bool twin);

// A `.fiz` coupler end's power lines (task driving 10, step 8): the low
// voltage lines it passes (PowerFlag: 24 V, 110 V, 3×400 V; MaSzyna's
// default the first two) where the joint carries all of PowerCoupling's
// lines (MaSzyna's default a permanent coupling: within units). None for
// an ENG/WAG vehicle.
int PowerFlag(const VehicleDefinition &vehicle, int end);
int PowerCoupling(const VehicleDefinition &vehicle, int end);

// The power lines' voltages at each vehicle (task driving 10, step 8), as
// MaSzyna's couplers pass them (Mover.cpp PowerCouplersCheck,
// GetTrainsetVoltage) at rest: `joints` the lines of each joint (consist
// order, joint i between vehicles i and i + 1). The high voltage passes
// joints with the high voltage line or a heating cable; a vehicle sends
// its own onto a heating cable only with `heatingCable`; it reads the high
// voltage through high voltage joints and the heating voltage through
// heating cables. The low voltages pass the joints carrying them. A
// vehicle never reads its own.
struct LineSupply {
    double highVoltage = 0.0;
    bool heatingCable = false;
    double v110 = 0.0;
    double v24 = 0.0;
};
struct LineVoltages {
    double highVoltage = 0.0;
    double heating = 0.0;
    double v110 = 0.0;
    double v24 = 0.0;
};
std::vector<LineVoltages> TrainVoltages(const std::vector<int> &joints, const std::vector<LineSupply> &supplies);

}
