#include <tsre/sim/SteamHeating.h>
#include <tsre/sim/steam/SteamTables.h>
#include <tsre/trains/VehicleDefinition.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Sim {

namespace {

constexpr double Pi = 3.14159265358979323846;
constexpr double Atmosphere = 101325.0;
constexpr double Psi = 6894.757293168;
constexpr double BtuPerLb = 2326.0;
// Steam's gas constant (J/(kg·K)): its density from its pressure and
// temperature, as an ideal gas (5 % light at 5 bar).
constexpr double SteamR = 461.5;
// The hose pair between two vehicles (m), half each vehicle's.
constexpr double HoseLength = 1.2;
// The steel pipe's outside over its bore (1.5 in: 48.3 mm over 38.1 mm);
// the lagging's and the hoses' rubber's conductivity (W/(m·K)).
constexpr double SteelOverBore = 1.27;
constexpr double LaggingK = 0.05, RubberK = 0.16;
// The pipe's steel, about 4 kg/m (J/(m·K)); a cold pipe's steel takes the
// steam's temperature over about this long (s), as fast as the steam
// reaching it allows (a faster one would make the solved pressures swing:
// a little more pressure, much more steam condensing).
constexpr double PipeHeatCapacity = 2000.0;
constexpr double WarmingTime = 30.0;
// Friction: Darcy's for a steel pipe (its Reynolds number some 10^5), and
// each hose coupling's loss as so many velocity heads.
constexpr double Friction = 0.02, CouplingK = 1.5;
// The least pressure (Pa gauge) that drives steam on; full at twice it
// (Open Rails heats a car from 1 psi).
constexpr double LeastPressure = Psi;
// A coupling's leak (m²) at its worst: Napier's flow, choked.
constexpr double LeakArea = 2e-6;
constexpr double Napier = 1.457e-3;
// A compartment: the inside film and the walls' (an insulated steel side)
// and floor's and the single glazing's resistances (m²·K/W); its air's heat
// (J/(m³·K)); its fittings' heat and their coupling to its air, by its
// volume (J/(m³·K), W/(m³·K)): the air follows them within minutes.
constexpr double InsideR = 0.13, WallR = 0.55, FloorR = 0.45, GlassR = 0.005;
constexpr double AirHeat = 1206.0;
constexpr double FittingsHeat = 17000.0, FittingsCoupling = 20.0;
// Radiators sized to hold the set temperature at this outside air, at
// 25 m/s, with 1 bar in the car's pipe (as coaches were specified); from
// Open Rails' pipe area factor at this heat (W/(m²·K)) and pipe (m).
constexpr double DesignAir = -20.0, DesignSpeed = 25.0, DesignPressure = 1e5;
constexpr double RadiatorFilm = 14.0, RadiatorPipe = 0.0603;
constexpr double CubicFoot = 0.0283168, Inch = 0.0254;
// A pipe started hot (°C).
constexpr double WarmPipe = 120.0;
// The pressure solve's passes a step.
constexpr int Passes = 3;

// The outside film (W/(m²·K)) at an air speed (McAdams), at most 40 m/s.
double film(double speed) {
    return 5.7 + 3.8 * std::min(std::fabs(speed), 40.0);
}

// A pipe's loss (W/(m·K)) through `lagging` of `k` to `outer` (m) and the
// outside film at `speed` (under the frame, half the train's).
double pipeLoss(double inner, double outer, double k, double speed) {
    const double lagging = outer > inner ? std::log(outer / inner) / (2.0 * Pi * k) : 0.0;
    const double size = std::max(outer, inner);
    return 1.0 / (lagging + 1.0 / (Pi * size * film(0.5 * speed)));
}

double surface(double r, double speed) {
    return 1.0 / (InsideR + r + 1.0 / film(speed));
}

const Steam::Table &temperatureTable() {
    static const Steam::Table &t = Steam::table("SaturatedSteamHeatPressureToTemperatureInterpolatorPSItoF");
    return t;
}
const Steam::Table &steamTable() {
    static const Steam::Table &t = Steam::table("SaturatedSteamHeatInterpolatorPSItoBTUpLB");
    return t;
}
const Steam::Table &waterTable() {
    static const Steam::Table &t = Steam::table("WaterHeatInterpolatorPSItoBTUpLB");
    return t;
}

// Its steam through a leak (kg/s): Napier's choked flow, falling to none
// with the pressure below choking.
double leakFlow(double area, double pressure) {
    if (pressure <= 0.0)
        return 0.0;
    const double absolute = pressure + Atmosphere;
    const double choked = Napier * area * absolute;
    constexpr double Choking = 1.83;
    if (absolute >= Choking * Atmosphere)
        return choked;
    const double ratio = Atmosphere / absolute;
    return choked * std::sqrt((1.0 - ratio * ratio) / (1.0 - 1.0 / (Choking * Choking)));
}

}

double SaturationTemperature(double pressure) {
    return (temperatureTable()(std::max(0.0, pressure) / Psi) - 32.0) / 1.8;
}

double LatentHeat(double pressure) {
    const double psi = std::max(0.0, pressure) / Psi;
    return (steamTable()(psi) - waterTable()(psi)) * BtuPerLb;
}

double WaterHeatOver100(double pressure) {
    return (waterTable()(std::max(0.0, pressure) / Psi) - waterTable()(0.0)) * BtuPerLb;
}

double BodyLoss(const HeatingSpec &car, double speed) {
    const double wall = surface(WallR, speed), floor = surface(FloorR, speed), glass = surface(GlassR, speed);
    return car.sides * ((1.0 - car.windows) * wall + car.windows * glass) + car.roof * (wall + floor)
            + car.ends * floor;
}

double AirChangeLoss(const HeatingSpec &car, double speed, bool doorsOpen) {
    const double changes = 1.5 + 0.4 * std::min(std::fabs(speed), 40.0) + (doorsOpen ? 20.0 : 0.0);
    return AirHeat * car.volume * changes / 3600.0;
}

HeatingSpec ReadHeating(const VehicleDefinition &d) {
    HeatingSpec h;
    constexpr double None = std::numeric_limits<double>::quiet_NaN();
    const auto given = [&](const char *path) { return d.number(path, 0, None); };
    const int type = d.wagonTypeId;
    const bool engine = type >= 4, tender = type == 3, carriage = type == 1;
    const QString special = d.text("wagon(ortswagonspecialtype").trimmed().toLower();
    const double set = given("wagon(ortsheatingcompartmenttemperatureset");
    bool keys = !std::isnan(set) || special == "heated" || special == "heatingboiler";
    for (const char *path : {"wagon(ortsheatingwindowderatingfactor", "wagon(ortsheatingcompartmentpipeareafactor",
                             "wagon(ortsheatingtrainpipeouterdiameter", "wagon(ortsheatingtrainpipeinnerdiameter",
                             "wagon(ortsheatingconnectinghoseouterdiameter",
                             "wagon(ortsheatingconnectinghoseinnerdiameter"})
        keys = keys || !std::isnan(given(path));
    h.maxPressure = std::max(0.0, d.number("engine(maxsteamheatingpressure", 0, 0.0));
    h.hoseEmitter = d.exhaustKinds.contains("heatinghose");
    h.pipeTrapEmitter = d.exhaustKinds.contains("heatingmainpipesteamtrap");
    h.radiatorTrapEmitter = d.exhaustKinds.contains("heatingcompartmentsteamtrap");
    h.pipe = engine || tender || carriage || keys || h.hoseEmitter || h.maxPressure > 0.0;
    h.heated = carriage || special == "heated" || !std::isnan(set);
    h.length = std::max(1.0, double(d.length()));
    const auto size = [&](const char *path, double otherwise) {
        const double v = given(path);
        return std::isnan(v) || v <= 0.0 ? otherwise : v;
    };
    h.bore = size("wagon(ortsheatingtrainpipeinnerdiameter", h.bore);
    h.pipeOuter = size("wagon(ortsheatingtrainpipeouterdiameter", h.pipeOuter);
    h.hoseOuter = size("wagon(ortsheatingconnectinghoseouterdiameter", h.hoseOuter);
    if (!h.heated)
        return h;
    // Its compartment inside its body: shorter by its ends, narrower by its
    // sides, from its floor (about 1.2 m up) to its ceiling.
    const double length = d.size[2] > 0.0f ? d.size[2] : 20.0;
    const double width = d.size[0] > 0.0f ? d.size[0] : 2.9;
    const double height = d.size[1] > 0.0f ? d.size[1] : 4.0;
    const double li = std::max(1.0, length - 1.2), wi = std::max(1.0, width - 0.2);
    const double hi = std::clamp(height - 1.6, 1.8, 2.6);
    h.volume = li * wi * hi;
    h.sides = 2.0 * li * hi;
    h.roof = li * wi;
    h.ends = 2.0 * wi * hi;
    h.windows = std::clamp(size("wagon(ortsheatingwindowderatingfactor", h.windows), 0.0, 1.0);
    if (!std::isnan(set))
        h.setTemperature = set;
    const double factor = given("wagon(ortsheatingcompartmentpipeareafactor");
    if (!std::isnan(factor) && factor > 0.0) {
        const double pipe = h.volume / CubicFoot / factor * Inch;
        h.radiators = RadiatorFilm * Pi * RadiatorPipe * pipe;
    } else {
        const double design = (BodyLoss(h, DesignSpeed) + AirChangeLoss(h, DesignSpeed, false))
                * (h.setTemperature - DesignAir);
        h.radiators = design / (SaturationTemperature(DesignPressure) - h.setTemperature);
    }
    return h;
}

void SteamHeating::setUp(std::vector<HeatingSpec> specs, double air, bool warm, unsigned seed) {
    vehicles.clear();
    vehicles.reserve(specs.size());
    active_ = false;
    for (size_t i = 0; i < specs.size(); ++i) {
        Vehicle v;
        v.spec = std::move(specs[i]);
        // 0.5 to 1, the same for the same train and place in it.
        const double hash = std::sin(double(i) * 12.9898 + double(seed % 100000) * 78.233) * 43758.5453;
        v.leak = 0.5 + 0.5 * (hash - std::floor(hash));
        v.pipe = warm && v.spec.pipe ? WarmPipe : air;
        v.air = v.fittings = warm && v.spec.heated ? v.spec.setTemperature : air;
        active_ = active_ || v.spec.heated || (v.spec.pipe && v.spec.maxPressure > 0.0);
        vehicles.push_back(std::move(v));
    }
}

void SteamHeating::demandAt(size_t i, double air, double speed) {
    Vehicle &v = vehicles[i];
    v.pipeHeat = v.radiatorHeat = v.demand = 0.0;
    if (v.spec.heated)
        v.valve = std::clamp((v.spec.setTemperature + 1.0 - v.air) / 2.0, 0.0, 1.0);
    const double reach = std::clamp((v.pressure - LeastPressure) / LeastPressure, 0.0, 1.0);
    if (reach <= 0.0)
        return;
    const double steam = SaturationTemperature(v.pressure);
    // Its pipe and its half of each hose pair joined: their losses to the
    // air, and warming the steel to the steam's temperature.
    const int joined = (int(i) > first) + (int(i) < last);
    const double pipeLength = v.spec.length, hoseLength = 0.5 * HoseLength * joined;
    const double lost = (pipeLoss(v.spec.bore * SteelOverBore, v.spec.pipeOuter, LaggingK, speed) * pipeLength
                         + pipeLoss(v.spec.bore, v.spec.hoseOuter, RubberK, speed) * hoseLength)
            * (v.pipe - air);
    const double warming = PipeHeatCapacity * (pipeLength + hoseLength) * (steam - v.pipe) / WarmingTime;
    v.pipeHeat = reach * std::max(0.0, warming + lost);
    if (v.spec.heated)
        v.radiatorHeat = reach * v.valve * v.spec.radiators * std::max(0.0, steam - v.air);
    v.demand = (v.pipeHeat + v.radiatorHeat) / LatentHeat(v.pressure);
    // The coupling toward the feeding vehicle leaks here.
    if (int(i) != source)
        v.demand += leakFlow(LeakArea * v.leak, v.pressure);
}

void SteamHeating::solveSide(int end, int dir) {
    if (end == source)
        return;
    double flow = 0.0;
    for (int i = end; i != source; i -= dir) {
        flow += vehicles[size_t(i)].demand;
        vehicles[size_t(i)].through = flow;
    }
    // Out from the feeding vehicle, each length's pressure squared falls by
    // its friction × the flow through it squared (isothermal).
    for (int i = source + dir;; i += dir) {
        const Vehicle &up = vehicles[size_t(i - dir)];
        Vehicle &v = vehicles[size_t(i)];
        const double area = Pi * up.spec.bore * up.spec.bore / 4.0;
        const double length = 0.5 * (up.spec.length + v.spec.length) + HoseLength;
        const double temperature = SaturationTemperature(up.pressure) + 273.15;
        const double k = (Friction * length / up.spec.bore + CouplingK) * SteamR * temperature / (area * area);
        const double before = up.pressure + Atmosphere;
        const double squared = before * before - k * v.through * v.through;
        const double next = squared > Atmosphere * Atmosphere ? std::sqrt(squared) - Atmosphere : 0.0;
        // Halfway each pass: a cold pipe's front settles, not flickers.
        v.pressure = 0.5 * (v.pressure + next);
        if (i == end)
            break;
    }
}

void SteamHeating::step(double dt, const std::vector<double> &supply, double air, double speed, bool doorsOpen) {
    const int n = int(vehicles.size());
    source = first = last = -1;
    double best = 0.0;
    for (int i = 0; i < n && size_t(i) < supply.size(); ++i)
        if (vehicles[size_t(i)].spec.pipe && supply[size_t(i)] > best) {
            best = supply[size_t(i)];
            source = i;
        }
    if (source >= 0) {
        first = last = source;
        while (first > 0 && vehicles[size_t(first - 1)].spec.pipe)
            --first;
        while (last + 1 < n && vehicles[size_t(last + 1)].spec.pipe)
            ++last;
    }
    for (int i = 0; i < n; ++i) {
        Vehicle &v = vehicles[size_t(i)];
        v.through = v.supplied = 0.0;
        if (source < 0 || i < first || i > last) {
            v.pressure = 0.0;
            v.demand = v.pipeHeat = v.radiatorHeat = 0.0;
            v.valve = v.spec.heated ? std::clamp((v.spec.setTemperature + 1.0 - v.air) / 2.0, 0.0, 1.0) : 0.0;
        }
    }
    if (source >= 0) {
        vehicles[size_t(source)].pressure = best;
        for (int pass = 0; pass < Passes; ++pass) {
            for (int i = first; i <= last; ++i)
                demandAt(size_t(i), air, speed);
            solveSide(last, 1);
            solveSide(first, -1);
        }
        for (int i = first; i <= last; ++i)
            demandAt(size_t(i), air, speed);
        double all = 0.0;
        for (int i = first; i <= last; ++i)
            all += vehicles[size_t(i)].demand;
        vehicles[size_t(source)].supplied = all;
    }
    // The steel, the compartments, what shows.
    const double flash = LatentHeat(0.0);
    for (int i = 0; i < n; ++i) {
        Vehicle &v = vehicles[size_t(i)];
        v.hoseLeak = v.pipeTrap = v.radiatorTrap = 0.0;
        if (v.spec.pipe) {
            const bool on = source >= 0 && i >= first && i <= last;
            const int joined = on ? (i > first) + (i < last) : 0;
            const double hoseLength = 0.5 * HoseLength * joined;
            const double loss = (pipeLoss(v.spec.bore * SteelOverBore, v.spec.pipeOuter, LaggingK, speed) * v.spec.length
                                 + pipeLoss(v.spec.bore, v.spec.hoseOuter, RubberK, speed) * hoseLength)
                    * (v.pipe - air);
            v.pipe += dt * (v.pipeHeat - loss) / (PipeHeatCapacity * (v.spec.length + hoseLength));
        }
        if (v.spec.heated) {
            const double losses = (BodyLoss(v.spec, speed) + AirChangeLoss(v.spec, speed, doorsOpen)) * (v.air - air);
            const double toFittings = FittingsCoupling * v.spec.volume * (v.air - v.fittings);
            v.air += dt * (v.radiatorHeat - losses - toFittings) / (AirHeat * v.spec.volume);
            v.fittings += dt * toFittings / (FittingsHeat * v.spec.volume);
        }
        if (v.pressure > 0.0) {
            const double latent = LatentHeat(v.pressure), share = WaterHeatOver100(v.pressure) / flash;
            v.pipeTrap = v.pipeHeat / latent * share;
            v.radiatorTrap = v.radiatorHeat / latent * share;
        }
    }
    // Each coupling's leak shows half on each side.
    for (int i = std::max(first, 0) + 1; source >= 0 && i <= last; ++i) {
        const int downstream = i <= source ? i - 1 : i;
        const double leak = leakFlow(LeakArea * vehicles[size_t(downstream)].leak, vehicles[size_t(downstream)].pressure);
        vehicles[size_t(i - 1)].hoseLeak += 0.5 * leak;
        vehicles[size_t(i)].hoseLeak += 0.5 * leak;
    }
}

}
