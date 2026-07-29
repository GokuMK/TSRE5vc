/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef FLEX_H
#define	FLEX_H

class QWidget;
class QPainter;
class QImage;
class QLabel;
class QPen;

class Flex {
public:

    static bool NewFlexDeprecatedStaged(int x, int z, float *p, float *q, float * dyntrackSections);
    static bool NewFlex(int x1, int z1, float *p1, float *q1, int x2, int z2, float *p2, float *q2, float * dyntrackSections, float preferredMinCurveRadius = 0.0f, bool preferNiceRadii = true);
    static bool NewFlexToPoint(int x1, int z1, float *p1, float startTdbYaw, int x2, int z2, float *p2, float *dyntrackSections, float minimumCurveRadius = 15.0f);
    static bool DyntrackEndpoint(int startTileX, int startTileZ, const float *startPosition, const float *startQuaternion, const float *dyntrackSections, int &endTileX, int &endTileZ, float *endPosition, float *endQuaternion);
    static bool OffsetWorldPose(int sourceTileX, int sourceTileZ, const float *sourcePosition, const float *sourceQuaternion, float rightOffset, int &targetTileX, int &targetTileZ, float *targetPosition, float *targetQuaternion);
    static bool ParallelDyntrackSections(const float *sourceSections, float rightOffset, float *targetSections);
    static float TdbYawFromTrackQuaternion(const float *q);
    static bool AutoFlex(int x1, int z1, float* p1, int x2, int z2, float* p2, float* dyntrackSections, float &elev, float preferredMinCurveRadius = 0.0f);
private:
    static int FlexStage;
    static float FlexP0[3];
    static float FlexQ0[4];
    static int FlexX;
    static int FlexZ;
    static QWidget* window;
    static QPainter* painter;
    static QImage* img;
    static QLabel* myLabel;
    static int windowInit;
    static int offx, offy;
    static void drawLine(QPen niebieski, int x1, int y1, int x2, int y2);
};

#endif	/* FLEX_H */

