/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

/* 
 * File:   Ruch.h
 * Author: Goku
 *
 * Created on 13 maja 2015, 23:57
 */

#ifndef RUCH_H
#define	RUCH_H

#include <QString>
#include <tsre/tdb/TDB.h>
#include <tsre/math3d/Vector3f.h>
#include <tsre/math3d/Vector2f.h>

#include <QMap>
#include <functional>

class Ruch {
public:
    int onJunction = 0;
    // Track ends passed: the traveller turns back at one.
    int endsPassed = 0;
    
    Ruch();
    void next(float m);
    void back(float m);
    void toNext(float m);
    void set(int nid, int m, int tdirection, QMap<int, int>* jDirections = NULL);
    // At `distance` along vector node `nid` (not rounded), facing the node's
    // way (tdirection 0) or the other (1): where the simulation has a
    // vehicle's end (task simulation 01).
    void place(int nid, float distance, int tdirection);
    float *getCurrentPosition(float *sElev = NULL);
    float getDistanceDownPath();
    int getVectorDirection();
    void trackPassingItems(bool val);
    QString getLastItemName();
    // The nearest track item ahead of the traveller (or behind it) that
    // wanted() accepts, on its way (the junctions it would take) within
    // maxDistance: its distance and id, or -1 (none, or the track ends).
    float distanceToItem(bool ahead, float maxDistance, const std::function<bool(int)> &wanted,
                         int *itemId = NULL) const;
    // The radius of the curve under the traveller, 0 on straight track.
    float curveRadius() const;
    // Where it is: its vector node, the distance along it and the node's
    // length, and its way along the node (+1 the node's way, -1 the other).
    int nodeIndex() const { return nodeIdx; }
    float nodeDistance() const { return nodeDist; }
    float nodeSpan() const { return nodeLength; }
    float nodeWay() const { return direction; }

private:
    void checkNode(int mSign);
    void checkPassingItems();
    float kierunek = 1;
    float direction = 1;
    int nodeIdx = -1;
    float nodeDist = 0;
    float nodeLength = 0;
    float drawPosition[8];
    float distanceDownPath = 0;
    QMap<int, int>* junctionDirections = NULL;
    // A placed traveller's junction choices: none.
    QMap<int, int> placedJunctions;
    
    bool trackItems = false;
    float lastNodeDist = 0;
    int lastItemId = -1;
    QString lastItemName;
};

#endif	/* RUCH_H */

