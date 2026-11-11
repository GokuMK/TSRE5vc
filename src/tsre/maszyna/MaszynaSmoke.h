/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MASZYNASMOKE_H
#define MASZYNASMOKE_H

#include <QString>
#include <QStringView>
#include <tsre/trains/CarExhaust.h>
#include <vector>

struct E3dModel;

// MaSzyna's smoke (task maszyna 07): a model's emitter parts, rotators named
// smokesource_<template> (Model3d.cpp:1022 at 66a940d4), and TSRE emitter
// presets fitted to MaSzyna's stock templates (its data/smokesource_*.txt):
// TSRE's own values, no MaSzyna folder read (the user, 2026-11-11).
// MaSzyna's numbers are a particle look (puffs a second, their size and
// launch speed, opacity, colour); TSRE's are the gas at the nozzle. The
// default template is TSRE's diesel exhaust (CarExhaust's exhaust kind);
// each other is that scaled as it differs from the default: the flow by
// puffs a second times a puff's area, the speed by the launch speed, the
// soot by the opacity, the nozzle by the smallest puff, with its colour.
namespace MaszynaSmoke {

struct Preset {
    const char *name;   // MaSzyna's template, lower case
    const char *kind;   // CarExhaust's
    float diameter;     // the nozzle (m)
    ExhaustOutput output;
    // A vehicle's (its engine's output scaled by it), or scenery's (as it
    // is: chimneys, cooling towers, a bonfire; for task shapes 18).
    bool vehicle;
};

// A template's preset by its part's name (smokesource_st44) or its own
// (st44), any case; the default's for one MaSzyna's stock lacks.
const Preset &preset(QStringView name);
const Preset &defaults();

// A vehicle's emitter: its engine's exhaust scaled by its preset against the
// default, the preset's colour where the engine gives none.
ExhaustOutput adapt(const ExhaustOutput &engine, const Preset &preset);

struct Source {
    // Its place in the car shape's space (MSTS: x right, y up, z forward).
    float position[3] = {0.0f, 0.0f, 0.0f};
    const Preset *preset = nullptr;
};

// The model's smoke sources, at rest.
std::vector<Source> find(const E3dModel &model);

}

#endif // MASZYNASMOKE_H
