/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "CameraMap.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/geo/GeoCoordinates.h>

namespace {
// Keys move the map by this share of the screen a second, and turn it by
// this many radians a second.
constexpr float KeyPanScreens = 0.75f;
constexpr float KeyTurnRadians = 1.2f;
// A drag across the full width turns the map this far.
constexpr float DragTurnRadians = 3.14159265f;
}

CameraMap::CameraMap() : Camera(nullptr) {
    pozT = tile;
}

CameraMap::~CameraMap() = default;

void CameraMap::syncTile() {
    tile[0] = float(view.tileX);
    tile[1] = float(view.tileZ);
}

void CameraMap::setViewport(int width, int height) {
    view.width = std::max(1, width);
    view.height = std::max(1, height);
}

void CameraMap::zoomAt(float px, float py, float wheelSteps) {
    // Half an octave a wheel notch (1.2 steps).
    view.zoomAt(px, py, std::pow(2.0f, -wheelSteps / 2.4f));
    syncTile();
}

float *CameraMap::getMatrix() {
    view.viewMatrix(lookAt);
    return lookAt;
}

float *CameraMap::getPos() {
    pos[0] = view.x;
    pos[1] = MapView::EyeHeight;
    pos[2] = view.z;
    return pos;
}

float *CameraMap::getTarget() {
    target[0] = view.x;
    target[1] = 0.0f;
    target[2] = view.z;
    return target;
}

void CameraMap::setPos(float *position) {
    setPos(position[0], position[1], position[2]);
}

void CameraMap::setPos(float x, float, float z) {
    view.x = x;
    view.z = z;
    view.normalize();
    syncTile();
}

void CameraMap::setPozT(int x, int y) {
    view.tileX = x;
    view.tileZ = y;
    syncTile();
}

float CameraMap::getRotX() {
    return view.heading;
}

float CameraMap::getRotY() {
    return -1.5707963f;
}

void CameraMap::check_coords() {
    view.normalize();
    syncTile();
}

void CameraMap::MouseDown(QMouseEvent *e) {
    last = e->position() * Game::PixelRatio;
    // The right button always moves the map, also while a tool paints with the
    // left one; turning, used less, is on the middle one, so a right click for a
    // context menu never turns the map (user, 2026-10-09).
    if (e->button() == Qt::LeftButton || e->button() == Qt::RightButton)
        dragging = true;
    if (e->button() == Qt::MiddleButton)
        turning = true;
}

void CameraMap::MouseMove(QMouseEvent *e) {
    const QPointF position = e->position() * Game::PixelRatio;
    const QPointF delta = position - last;
    last = position;
    if (dragging)
        view.pan(float(delta.x()), float(delta.y()));
    else if (turning)
        view.rotate(float(delta.x()) / float(view.width) * DragTurnRadians);
    syncTile();
}

void CameraMap::MouseUp(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton || e->button() == Qt::RightButton)
        dragging = false;
    if (e->button() == Qt::MiddleButton)
        turning = false;
}

void CameraMap::keyDown(QKeyEvent *e) {
    switch (e->key()) {
    case Qt::Key_W:
    case Qt::Key_Up:
        moveF = true;
        break;
    case Qt::Key_S:
    case Qt::Key_Down:
        moveB = true;
        break;
    case Qt::Key_A:
    case Qt::Key_Left:
        moveL = true;
        break;
    case Qt::Key_D:
    case Qt::Key_Right:
        moveR = true;
        break;
    case Qt::Key_Q:
        turnLeft = true;
        break;
    case Qt::Key_E:
        turnRight = true;
        break;
    case Qt::Key_N:
        view.heading = MapView::NorthUp;
        break;
    default:
        break;
    }
}

void CameraMap::keyUp(QKeyEvent *e) {
    switch (e->key()) {
    case Qt::Key_W:
    case Qt::Key_Up:
        moveF = false;
        break;
    case Qt::Key_S:
    case Qt::Key_Down:
        moveB = false;
        break;
    case Qt::Key_A:
    case Qt::Key_Left:
        moveL = false;
        break;
    case Qt::Key_D:
    case Qt::Key_Right:
        moveR = false;
        break;
    case Qt::Key_Q:
        turnLeft = false;
        break;
    case Qt::Key_E:
        turnRight = false;
        break;
    default:
        break;
    }
}

void CameraMap::update(float fps) {
    const float seconds = 1.0f / std::max(fps, 1.0f);
    const float step = KeyPanScreens * float(std::min(view.width, view.height)) * seconds;
    // A drag moves the ground with the mouse; keys move the view the other
    // way.
    float dx = 0.0f, dy = 0.0f;
    if (moveF)
        dy += step;
    if (moveB)
        dy -= step;
    if (moveL)
        dx += step;
    if (moveR)
        dx -= step;
    if (dx != 0.0f || dy != 0.0f)
        view.pan(dx, dy);
    if (turnLeft)
        view.rotate(KeyTurnRadians * seconds);
    if (turnRight)
        view.rotate(-KeyTurnRadians * seconds);
    syncTile();
}

PreciseTileCoordinate *CameraMap::getCurrentPos() {
    if (currentPos == nullptr)
        currentPos = new PreciseTileCoordinate();
    currentPos->TileX = view.tileX;
    currentPos->TileZ = -view.tileZ;
    currentPos->setWxyz(view.x, 0.0f, view.z);
    return currentPos;
}
