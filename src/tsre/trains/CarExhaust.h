#pragma once
#include <QAnyStringView>
#include <QString>
#include <QStringList>
#include <vector>

// What a vehicle's emitters give out (task driving 05): the hand-over from
// the simulation to the renderer. Its emitters (VehicleDefinition::emitters)
// say where; this says how much, one output per kind of the definition's
// (VehicleDefinition::exhaustKinds, in that order). The flow is the kind's
// whole, shared equally by its emitters (a loco's two exhausts, its two
// cylinder cocks' emitters); the rest is the same at each. In SI, physical quantities (the user's, 2026-10-30);
// everything after the nozzle (carried by the train and the wind, rising,
// spreading, its light) is the renderer's.
struct ExhaustOutput {
    // Gas or steam leaving all the kind's nozzles together (m³/s); 0 is off.
    float flow = 0.0f;
    // Its speed at the nozzle (m/s).
    float speed = 0.0f;
    // Its temperature (°C).
    float temperature = 0.0f;
    // The share of it that is water vapour (0 to 1); the rest is smoke.
    float steam = 0.0f;
    // How dark the smoke is (0 to 1).
    float soot = 0.0f;
    // Puffs a second (a steam loco's stack: its exhaust beats); 0 for an
    // even flow.
    float beats = 0.0f;
    // A colour the vehicle file gives (ARGB, Open Rails' diesel exhaust
    // colours); 0 for none, the renderer's own.
    unsigned color = 0;
};

struct CarExhaust {
    // By kind, in the order of the definition's exhaustKinds.
    std::vector<ExhaustOutput> outputs;
};

// An emitter's kind from its name in the vehicle file: lower case, without
// a trailing "fx", Open Rails' Exhaust1, Exhaust2... one kind ("StackFX"
// stack, "SafetyValves2FX" safetyvalves2, "Exhaust2" exhaust).
QString exhaustKind(QStringView name);

// The name of a kind's value in a vehicle model's values: "fx.<kind>.<field>"
// ("fx.stack.flow"), the field one of flow, speed, temperature, steam, soot,
// beats, color.
QString exhaustValueName(const QString &kind, QAnyStringView field);

// The test state (TSRE_EXHAUST_TEST=1, for drawing without the simulation):
// every kind on at set values, not a model's.
CarExhaust testExhaust(const QStringList &kinds);
