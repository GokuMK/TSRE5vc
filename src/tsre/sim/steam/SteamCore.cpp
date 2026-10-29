#include <tsre/sim/steam/SteamCore.h>
#include <algorithm>
#include <cmath>
#include <limits>

// Open Rails' steam loco, one step (task driving 04), written afresh from
// its behaviour (docs/tasks/driving/reports/or-steam-loco-spec-step.md, read
// 2026-10-29 at upstream 8aafb3147; no code taken). In its units: psi, lb,
// BTU, °F; the section numbers are the spec's.

namespace Sim::Steam {

namespace {

constexpr double Pi = 3.14159265358979323846;
// Open Rails' conversions (Orts.Common/Conversions.cs).
constexpr double Atmosphere = 14.696;
constexpr double LbKg = 1.0 / 2.20462;
constexpr double PsiPa = 6894.757293168;
constexpr double LbfN = 1.0 / 0.224808943871;
constexpr double InchM = 0.0254;
constexpr double BtuKj = 1.05505585;
constexpr double HpW = 745.699872;
constexpr double MpsMph = 2.23693629;
constexpr double UkGallonM3 = 0.00454609;
constexpr double WaterLbpGallon = 10.021;

double kelvinFromF(double f) {
    return (f - 32.0) * 100.0 / 180.0 + 273.15;
}

// The tables a step reads, found once.
struct Tables {
    const Table &steamHeat = table("SaturatedSteamHeatInterpolatorPSItoBTUpLB");
    const Table &waterHeat = table("WaterHeatInterpolatorPSItoBTUpLB");
    const Table &steamDensity = table("SteamDensityInterpolatorPSItoLBpFT3");
    const Table &waterDensity = table("WaterDensityInterpolatorPSItoLBpFT3");
    const Table &saturationF = table("SaturatedSteamHeatPressureToTemperatureInterpolatorPSItoF");
    const Table &saturationPressure = table("SaturationPressureInterpolatorKtoPSI");
    const Table &specificHeat = table("SpecificHeatInterpolatorKtoKJpKGpK");
    const Table &pressureOfWaterF = table("TemperatureToPressureInterpolatorFtoPSI");
    const Table &superheatTemperature = table("SuperheatTempInterpolatorLbpHtoDegF");
    const Table &superheatLimit = table("SuperheatTempLimitInterpolatorXtoDegF");
    const Table &condensation = table("CylinderCondensationFractionInterpolatorX");
    const Table2D &cutoffDropUpper = table2D("CutoffInitialPressureUpper");
    const Table2D &cutoffDropLower = table2D("CutoffInitialPressureLower");
    const Table2D &liveMaxima = table2D("LiveSteamInjectorWaterDeliveryMaximaLbsPerPSIPerF");
    const Table2D &liveMinima = table2D("LiveSteamInjectorWaterDeliveryMinimaLbsPerPSIPerF");
    const Table2D &liveDelivery = table2D("LiveSteamInjectorWaterDeliveryTemperatureFPerLbsPerPSI");
    const Table &liveWaterPerSteam = table("LiveSteamInjectorWaterFedForSteamUsedAtPressureInterpolatorLbstoPSI");
    const Table2D &exhaustMaxima = table2D("ExhaustSteamInjectorWaterDeliveryMaximaLbsPerPSIPerF");
    const Table2D &exhaustMinima = table2D("ExhaustSteamInjectorWaterDeliveryMinimaLbsPerPSIPerF");
    const Table2D &exhaustDelivery = table2D("ExhaustSteamInjectorWaterDeliveryTemperatureFPerLbsPerPSI");
    const Table &exhaustSteamUsed = table("ExhaustSteamInjectorSteamUsedLbstoPSI");
};

const Tables &tables() {
    static const Tables t;
    return t;
}

// Open Rails' SmoothedData: s ← s·r + x·(1 − r), r = exp(−k·dt); the first
// value as is, the input itself once r < 0.5.
void smooth(double &value, double &set, double x, double periodS, double dt) {
    if (dt <= 0.0) {
        if (set == 0.0)
            value = x;
        set = 1.0;
        return;
    }
    const double k = -60.0 * std::log(1.0 - 1.0 / (60.0 * periodS));
    const double r = std::exp(-k * dt);
    if (set == 0.0 || r < 0.5)
        value = x;
    else
        value = value * r + x * (1.0 - r);
    set = 1.0;
}

// ln R / (R − 1), 1 at R = 1 (Open Rails gives NaN there).
double logRatio(double r) {
    return std::fabs(r - 1.0) < 1e-9 ? 1.0 : std::log(r) / (r - 1.0);
}

// An angle in (−π, π].
double wrap(double a) {
    a = std::remainder(a, 2.0 * Pi);
    return a <= -Pi ? a + 2.0 * Pi : a;
}

// The force average's time constant (Open Rails averages the last 36
// steps: 0.6 s at its 60 frames a second).
constexpr double AverageForceS = 0.3;

// One step, its parts in Open Rails' order.
struct Step {
    const Config &c;
    State &s;
    const Controls &k;
    const Inputs &in;
    Outputs &out;
    const Tables &tb = tables();
    double dt = in.dt;
    double t = 0.0, cutoff = 0.0, speed = 0.0;
    bool manual = k.manualFiring;
    // This step's burn (kg/s, smoothed) and fuel fed (kg/s, and the share).
    double burn = 0.0, feed = 0.0, feedShare = 0.0;
    // The superheat's factor on the cylinders' steam.
    double superheatFactor = 1.0;
    double tenderWaterF = 0.0;
    double waterGlass = 0.0, gradientGlass = 0.0;
    double elevationPercent = 0.0;

    double smoothedHeat() const { return std::clamp(s.heatSmoothed, 0.0, 1.05 * c.maxBoilerSafetyHeatBtu); }
    double heatPerLb() const { return s.steamEnthalpy - s.waterEnthalpy; }
    // Steam taken from the boiler (lb/s).
    void draw(double lbps) {
        s.boilerMass -= dt * lbps;
        s.boilerHeat -= dt * lbps * heatPerLb();
        s.heatOut += lbps * heatPerLb();
        s.steamOut += lbps;
    }
    void sound(int n) { out.sounds.push_back(n); }
    void message(const char *name) { out.messages.push_back(name); }
    double burnCurveKgps(double steamLbps) const { return c.burnRate(3600.0 * steamLbps) * LbKg / 3600.0; }

    void run();
    void exhaustBeats();
    void tender();
    void firebox();
    void boiler();
    void applyPressure();
    void auxiliaries();
    void superheat();
    void engines();
    void waterGauge();
    void injectors();
    void firing();
};

void Step::run() {
    if (s.started == 0.0)
        start(c, s);
    // §0, 2: the regulator and the cutoff (no AI cutoff rule yet).
    t = std::clamp(k.regulator, 0.0, 1.0);
    cutoff = std::clamp(std::fabs(k.reverser), 0.0, 1.0);
    out.forward = k.reverser >= 0.0 ? 1.0 : -1.0;
    speed = std::fabs(in.speed);
    elevationPercent = -100.0 * in.gradient;
    // The driver's: shovels on the fire (any firing), the fireman's
    // overrides (Open Rails' commands).
    s.fireMass += k.shovels * c.shovelMassKg;
    if (k.refill) {
        s.tenderCoal = c.maxTenderCoalKg;
        s.tenderWater = c.maxTenderWaterUkGallons;
    }
    if (k.fireOn) {
        s.fireOn = 1.0;
        s.fireOff = 0.0;
    }
    if (k.fireOff) {
        s.fireOff = 1.0;
        s.fireOn = 0.0;
    }
    if (k.fireReset) {
        s.fireOn = 0.0;
        s.fireOff = 0.0;
    }
    if (manual) {
        if (k.injector1Switch) {
            s.injector1On = s.injector1On != 0.0 ? 0.0 : 1.0;
            sound(s.injector1On != 0.0 ? 30 : 31);
        }
        if (k.injector2Switch) {
            s.injector2On = s.injector2On != 0.0 ? 0.0 : 1.0;
            sound(s.injector2On != 0.0 ? 27 : 28);
        }
    }
    // The driving axle turns with the train (no slip yet).
    const double radius = c.engines[0].wheelRadiusIn * InchM;
    if (radius > 0.0)
        s.axleAngle = wrap(s.axleAngle + in.speed / radius * dt);
    // The train's heating (LOCO.Update, before the steam parts).
    if (in.heatingSteam > 0.0)
        draw(in.heatingSteam);

    exhaustBeats();
    tender();
    firebox();
    boiler();
    auxiliaries();
    superheat();
    engines();
    waterGauge();
    injectors();
    firing();

    out.boilerPressure = s.pressure * PsiPa;
    out.waterGlass = waterGlass;
    out.waterGlassGradient = gradientGlass;
    out.fire = c.maxFireMassKg > 0.0 ? s.fireMass / c.maxFireMassKg : 0.0;
    out.fireMass = s.fireMass;
    out.burnRate = burn;
    out.feedRate = feed;
    out.tenderCoal = s.tenderCoal;
    out.tenderWater = s.tenderWater * UkGallonM3;
    out.cylinderSteam = s.cylinderSteam * LbKg;
    out.steamUsage = s.steamOutBefore * LbKg;
    out.injector1 = s.injector1On != 0.0;
    out.injector2 = s.injector2On != 0.0;
    out.safetyValves = s.safety1 != 0.0;
    out.fusiblePlug = s.fusiblePlug != 0.0;
    out.priming = s.priming != 0.0;
    out.variable3 = feedShare * 100.0;
}

// The sound variables and the exhaust beats (UpdateFX, §11): 2 × the
// cylinders' beats a wheel turn, numbered over two turns.
void Step::exhaustBeats() {
    const double radius = c.engines[0].wheelRadiusIn * InchM;
    out.variable1 = t == 0.0 || radius <= 0.0 ? 0.0 : speed / radius / Pi * 5.0;
    out.variable2 = s.pressure > 0.0 ? std::clamp((s.cockPressure - Atmosphere) / s.pressure * 100.0, 0.0, 100.0) : 0.0;
    const int pulses = c.mstsCylinders * 2 * 2;
    const double advance = out.variable1 / 20.0 * pulses * dt;
    s.pulseTracker += advance;
    if (s.pulseTracker > s.nextPulse - advance / 2.0) {
        sound(121 + int(s.nextPulse) - 1);
        s.pulseTracker = std::fmod(s.pulseTracker, double(pulses));
        s.nextPulse = double(int(s.nextPulse) % pulses + 1);
    }
}

// §1: coal by the burn curve at the cylinders' steam; water as injected.
void Step::tender() {
    const bool has = in.tenderCoupled || !c.tenderRequired;
    const double steam = std::clamp(s.cylinderSteam, 0.1, c.maxSteamOutputLbps);
    s.tenderCoal = has ? std::clamp(s.tenderCoal - dt * burnCurveKgps(steam), 0.0, c.maxTenderCoalKg) : 0.0;
    if (s.tenderCoal < 1.0) {
        if (s.coalOut == 0.0)
            message("coalEmpty");
        s.coalOut = 1.0;
    } else {
        s.coalOut = 0.0;
    }
    const double air = in.outsideTemperatureC;
    tenderWaterF = std::clamp(air + 3.0 + 0.3 * (air - 10.0), 0.0, 50.0) * 1.8 + 32.0;
    s.tenderWater = has ? std::max(0.0, s.tenderWater - s.waterInput / WaterLbpGallon) : 0.0;
    if (s.tenderWater < 1.0) {
        if (s.waterOut == 0.0)
            message("waterEmpty");
        s.waterOut = 1.0;
    } else {
        s.waterOut = 0.0;
    }
}

// §2, §3: the burn (the draught's, or the AI fireman's from the heat that
// left), the fire's heat share, the feed.
void Step::firebox() {
    double burnRaw;
    if (manual) {
        burnRaw = burnCurveKgps(s.radiationSteam + in.heatingSteam + s.blowerEffect + s.damperEffect);
    } else {
        const double exceptions = s.safetyHeatOut + s.cockHeatOut;
        const double base = (s.heatOutBefore - exceptions) * BtuKj / c.fuelCalorificKJpKg;
        const double H = s.boilerHeat, Hmax = c.maxBoilerHeatBtu, Hsafe = c.maxBoilerSafetyHeatBtu;
        if (s.shovelAnyway != 0.0 && H < Hmax)
            burnRaw = base * s.heatRatio * s.pressureRatio;
        else if (H > Hmax && H <= Hsafe && t > 0.1 && cutoff > 0.1 && s.heatIn < s.heatOutBefore
                 && s.fullMaxPressureHeat != 0.0)
            burnRaw = base * 4.0;
        else
            burnRaw = base * s.heatRatio * s.pressureRatio * s.fullHeatRatio * s.maxHeatRatio;
        const double smoothed = smoothedHeat(), p = s.pressure, pmax = c.maxBoilerPressurePsi;
        if (s.fireOn != 0.0 || s.fireOff != 0.0)
            s.fireOverride = 1.0;
        else if (p < pmax && smoothed < Hmax && H < Hsafe)
            s.fireOverride = 0.0;
        if (s.fireOff != 0.0) {
            // Open Rails' third test compares heat with 0.90 BTU.
            if (p < pmax - 20.0 || smoothed < 0.90 || (speed < 0.01 && t < 0.01))
                s.fireOff = 0.0;
            burnRaw = 0.0035;
        }
        if (s.fireOn != 0.0) {
            if ((smoothed > 0.995 * Hmax && speed > 10.0) || p > pmax
                || (speed <= 10.0 && (smoothed > Hmax || H > 1.1 * Hsafe)))
                s.fireOn = 0.0;
            burnRaw = 0.9 * burnCurveKgps(c.maxSteamOutputLbps);
        }
    }
    burnRaw = std::max(burnRaw, 0.0012626);
    const double fireRatio = c.idealFireMassKg > 0.0 ? s.fireMass / c.idealFireMassKg : 0.0;
    if (fireRatio < 0.05) {
        burnRaw = 0.0;
        if (s.fireOut == 0.0)
            message("fireOut");
        s.fireOut = 1.0;
    }
    // Too much or too little air for the fire's mass; at exactly the ideal
    // mass the last loss is kept.
    if (fireRatio < 1.0) {
        const double x = 1.0 - fireRatio;
        s.fireLoss = std::clamp(0.0058 * x * x + 0.035 * x, 0.0, 1.0);
    } else if (fireRatio > 1.0) {
        const double x = 1.0 - (fireRatio - 1.0);
        s.fireLoss = std::clamp(0.0434 * x * x - 0.1276 * x, 0.0, 1.0);
    }
    if (s.fusiblePlug != 0.0)
        burnRaw = 0.0;
    // Oil is burnt as coal for now (task 04, step 5).
    smooth(s.burnSmoothed, s.burnSmoothedSet, burnRaw, c.fuel == Fuel::Wood ? 20.0 : 150.0, dt);
    burn = std::clamp(s.burnSmoothed, 0.0, c.maxFuelBurnGrateKgps);

    const double rate = c.firemanMaxFiringRateKgps;
    if (manual) {
        feedShare = s.coalOut != 0.0 ? 0.0 : std::clamp(k.firingRate, 0.0, 1.0);
        feed = rate * feedShare;
    } else if (dt > 0.001 && rate > 0.001) {
        const double wanted = std::clamp((c.idealFireMassKg - s.fireMass + burn) / rate, 0.001, 1.0);
        smooth(s.feedSmoothed, s.feedSmoothedSet, wanted, c.mechanicalStoker ? 15.0 : 45.0, dt);
        feedShare = s.coalOut != 0.0 ? 0.0 : s.feedSmoothed;
        if (s.fireMass / c.idealFireMassKg < 0.95) {
            if (s.boostTime < 300.0) {
                s.boostResetTime = 0.01;
                if (s.fuelBoost == 0.0) {
                    s.fuelBoost = 1.0;
                    if (!c.mechanicalStoker)
                        message("fuelBoost");
                }
            }
        } else if (s.fireMass >= c.idealFireMassKg && s.fuelBoost != 0.0) {
            s.fuelBoost = 0.0;
            if (!c.mechanicalStoker)
                message("fuelBoostEnd");
        }
        if (s.fuelBoost != 0.0 && s.fuelBoostReset == 0.0) {
            feed = c.maxTheoreticalFiringRateKgps * feedShare;
            s.boostTime += dt;
        } else {
            feed = rate * feedShare;
        }
    } else {
        feed = burnRaw;
    }
    s.fireMass = std::clamp(s.fireMass + dt * (feed - burn), 0.0, c.maxFireMassKg);
    // The fireman boosts at most 300 s, then rests 1200 s.
    if (s.boostTime >= 300.0) {
        s.boostResetTime += dt;
        s.fuelBoostReset = 1.0;
    }
    if (s.boostResetTime > 1200.0) {
        s.boostTime = 0.01;
        s.fuelBoostReset = 0.0;
    }
}

// §4: safety valves, blowdown, blower, the AI fireman's heat ratios, heat
// in, radiation, the water's share, the pressure.
void Step::boiler() {
    const double p = s.pressure, pmax = c.maxBoilerPressurePsi;
    const SafetyValves &v = c.safetyValves;
    if (manual) {
        // Four valves whatever their count; 2 to 4 follow valve 1.
        if (p > v.openPsi[0]) {
            if (s.safety1 == 0.0)
                sound(42);
            s.safety1 = 1.0;
        } else if (p < v.closePsi[0] && s.safety1 != 0.0) {
            sound(43);
            s.safety1 = 0.0;
        }
        if (s.safety1 != 0.0) {
            const double one = v.areaIn2 * (p + Atmosphere) / 70.0;
            double total = one;
            double *open[3] = {&s.safety2, &s.safety3, &s.safety4};
            // Valve 2 reseats at valve 1's pressure.
            const double reseat[3] = {v.closePsi[0], v.closePsi[2], v.closePsi[3]};
            for (int i = 0; i < 3; ++i) {
                if (p > v.openPsi[size_t(i + 1)])
                    *open[i] = 1.0;
                else if (p < reseat[i])
                    *open[i] = 0.0;
                if (*open[i] != 0.0)
                    total += one;
            }
            draw(total);
            s.safetyHeatOut = total * heatPerLb();
        }
    } else {
        // Open Rails signals these every step while they hold; here once.
        if (s.fireOverride != 0.0 && p > v.openPsi[0]) {
            if (s.safety1 == 0.0)
                sound(42);
            s.safety1 = 1.0;
        } else if (s.fireOverride == 0.0 && p < v.closePsi[0]) {
            if (s.safety1 != 0.0)
                sound(43);
            s.safety1 = 0.0;
        }
        if (s.safety1 != 0.0) {
            draw(v.maxDischargeLbps);
            s.safetyHeatOut = v.maxDischargeLbps * heatPerLb();
        }
    }
    if (k.blowdown)
        draw(Pi * 0.75 * 0.75 * (pmax + Atmosphere) / 70.0);
    if (s.blowerOn != 0.0)
        draw(s.blowerSteam);
    const double waterK = kelvinFromF(tb.saturationF(p));

    const double H = s.boilerHeat, Hmax = c.maxBoilerHeatBtu, Hsafe = c.maxBoilerSafetyHeatBtu;
    if (!manual) {
        if (H > Hmax && s.heatIn > 0.9 * s.heatOutBefore) {
            if (H <= Hsafe && s.fullMaxPressureHeat == 0.0 && smoothedHeat() > 0.995 * Hmax) {
                s.fullHeatRatio = 1.0 - (H - Hmax) / (Hsafe - Hmax);
                s.fullBoilerHeat = 1.0;
            } else if (H > Hsafe) {
                s.fullHeatRatio = 0.1;
                s.fullMaxPressureHeat = 1.0;
            }
        } else {
            s.fullBoilerHeat = 0.0;
            s.fullHeatRatio = 1.0;
        }
        if (H < Hmax || s.heatIn > s.heatOutBefore)
            s.fullMaxPressureHeat = 0.0;
        if (H > Hsafe) {
            if (H > 1.1 * Hsafe)
                s.maxHeatRatio = 0.01;
            else if (s.heatIn > s.heatOutBefore)
                s.maxHeatRatio = Hmax / std::pow(H, H / (Hsafe - Hmax));
            else
                s.maxHeatRatio = 1.0;
        } else {
            s.maxHeatRatio = 1.0;
        }
        s.maxHeatRatio = std::clamp(s.maxHeatRatio, 0.001, 1.0);
    }

    // Heat in: the fire's at the boiler's efficiency for the grate's rate.
    const double grateRate = 3600.0 * burn / LbKg / c.grateAreaFt2;
    if (grateRate > c.grateLimitLbpFt2) {
        if (s.grateLimit == 0.0)
            message("grateLimit");
        s.grateLimit = 1.0;
    } else {
        if (s.grateLimit != 0.0)
            message("grateLimitEnd");
        s.grateLimit = 0.0;
    }
    const double fireKw = c.fuelCalorificKJpKg * burn * std::max(0.0, 1.0 - s.fireLoss);
    s.heatIn = fireKw / BtuKj * c.boilerEfficiency(grateRate);
    s.boilerHeat += dt * s.heatIn;

    // Radiation from the boiler, faster in the wind (Open Rails converts
    // the temperature difference as a temperature, +32 °F).
    const double differenceF = (waterK - 273.15 - in.outsideTemperatureC) * 1.8 + 32.0;
    const auto kc = [](double v) { return 10.45 - v + 10.0 * std::sqrt(v); };
    const double wind = speed > 2.0 && speed < 20.0 ? kc(speed) / kc(2.0) : speed < 2.0 ? 1.0 : kc(20.0) / kc(2.0);
    const double lossBtuph = c.boilerSurfaceFt2 * wind * differenceF
            * ((1.0 - c.insulatedFraction) * 1.67 + c.insulatedFraction * c.insulationCoefficient);
    s.radiationSteam = lossBtuph / 3600.0 / heatPerLb();
    s.boilerHeat -= dt * lossBtuph / 3600.0;
    s.heatOut += lossBtuph / 3600.0;

    // The water's share, at the last pressure's densities.
    const double V = c.boilerVolumeFt3;
    s.waterFraction = std::clamp((s.boilerMass / V - s.steamDensity) / (s.waterDensity - s.steamDensity), 0.0, 1.01);

    // The pressure: the water's temperature follows the 240 s smoothed heat.
    smooth(s.heatSmoothed, s.heatSmoothedSet, s.boilerHeat, 240.0, dt);
    const double smoothed = smoothedHeat();
    if (s.heatSmoothedBefore != 0.0)
        s.heatChange = 3600.0 * (smoothed - s.heatSmoothedBefore) * 0.0002931;
    const double heatCapacity = tb.specificHeat(s.waterTemperature) * s.waterFraction * V * 28.31;
    if (heatCapacity > 0.0)
        s.waterTemperature = std::clamp(s.waterTemperature + s.heatChange / heatCapacity, 274.0, 496.0);
    s.heatSmoothedBefore = smoothed;
    s.pressure = s.fusiblePlug != 0.0 ? 0.5 : tb.saturationPressure(s.waterTemperature);
    if (!manual) {
        const double q = s.pressure;
        if (s.boilerHeat > Hmax || s.heatIn > 1.05 * s.heatOutBefore)
            s.pressureRatio = 1.0;
        else if (q > pmax - 2.0 && q <= pmax)
            s.pressureRatio = 1.0;
        else if (q > pmax - 20.0 && q <= pmax)
            s.pressureRatio = 0.0875 * (pmax - q) + 1.0;
        else if (q <= pmax - 20.0)
            s.pressureRatio = 2.75;
        s.pressureRatio = std::clamp(s.pressureRatio, 0.001, 2.75);
        if (s.pressure > pmax && s.fireOverride == 0.0)
            s.pressure = pmax;
    }
    s.pressure = std::clamp(s.pressure, 0.0, pmax + 7.0);
    applyPressure();
}

// The steam's properties at the new pressure; the totals handed on.
void Step::applyPressure() {
    const double p = s.pressure;
    s.steamEnthalpy = tb.steamHeat(p);
    s.waterEnthalpy = tb.waterHeat(p);
    s.steamDensity = tb.steamDensity(p);
    s.waterDensity = tb.waterDensity(p);
    s.heatOutBefore = s.heatOut;
    s.steamOutBefore = s.steamOut;
    s.heatOut = 0.0;
    s.steamOut = 0.0;
}

// §6: the compressor, ejectors, cocks, stoker.
void Step::auxiliaries() {
    // A 9.5 × 10 in cylinder, 120 strokes a minute.
    if (k.compressor)
        draw(Pi * 4.75 * 4.75 * 10.0 * 2.0 / 1728.0 * s.steamDensity);
    if (k.smallEjector)
        draw(c.smallEjectorLbps);
    if (k.largeEjector)
        draw(c.largeEjectorLbps);
    if (k.cylinderCocks) {
        draw(s.cockSteam);
        s.cockHeatOut = s.cockSteam * heatPerLb();
    } else {
        s.cockSteam = 0.0;
    }
    if (c.mechanicalStoker && c.fuel != Fuel::Oil && c.firemanMaxFiringRateKgps > 0.0)
        draw(c.maxBoilerOutputLbph / 3600.0 * (0.005 + 0.005 * feed / c.firemanMaxFiringRateKgps));
}

// §5: superheat lowers the cylinders' steam (last step's).
void Step::superheat() {
    const double revsPerS = speed / (2.0 * Pi * c.engines[0].wheelRadiusIn * InchM);
    double temperatureF = 0.0;
    superheatFactor = Steam::superheatFactor(c, s.cylinderSteam, cutoff, revsPerS, s.superheatSet, temperatureF);
    s.superheatFactor = superheatFactor;
    out.superheat = temperatureF * 5.0 / 9.0;
}

// §8, §9: each engine's cylinders, its steam, its force.
void Step::engines() {
    const double p = s.pressure, pmax = c.maxBoilerPressurePsi;
    const double mph = speed * MpsMph;
    // The cylinders' steam, Open Rails' 0.6/0.4 average a step at its 60
    // frames a second.
    const double keep = std::pow(0.6, 60.0 * dt);
    const double maxForceN = c.maxTractiveEffortLbf * c.cylinderEfficiency * LbfN;
    double steam = 0.0, cockSteam = 0.0, cockPressure = 0.0, mep = 0.0, chest = 0.0, back = 0.0, ihpSum = 0.0;
    double force = 0.0;
    for (size_t i = 0; i < c.engines.size(); ++i) {
        const EngineConfig &e = c.engines[i];
        EngineState &es = s.engines[i];
        const double wheelM = e.wheelRadiusIn * InchM;
        const double revsPerS = wheelM > 0.0 ? speed / (2.0 * Pi * wheelM) : 0.0;
        const double backPsi = c.backPressure(3600.0 * es.steam);
        back = std::max(back, backPsi);

        const Diagram d = diagram(c, e, p, t, cutoff, revsPerS, backPsi);
        const double Pb = d.pb;

        // §8.4 The cocks cost steam, not force.
        double engineCockSteam = 0.0;
        if (k.cylinderCocks && e.kind != EngineKind::Booster) {
            const bool moving = t > 0.01 && speed > 0.1;
            if (moving)
                engineCockSteam = e.cylinders * 24.24 * es.cockPressure * 0.25 / 3600.0;
            else if (t > 0.01)
                engineCockSteam = e.cylinders * 24.24 * Pb * 0.25 / 3600.0;
            double reduce = 1.0;
            if (moving) {
                const double used = c.hasSuperheater ? es.steam / superheatFactor : es.steam;
                reduce = used + engineCockSteam > 0.0 ? used / (used + engineCockSteam) : 1.0;
            }
            es.cockPressure = Pb * reduce;
        } else {
            es.cockPressure = Pb;
        }
        es.cockPressure = std::clamp(es.cockPressure, 0.0, pmax + Atmosphere);

        // §8.5 The steam: the average of a step at Open Rails' 60 a second.
        double calculated = d.steam * superheatFactor;
        if ((t < 0.01 && speed > 0.1) || s.fusiblePlug != 0.0)
            calculated = 0.0001;
        es.steam = keep * es.steam + (1.0 - keep) * calculated;
        draw(es.steam);
        steam += es.steam;
        cockSteam += engineCockSteam;
        cockPressure = std::max(cockPressure, es.cockPressure);
        mep = std::max(mep, d.mep);
        if (e.kind != EngineKind::Booster)
            chest = std::max(chest, d.chest);

        // §9 The force.
        double engineForce, ihp;
        const double averaging = 1.0 - std::exp(-dt / AverageForceS);
        if (k.crankForce && e.kind == EngineKind::Adhesion) {
            engineForce = crankForce(c, e, d, t, cutoff, revsPerS, s.axleAngle) * LbfN;
            es.averageForce += (engineForce - es.averageForce) * averaging;
            ihp = es.averageForce / LbfN * mph / 375.0;
        } else {
            engineForce = e.cylinders / 2.0 * (e.diameterIn * e.diameterIn * e.strokeIn / (2.0 * e.wheelRadiusIn))
                    * d.mep * c.cylinderEfficiency * LbfN;
            if (t < 0.001)
                engineForce = 0.0;
            engineForce = std::max(engineForce, 0.0);
            es.averageForce += (engineForce - es.averageForce) * averaging;
            ihp = engineForce / LbfN * mph / 375.0;
            // Starting at full gear and regulator: the design effort.
            if (speed < 1.0 && cutoff > 0.70 && t > 0.98)
                engineForce = maxForceN * p / pmax;
            if (speed == 0.0 && cutoff < 0.05)
                engineForce = 0.0;
        }
        if (s.priming != 0.0)
            engineForce *= 0.1;
        if (s.fusiblePlug != 0.0)
            engineForce = 0.5;
        if (e.maxIndicatedHp > 0.0 && ihp >= e.maxIndicatedHp) {
            engineForce = e.maxIndicatedHp * 375.0 / mph * LbfN;
            ihp = e.maxIndicatedHp;
        }
        // §9.5 No force with the regulator shut.
        if (t < 0.001) {
            engineForce = 0.0;
            es.averageForce = 0.0;
        }
        ihpSum += std::max(ihp, 0.0);
        force += engineForce;
    }
    s.cylinderSteam = steam;
    s.cockSteam = cockSteam;
    s.cockPressure = cockPressure;
    out.force = force * out.forward;
    out.steamChestPressure = std::max(chest, 0.0) * PsiPa;
    out.backPressure = back * PsiPa;
    out.meanEffectivePressure = mep * PsiPa;
    out.indicatedPower = ihpSum * HpW;
}

// §4 The water glass, the gradient glass, the fusible plug, priming.
void Step::waterGauge() {
    const double W = s.waterFraction;
    const double span = c.waterGlassMaxLevel - c.waterMinLevel;
    waterGlass = span != 0.0 ? (W - c.waterMinLevel) / span : 0.0;
    const double L = c.boilerLengthM, D = c.boilerDiameterM;
    const double variation = -(elevationPercent * L / 200.0);
    double base;
    switch (c.orientation) {
    case BoilerOrientation::CabForward: base = W - variation / D; break;
    case BoilerOrientation::Vertical: base = W; break;
    case BoilerOrientation::Sloping: base = W + (variation - std::tan(c.boilerAngleRad) * L / 2.0) / D; break;
    default: base = W + variation / D; break;
    }
    gradientGlass = std::fabs(span) < 1e-6 ? 0.0 : std::clamp((base - c.waterMinLevel) / span, 0.0, 1.0);
    if (W < c.waterMinLevel && s.fusiblePlug == 0.0) {
        s.fusiblePlug = 1.0;
        message("fusiblePlug");
    }
    if (W >= c.waterMaxLevel) {
        if (s.priming == 0.0)
            message("priming");
        s.priming = 1.0;
    } else if (W < c.waterMaxLevelSafe) {
        s.priming = 0.0;
    }
}

// §7 The injectors: water into the boiler, its heat and the steam's heat
// out; Open Rails' copy slips in injector 2 kept (its §15, 7).
void Step::injectors() {
    s.waterInput = 0.0;
    const double p = s.pressure;
    const double boilerWater = s.waterEnthalpy, boilerSteam = s.steamEnthalpy;
    const bool high = c.maxBoilerPressurePsi > 250.0;
    double flow[2] = {0.0, 0.0}, minimum[2] = {0.0, 0.0}, deliveryF[2] = {65.0, 65.0}, steamUsed[2] = {0.0, 0.0};
    double correctedPsi[2] = {0.0, 0.0};
    double *deliveryPsi[2] = {&s.injector1DeliveryPsi, &s.injector2DeliveryPsi};
    double *on[2] = {&s.injector1On, &s.injector2On};
    const double fraction[2] = {s.injector1Fraction, s.injector2Fraction};
    if (s.waterOut != 0.0) {
        s.injector1On = 0.0;
        s.injector2On = 0.0;
    } else {
        const double exhaustPsi = 0.85 * c.backPressure(3600.0 * s.cylinderSteam);
        for (int i = 0; i < 2; ++i) {
            if (*on[i] == 0.0)
                continue;
            const double phi = fraction[i], nozzle = c.injectorFactor[size_t(i)];
            const double size = c.injectorSizeMm[size_t(i)];
            double steamLoss;
            if (c.injectors[size_t(i)] == InjectorType::Exhaust && t > 0.01 && exhaustPsi > 1.0) {
                const double P = correctedPsi[i] = exhaustPsi;
                flow[i] = phi * nozzle * tb.exhaustMaxima(tenderWaterF, P) / 3600.0;
                minimum[i] = phi * nozzle * tb.exhaustMinima(tenderWaterF, P) / 3600.0;
                deliveryF[i] = std::clamp(tb.exhaustDelivery(P, 3600.0 * flow[i] / nozzle), 0.0, 500.0);
                steamUsed[i] = nozzle * tb.exhaustSteamUsed(P) / 3600.0;
                // A live supplementary jet a millimetre narrower, its heat to
                // injector 1's last delivery.
                const double jetIn = (size - 1.0) / 1000.0 / InchM;
                const double jet = (p + Atmosphere) * Pi * jetIn * jetIn / 4.0 / 70.0;
                steamLoss = jet * (boilerSteam - tb.waterHeat(s.injector1DeliveryPsi));
            } else {
                // Live steam, or an exhaust injector's auxiliary live steam.
                const bool auxiliary = c.injectors[size_t(i)] == InjectorType::Exhaust;
                const double pressureFactor = !high ? 1.0 : auxiliary ? c.auxiliaryCorrection : c.liveCorrection;
                const double factor = auxiliary ? (size / 10.5) * (size / 10.5) : nozzle;
                const double P = correctedPsi[i] = pressureFactor * p;
                flow[i] = phi * factor * tb.liveMaxima(tenderWaterF, P) / 3600.0;
                minimum[i] = phi * factor * tb.liveMinima(tenderWaterF, P) / 3600.0;
                deliveryF[i] = tb.liveDelivery(P, 3600.0 * flow[i] / factor);
                if (i == 0 || !auxiliary)
                    deliveryF[i] = std::clamp(deliveryF[i], 0.0, 500.0);
                if (i == 0) {
                    steamUsed[i] = factor * tb.liveMaxima(tenderWaterF, P) / tb.liveWaterPerSteam(P) / 3600.0;
                    s.injector1DeliveryPsi = tb.pressureOfWaterF(deliveryF[0]);
                    steamLoss = steamUsed[0] * (boilerSteam - tb.waterHeat(s.injector1DeliveryPsi));
                } else if (!auxiliary) {
                    // Injector 2 live: injector 1's delivery and steam.
                    steamUsed[i] = factor * tb.liveMaxima(tenderWaterF, P) / tb.liveWaterPerSteam(P) / 3600.0;
                    s.injector2DeliveryPsi = tb.pressureOfWaterF(deliveryF[0]);
                    steamLoss = steamUsed[0] * (boilerSteam - tb.waterHeat(s.injector1DeliveryPsi));
                } else {
                    // Injector 2 auxiliary: injector 1's pressure for the
                    // steam, its delivery written to injector 1's.
                    steamUsed[i] = factor * tb.liveMaxima(tenderWaterF, correctedPsi[0]) / tb.liveWaterPerSteam(P)
                            / 3600.0;
                    s.injector1DeliveryPsi = tb.pressureOfWaterF(deliveryF[0]);
                    steamLoss = steamUsed[i] * (boilerSteam - tb.waterHeat(s.injector2DeliveryPsi));
                }
            }
            // Injector 1's share is applied twice.
            const double water = i == 0 ? phi * flow[i] : flow[i];
            s.boilerMass += dt * water;
            const double waterLoss = water * (boilerWater - tb.waterHeat(*deliveryPsi[i]));
            s.boilerHeat -= dt * (waterLoss + steamLoss);
            s.waterInput += dt * water;
            s.heatOut += waterLoss + steamLoss;
            (i == 0 ? out.injector1Flow : out.injector2Flow) = water * LbKg;
        }
        // Below its minimum an injector delivers nothing (after the water
        // went in, as in Open Rails).
        for (int i = 0; i < 2; ++i)
            if (flow[i] < minimum[i])
                (i == 0 ? out.injector1Flow : out.injector2Flow) = 0.0;
    }
    // Locked out 15 s after each change.
    double *locked[2] = {&s.injector1Locked, &s.injector2Locked};
    double *lockTime[2] = {&s.injector1LockTime, &s.injector2LockTime};
    for (int i = 0; i < 2; ++i) {
        if (*on[i] == 0.0 && *locked[i] == 0.0)
            continue;
        if (*locked[i] != 0.0)
            *lockTime[i] += dt;
        if (*lockTime[i] > 15.0) {
            *locked[i] = 0.0;
            *lockTime[i] = 0.0;
        }
    }
}

// §3 Manual firing's blower, injectors and damper; or the AI fireman's
// injectors by the glass, and his heat ratio.
void Step::firing() {
    if (manual) {
        const double blower = std::clamp(k.blower, 0.0, 1.0);
        if (blower > 0.0) {
            s.blowerOn = 1.0;
            s.blowerSteam = c.blowerFactorLbpspPsi * blower * s.pressure;
            s.blowerEffect = 20.0 * s.blowerSteam;
        } else {
            s.blowerOn = 0.0;
            s.blowerSteam = 0.0;
            s.blowerEffect = 0.0;
        }
        if (s.injector1On != 0.0)
            s.injector1Fraction = std::clamp(k.injector1, 0.0, 1.0);
        if (s.injector2On != 0.0)
            s.injector2Fraction = std::clamp(k.injector2, 0.0, 1.0);
        s.damperEffect = speed < 1.0 ? 0.0 : std::clamp(k.damper, 0.0, 1.0) * s.cylinderSteam * 1.2;
        s.damperEffect = std::clamp(s.damperEffect, 0.0, 1.5 * c.maxSteamOutputLbps);
        return;
    }
    const double g = waterGlass, gg = gradientGlass;
    // Each injector's rules, in order: off above its level, else on at a
    // level from the one before (0.6, 0.75, full); off: level 0, minimum
    // 1, midway 2, full 3.
    struct Rule {
        double gradientAbove, glassAbove, glassOver;
        double toMinimum[2], toMidway[2], fromOff[2], fromMinimum[2], toFull[2];
    };
    static const Rule rules[2] = {
            {0.52, 0.59, 0.90, {0.50, 0.58}, {0.46, 0.56}, {0.48, 0.57}, {0.44, 0.55}, {0.42, 0.54}},
            {0.40, 0.53, 0.80, {0.38, 0.52}, {0.34, 0.50}, {0.36, 0.51}, {0.32, 0.49}, {0.30, 0.48}}};
    double *on[2] = {&s.injector1On, &s.injector2On};
    double *fraction[2] = {&s.injector1Fraction, &s.injector2Fraction};
    double *locked[2] = {&s.injector1Locked, &s.injector2Locked};
    double *level[2] = {&s.injector1Level, &s.injector2Level};
    double *sounding[2] = {&s.injector1Sound, &s.injector2Sound};
    const int onSound[2] = {30, 27}, offSound[2] = {31, 28};
    for (int i = 0; i < 2; ++i) {
        const Rule &r = rules[i];
        const bool free = *locked[i] == 0.0;
        // Injector 1 goes off when it is locked too; injector 2's last
        // test only when free.
        const bool off = (gg > r.gradientAbove && g > r.glassAbove) || (g > r.glassOver && (i == 0 || free));
        double set = -1.0, next = 0.0;
        if (off) {
            *on[i] = 0.0;
            *fraction[i] = 0.0;
            *locked[i] = 1.0;
            *level[i] = 0.0;
            if (*sounding[i] != 0.0) {
                *sounding[i] = 0.0;
                sound(offSound[i]);
            }
            continue;
        }
        if (!free)
            continue;
        if (gg > r.toMinimum[0] && g > r.toMinimum[1] && *level[i] == 2.0)
            set = 0.6, next = 1.0;
        else if (gg > r.toMidway[0] && g > r.toMidway[1] && *level[i] == 3.0)
            set = 0.75, next = 2.0;
        else if ((gg < r.fromOff[0] || g < r.fromOff[1]) && *level[i] == 0.0)
            set = 0.6, next = 1.0;
        else if ((gg < r.fromMinimum[0] || g < r.fromMinimum[1]) && *level[i] == 1.0)
            set = 0.75, next = 2.0;
        else if (gg < r.toFull[0] || g < r.toFull[1])
            set = 1.0, next = 3.0;
        if (set < 0.0)
            continue;
        *on[i] = 1.0;
        *fraction[i] = set;
        *locked[i] = 1.0;
        *level[i] = next;
        if (*sounding[i] == 0.0) {
            *sounding[i] = 1.0;
            sound(onSound[i]);
        }
    }
    // Firing on regardless on a climb at full regulator.
    const double heatCheck = s.heatIn > 0.0 ? s.heatOut / s.heatIn : std::numeric_limits<double>::infinity();
    const double excess = s.boilerHeat / c.maxBoilerHeatBtu;
    s.shovelAnyway = t > 0.98 && elevationPercent < -0.3 && heatCheck > 1.25 && excess < 1.07 ? 1.0 : 0.0;
    const double H = s.boilerHeat, Hmax = c.maxBoilerHeatBtu;
    if (s.fullBoilerHeat != 0.0 && s.shovelAnyway == 0.0) {
        s.heatRatio = 1.0;
    } else {
        if (s.heatIn > 1.05 * s.heatOutBefore)
            s.heatRatio = 1.0;
        else if (H > 0.9 * Hmax && H <= Hmax)
            s.heatRatio = 50.0 * (Hmax - H) / Hmax + 1.0;
        else if (H <= 0.9 * Hmax)
            s.heatRatio = 6.0;
        else
            s.heatRatio = 1.0;
        s.heatRatio = std::clamp(s.heatRatio, 0.001, 6.0);
    }
}

}

Diagram diagram(const Config &c, const EngineConfig &e, double p, double t, double cutoff, double revsPerS,
                double backPsi) {
    const Tables &tb = tables();
    Diagram d;
    const double E = d.release = c.cylinderExhaust(cutoff);
    const double K = d.compression = c.cylinderCompression(cutoff);
    const double A = d.admission = c.cylinderAdmission(cutoff);
    const double Cl = c.clearance;
    const double revsPerMin = 60.0 * revsPerS;
    // The steam chest's pressure drop with speed; the drop to cutoff,
    // saturated by the port's opening, superheated by the speed.
    const double drop = c.initialPressureDrop(revsPerMin);
    d.chest = t * drop * p;
    d.pa = (t * p + Atmosphere) * drop;
    double cutoffDrop;
    if (c.hasSuperheater) {
        cutoffDrop = 1.0 - std::sqrt(revsPerMin) / c.superheatCutoffFactor;
    } else {
        const double lower = tb.cutoffDropLower(revsPerMin, cutoff);
        const double upper = tb.cutoffDropUpper(revsPerMin, cutoff);
        cutoffDrop = (c.portOpening - 0.05) / (0.12 - 0.05) * (upper - lower) + lower;
    }
    d.pb = d.pa * cutoffDrop;
    d.pc = d.pb * (cutoff + Cl) / (E + Cl);
    d.pd = backPsi + Atmosphere;
    // Open Rails zeroes these "for display" before using them.
    if (t < 0.02) {
        d.pa = 0.0;
        d.pd = 0.0;
    }
    d.pe = d.pd;
    d.pf = d.pe * (K + Cl) / (A + Cl);
    // The work of a stroke per square inch of piston: forward, from the
    // absolute pressures, less the return stroke's.
    const double S = e.strokeIn;
    const double R = (E + Cl) / (cutoff + Cl), Rk = (K + Cl) / (A + Cl);
    const double work = (d.pa + d.pb) / 2.0 * S * cutoff + d.pb * logRatio(R) * S * (E - cutoff)
            + (d.pc + d.pd) / 2.0 * S * (1.0 - E) - (d.pd + d.pe) / 2.0 * S * (1.0 - K)
            - d.pe * Rk * logRatio(Rk) * S * (K - A) - (d.pa + d.pf) / 2.0 * S * A;
    d.mep = t < 0.01 ? 0.0 : std::clamp(work / S, 0.0, c.maxBoilerPressurePsi);
    // The steam at release less the steam kept from pre-admission, each
    // stroke.
    const double released = std::clamp(d.pc - Atmosphere, 0.0, p);
    const double admitted = std::clamp(d.pf - Atmosphere, 0.0, p);
    const double atRelease = e.sweptVolumeFt3 * (E + Cl) * tb.steamDensity(released);
    const double atAdmission = admitted > 0.0 ? e.sweptVolumeFt3 * (A + Cl) * tb.steamDensity(admitted) : 0.0;
    d.steam = e.cylinders * revsPerS * 2.0 * (atRelease - atAdmission);
    return d;
}

// Steam on each piston and the rods' inertia, turned at the crank. Every
// engine takes the driving axle's angle, as in Open Rails.
double crankForce(const Config &c, const EngineConfig &e, const Diagram &d, double t, double cutoff,
                  double revsPerS, double axleAngle) {
    const double Pa = std::max(d.pa, 0.0), Pb = std::max(d.pb, 0.0), Pc = std::max(d.pc, 0.0);
    const double Pd = std::max(d.pd, 0.0);
    const double E = d.release, K = d.compression, A = d.admission, Cl = c.clearance;
    const double pistonIn2 = Pi * e.diameterIn * e.diameterIn / 4.0;
    // Open Rails' crank (1.08 ft) and rod (10.8 ft, its weight's centre at
    // 4.32 ft) for every loco.
    const double crankRod = 0.1, rodCentre = 0.04;
    const double revsPerMin = 60.0 * revsPerS;
    const double speedFactor = 0.00034 * 1.08 * revsPerMin * revsPerMin;
    double force = 0.0;
    for (int i = 0; i < e.cylinders; ++i) {
        const double offset = c.crankAnglesRad[size_t(std::min(i, 3))];
        double angle = wrap(axleAngle + offset);
        const double x = std::fabs(angle) / Pi;
        double forwardX, backwardX;
        if (angle > 0.0 && angle < Pi) {
            forwardX = x;
            backwardX = 1.0 - x;
        } else {
            angle = -angle;
            forwardX = 1.0 - x;
            backwardX = x;
        }
        // The side of the axle the rods are on.
        double side = wrap(axleAngle + offset);
        if (side < 0.0)
            side += 2.0 * Pi;
        const double sinA = std::sin(angle), cosA = std::cos(angle), cos2A = std::cos(2.0 * angle);
        double forwardP;
        if (cutoff > forwardX)
            forwardP = Pa + (Pb - Pa) / cutoff * (forwardX - Cl);
        else if (E > forwardX)
            forwardP = Pb * (cutoff + Cl) / (forwardX + Cl);
        else
            forwardP = Pc * (E + Cl) / (forwardX + Cl);
        const double backwardP = backwardX < A ? Pd * Cl / (K + Cl) : Pd;
        const double crankP = t < 0.01 ? 0.0 : forwardP - backwardP;
        const double tangential = std::fabs(sinA + crankRod * sinA * cosA);
        double reciprocating = speedFactor * (cosA + crankRod * cos2A) * 580.0;
        double rod = speedFactor * (cosA + rodCentre * cos2A) * 600.0;
        if (side > 0.0 && side < Pi) {
            reciprocating = -reciprocating;
            rod = -rod;
        }
        if ((e.cylinders == 3 && i > 1) || (e.cylinders == 4 && (i == 1 || i == 3)))
            reciprocating = 0.0;
        double inertia = (reciprocating + rod) * tangential;
        if (t < 0.01)
            inertia *= 0.2;
        force += (tangential * pistonIn2 * crankP + inertia) * e.strokeIn / (2.0 * e.wheelRadiusIn);
    }
    return force;
}

double superheatFactor(const Config &c, double steamLbps, double cutoff, double revsPerS, double &set,
                       double &temperatureF) {
    const Tables &tb = tables();
    double factor;
    temperatureF = 0.0;
    if (c.hasSuperheater) {
        // Less superheat keeps hotter cylinders from condensing; the latch
        // holds until the superheat a step on would fall 25 °F short.
        const double steamLbph = 3600.0 * steamLbps;
        temperatureF = std::clamp(tb.superheatTemperature(steamLbph) * c.superheatRatio, 0.0, c.superheatTempF);
        const double hot = std::clamp(1.0 - 0.00214 * 60.0 * revsPerS, 0.25, 1.0);
        const double limit = tb.superheatLimit(cutoff);
        const double volumeRatio = 1.0 + 0.0015 * (temperatureF - limit * hot);
        const double future = tb.superheatTemperature(steamLbph / volumeRatio) * c.superheatRatio;
        if (temperatureF > limit * hot)
            set = 1.0;
        else if (future < (limit - 25.0) * hot)
            set = 0.0;
        factor = set != 0.0 ? 1.0 / volumeRatio : 1.0;
    } else {
        // A saturated loco's steam condenses, the more at short cutoff.
        factor = 1.0 + tb.condensation(cutoff);
    }
    return std::max(factor, 0.6);
}

void State::write(double *numbers) const {
    int n = 0;
#define TSRE_STEAM_PUT(name) numbers[n++] = name;
    TSRE_STEAM_STATE(TSRE_STEAM_PUT)
    for (const EngineState &e : engines) {
#define TSRE_STEAM_PUT_ENGINE(name) numbers[n++] = e.name;
        TSRE_STEAM_ENGINE_STATE(TSRE_STEAM_PUT_ENGINE)
    }
#undef TSRE_STEAM_PUT
#undef TSRE_STEAM_PUT_ENGINE
}

void State::read(const double *numbers, int engineCount) {
    int n = 0;
#define TSRE_STEAM_GET(name) name = numbers[n++];
    TSRE_STEAM_STATE(TSRE_STEAM_GET)
    engines.resize(size_t(std::max(engineCount, 0)));
    for (EngineState &e : engines) {
#define TSRE_STEAM_GET_ENGINE(name) e.name = numbers[n++];
        TSRE_STEAM_ENGINE_STATE(TSRE_STEAM_GET_ENGINE)
    }
#undef TSRE_STEAM_GET
#undef TSRE_STEAM_GET_ENGINE
}

void start(const Config &c, State &s) {
    s = State();
    s.engines.resize(c.engines.size());
    s.started = 1.0;
    s.pressure = c.startPressurePsi;
    s.waterFraction = c.startWaterFraction;
    s.boilerMass = c.startBoilerMassLb;
    s.boilerHeat = c.startBoilerHeatBtu;
    s.waterTemperature = c.startWaterTempK;
    s.fireMass = c.idealFireMassKg;
    s.tenderCoal = c.maxTenderCoalKg;
    s.tenderWater = c.maxTenderWaterUkGallons;
    // Open Rails' first step sees 1 lb/s of cylinder steam.
    s.cylinderSteam = 1.0;
    s.heatRatio = 0.001;
    s.pressureRatio = 0.001;
    s.fullHeatRatio = 1.0;
    s.maxHeatRatio = 1.0;
    s.boostTime = 0.01;
    s.superheatFactor = 1.0;
    s.nextPulse = 1.0;
    const Tables &tb = tables();
    s.steamEnthalpy = tb.steamHeat(s.pressure);
    s.waterEnthalpy = tb.waterHeat(s.pressure);
    s.steamDensity = tb.steamDensity(s.pressure);
    s.waterDensity = tb.waterDensity(s.pressure);
}

void step(const Config &c, State &s, const Controls &controls, const Inputs &in, Outputs &out) {
    if (c.engines.empty())
        return;
    if (s.started != 0.0 && s.engines.size() != c.engines.size())
        s.engines.resize(c.engines.size());
    Step{c, s, controls, in, out}.run();
}

}
