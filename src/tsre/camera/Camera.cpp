/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "Camera.h"
#include <math.h>
#include <algorithm>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/Game.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/GameObj.h>
#include <tsre/renderer/RenderQueue.h>

Camera::Camera(float* pt) {
    pozT = pt;
    
    up[0] = 0;
    up[1] = 1;
    up[2] = 0;
    playerRot[0] = 0.0;
    playerRot[1] = 0.0;
    playerPos[0] = 0;
    playerPos[1] = 10;
    playerPos[2] = 0;
    
    relativeRot[0] = 0;
    relativeRot[1] = 0;
    relativeRot[2] = 0;
    relativePos[0] = 0;
    relativePos[1] = 0;
    relativePos[2] = 0;
    
    Mat4::identity(lookAt);
    
    moveF = moveR = moveB = moveL = false;
    fov = Game::cameraFov;
}

Camera::Camera(const Camera& orig) {
}

Camera::~Camera() {
}

PreciseTileCoordinate* Camera::getCurrentPos(){
    if(this->currentPos == NULL)
        this->currentPos = new PreciseTileCoordinate();
    return this->currentPos;
}

float* Camera::getMatrix() {
    return lookAt;
};

float* Camera::getTarget() {
    return target;
}

float* Camera::getUp() {
    return up;
}

void Camera::setPos(float* pos){
}

void Camera::setPos(float x, float y, float z){
}

void Camera::setPlayerRot(float x, float y){
    this->playerRot[0] = x;
    this->playerRot[1] = y;
}

void Camera::setPozT(int x, int y){
}

float* Camera::getPos() {
    return pos;
}

float Camera::getRotX() {
    return 0;
}
float Camera::getRotY() {
    return 0;
}

void Camera::update(float fps) {
}

void Camera::pushRenderHud(RenderQueue &queue){
    if(cameraObject != NULL)
        cameraObject->pushRenderHud(queue);
}

// Riding the object: where it says (a 3D cab's head), else 3 m over its
// place looking ahead.
void Camera::setCameraObject(GameObj* o){
    if(cameraObject != NULL && cameraObject != o)
        cameraObject->cameraAttached(false);
    cameraObject = o;
    if(o != NULL)
        o->cameraAttached(true);
    mount = CameraMount();
    mounted = o != NULL && o->getCameraMount(mount);
    playerRot[0] = mount.look[0];
    playerRot[1] = mount.look[1];
    playerPos[0] = mount.position[0];
    playerPos[1] = mount.position[1];
    playerPos[2] = mount.position[2];
}

bool Camera::ridingPlacement(float out[8]) const {
    if (cameraObject == NULL || pozT == NULL)
        return false;
    out[0] = pozT[0];
    out[1] = pozT[1];
    std::copy(relativePos, relativePos + 3, out + 2);
    std::copy(relativeRot, relativeRot + 3, out + 5);
    return true;
}

void Camera::viewAtPlacement(const float placement[8], float *view) {
    float saved[8] = {pozT != NULL ? pozT[0] : 0.0f, pozT != NULL ? pozT[1] : 0.0f};
    std::copy(relativePos, relativePos + 3, saved + 2);
    std::copy(relativeRot, relativeRot + 3, saved + 5);
    std::copy(placement + 2, placement + 5, relativePos);
    std::copy(placement + 5, placement + 8, relativeRot);
    const float *matrix = getMatrix();
    std::copy(matrix, matrix + 16, view);
    std::copy(saved + 2, saved + 5, relativePos);
    std::copy(saved + 5, saved + 8, relativeRot);
    getMatrix();
}

void Camera::releaseObject(){
    if(cameraObject != NULL)
        cameraObject->cameraAttached(false);
    cameraObject = NULL;
    mounted = false;
}

void Camera::moveForward(float fps) {
}

void Camera::moveBackward(float fps) {
}

void Camera::moveLeft(float fps) {
}

void Camera::moveRight(float fps) {
}

void Camera::moveUp() {
}

void Camera::moveDown() {
}

void Camera::patrzX(float f) {
}

void Camera::patrzY(float f) {
}

void Camera::check_coords() {
}

void Camera::MouseWheel(QWheelEvent* e) {
}

void Camera::MouseMove(QMouseEvent* e) {
}

void Camera::MouseDown(QMouseEvent* e) {
}

void Camera::MouseUp(QMouseEvent* e) {
}

void Camera::releaseKeys() {
    jestcontrol = 0;
    moveF = moveR = moveB = moveL = false;
    przesx = przesz = Game::cameraSpeedStd;
}

void Camera::keyUp(QKeyEvent * e) {
}

void Camera::keyDown(QKeyEvent * e) {
};
