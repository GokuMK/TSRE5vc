#include <tsre/sim/steam/SteamConfig.h>
#include <QString>
#include <algorithm>
#include <cmath>
#include <tsre/fileFunctions/SimisDocument.h>
#include <tsre/trains/VehicleDefinition.h>

namespace Sim::Steam {

namespace {

// Open Rails' conversions (Orts.Common/Conversions.cs).
constexpr double Pi = 3.14159265358979323846;
constexpr double InchM = 0.0254;
constexpr double FootM = 0.3048;
constexpr double Ft2M2 = 0.092903;
constexpr double Ft3M3 = 1.0 / 35.3146665722;
constexpr double LbKg = 1.0 / 2.20462;
constexpr double PsiPa = 6894.757293168;
constexpr double MphMps = 1.0 / 2.23693629;
constexpr double HpW = 745.699872;
constexpr double UsTonKg = 907.1847;

double kelvinFromF(double f) {
    return (f - 32.0) * 100.0 / 180.0 + 273.15;
}

using Item = SimisDocument::Item;

// A file's values as TSRE reads them: in SI through the key catalogue.
struct Reader {
    const VehicleDefinition &d;
    double si(const char *path, double otherwise, int index = 0) const { return d.number(path, index, otherwise); }
    QString text(const char *path, int index = 0) const { return d.text(path, index); }
    // A table of (x y) pairs, its units' suffixes converted (Open Rails'
    // Any reader); empty without one.
    Table table(const char *path) const {
        if (!d.document)
            return Table();
        const SimisDocument &doc = *d.document;
        std::vector<double> v;
        for (const Item &item : doc.values(doc.last(path))) {
            double n = 0.0;
            if (doc.number(item, Units::Quantity::Any, n))
                v.push_back(n);
        }
        if (v.size() < 4)
            return Table();
        std::vector<double> x, y;
        for (size_t i = 0; i + 1 < v.size(); i += 2) {
            x.push_back(v[i]);
            y.push_back(v[i + 1]);
        }
        return Table(std::move(x), std::move(y));
    }
    // A key inside a block (ORTSSteamEngines' Steam blocks: Open Rails' Any
    // reader, bare numbers as written).
    double in(Item block, const char *key, double otherwise) const {
        const SimisDocument &doc = *d.document;
        const QVector<Item> found = doc.all(key, block);
        if (found.isEmpty())
            return otherwise;
        double n = 0.0;
        const QVector<Item> values = doc.values(found.last());
        return !values.isEmpty() && doc.number(values[0], Units::Quantity::Any, n) ? n : otherwise;
    }
    QString textIn(Item block, const char *key) const {
        const SimisDocument &doc = *d.document;
        const QVector<Item> found = doc.all(key, block);
        if (found.isEmpty())
            return QString();
        const QVector<Item> values = doc.values(found.last());
        return values.isEmpty() ? QString() : doc.text(values[0]).toString();
    }
};

// Steam an IHP-hour takes (lb), from Johnson, by superheat and pressure,
// with Open Rails' bands as written (their gaps take the last column).
double steamPerIhpHour(double superheatF, bool superheated, double pressurePsi) {
    static const double rows[6][5] = {{16.53, 16.1, 15.825, 15.625, 17.01},
                                      {17.41, 16.925, 16.675, 16.5, 17.91},
                                      {18.325, 17.8125, 17.575, 17.3875, 19.05},
                                      {19.3, 18.725, 18.5, 18.3, 20.1},
                                      {20.6, 19.95, 19.55, 19.3, 21.375},
                                      {27.5, 26.6, 26.05, 25.7, 28.425}};
    int column = 4;
    if (pressurePsi >= 200 && pressurePsi < 249)
        column = 0;
    else if (pressurePsi >= 250 && pressurePsi < 299)
        column = 1;
    else if (pressurePsi >= 300 && pressurePsi < 349)
        column = 2;
    else if (pressurePsi >= 350)
        column = 3;
    int row = 5;
    if (superheated) {
        if (superheatF >= 350)
            row = 0;
        else if (superheatF >= 300)
            row = 1;
        else if (superheatF >= 250)
            row = 2;
        else if (superheatF > 200)
            row = 3;
        else if (superheatF >= 150)
            row = 4;
    }
    return rows[row][column];
}

// A piston's area (ft²) and the steam one stroke sweeps (ft³): Open Rails'
// with its rod "volume", half the rod's section in ft².
void cylinderGeometry(EngineConfig &e) {
    const double rodFt2 = Pi * (3.5 / 2.0) * (3.5 / 2.0) / 144.0;
    e.pistonAreaFt2 = Pi * (e.diameterIn / 2.0) * (e.diameterIn / 2.0) / 144.0;
    e.sweptVolumeFt3 = e.pistonAreaFt2 * e.strokeIn / 12.0 - rodFt2 / 2.0;
    e.lpPistonAreaFt2 = Pi * (e.lpDiameterIn / 2.0) * (e.lpDiameterIn / 2.0) / 144.0;
    e.lpSweptVolumeFt3 = e.lpPistonAreaFt2 * e.strokeIn / 12.0 - rodFt2 / 2.0;
}

// Safety valves sized to the boiler (Open Rails sizes them every step).
SafetyValves safetyValvesFor(double evaporationFt2, double pressurePsi) {
    SafetyValves v;
    const double total = 0.036 * evaporationFt2 / (0.1 * (pressurePsi + 14.696)) + 1.0;
    static const double sizes[4] = {2.5, 3.0, 3.5, 4.0};
    static const double areas[4] = {0.6104, 0.7993, 0.9327, 0.9775};
    double area = areas[3];
    v.count = 4;
    v.diameterIn = 4.0;
    for (int count = 2; count <= 4; ++count) {
        const double each = total / count;
        if (each <= 4.0) {
            v.count = count;
            for (int k = 0; k < 4; ++k)
                if (each <= sizes[k]) {
                    v.diameterIn = sizes[k];
                    area = areas[k];
                    break;
                }
            break;
        }
    }
    v.maxDischargeLbps = v.count * area * (pressurePsi + 14.696) / 70.0;
    const double open[4] = {0.1, 2.0, 4.0, 6.0}, close[4] = {-4.0, -3.0, -2.0, -1.0};
    for (int k = 0; k < 4; ++k) {
        v.openPsi[k] = pressurePsi + open[k];
        v.closePsi[k] = pressurePsi + close[k];
    }
    return v;
}

}

double boilerHeatBtu(const Config &c, double pressurePsi, double waterFraction) {
    const double v = c.boilerVolumeFt3;
    return waterFraction * v * table("WaterDensityInterpolatorPSItoLBpFT3")(pressurePsi)
                   * table("WaterHeatInterpolatorPSItoBTUpLB")(pressurePsi)
           + (1.0 - waterFraction) * v * table("SteamDensityInterpolatorPSItoLBpFT3")(pressurePsi)
                   * table("SaturatedSteamHeatInterpolatorPSItoBTUpLB")(pressurePsi);
}

std::shared_ptr<const Config> readConfig(const VehicleDefinition &definition) {
    const Reader r{definition};
    auto config = std::make_shared<Config>();
    Config &c = *config;
    const Table &tsat = table("SaturatedSteamHeatPressureToTemperatureInterpolatorPSItoF");
    const Table &steamHeat = table("SaturatedSteamHeatInterpolatorPSItoBTUpLB");
    const Table &waterDensity = table("WaterDensityInterpolatorPSItoLBpFT3");
    const Table &steamDensity = table("SteamDensityInterpolatorPSItoLBpFT3");

    // The car (MSTSLocomotive and MSTSWagon).
    c.massKg = r.si("wagon(mass", 0.0);
    c.lengthM = r.si("wagon(size", 0.0, 2);
    c.wheelRadiusM = r.si("engine(wheelradius", 0.0);
    if (c.wheelRadiusM <= 0.0)
        c.wheelRadiusM = 30.0 * InchM;
    const int wheels = int(r.si("engine(numwheels", 0.0));
    c.driveAxles = wheels >= 1 && wheels <= 6 ? wheels : 4;
    c.driveWheelWeightKg = r.si("engine(ortsdrivewheelweight", 0.0);
    if (c.driveWheelWeightKg <= 0.0)
        c.driveWheelWeightKg = c.massKg;
    c.tenderRequired = r.si("engine(istenderrequired", 1.0) != 0.0;
    c.steamHeating = r.si("engine(maxsteamheatingpressure", 0.0) > 0.0;

    // The type and the engines (spec §1, §2): one from the MSTS keys, else
    // each Steam block's own keys over them.
    const QString type = r.text("engine(ortssteamlocomotivetype");
    c.type = type == "Simple" ? LocoType::Simple : type == "Geared" ? LocoType::Geared
            : type == "Compound" ? LocoType::Compound : LocoType::Unknown;
    const QString gear = r.text("engine(ortssteamgeartype");
    if (c.type == LocoType::Geared)
        c.gear = gear == "Fixed" ? GearType::Fixed : gear == "Select" ? GearType::Select : GearType::None;
    const int mstsCylinders = std::max(0, int(r.si("engine(numcylinders", r.si("engine(numberofcylinders", 2.0))));
    EngineConfig base;
    base.cylinders = mstsCylinders != 0 ? mstsCylinders : 2;
    base.diameterIn = r.si("engine(cylinderdiameter", 0.0) / InchM;
    base.strokeIn = r.si("engine(cylinderstroke", 0.0) / InchM;
    if (base.diameterIn <= 0.0)
        base.diameterIn = 1.0 / InchM;
    if (base.strokeIn <= 0.0)
        base.strokeIn = 1.0 / InchM;
    base.lpCylinders = int(r.si("engine(lpnumcylinders", 2.0));
    base.lpDiameterIn = r.si("engine(lpcylinderdiameter", 0.0) / InchM;
    if (base.lpDiameterIn <= 0.0)
        base.lpDiameterIn = 1.0 / InchM;
    // Open Rails' MSTS path leaves the LP stroke 0 (its spec, odd 12).
    base.lpStrokeIn = r.si("engine(lpcylinderstroke", 0.0) / InchM;
    base.wheelRadiusIn = c.wheelRadiusM / InchM;
    base.counterPressureBraking = r.si("engine(ortscounterpressurebraking", 0.0) != 0.0;
    const QVector<Item> blocks = definition.document ? definition.document->all("engine(ortssteamengines(steam")
                                                     : QVector<Item>();
    for (const Item &block : blocks) {
        const auto value = [&](const char *key, double otherwise) { return r.in(block, key, otherwise); };
        EngineConfig e = base;
        e.cylinders = int(value("numcylinders", e.cylinders));
        e.diameterIn = value("cylinderdiameter", e.diameterIn * InchM) / InchM;
        e.strokeIn = value("cylinderstroke", e.strokeIn * InchM) / InchM;
        e.lpCylinders = int(value("lpnumcylinders", e.lpCylinders));
        e.lpDiameterIn = value("lpcylinderdiameter", e.lpDiameterIn * InchM) / InchM;
        e.lpStrokeIn = value("lpcylinderstroke", e.lpStrokeIn * InchM) / InchM;
        e.axle = int(value("attachedaxle", 0));
        e.boosterCutoff = value("boostercutoff", 0.3);
        e.boosterThrottleCutoff = value("boosterthrottlecutoff", 0.3);
        e.boosterGearRatio = value("boostergearratio", 1.0);
        e.counterPressureBraking = value("counterpressurebraking", e.counterPressureBraking) != 0.0;
        e.maxIndicatedHp = value("maxindicatedhorsepower", 0.0) / HpW;
        e.excessRodBalanceLb = value("excessrodbalance", 0.0) / LbKg;
        const QString kind = r.textIn(block, "auxiliarysteamenginetype");
        e.kind = kind == "Booster" ? EngineKind::Booster : kind == "Rack" ? EngineKind::Rack : EngineKind::Adhesion;
        c.engines.push_back(e);
    }
    if (c.engines.empty())
        c.engines.push_back(base);

    // Fuel, orientation (spec §3.3).
    const QString fuel = r.text("engine(ortssteamlocomotivefueltype");
    c.fuel = fuel == "Oil" ? Fuel::Oil : fuel == "Wood" ? Fuel::Wood : Fuel::Coal;
    c.fuelCalorificKJpKg = r.si("engine(ortsfuelcalorific", 33400.0e3) / 1000.0;
    c.boilerAngleRad = r.si("engine(ortsboilerangle", 0.0);
    if (c.boilerAngleRad == 0.0 && r.text("engine(ortssteamlocomotiveboilerorientation") == "Sloping")
        c.boilerAngleRad = 5.0 * Pi / 180.0;
    c.cylinderExhaust = r.table("engine(ortscylindereventexhaust");
    if (c.cylinderExhaust.empty())
        c.cylinderExhaust = table("CylinderEventExhausttoCutoff");
    c.cylinderCompression = r.table("engine(ortscylindereventcompression");
    if (c.cylinderCompression.empty())
        c.cylinderCompression = table("CylinderEventCompressiontoCutoff");
    c.cylinderAdmission = r.table("engine(ortscylindereventadmission");
    if (c.cylinderAdmission.empty())
        c.cylinderAdmission = table("CylinderEventAdmissiontoCutoff");

    // The boiler (§3.2, §3.4): pressure and volume, else 1 psi and 1 ft³.
    c.maxBoilerPressurePsi = r.si("engine(maxboilerpressure", 180.0 * PsiPa) / PsiPa;
    if (c.maxBoilerPressurePsi <= 0.0)
        c.maxBoilerPressurePsi = 1.0;
    c.boilerVolumeFt3 = r.si("engine(boilervolume", 0.0) / Ft3M3;
    if (c.boilerVolumeFt3 <= 0.0)
        c.boilerVolumeFt3 = 1.0;
    const double P = c.maxBoilerPressurePsi;
    // Grate and heating surface from the cylinders: K = (n/2)·d²·S·0.7·P/D,
    // the grate the sum of every engine's K/500, the surface the last's /10.
    double grate = 0.0, evaporation = 0.0;
    for (const EngineConfig &e : c.engines) {
        const double k = e.cylinders / 2.0 * e.diameterIn * e.diameterIn * e.strokeIn * 0.7 * P
                / (2.0 * e.wheelRadiusIn);
        grate += k / 500.0;
        evaporation = k / 10.0;
    }
    const double grateGivenM2 = r.si("engine(ortsgratearea", 0.0);
    c.grateAreaFt2 = grateGivenM2 > 0.0 ? grateGivenM2 / Ft2M2 : std::max(grate, 6.0);
    c.idealFireMassKg = c.grateAreaFt2 * Ft2M2 * 0.1778 * 864.5;
    c.maxFuelBurnGrateKgps = c.grateAreaFt2 * 150.0 * 1.1 * LbKg / 3600.0;
    c.maxFireMassKg = 2.0 * c.idealFireMassKg;
    c.saturationTempK = kelvinFromF(tsat(P));
    const double evaporationGivenM2 = r.si("engine(ortsevaporationarea", 0.0);
    c.evaporationAreaFt2 = evaporationGivenM2 > 0.0 ? evaporationGivenM2 / Ft2M2 : std::max(evaporation, 100.0);
    const double rateDefault = c.fuel == Fuel::Wood ? 11.5 : c.fuel == Fuel::Oil ? 18.0 : 15.0;
    c.evaporationRateLbpFt2pH = std::clamp(r.si("engine(ortsboilerevaporationrate", rateDefault), 7.5, 30.0);
    c.maxSteamOutputLbps = c.evaporationAreaFt2 * c.evaporationRateLbpFt2pH / 3600.0;
    const double smaxH = c.maxSteamOutputLbps * 3600.0;
    c.maxBoilerOutputLbph = smaxH;

    // Boiler geometry and the water model (§3.5), fractions of the diameter.
    const double carLength = c.lengthM;
    c.boilerLengthM = r.si("engine(ortsboilerlength", 0.0);
    if (c.boilerLengthM <= 0.0) {
        const double msts = r.si("engine(boilerlength", 0.0);
        if (msts > 0.0)
            c.boilerLengthM = msts > 0.4 * carLength && msts < carLength ? msts : 20.0 * FootM;
        else
            c.boilerLengthM = 0.48 * carLength;
    }
    c.boilerDiameterM = r.si("engine(ortsboilerdiameter", 0.0);
    if (c.boilerDiameterM <= 0.0)
        c.boilerDiameterM = 3.3 * std::sqrt(c.boilerVolumeFt3 * Ft3M3 / (Pi * c.boilerLengthM));
    c.crownHeightM = r.si("engine(ortsboilercrownheight", 0.2 * c.boilerDiameterM);
    c.crownCoverageM = r.si("engine(ortsboilercrowncoverageheight", 3.0 * InchM);
    c.glassLengthM = r.si("engine(ortswatergaugeglassheight", 0.0);
    if (c.glassLengthM <= 0.0) {
        const double msts = r.si("engine(steamgaugeglassheight", 0.0);
        if (msts > 0.0 && msts < 12.0 * InchM) {
            c.glassLengthM = msts;
        } else {
            const double feet = c.boilerLengthM / FootM;
            c.glassLengthM = (feet <= 10.0 ? 6.0 : feet <= 15.0 ? 8.0 : feet <= 20.0 ? 10.0 : 12.0) * InchM;
        }
    }
    const double D = c.boilerDiameterM;
    c.waterMinLevel = (c.crownCoverageM + c.crownHeightM + D / 2.0) / D;
    c.waterGlassMaxLevel = (c.glassLengthM + c.crownCoverageM + c.crownHeightM + D / 2.0) / D;
    c.startWaterFraction = (c.waterMinLevel + c.waterGlassMaxLevel) / 2.0;

    // Superheat as set before §3.14 decides it for MSTS locos (odd 1): the
    // boiler efficiency and pressure drop tables follow this.
    const QString boilerType = r.text("engine(ortssteamboilertype");
    c.hasSuperheater = boilerType == "Superheated";
    c.saturated = boilerType == "Saturated";

    // Boiler efficiency, firing rate, grate limit (§3.6).
    c.boilerEfficiency = r.table("engine(ortsboilerefficiency");
    if (c.boilerEfficiency.empty()) {
        if (c.fuel == Fuel::Wood) {
            const double woodLbph = (smaxH / 0.47) * steamHeat(P) / (c.fuelCalorificKJpKg / 2.326);
            c.boilerEfficiency = Table({0.0, woodLbph / c.grateAreaFt2}, {0.94, 0.47});
        } else {
            c.boilerEfficiency = table(c.hasSuperheater ? "SuperBoilerEfficiencyGrateAreaInterpolatorLbstoX"
                                                        : "SatBoilerEfficiencyGrateAreaInterpolatorLbstoX");
        }
    }
    const double be0 = c.boilerEfficiency(0.0);
    c.maxFiringRateLbph = (smaxH / (be0 / 2.0)) * steamHeat(P) / (c.fuelCalorificKJpKg / 2.326);
    c.grateLimitLbpFt2 = (be0 / 2.0 - be0) / ((c.boilerEfficiency(100.0) - be0) / 100.0);

    // Cylinder constants (§3.7).
    c.cylinderEfficiency = r.si("engine(ortscylinderefficiencyrate", 0.0);
    if (c.cylinderEfficiency == 0.0)
        c.cylinderEfficiency = 1.0;
    c.superheatCutoffFactor = r.si("engine(ortssuperheatcutoffpressurefactor", 0.0);
    if (c.superheatCutoffFactor == 0.0)
        c.superheatCutoffFactor = 40.0;
    c.portOpening = r.si("engine(ortscylinderportopening", 0.0);
    if (c.portOpening == 0.0)
        c.portOpening = 0.085;
    c.portOpening = std::clamp(c.portOpening, 0.05, 0.12);

    // Ejectors, heat loss, the booster's choke (§3.8).
    c.smallEjectorLbps = r.si("engine(vacuumbrakessmallejectorusagerate", 300.0 * LbKg / 3600.0) / LbKg;
    c.largeEjectorLbps = r.si("engine(vacuumbrakeslargeejectorusagerate", 650.0 * LbKg / 3600.0) / LbKg;
    // Open Rails reads the surface in m² into a ft² field (odd 8).
    c.boilerSurfaceFt2 = r.si("engine(ortsboilersurfacearea", 0.0);
    if (c.boilerSurfaceFt2 == 0.0)
        c.boilerSurfaceFt2 = 16.0 * c.grateAreaFt2;
    c.insulationCoefficient = r.si("engine(ortsheatcoefficientinsulation", 0.4);
    c.insulatedFraction = r.si("engine(ortsfractionboilerinsulated", 0.86);
    c.boosterChokeIn = P > 300 ? 0.375 : P > 275 ? 0.4375 : P > 250 ? 0.5 : P > 200 ? 0.5625 : 0.625;
    c.boosterPressureFactor = c.boosterChokeIn / 0.625;

    // Crank angles (§3.9), per loco (Open Rails shares one array, odd 4).
    if (r.si("engine(ortswheelcrankangledifference", 0.0, 1) != 0.0) {
        for (int k = 0; k < 4; ++k)
            c.crankAnglesRad[size_t(k)] = r.si("engine(ortswheelcrankangledifference", 0.0, k);
    } else {
        const double degrees[3][4] = {{0, 90, 0, 0}, {0, 120, 240, 0}, {0, 90, 180, 270}};
        const int set = mstsCylinders == 4 ? 2 : mstsCylinders == 3 ? 1 : 0;
        for (int k = 0; k < 4; ++k)
            c.crankAnglesRad[size_t(k)] = degrees[set][k] * Pi / 180.0;
    }

    // Tractive effort per engine (§3.10).
    const double teFactor = r.si("engine(ortstractiveeffortfactor", 0.85);
    const double gearedFactor = r.si("engine(ortsgearedtractiveeffortfactor", 0.7);
    const double gearLow = r.si("engine(ortssteamgearratio", c.gear == GearType::Select ? 9.0 : 5.0, 0);
    const double gearRate = r.si("engine(ortssteammaxgearpistonrate", c.gear == GearType::Select ? 500.0 : 700.0);
    const double eta = c.cylinderEfficiency;
    for (EngineConfig &e : c.engines) {
        const double d = e.diameterIn, s = e.strokeIn, wheel = 2.0 * e.wheelRadiusIn;
        if (e.kind == EngineKind::Booster) {
            e.maxTractiveEffortLbf = (0.2629 * e.boosterCutoff + 0.5971) * P * d * d * s * e.boosterGearRatio / wheel;
        } else if (c.type == LocoType::Compound) {
            const double ratio = (e.lpDiameterIn / d) * (e.lpDiameterIn / d);
            e.maxTractiveEffortLbf = eta * 1.6 * P * e.lpDiameterIn * e.lpDiameterIn * e.lpStrokeIn / ((ratio + 1.0) * wheel);
        } else if (c.type == LocoType::Geared && c.gear != GearType::None) {
            e.maxTractiveEffortLbf = e.cylinders / 2.0 * (d * d * s / wheel) * P * gearedFactor * gearLow * eta;
        } else {
            e.maxTractiveEffortLbf = e.cylinders / 2.0 * (d * d * s / wheel) * P * teFactor * eta;
        }
        c.maxTractiveEffortLbf += e.maxTractiveEffortLbf;
        cylinderGeometry(e);
    }

    // Tender (§3.11): kg in the file, 1 kg where none.
    c.maxTenderCoalKg = r.si("engine(maxtendercoalmass", r.si("engine(ortsmaxtenderwoodmass", 1.0));
    if (c.fuel == Fuel::Oil && r.si("engine(ortsmaxtenderfueloilvolume", 0.0) != 0.0)
        c.maxTenderCoalKg = r.si("engine(ortsmaxtenderfueloilvolume", 0.0) * 1000.0
                * r.si("engine(ortsfueloilspecificgravity", 0.9659);
    c.maxTenderWaterLb = r.si("engine(maxtenderwatermass", 1.0) / LbKg;
    c.maxTenderWaterUkGallons = c.maxTenderWaterLb / 10.021;

    // Pressure drop, boiler heat, back pressure (§3.12).
    c.initialPressureDrop = r.table("engine(ortscylinderinitialpressuredrop");
    if (c.initialPressureDrop.empty())
        c.initialPressureDrop = table(c.hasSuperheater ? "SuperInitialPressureDropRatioInterpolatorRpMtoX"
                                                       : "SatInitialPressureDropRatioInterpolatorRpMtoX");
    c.maxBoilerHeatBtu = boilerHeatBtu(c, P, c.startWaterFraction);
    c.maxBoilerSafetyHeatBtu = boilerHeatBtu(c, P + 0.1 + 6.0, c.startWaterFraction);
    c.backPressure = r.table("engine(ortscylinderbackpressurevssteamoutput");
    if (c.backPressure.empty()) {
        std::vector<double> x, y;
        for (int k = 0; k <= 11; ++k) {
            x.push_back(smaxH * k / 10.0);
            y.push_back(0.0002762 * smaxH * (k / 10.0) * (k / 10.0));
        }
        c.backPressure = Table(std::move(x), std::move(y));
    }

    // Injectors (§3.13): live sized to the whole design output, exhaust to
    // three quarters, at 65 °F.
    for (int k = 0; k < 2; ++k) {
        double t = r.si("engine(ortsinjectortypes", -1.0, k);
        if (t != 0.0 && t != 1.0)
            t = r.si("engine(injectortypes", -1.0, k);
        c.injectors[size_t(k)] = t == 1.0 ? InjectorType::Exhaust : InjectorType::Live;
    }
    if (P > 250.0) {
        c.liveCorrection = 200.0 / P;
        c.auxiliaryCorrection = 200.0 / P;
    }
    const Table2D &liveMaxima = table2D("LiveSteamInjectorWaterDeliveryMaximaLbsPerPSIPerF");
    const Table2D &exhaustMaxima = table2D("ExhaustSteamInjectorWaterDeliveryMaximaLbsPerPSIPerF");
    const double designBackPressure = c.backPressure(smaxH);
    for (int k = 0; k < 2; ++k) {
        const bool live = c.injectors[size_t(k)] == InjectorType::Live;
        const double reference = live ? 10.5 : 10.0;
        double size = r.si("engine(ortsinjectorsize", 0.0, k) * 1000.0;
        if (size <= 0.0) {
            size = 19.0;
            for (int mm = 3; mm <= 19; ++mm) {
                const double flow = (mm / reference) * (mm / reference)
                        * (live ? liveMaxima(65.0, c.liveCorrection * P) : exhaustMaxima(65.0, designBackPressure));
                if (flow >= (live ? 1.0 : 0.75) * smaxH) {
                    size = mm;
                    break;
                }
            }
        }
        c.injectorSizeMm[size_t(k)] = size;
        c.injectorFactor[size_t(k)] = (size / reference) * (size / reference);
        if (!live) {
            c.supplementalFactor = ((size - 1.0) / 10.5) * ((size - 1.0) / 10.5);
            c.auxiliaryFactor = (size / 10.5) * (size / 10.5);
        }
    }

    // Superheat (§3.14).
    const double superheatGivenM2 = r.si("engine(ortssuperheatarea", 0.0);
    const double superheatGivenF = r.si("engine(ortsmaxsuperheattemperature", 0.0);
    const Table &superheatTemp = table("SuperheatTempInterpolatorLbpHtoDegF");
    if (c.hasSuperheater) {
        c.superheatAreaFt2 = superheatGivenM2 / Ft2M2;
        c.superheatTempF = superheatGivenF != 0.0 ? superheatGivenF : c.superheatAreaFt2 * 15000.0 / smaxH;
        c.superheatRatio = c.superheatTempF / superheatTemp(smaxH);
        c.clearance = 0.09;
    } else if (c.saturated) {
        c.clearance = 0.09;
    } else if (superheatGivenM2 == 0.0 && r.si("engine(superheater", 1.0) > 1.0) {
        c.hasSuperheater = true;
        c.superheatTempF = 250.0;
        c.superheatRatio = c.superheatTempF / superheatTemp(smaxH);
        c.superheatAreaFt2 = 250.0 * smaxH / 15000.0;
        c.clearance = 0.09;
    } else {
        c.superheatTempF = 0.0;
        c.clearance = 0.08;
    }

    // Speed, burn table, boiler power, IHP (§3.15).
    if (c.hasSuperheater) {
        c.maxPistonSpeedFtpMin = 1000.0;
        c.maxSpeedFactor = table("SuperheatedSpeedFactorSpeedDropFtpMintoX")(1000.0);
    } else if (c.type == LocoType::Geared && c.gear != GearType::None) {
        c.maxPistonSpeedFtpMin = gearRate;
        c.maxSpeedFactor = table("SaturatedSpeedFactorSpeedDropFtpMintoX")(gearRate);
    } else {
        c.maxPistonSpeedFtpMin = 700.0;
        c.maxSpeedFactor = table("SaturatedSpeedFactorSpeedDropFtpMintoX")(700.0);
    }
    const double strokeM = c.engines[0].strokeIn * InchM;
    if (c.type == LocoType::Geared && c.gear != GearType::None) {
        // Low gear's top speed, on engine 0's axle.
        const double pistonMps = gearRate / gearLow * FootM / 60.0;
        c.maxLocoSpeedMph = pistonMps * 2.0 * Pi * c.engines[0].wheelRadiusIn * InchM / (2.0 * strokeM) / MphMps;
    } else {
        const double pistonMps = c.maxPistonSpeedFtpMin * FootM / 60.0;
        c.maxLocoSpeedMph = pistonMps * 2.0 * Pi * c.wheelRadiusM / (2.0 * strokeM) / MphMps;
    }
    c.burnRate = r.table("engine(ortsburnrate");
    if (c.burnRate.empty()) {
        const double f = c.maxFiringRateLbph;
        c.burnRate = Table({0.0, smaxH, 1.1 * smaxH, 1.2 * smaxH, 1.3 * smaxH, 1.4 * smaxH},
                           {0.0, f, 1.5 * f, 2.0 * f, 3.0 * f, 4.0 * f});
    } else {
        c.maxFiringRateLbph = c.burnRate(smaxH);
    }
    c.steamPerIhpHourLb = steamPerIhpHour(c.superheatTempF, c.hasSuperheater, P);
    c.maxBoilerOutputHp = smaxH / c.steamPerIhpHourLb;
    const double locoIhp = r.si("engine(ortsmaxindicatedhorsepower", 0.0) / HpW;
    for (EngineConfig &e : c.engines) {
        if (c.engines.size() == 1 && locoIhp > 0.0)
            e.maxIndicatedHp = locoIhp;
        else if (e.maxIndicatedHp <= 0.0)
            e.maxIndicatedHp = c.maxSpeedFactor * e.maxTractiveEffortLbf * c.maxLocoSpeedMph / 375.0;
        c.maxIndicatedHp += e.maxIndicatedHp;
        if (e.excessRodBalanceLb <= 0.0) {
            e.excessRodBalanceLb = 440.0;
            const double tons = c.massKg / UsTonKg;
            if (tons < 10.0)
                e.excessRodBalanceLb *= 0.2;
            else if (tons < 20.0)
                e.excessRodBalanceLb *= 0.3;
        }
    }

    // The start (§3.17): a hot start, 5 psi below the maximum.
    c.startPressurePsi = P - 5.0;
    const double p0 = c.startPressurePsi;
    c.startWaterTempK = kelvinFromF(tsat(p0));
    const double startKw = (p0 / P) * c.maxSteamOutputLbps * LbKg * steamHeat(p0) * 1.05506;
    c.startFlueTempK = startKw / (0.045 * c.evaporationAreaFt2 * Ft2M2) + c.startWaterTempK;
    const double f = c.startWaterFraction;
    c.startBoilerMassLb = f * c.boilerVolumeFt3 * waterDensity(p0) + (1.0 - f) * c.boilerVolumeFt3 * steamDensity(p0);
    c.startBoilerHeatBtu = boilerHeatBtu(c, p0, f);
    c.damperFactor = c.maxSteamOutputLbps / 27.0;
    c.blowerFactorLbpspPsi = 0.04 * c.maxSteamOutputLbps / P;

    // Firing (§3.18).
    const double firemanLbph = r.si("engine(ortssteamfiremanmaxpossiblefiringrate", 0.0) > 0.0
            ? r.si("engine(ortssteamfiremanmaxpossiblefiringrate", 0.0) / LbKg * 3600.0
            : r.si("engine(steamfiremanmaxpossiblefiringrate", 0.0) / LbKg * 3600.0;
    c.mechanicalStoker = r.si("engine(steamfiremanismechanicalstoker", 0.0) == 1.0 || c.fuel == Fuel::Oil;
    const double fireman = firemanLbph > 0.0 ? firemanLbph
            : c.mechanicalStoker ? 14400.0 : 180.0 / 775.0 * c.maxBoilerOutputLbph;
    c.firemanMaxFiringRateKgps = fireman / 2.2046 / 3600.0;
    c.maxTheoreticalFiringRateKgps = c.mechanicalStoker ? c.firemanMaxFiringRateKgps : 1.33 * c.firemanMaxFiringRateKgps;
    c.shovelMassKg = r.si("engine(shovelcoalmass", 6.0);

    // Per step constants (§3.19).
    c.safetyValves = safetyValvesFor(c.evaporationAreaFt2, P);
    return config;
}

}
