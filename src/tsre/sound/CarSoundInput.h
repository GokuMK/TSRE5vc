#pragma once
#include <vector>

// What a driven car's model gives its sounds (task sound 02), in the units
// Open Rails' sound controls read: its brake cylinder (psi); an engine's
// Variable1 (by engine), Variable2, Variable2_Booster and Variable3 as Open
// Rails fills them for its type, its engines' RPM, power (W) and torque
// (N·m), its tractive effort (N, + pulling the way it runs, - braking
// dynamically) and power (W), a steam engine's back pressure (psi).
struct CarSoundInput {
    float brakeCylinder = 0.0f;
    std::vector<float> variable1;
    float variable2 = 0.0f;
    float variable2Booster = 0.0f;
    float variable3 = 0.0f;
    std::vector<float> engineRpm;
    std::vector<float> enginePower;
    std::vector<float> engineTorque;
    float tractiveEffort = 0.0f;
    float tractivePower = 0.0f;
    float backPressure = 0.0f;
};
