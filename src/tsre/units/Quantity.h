#pragma once

// Task core si-units: what a value measures (Units.h).
namespace Units {

// What a value measures, and its unit inside TSRE.
enum class Quantity {
    // A file's value its reader does not name: its unit tells (fromFile).
    Any,
    Length,             // m
    Distance,           // m, shown in km or miles
    Speed,              // m/s
    Mass,               // kg
    Force,              // N
    Power,              // W
    Pressure,           // Pa
    Temperature,        // °C
    Angle,              // rad, shown in degrees
    Time,               // s
    Current,            // A
    Voltage,            // V
    Frequency,          // Hz
    Area,               // m²
    Volume,             // m³
    Resistance,         // N/(m/s), Davis B
    ResistanceSquared,  // N/(m/s)², Davis C
    Stiffness,          // N/m
    Energy,             // J
    EnergyDensity,      // J/kg
    MassRate,           // kg/s
    PressureRate,       // Pa/s
    ForceRate,          // N/s
    PowerRate,          // W/s
};

}
