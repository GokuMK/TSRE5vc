/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef CAMERAMAP_H
#define CAMERAMAP_H

#include "Camera.h"
#include <tsre/map/MapView.h>

// The map mode's camera (task editor 04): straight down on a MapView. The
// left and right buttons drag the map, the middle one turns it, the wheel zooms
// about the mouse; W, A, S, D (and the arrow keys) move it, Q and E turn
// it, N turns north up. pozT follows the view's tile, so code that reads the
// camera's tile keeps working.
class CameraMap : public Camera {
public:
    CameraMap();
    ~CameraMap() override;

    MapView view;
    // The viewport in device pixels (the view's size).
    void setViewport(int width, int height);
    // Zooms by wheel steps about a point in device pixels.
    void zoomAt(float px, float py, float wheelSteps);

    float *getMatrix() override;
    float *getPos() override;
    float *getTarget() override;
    void setPos(float *position) override;
    void setPos(float x, float y, float z) override;
    void setPozT(int x, int y) override;
    float getRotX() override;
    float getRotY() override;
    void check_coords() override;
    void MouseMove(QMouseEvent *e) override;
    void MouseDown(QMouseEvent *e) override;
    void MouseUp(QMouseEvent *e) override;
    void keyDown(QKeyEvent *e) override;
    void keyUp(QKeyEvent *e) override;
    void update(float fps) override;
    PreciseTileCoordinate *getCurrentPos() override;

private:
    void syncTile();
    float tile[2] = {0, 0};
    bool dragging = false;
    bool turning = false;
    QPointF last;
    bool turnLeft = false;
    bool turnRight = false;
};

#endif
