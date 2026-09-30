/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */


#ifndef COMPLEXLINE_H
#define	COMPLEXLINE_H

#include <QVector>
#include <tsre/tdb/TSection.h>

class ComplexLinePoint {
public:
    bool selected = false;
    int shapeType = 0;
    float position[3];
    
    ComplexLinePoint();
    ComplexLinePoint(const ComplexLinePoint& o);
};

struct ComplexLineFrame {
    float position[3] = {0, 0, 0};
    float right[3] = {1, 0, 0};
    float up[3] = {0, 1, 0};
    float forward[3] = {0, 0, 1};
    float distance = 0;
    int spanIndex = -1;
};

class ComplexLine {
public:
    float length;
    QString hash;
    
    ComplexLine();
    virtual ~ComplexLine();
    void init(QVector<TSection> s);
    void init(QVector<ComplexLinePoint> s);
    float getLength();
    QString getHash();
    void getDrawPosition(float* posRot, float distance, float xOffset = 0);
    bool getFrame(ComplexLineFrame &frame, float distance,
            float xOffset = 0) const;
    bool getSpanFrame(ComplexLineFrame &frame, int spanIndex,
            bool atEnd, float xOffset = 0) const;
    bool getNodeFrame(ComplexLineFrame &frame, int nodeIndex,
            float xOffset = 0) const;
    bool getNodeInterpolatedFrame(ComplexLineFrame &frame, float distance,
            float xOffset = 0) const;
    bool isPointPath() const;
    const QVector<float> &getNodeDistances() const;
private:
    QVector<TSection> sections;
    QVector<ComplexLinePoint> points;
    QVector<float> nodeDistances;
    QVector<ComplexLineFrame> nodeFrames;
    void getDrawPositionFromTSection(float* posRot, float distance, float xOffset = 0);
    void getDrawPositionFromPoints(float* posRot, float distance, float xOffset = 0);
};

#endif	/* COMPLEXLINE_H */

