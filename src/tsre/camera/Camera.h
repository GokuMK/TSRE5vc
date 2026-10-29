/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef CAMERA_H
#define	CAMERA_H

#include <tsre/math3d/Vector2i.h>
#include <tsre/math3d/Vector2f.h>
#include <tsre/math3d/Vector3f.h>
#include <QMatrix4x4>
#include <QVector3D>
#include <QMouseEvent>
#include <tsre/camera/CameraMount.h>

class RenderQueue;

class PreciseTileCoordinate;
class GameObj;
class GLUU;

class Camera {
public:
    Camera(float* pt = 0);
    Camera(const Camera& orig);
    virtual ~Camera();
    virtual float* getTarget();
    virtual float* getUp();
    virtual float* getMatrix();
    virtual float* getPos();
    virtual void setPos(float* pos);
    virtual void setPos(float x, float y, float z);
    virtual void setPlayerRot(float x, float y);
    virtual float getRotX();
    virtual float getRotY();
    virtual void setPozT(int x, int y);
    virtual void moveForward(float fps);
    virtual void moveBackward(float fps);
    virtual void moveLeft(float fps);
    virtual void moveRight(float fps);
    virtual void moveUp();
    virtual void moveDown();
    virtual void patrzX(float f);
    virtual void patrzY(float f);
    virtual void check_coords();
    virtual void MouseWheel(QWheelEvent* e);
    virtual void MouseMove(QMouseEvent* e);
    virtual void MouseDown(QMouseEvent* e);
    virtual void MouseUp(QMouseEvent* e);
    virtual void keyDown(QKeyEvent * e);
    virtual void keyUp(QKeyEvent * e);
    // Lets go of the keys it holds, as if released (the UI took the keys,
    // or the view lost the focus): it stops moving. Mouse buttons need not:
    // their release reaches the view that got the press.
    virtual void releaseKeys();
    virtual void update(float fps);
    virtual void pushRenderHud(RenderQueue &queue);
    virtual void setCameraObject(GameObj* o);
    // Off the object it rides, staying where it is and looking the same way.
    virtual void releaseObject();
    // What the camera rides (a consist in play mode), or NULL.
    GameObj* getCameraObject() const { return cameraObject; }
    // The place of the object it rides as the camera took it (task 38):
    // getCameraPosition's tile x and z, position and rotation; false when
    // it rides none.
    bool ridingPlacement(float out[8]) const;
    // Its view as if the object it rides lay at `placement` (in the camera's
    // tile), its own place and look in it as now: where it held still while
    // the object moved.
    void viewAtPlacement(const float placement[8], float *view);
    virtual PreciseTileCoordinate* getCurrentPos();
    float * pozT = NULL;
    float starex, starey;
    float fov;
protected:
    float pos[3];
    float target[3];
    float up[3];
    float playerPos[3];
    float playerRot[2];
    float relativePos[3];
    float relativeRot[3];
    float lookAt[16];
    float przesx = 3, przesy = 1, przesz = 3;
    bool lockYaxis = false;
    int jestcontrol;
    int lpm = 0;
    int rpm = 0;
    bool moveF, moveR, moveB, moveL;
    PreciseTileCoordinate* currentPos = 0;
    GameObj* cameraObject = NULL;
    // The object's place for the camera (GameObj::getCameraMount), while
    // it gives one: the camera starts there and keeps within it.
    bool mounted = false;
    CameraMount mount;
};

#endif	/* CAMERA_H */

