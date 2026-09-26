/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/procedural/ComplexLine.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/math3d/Vector3f.h>
#include <algorithm>
#include <cmath>

namespace {

bool normalize(float *vector) {
    const float magnitude = std::sqrt(
            vector[0] * vector[0]
            + vector[1] * vector[1]
            + vector[2] * vector[2]);
    if(!std::isfinite(magnitude) || magnitude < 0.000001f)
        return false;
    vector[0] /= magnitude;
    vector[1] /= magnitude;
    vector[2] /= magnitude;
    return true;
}

void cross(float *result, const float *a, const float *b) {
    result[0] = a[1] * b[2] - a[2] * b[1];
    result[1] = a[2] * b[0] - a[0] * b[2];
    result[2] = a[0] * b[1] - a[1] * b[0];
}

void subtract(float *result, const float *a, const float *b) {
    result[0] = a[0] - b[0];
    result[1] = a[1] - b[1];
    result[2] = a[2] - b[2];
}

void frameFromForward(ComplexLineFrame &frame, const float *forward) {
    frame.forward[0] = forward[0];
    frame.forward[1] = forward[1];
    frame.forward[2] = forward[2];
    if(!normalize(frame.forward))
        Vec3::set(frame.forward, 0, 0, 1);
    const float worldUp[3] = {0, 1, 0};
    cross(frame.right, worldUp, frame.forward);
    if(!normalize(frame.right))
        Vec3::set(frame.right, 1, 0, 0);
    cross(frame.up, frame.forward, frame.right);
    normalize(frame.up);
}

}

ComplexLinePoint::ComplexLinePoint(){
    
}

ComplexLinePoint::ComplexLinePoint(const ComplexLinePoint& o){
    this->selected = o.selected;
    this->shapeType = o.shapeType;
    Vec3::copy(this->position, o.position);
}

ComplexLine::ComplexLine() {
}

ComplexLine::~ComplexLine() {
}

void ComplexLine::init(QVector<TSection> s){
    sections = s;
    points.clear();
    nodeDistances.clear();
    nodeFrames.clear();
    length = 0;
    hash = "";
    for(int i = 0; i < sections.size(); i++){
        hash += QString::number(sections[i].getHash(), 16) + "_";
        length += sections[i].getDlugosc();
    }
}

void ComplexLine::init(QVector<ComplexLinePoint> s){
    sections.clear();
    points.clear();
    nodeDistances.clear();
    nodeFrames.clear();
    length = 0;
    hash = "";

    if(s.isEmpty())
        return;
    const float origin[3] = {
        s.first().position[0], s.first().position[1], s.first().position[2]
    };
    for(const ComplexLinePoint &source : s){
        ComplexLinePoint point(source);
        point.position[0] -= origin[0];
        point.position[1] -= origin[1];
        point.position[2] -= origin[2];
        if(!points.isEmpty()
                && Vec3::distance(points.last().position, point.position)
                    < 0.000001f)
            continue;
        points.append(point);
    }

    if(points.size() < 2){
        points.clear();
        return;
    }

    nodeDistances.reserve(points.size());
    nodeFrames.resize(points.size());
    nodeDistances.append(0);
    for(int i = 1; i < points.size(); i++){
        length += Vec3::distance(
                points[i].position, points[i - 1].position);
        nodeDistances.append(length);
        hash += QString::number((int)(length * 200), 16) + "_";
    }

    for(int i = 0; i < points.size(); i++){
        const int first = i < points.size() - 1 ? i : i - 1;
        const int second = first + 1;
        float forward[3];
        Vec3::sub(forward,
                  points[second].position, points[first].position);
        frameFromForward(nodeFrames[i], forward);
        Vec3::copy(nodeFrames[i].position, points[i].position);
        nodeFrames[i].distance = nodeDistances[i];
        nodeFrames[i].spanIndex = std::min(i, (int)points.size() - 2);
    }
}

float ComplexLine::getLength(){
    return length;
}

QString ComplexLine::getHash(){
    return hash;
}
void ComplexLine::getDrawPosition(float* posRot, float distance, float xOffset){
    if(sections.size() > 0)
        return getDrawPositionFromTSection(posRot, distance, xOffset);
    if(points.size() > 0)
        return getDrawPositionFromPoints(posRot, distance, xOffset);
}

void ComplexLine::getDrawPositionFromPoints(float* posRot, float distance, float xOffset){
    ComplexLineFrame frame;
    if(!getFrame(frame, distance, xOffset)){
        Vec3::set(posRot, 0, 0, 0);
        Vec3::set(posRot + 3, 0, 0, 0);
        return;
    }
    Vec3::copy(posRot, frame.position);
    posRot[3] = -std::asin(std::max(-1.0f,
                                   std::min(1.0f, frame.forward[1])));
    posRot[4] = std::atan2(frame.forward[0], frame.forward[2]);
    posRot[5] = 0;
}

bool ComplexLine::getFrame(ComplexLineFrame &frame, float distance,
        float xOffset) const {
    if(points.size() < 2 || nodeDistances.size() != points.size())
        return false;

    const float clamped = std::max(0.0f, std::min(distance, length));
    auto upper = std::upper_bound(
            nodeDistances.cbegin(), nodeDistances.cend(), clamped);
    int span = (int)(upper - nodeDistances.cbegin()) - 1;
    span = std::max(0, std::min(span, (int)points.size() - 2));
    const float spanStart = nodeDistances[span];
    const float spanLength = nodeDistances[span + 1] - spanStart;
    const float fraction = spanLength > 0
            ? (clamped - spanStart) / spanLength : 0;

    for(int axis = 0; axis < 3; axis++){
        frame.position[axis] = points[span].position[axis]
                * (1.0f - fraction)
                + points[span + 1].position[axis] * fraction;
        frame.forward[axis] = nodeFrames[span].forward[axis]
                * (1.0f - fraction)
                + nodeFrames[span + 1].forward[axis] * fraction;
    }
    if(!normalize(frame.forward))
        Vec3::copy(frame.forward, nodeFrames[span].forward);
    frameFromForward(frame, frame.forward);
    if(xOffset != 0){
        frame.position[0] += frame.right[0] * xOffset;
        frame.position[1] += frame.right[1] * xOffset;
        frame.position[2] += frame.right[2] * xOffset;
    }
    frame.distance = clamped;
    frame.spanIndex = span;
    return true;
}

bool ComplexLine::getSpanFrame(ComplexLineFrame &frame, int spanIndex,
        bool atEnd, float xOffset) const {
    if(spanIndex < 0 || spanIndex + 1 >= points.size())
        return false;

    float forward[3];
    subtract(forward, points[spanIndex + 1].position,
             points[spanIndex].position);
    frameFromForward(frame, forward);
    const int nodeIndex = spanIndex + (atEnd ? 1 : 0);
    Vec3::copy(frame.position, points[nodeIndex].position);
    if(xOffset != 0){
        frame.position[0] += frame.right[0] * xOffset;
        frame.position[1] += frame.right[1] * xOffset;
        frame.position[2] += frame.right[2] * xOffset;
    }
    frame.distance = nodeDistances[nodeIndex];
    frame.spanIndex = spanIndex;
    return true;
}

bool ComplexLine::getNodeFrame(ComplexLineFrame &frame, int nodeIndex,
        float xOffset) const {
    if(nodeIndex < 0 || nodeIndex >= points.size())
        return false;

    float forward[3];
    if(nodeIndex == 0){
        subtract(forward, points[1].position, points[0].position);
    } else if(nodeIndex == points.size() - 1){
        subtract(forward, points[nodeIndex].position,
                 points[nodeIndex - 1].position);
    } else {
        float incoming[3];
        float outgoing[3];
        subtract(incoming, points[nodeIndex].position,
                 points[nodeIndex - 1].position);
        subtract(outgoing, points[nodeIndex + 1].position,
                 points[nodeIndex].position);
        normalize(incoming);
        normalize(outgoing);
        // The normalized sum of two unit tangents is their shortest angular
        // bisector, so node objects do not favor either adjacent span.
        Vec3::add(forward, incoming, outgoing);
        // A 180-degree reversal has no unique angular bisector. Keep the
        // authored outgoing direction rather than producing an invalid frame.
        if(!normalize(forward))
            Vec3::copy(forward, outgoing);
    }
    frameFromForward(frame, forward);
    Vec3::copy(frame.position, points[nodeIndex].position);
    if(xOffset != 0){
        frame.position[0] += frame.right[0] * xOffset;
        frame.position[1] += frame.right[1] * xOffset;
        frame.position[2] += frame.right[2] * xOffset;
    }
    frame.distance = nodeDistances[nodeIndex];
    frame.spanIndex = std::min(nodeIndex, (int)points.size() - 2);
    return true;
}

bool ComplexLine::getNodeInterpolatedFrame(ComplexLineFrame &frame,
        float distance, float xOffset) const {
    if(points.size() < 2 || nodeDistances.size() != points.size())
        return false;

    const float clamped = std::max(0.0f, std::min(distance, length));
    auto upper = std::upper_bound(
            nodeDistances.cbegin(), nodeDistances.cend(), clamped);
    int span = (int)(upper - nodeDistances.cbegin()) - 1;
    span = std::max(0, std::min(span, (int)points.size() - 2));
    const float spanLength = nodeDistances[span + 1] - nodeDistances[span];
    const float fraction = spanLength > 0
            ? (clamped - nodeDistances[span]) / spanLength : 0;

    ComplexLineFrame startFrame;
    ComplexLineFrame endFrame;
    if(!getNodeFrame(startFrame, span)
            || !getNodeFrame(endFrame, span + 1))
        return false;
    for(int axis = 0; axis < 3; axis++){
        frame.position[axis] = points[span].position[axis]
                * (1.0f - fraction)
                + points[span + 1].position[axis] * fraction;
        frame.forward[axis] = startFrame.forward[axis]
                * (1.0f - fraction)
                + endFrame.forward[axis] * fraction;
    }
    if(!normalize(frame.forward))
        Vec3::copy(frame.forward, startFrame.forward);
    frameFromForward(frame, frame.forward);
    if(xOffset != 0){
        frame.position[0] += frame.right[0] * xOffset;
        frame.position[1] += frame.right[1] * xOffset;
        frame.position[2] += frame.right[2] * xOffset;
    }
    frame.distance = clamped;
    frame.spanIndex = span;
    return true;
}

bool ComplexLine::isPointPath() const {
    return points.size() >= 2;
}

const QVector<float> &ComplexLine::getNodeDistances() const {
    return nodeDistances;
}

void ComplexLine::getDrawPositionFromTSection(float* posRot, float distance, float xOffset){
    float tLength = 0;
    float sLength = 0;
    float tpos[3];
    float trot[3];
    Vec3::set(tpos, 0, 0, 0);
    Vec3::set(trot, 0, 0, 0);
    Vector3f vPos;
    Vector3f off;
    
    if(distance > length)
        distance = length;
    
    for(int i = 0; i < sections.size(); i++){
        sLength = sections[i].getDlugosc();
        //qDebug() << "sLength" << sLength << distance;
        if(distance > tLength + sLength){
            sections[i].getDrawPosition(&vPos, sLength);
            vPos.rotateY(trot[1], 0);
            Vec3::add(tpos, tpos, (float*)&vPos);
            trot[1] += sections[i].getDrawAngle(sLength);
            tLength += sLength;
            continue;
        }

        sections[i].getDrawPosition(&vPos, distance - tLength);
        vPos.rotateY(trot[1], 0);
        Vec3::set(posRot, vPos.x, vPos.y, vPos.z);
        Vec3::add(posRot, posRot, tpos);
        /*off.set(xOffset, 0, 0);
        off.rotateY(sections[i].getDrawAngle(distance - tLength), 0);
        Vec3::set(tpos, off.x, off.y, off.z);
        Vec3::add(posRot, posRot, tpos);*/
        posRot[3] = M_PI;
        posRot[4] = - trot[1] - sections[i].getDrawAngle(distance - tLength);
        posRot[5] = 0;
        return;
    }
}
