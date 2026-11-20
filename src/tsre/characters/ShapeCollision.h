/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#ifndef CHARACTERS_SHAPECOLLISION_H
#define CHARACTERS_SHAPECOLLISION_H

#include <tsre/characters/CollisionMesh.h>
#include <QString>
#include <memory>

namespace Characters {

// What a walking character collides with in a shape (task characters 02;
// the user, 2026-11-20):
// - Default: as the shape's .sd says (ESD_TSRECollision), else every part
//   of its most detailed level but vegetation (MSTS's cruciform light
//   materials, a glTF tree with an impostor), alpha-tested parts too
//   (fences);
// - Solid: every part, vegetation too (a hedge of crossed faces);
// - None: nothing (walked through);
// - Box: its boxes (its .sd's ESD_Complex boxes, else its
//   ESD_Bounding_Box, else its triangles' bounds): only where the route's
//   designer chose it, never by default.
// A world object's TSRECollision goes over its shape's.
enum class Collision : unsigned char { Default, Solid, None, Box };
// By name (default, solid, none, box; any case); else Default.
Collision collisionNamed(const QString &name);
// What a world object's TSRECollision value is (0 Default, 1 Solid, 2
// None, 3 Box).
Collision collisionOfObject(unsigned char value);

// A shape's collision mesh as `mode` says (Default: the shape's own say,
// else the rules), in its space as drawn; read once by file and mode, then
// kept; any thread. Null for nothing to collide with.
std::shared_ptr<const CollisionMesh> shapeCollision(const QString &path, const QString &texPath, Collision mode);
// What it was read from: its parts (all, and vegetation among them) and
// the mode taken (the shape's own when asked Default), for logs and tests.
struct ShapeCollisionInfo {
    int parts = 0;
    int vegetationParts = 0;
    int alphaTestedParts = 0;
    Collision taken = Collision::Default;
};
ShapeCollisionInfo shapeCollisionInfo(const QString &path, const QString &texPath, Collision mode);
// Forgets every shape read (read again when asked).
void forgetShapeCollisions();

}

#endif
