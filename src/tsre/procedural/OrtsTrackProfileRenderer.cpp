/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/procedural/OrtsTrackProfileRenderer.h>

#include <tsre/ogl/OglObj.h>
#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/procedural/ComplexLine.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/shape/ObjFile.h>
#include <tsre/tdb/TrackShape.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/texture/TexLib.h>
#include <tsre/world/Route.h>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSharedPointer>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr float MaximumPathLength = 2048.0f;
constexpr int MaximumFrames = 4096;
constexpr float OpaqueAlpha = 1.0f;
constexpr float BlendedTransparentCutoff = -1.0f / 255.0f;
constexpr float AlphaTestCutoff = -0.51f;

OrtsGeneratedProfileMesh::MaterialPass materialPass(
        const OrtsProfileLodItem &item) {
    if(item.alphaTestMode == 1)
        return OrtsGeneratedProfileMesh::MaterialPass::AlphaTest;
    if(item.shaderName.compare("BlendATexDiff", Qt::CaseInsensitive) == 0)
        return OrtsGeneratedProfileMesh::MaterialPass::Blended;
    return OrtsGeneratedProfileMesh::MaterialPass::Opaque;
}

float materialAlpha(const OrtsProfileLodItem &item) {
    // TSRE's VNTA alpha attribute encodes material behavior: positive one
    // forces an opaque fragment, while a negative value preserves texture
    // alpha and discards fragments below its absolute threshold. Blended
    // materials use the smallest meaningful cutoff so invisible fragments
    // cannot write depth before the opaque profile layers are rendered.
    if(item.alphaTestMode == 1)
        return AlphaTestCutoff;
    if(item.shaderName.compare("BlendATexDiff", Qt::CaseInsensitive) == 0)
        return BlendedTransparentCutoff;
    return OpaqueAlpha;
}

int curveSegments(const OrtsTrackProfile &profile, const TSection &section) {
    const float absoluteAngle = std::abs(section.angle);
    int segments = (int)std::floor(qRadiansToDegrees(absoluteAngle)
                                   / profile.chordSpanDegrees);
    if(segments == 0)
        segments = 2;

    if(profile.pitchControl == OrtsTrackProfile::PitchControl::ChordLength
            && profile.pitchControlScalar > 0
            && section.radius > profile.pitchControlScalar * 0.5f){
        const float chordLength = 2.0f * section.radius
                * std::sin(0.5f * absoluteAngle / segments);
        if(chordLength > profile.pitchControlScalar){
            const float chordAngle = 2.0f * std::asin(
                    0.5f * profile.pitchControlScalar / section.radius);
            if(chordAngle > 0)
                segments = (int)std::floor(absoluteAngle / chordAngle);
        }
    } else if(profile.pitchControl
                    == OrtsTrackProfile::PitchControl::ChordDisplacement
            && profile.pitchControlScalar > 0
            && profile.pitchControlScalar < section.radius){
        const float displacement = section.radius
                * (1.0f - std::cos(0.5f * absoluteAngle / segments));
        if(displacement > profile.pitchControlScalar){
            const float chordAngle = 2.0f * std::acos(
                    1.0f - profile.pitchControlScalar / section.radius);
            if(chordAngle > 0)
                segments = (int)std::floor(absoluteAngle / chordAngle);
        }
    }

    segments = std::max(2, std::min(250, segments));
    if(segments % 2 == 1)
        segments++;
    return segments;
}

QVector<float> frameDistances(const OrtsTrackProfile &profile,
        const QVector<TSection> &sections, float endExtension,
        QStringList *diagnostics) {
    QVector<float> distances;
    distances.append(0);
    float offset = 0;
    bool truncated = false;
    for(TSection section : sections){
        const float length = section.getDlugosc();
        if(length <= 0)
            continue;
        const int segments = section.type == 1
                ? curveSegments(profile, section) : 1;
        for(int i = 1; i <= segments; i++){
            const float distance = offset + length * (float)i / segments;
            if(distance > MaximumPathLength){
                if(distances.last() < MaximumPathLength)
                    distances.append(MaximumPathLength);
                truncated = true;
                break;
            }
            distances.append(distance);
            if(distances.size() >= MaximumFrames){
                truncated = true;
                break;
            }
        }
        offset += length;
        if(truncated)
            break;
    }
    if(!truncated && endExtension > 0 && distances.size() < MaximumFrames
            && distances.last() + endExtension <= MaximumPathLength)
        distances.append(distances.last() + endExtension);
    if(truncated && diagnostics != nullptr)
        diagnostics->append("ORTS profile geometry truncated to safety budget");
    return distances;
}

QVector<float> frameDistances(const ComplexLine &line,
        QStringList *diagnostics) {
    QVector<float> distances;
    bool truncated = false;
    for(float distance : line.getNodeDistances()){
        if(distance > MaximumPathLength){
            if(distances.isEmpty() || distances.last() < MaximumPathLength)
                distances.append(MaximumPathLength);
            truncated = true;
            break;
        }
        distances.append(distance);
        if(distances.size() >= MaximumFrames){
            truncated = true;
            break;
        }
    }
    if(truncated && diagnostics != nullptr)
        diagnostics->append("ORTS profile point path truncated to safety budget");
    return distances;
}

struct GeneratedVertex {
    float values[9];
};

struct GeneratedPathFrame {
    float position[3] = {0, 0, 0};
    float right[3] = {1, 0, 0};
    float up[3] = {0, 1, 0};
    float forward[3] = {0, 0, 1};
    float rollCosine = 1;
    float rollSine = 0;
    float distance = 0;
};

bool normalizeVector(float *vector) {
    const float length = std::sqrt(
            vector[0] * vector[0]
            + vector[1] * vector[1]
            + vector[2] * vector[2]);
    if(!std::isfinite(length) || length < 1e-6f)
        return false;
    vector[0] /= length;
    vector[1] /= length;
    vector[2] /= length;
    return true;
}

void crossVector(float *result, const float *a, const float *b) {
    result[0] = a[1] * b[2] - a[2] * b[1];
    result[1] = a[2] * b[0] - a[0] * b[2];
    result[2] = a[0] * b[1] - a[1] * b[0];
}

void applyPathRotation(float *vector,
        const ProceduralPathTransform *pathTransform) {
    if(pathTransform == nullptr || !pathTransform->enabled)
        return;
    float rotated[3] = {vector[0], vector[1], vector[2]};
    Vec3::transformQuat(rotated, rotated,
            const_cast<float*>(pathTransform->rotation));
    Vec3::copy(vector, rotated);
}

GeneratedPathFrame samplePathFrame(ComplexLine &line, float distance,
        float startRoll, float endRoll, float endExtension, float endDrop,
        const ProceduralPathTransform *pathTransform,
        const ComplexLineFrame *pointFrame = nullptr) {
    const float sampledDistance = std::min(distance, line.length);
    GeneratedPathFrame result;
    if(line.isPointPath()){
        ComplexLineFrame frame;
        if(pointFrame != nullptr)
            frame = *pointFrame;
        else if(!line.getFrame(frame, sampledDistance))
            return result;
        Vec3::copy(result.position, frame.position);
        Vec3::copy(result.right, frame.right);
        Vec3::copy(result.up, frame.up);
        Vec3::copy(result.forward, frame.forward);
    } else {
        float frame[6] = {0, 0, 0, 0, 0, 0};
        line.getDrawPosition(frame, sampledDistance);
        const float yaw = frame[4];
        result.position[0] = frame[0];
        result.position[1] = frame[1];
        result.position[2] = frame[2];
        result.right[0] = std::cos(yaw);
        result.right[2] = -std::sin(yaw);
        result.forward[0] = std::sin(yaw);
        result.forward[2] = std::cos(yaw);
    }
    const float overflow = std::max(0.0f, distance - line.length);
    if(overflow > 0){
        result.position[0] += result.forward[0] * overflow;
        result.position[1] += result.forward[1] * overflow;
        result.position[2] += result.forward[2] * overflow;
        if(endExtension > 0)
            result.position[1] -= endDrop * overflow / endExtension;
    }
    const float fraction = line.length > 0 ? sampledDistance / line.length : 0;
    const float roll = startRoll * (1.0f - fraction) + endRoll * fraction;

    applyPathRotation(result.position, pathTransform);
    applyPathRotation(result.right, pathTransform);
    applyPathRotation(result.up, pathTransform);
    applyPathRotation(result.forward, pathTransform);
    normalizeVector(result.forward);

    if(pathTransform != nullptr && pathTransform->enabled
            && pathTransform->uprightCrossSections) {
        // Match Open Rails' generated-track frame: bake the complete object
        // orientation into the centerline, then remove bank from the profile
        // by rebuilding its lateral vector against world up. Pitch remains
        // in the tangent, but a curved pitched object no longer rolls a wide
        // road surface around its original rigid X/Z plane.
        const float worldUp[3] = {0, 1, 0};
        crossVector(result.right, worldUp, result.forward);
        if(!normalizeVector(result.right)) {
            result.right[0] = 1;
            result.right[1] = 0;
            result.right[2] = 0;
        }
        crossVector(result.up, result.forward, result.right);
        normalizeVector(result.up);
    } else {
        normalizeVector(result.right);
        normalizeVector(result.up);
    }
    result.rollCosine = std::cos(roll);
    result.rollSine = std::sin(roll);
    result.distance = distance;
    return result;
}

GeneratedVertex transformVertex(const OrtsProfileVertex &source,
        const OrtsProfilePolyline &polyline,
        const GeneratedPathFrame &frame, float alpha) {
    const float rolledX = source.position[0] * frame.rollCosine
            - source.position[1] * frame.rollSine;
    const float rolledY = source.position[0] * frame.rollSine
            + source.position[1] * frame.rollCosine;
    const float rolledNormalX = source.normal[0] * frame.rollCosine
            - source.normal[1] * frame.rollSine;
    const float rolledNormalY = source.normal[0] * frame.rollSine
            + source.normal[1] * frame.rollCosine;
    // ORTS/XNA profiles face local -Z, while TSRE sweeps paths toward local
    // +Z. Convert the complete profile basis with a 180 degree Y rotation,
    // not a one-axis reflection: keeping X unchanged mirrors asymmetric
    // profiles left-to-right relative to travel. UVs deliberately stay paired
    // with their source vertices.
    const float profileX = -rolledX;
    const float profileZ = -source.position[2];
    const float profileNormalX = -rolledNormalX;
    const float profileNormalZ = -source.normal[2];

    GeneratedVertex result;
    // The frame basis already contains the inverse ComplexLine yaw required
    // at the ORTS/MSTS -Z-forward to TSRE +Z-forward boundary. Expressing the
    // vertex through right/up/forward also lets a baked 3D path replace only
    // that frame construction without duplicating profile/UV conversion.
    result.values[0] = frame.position[0]
            + profileX * frame.right[0]
            + rolledY * frame.up[0]
            + profileZ * frame.forward[0];
    result.values[1] = frame.position[1]
            + profileX * frame.right[1]
            + rolledY * frame.up[1]
            + profileZ * frame.forward[1];
    result.values[2] = frame.position[2]
            + profileX * frame.right[2]
            + rolledY * frame.up[2]
            + profileZ * frame.forward[2];
    result.values[3] = profileNormalX * frame.right[0]
            + rolledNormalY * frame.up[0]
            + profileNormalZ * frame.forward[0];
    result.values[4] = profileNormalX * frame.right[1]
            + rolledNormalY * frame.up[1]
            + profileNormalZ * frame.forward[1];
    result.values[5] = profileNormalX * frame.right[2]
            + rolledNormalY * frame.up[2]
            + profileNormalZ * frame.forward[2];
    result.values[6] = source.texCoord[0]
            + polyline.deltaTexCoord[0] * frame.distance;
    result.values[7] = source.texCoord[1]
            + polyline.deltaTexCoord[1] * frame.distance;
    result.values[8] = alpha;
    return result;
}

GeneratedPathFrame frameForMode(GeneratedPathFrame frame,
        OrtsProfileLodItem::PathFrameMode mode) {
    if(mode == OrtsProfileLodItem::PathFrameMode::Full)
        return frame;
    frame.rollCosine = 1;
    frame.rollSine = 0;
    const float worldUp[3] = {0, 1, 0};
    if(mode == OrtsProfileLodItem::PathFrameMode::Upright){
        frame.forward[1] = 0;
        if(!normalizeVector(frame.forward)){
            frame.forward[0] = 0;
            frame.forward[1] = 0;
            frame.forward[2] = 1;
        }
    }
    crossVector(frame.right, worldUp, frame.forward);
    if(!normalizeVector(frame.right)){
        frame.right[0] = 1;
        frame.right[1] = 0;
        frame.right[2] = 0;
    }
    if(mode == OrtsProfileLodItem::PathFrameMode::Upright){
        frame.up[0] = 0;
        frame.up[1] = 1;
        frame.up[2] = 0;
    } else {
        crossVector(frame.up, frame.forward, frame.right);
        normalizeVector(frame.up);
    }
    return frame;
}

QString templateMeshPath(const OrtsTrackProfile &profile,
        QString shapeName) {
    shapeName.replace('\\', '/');
    const QFileInfo source(profile.sourcePath);
    return QDir::cleanPath(source.absoluteDir().absoluteFilePath(shapeName));
}

QSharedPointer<const ObjFile> templateMesh(const OrtsTrackProfile &profile,
        const QString &shapeName, QStringList *diagnostics) {
    static QHash<QString, QSharedPointer<const ObjFile>> cache;
    const QString path = templateMeshPath(profile, shapeName);
    const QString key = ContentPath::key(path);
    if(!cache.contains(key))
        cache.insert(key, QSharedPointer<const ObjFile>(new ObjFile(path)));
    const QSharedPointer<const ObjFile> result = cache.value(key);
    if((result == nullptr || !result->valid) && diagnostics != nullptr){
        diagnostics->append("Template3D " + shapeName + ": "
                + (result == nullptr ? QString("mesh unavailable")
                                     : result->error));
    }
    return result != nullptr && result->valid
            ? result : QSharedPointer<const ObjFile>();
}

int selectedShapeIndex(const OrtsProfileTemplate3D &source,
        int objectIndex, int copyIndex, const QString &profileId) {
    if(source.shapes.size() <= 1)
        return 0;
    switch(source.shapeSelectionMode){
        case OrtsProfileTemplate3D::ShapeSelectionMode::ByObject:
            return std::abs(objectIndex) % source.shapes.size();
        case OrtsProfileTemplate3D::ShapeSelectionMode::Cycle:
            return std::abs(copyIndex) % source.shapes.size();
        case OrtsProfileTemplate3D::ShapeSelectionMode::DeterministicRandom:
            return (int)(qHash(profileId + ':' + QString::number(objectIndex)
                              + ':' + QString::number(copyIndex), 0)
                         % (uint)source.shapes.size());
        case OrtsProfileTemplate3D::ShapeSelectionMode::First:
        default:
            return 0;
    }
}

GeneratedVertex transformTemplateVertex(const float *source,
        const GeneratedPathFrame &inputFrame, const float *offset,
        float alpha, bool reverseFacing, bool deformLongitudinal) {
    const GeneratedPathFrame &frame = inputFrame;
    const float rolledX = (source[0] + offset[0]) * frame.rollCosine
            - (source[1] + offset[1]) * frame.rollSine;
    const float rolledY = (source[0] + offset[0]) * frame.rollSine
            + (source[1] + offset[1]) * frame.rollCosine;
    const float rolledNormalX = source[3] * frame.rollCosine
            - source[4] * frame.rollSine;
    const float rolledNormalY = source[3] * frame.rollSine
            + source[4] * frame.rollCosine;
    float profileX = -rolledX;
    float profileZ = deformLongitudinal ? 0 : -(source[2] + offset[2]);
    float profileNormalX = -rolledNormalX;
    float profileNormalZ = -source[5];
    if(reverseFacing){
        profileX = -profileX;
        profileZ = -profileZ;
        profileNormalX = -profileNormalX;
        profileNormalZ = -profileNormalZ;
    }

    GeneratedVertex result;
    result.values[0] = frame.position[0]
            + profileX * frame.right[0]
            + rolledY * frame.up[0]
            + profileZ * frame.forward[0];
    result.values[1] = frame.position[1]
            + profileX * frame.right[1]
            + rolledY * frame.up[1]
            + profileZ * frame.forward[1];
    result.values[2] = frame.position[2]
            + profileX * frame.right[2]
            + rolledY * frame.up[2]
            + profileZ * frame.forward[2];
    result.values[3] = profileNormalX * frame.right[0]
            + rolledNormalY * frame.up[0]
            + profileNormalZ * frame.forward[0];
    result.values[4] = profileNormalX * frame.right[1]
            + rolledNormalY * frame.up[1]
            + profileNormalZ * frame.forward[1];
    result.values[5] = profileNormalX * frame.right[2]
            + rolledNormalY * frame.up[2]
            + profileNormalZ * frame.forward[2];
    result.values[6] = source[6];
    result.values[7] = source[7];
    result.values[8] = alpha;
    return result;
}

int pointNodeIndex(const ComplexLine &line, float distance) {
    const QVector<float> &nodes = line.getNodeDistances();
    if(nodes.isEmpty())
        return -1;
    auto found = std::lower_bound(nodes.cbegin(), nodes.cend(), distance);
    if(found == nodes.cend())
        return nodes.size() - 1;
    int index = (int)(found - nodes.cbegin());
    if(index > 0 && std::abs(nodes[index - 1] - distance)
            < std::abs(nodes[index] - distance))
        index--;
    return index;
}

GeneratedPathFrame samplePlacementFrame(ComplexLine &line, float distance,
        int spanIndex, bool atEnd, bool averagedNode,
        float startRoll, float endRoll, float endExtension, float endDrop,
        const ProceduralPathTransform *pathTransform) {
    ComplexLineFrame pointFrame;
    const ComplexLineFrame *pointFramePointer = nullptr;
    if(line.isPointPath()){
        const bool valid = averagedNode
                ? line.getNodeFrame(pointFrame,
                        spanIndex + (atEnd ? 1 : 0))
                : line.getSpanFrame(pointFrame, spanIndex, atEnd);
        if(valid)
            pointFramePointer = &pointFrame;
    }
    return samplePathFrame(line, distance,
            startRoll, endRoll, endExtension, endDrop,
            pathTransform, pointFramePointer);
}

void appendVertex(QVector<float> &target, const GeneratedVertex &vertex);

void appendRigidTemplate(QVector<float> &target, const ObjFile &source,
        const GeneratedPathFrame &frame, const float *offset,
        float alpha, bool reverseFacing) {
    const int vertexCount = source.points.size() / 8;
    const int outputStart = target.size();
    target.resize(outputStart + vertexCount * 9);
    float *output = target.data() + outputStart;
    for(int index = 0; index + 7 < source.points.size(); index += 8){
        const GeneratedVertex vertex = transformTemplateVertex(
                source.points.constData() + index, frame, offset,
                alpha, reverseFacing, false);
        std::copy(vertex.values, vertex.values + 9, output);
        output += 9;
    }
}

float templateDepth(const ObjFile &source) {
    float minimumZ = std::numeric_limits<float>::max();
    float maximumZ = -std::numeric_limits<float>::max();
    for(int index = 0; index + 7 < source.points.size(); index += 8){
        minimumZ = std::min(minimumZ, source.points[index + 2]);
        maximumZ = std::max(maximumZ, source.points[index + 2]);
    }
    return maximumZ - minimumZ;
}

void appendDeformedTemplate(QVector<float> &target, const ObjFile &source,
        ComplexLine &line, float startDistance, float endDistance,
        float startRoll, float endRoll, float endExtension, float endDrop,
        const ProceduralPathTransform *pathTransform,
        OrtsProfileLodItem::PathFrameMode frameMode,
        bool usePathLengthTexture, bool useAveragedNodeFrames,
        const float *offset, float alpha) {
    float minimumZ = std::numeric_limits<float>::max();
    float maximumZ = -std::numeric_limits<float>::max();
    for(int index = 0; index + 7 < source.points.size(); index += 8){
        minimumZ = std::min(minimumZ, source.points[index + 2]);
        maximumZ = std::max(maximumZ, source.points[index + 2]);
    }
    const float depth = maximumZ - minimumZ;
    if(depth <= 0.000001f)
        return;
    QHash<qint64, GeneratedPathFrame> frameCache;
    const int vertexCount = source.points.size() / 8;
    const int outputStart = target.size();
    target.resize(outputStart + vertexCount * 9);
    float *output = target.data() + outputStart;
    for(int index = 0; index + 7 < source.points.size(); index += 8){
        const float *vertex = source.points.constData() + index;
        const float fraction = (maximumZ - vertex[2]) / depth;
        const float pathDistance = startDistance
                + fraction * (endDistance - startDistance);
        const float distance = pathDistance - offset[2];
        const qint64 frameKey = qRound64(distance * 1000000.0f);
        auto cachedFrame = frameCache.constFind(frameKey);
        if(cachedFrame == frameCache.cend()){
            ComplexLineFrame averagedFrame;
            const ComplexLineFrame *averagedFramePointer = nullptr;
            if(useAveragedNodeFrames && line.isPointPath()
                    && line.getNodeInterpolatedFrame(
                        averagedFrame, distance))
                averagedFramePointer = &averagedFrame;
            GeneratedPathFrame sampled = samplePathFrame(
                    line, distance, startRoll, endRoll,
                    endExtension, endDrop, pathTransform,
                    averagedFramePointer);
            sampled = frameForMode(sampled, frameMode);
            cachedFrame = frameCache.insert(frameKey, sampled);
        }
        GeneratedVertex transformed = transformTemplateVertex(
                vertex, cachedFrame.value(), offset, alpha, false, true);
        if(usePathLengthTexture){
            // Legacy Rail/Ballast expansion kept U from the source mesh but
            // advanced V continuously with distance along the generated path.
            // This preserves texture density when a short authored mesh is
            // expanded into a longer swept span.
            transformed.values[7] = source.texYmin
                    + (source.texYmax - source.texYmin) * pathDistance;
        }
        std::copy(transformed.values, transformed.values + 9, output);
        output += 9;
    }
}

void appendSharedTemplateVertices(OrtsGeneratedProfileSharedMesh &target,
        const ObjFile &source, const float *offset, float alpha) {
    target.vertices.reserve(source.points.size() * 9 / 8);
    for(int index = 0; index + 7 < source.points.size(); index += 8){
        const float *vertex = source.points.constData() + index;
        target.vertices.append(-(vertex[0] + offset[0]));
        target.vertices.append(vertex[1] + offset[1]);
        target.vertices.append(-(vertex[2] + offset[2]));
        target.vertices.append(-vertex[3]);
        target.vertices.append(vertex[4]);
        target.vertices.append(-vertex[5]);
        target.vertices.append(vertex[6]);
        target.vertices.append(vertex[7]);
        target.vertices.append(alpha);
    }
    if(!target.vertices.isEmpty()){
        target.bounds[0] = target.bounds[1] = target.vertices[0];
        target.bounds[2] = target.bounds[3] = target.vertices[1];
        target.bounds[4] = target.bounds[5] = target.vertices[2];
        for(int index = 0; index < target.vertices.size(); index += 9){
            target.bounds[0] = std::max(target.bounds[0], target.vertices[index]);
            target.bounds[1] = std::min(target.bounds[1], target.vertices[index]);
            target.bounds[2] = std::max(target.bounds[2], target.vertices[index + 1]);
            target.bounds[3] = std::min(target.bounds[3], target.vertices[index + 1]);
            target.bounds[4] = std::max(target.bounds[4], target.vertices[index + 2]);
            target.bounds[5] = std::min(target.bounds[5], target.vertices[index + 2]);
        }
    }
}

std::array<float, 16> sharedTemplateTransform(
        const GeneratedPathFrame &frame, bool reverseFacing) {
    std::array<float, 16> result = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    const float reverse = reverseFacing ? -1.0f : 1.0f;
    for(int axis = 0; axis < 3; axis++){
        result[axis] = reverse * (
                frame.right[axis] * frame.rollCosine
                - frame.up[axis] * frame.rollSine);
        result[4 + axis] = frame.right[axis] * frame.rollSine
                + frame.up[axis] * frame.rollCosine;
        result[8 + axis] = reverse * frame.forward[axis];
        result[12 + axis] = frame.position[axis];
    }
    return result;
}

void appendSharedTemplateGeometry(const OrtsTrackProfile &profile,
        const OrtsProfileLodItem &item,
        const OrtsProfileTemplate3D &source, ComplexLine &line,
        const QVector<GeneratedPathFrame> &pathFrames,
        QVector<OrtsGeneratedProfileSharedMesh> &outputs, float alpha,
        float minimumDistance, float maximumDistance,
        float startRoll, float endRoll, float endExtension, float endDrop,
        const ProceduralPathTransform *pathTransform, int objectIndex,
        QStringList *diagnostics) {
    QVector<QSharedPointer<const ObjFile>> sourceMeshes;
    sourceMeshes.reserve(source.shapes.size());
    for(const QString &shape : source.shapes)
        sourceMeshes.append(templateMesh(profile, shape, diagnostics));

    QHash<int, int> outputByShape;
    auto outputFor = [&](int shapeIndex)
            -> OrtsGeneratedProfileSharedMesh* {
        if(shapeIndex < 0 || shapeIndex >= sourceMeshes.size()
                || sourceMeshes[shapeIndex] == nullptr)
            return nullptr;
        auto existing = outputByShape.constFind(shapeIndex);
        if(existing != outputByShape.cend())
            return &outputs[existing.value()];
        OrtsGeneratedProfileSharedMesh output;
        output.textureName = item.textureName;
        output.materialPass = materialPass(item);
        output.minimumDistance = minimumDistance;
        output.maximumDistance = maximumDistance;
        appendSharedTemplateVertices(
                output, *sourceMeshes[shapeIndex], source.offset, alpha);
        const int outputIndex = outputs.size();
        outputs.append(output);
        outputByShape.insert(shapeIndex, outputIndex);
        return &outputs[outputIndex];
    };
    auto appendOccurrence = [&](float distance, int copyIndex,
            int selectionObjectIndex, bool reverseFacing,
            int spanIndex = -1, bool atEnd = false,
            bool averagedNode = false) {
        const int shapeIndex = selectedShapeIndex(
                source, selectionObjectIndex, copyIndex, profile.id);
        OrtsGeneratedProfileSharedMesh *output = outputFor(shapeIndex);
        if(output == nullptr)
            return;
        GeneratedPathFrame frame = spanIndex >= 0
                ? samplePlacementFrame(line, distance, spanIndex, atEnd,
                        averagedNode, startRoll, endRoll,
                        endExtension, endDrop, pathTransform)
                : samplePathFrame(line, distance, startRoll, endRoll,
                        endExtension, endDrop, pathTransform);
        frame = frameForMode(frame, item.pathFrameMode);
        output->transforms.append(
                sharedTemplateTransform(frame, reverseFacing));
    };

    if(source.generationMode == OrtsProfileTemplate3D::GenerationMode::Repeat){
        int copyIndex = 0;
        float distance = source.phase;
        while(distance < 0){
            distance += source.spacing;
            copyIndex++;
        }
        const float generationEnd = pathFrames.last().distance;
        for(; distance <= generationEnd + 0.0001f;
                distance += source.spacing, copyIndex++)
            appendOccurrence(distance, copyIndex, objectIndex, false);
        return;
    }

    const int placementSpanCount = line.isPointPath()
            ? pathFrames.size() - 1 : 1;
    int copyIndex = 0;
    for(int span = 0; span < placementSpanCount; span++){
        const float spanStart = line.isPointPath()
                ? pathFrames[span].distance : pathFrames.first().distance;
        const float spanEnd = line.isPointPath()
                ? pathFrames[span + 1].distance : pathFrames.last().distance;
        for(const OrtsProfileTemplate3D::Placement &placement
                : source.placements){
            if(placement.location
                    == OrtsProfileTemplate3D::PlacementLocation::Nodes){
                const bool reverseFacing = placement.facing
                        == OrtsProfileTemplate3D::PlacementFacing::AgainstPath;
                appendOccurrence(spanStart, copyIndex++,
                        objectIndex + span, reverseFacing,
                        span, false, true);
                if(span + 1 == placementSpanCount)
                    appendOccurrence(spanEnd, copyIndex++,
                            objectIndex + span + 1, reverseFacing,
                            span, true, true);
                continue;
            }
            const bool atEnd = placement.location
                    == OrtsProfileTemplate3D::PlacementLocation::End;
            bool reverseFacing = false;
            if(placement.facing
                    == OrtsProfileTemplate3D::PlacementFacing::AgainstPath)
                reverseFacing = true;
            else if(placement.facing
                    == OrtsProfileTemplate3D::PlacementFacing::Outward)
                reverseFacing = !atEnd;
            else if(placement.facing
                    == OrtsProfileTemplate3D::PlacementFacing::Inward)
                reverseFacing = atEnd;
            appendOccurrence(atEnd ? spanEnd : spanStart,
                    copyIndex++, objectIndex + span, reverseFacing,
                    span, atEnd, false);
        }
    }
}

int appendTemplateGeometry(const OrtsTrackProfile &profile,
        const OrtsProfileLodItem &item,
        const OrtsProfileTemplate3D &source, ComplexLine &line,
        const QVector<GeneratedPathFrame> &pathFrames,
        QVector<float> &target, float alpha,
        float startRoll, float endRoll, float endExtension, float endDrop,
        const ProceduralPathTransform *pathTransform, int objectIndex,
        QStringList *diagnostics,
        QVector<OrtsGeneratedProfileSharedMesh> *sharedMeshes,
        float minimumDistance, float maximumDistance,
        int copyIndexBase = 0) {
    if(source.geometryMode
            == OrtsProfileTemplate3D::GeometryMode::Shared
            && sharedMeshes != nullptr){
        appendSharedTemplateGeometry(profile, item, source, line,
                pathFrames, *sharedMeshes, alpha,
                minimumDistance, maximumDistance,
                startRoll, endRoll, endExtension, endDrop,
                pathTransform, objectIndex, diagnostics);
        return copyIndexBase;
    }
    QVector<QSharedPointer<const ObjFile>> sourceMeshes;
    sourceMeshes.reserve(source.shapes.size());
    for(const QString &shape : source.shapes)
        sourceMeshes.append(templateMesh(profile, shape, diagnostics));
    auto loadShape = [&](int copyIndex, int selectionObjectIndex) {
        const int shapeIndex = selectedShapeIndex(
                source, selectionObjectIndex, copyIndex, profile.id);
        return sourceMeshes.value(shapeIndex);
    };

    if(source.generationMode == OrtsProfileTemplate3D::GenerationMode::Sweep){
        int copyIndex = copyIndexBase;
        for(int span = 1; span < pathFrames.size(); span++){
            float start = pathFrames[span - 1].distance;
            const float spanEnd = pathFrames[span].distance;
            while(start < spanEnd - 0.000001f){
                const QSharedPointer<const ObjFile> mesh = loadShape(
                        copyIndex++, objectIndex
                        + (line.isPointPath() ? span - 1 : 0));
                if(mesh == nullptr)
                    break;
                const float depth = templateDepth(*mesh);
                if(depth <= 0.000001f){
                    if(diagnostics != nullptr)
                        diagnostics->append("Template3D Sweep mesh has no depth");
                    break;
                }
                const float end = std::min(spanEnd, start + depth);
                appendDeformedTemplate(target, *mesh, line,
                        start, end,
                        startRoll, endRoll, endExtension, endDrop,
                        pathTransform, item.pathFrameMode,
                        true, false,
                        source.offset, alpha);
                start = end;
            }
        }
        return copyIndex;
    }
    if(source.generationMode == OrtsProfileTemplate3D::GenerationMode::Stretch){
        if(line.isPointPath()){
            for(int span = 1; span < pathFrames.size(); span++){
                const QSharedPointer<const ObjFile> mesh = loadShape(
                        copyIndexBase + span - 1,
                        objectIndex + span - 1);
                if(mesh != nullptr)
                    appendDeformedTemplate(target, *mesh, line,
                            pathFrames[span - 1].distance,
                            pathFrames[span].distance,
                            startRoll, endRoll, endExtension, endDrop,
                            pathTransform, item.pathFrameMode, false,
                            true,
                            source.offset, alpha);
            }
        } else {
            const QSharedPointer<const ObjFile> mesh = loadShape(0, objectIndex);
            if(mesh != nullptr)
                appendDeformedTemplate(target, *mesh, line,
                        pathFrames.first().distance, pathFrames.last().distance,
                        startRoll, endRoll, endExtension, endDrop,
                        pathTransform, item.pathFrameMode, false,
                        false,
                        source.offset, alpha);
        }
        return copyIndexBase + (line.isPointPath()
                ? pathFrames.size() - 1 : 1);
    }
    if(source.generationMode == OrtsProfileTemplate3D::GenerationMode::Repeat){
        const float generationEnd = pathFrames.last().distance;
        if(!sourceMeshes.isEmpty() && sourceMeshes.first() != nullptr){
            const int copyCount = std::max(0, (int)std::floor(
                    (generationEnd - std::max(0.0f, source.phase))
                    / source.spacing) + 1);
            target.reserve(target.size()
                    + copyCount * sourceMeshes.first()->points.size() * 9 / 8);
        }
        int copyIndex = 0;
        float distance = source.phase;
        const float generationStart = pathFrames.first().distance;
        while(distance < generationStart - 0.0001f){
            distance += source.spacing;
            copyIndex++;
        }
        for(; distance <= generationEnd + 0.0001f;
                distance += source.spacing, copyIndex++){
            const QSharedPointer<const ObjFile> mesh = loadShape(
                    copyIndex, objectIndex);
            if(mesh == nullptr)
                continue;
            GeneratedPathFrame frame = samplePathFrame(
                    line, distance, startRoll, endRoll,
                    endExtension, endDrop, pathTransform);
            frame = frameForMode(frame, item.pathFrameMode);
            appendRigidTemplate(target, *mesh, frame, source.offset,
                                alpha, false);
        }
        return copyIndex;
    }

    const int placementSpanCount = line.isPointPath()
            ? pathFrames.size() - 1 : 1;
    int copyIndex = copyIndexBase;
    for(int span = 0; span < placementSpanCount; span++){
        const float spanStart = line.isPointPath()
                ? pathFrames[span].distance : pathFrames.first().distance;
        const float spanEnd = line.isPointPath()
                ? pathFrames[span + 1].distance : pathFrames.last().distance;
        const int pointSpan = line.isPointPath()
                ? pointNodeIndex(line, spanStart) : span;
        for(const OrtsProfileTemplate3D::Placement &placement
                : source.placements){
            if(placement.location
                    == OrtsProfileTemplate3D::PlacementLocation::Nodes){
                const bool reverseFacing = placement.facing
                        == OrtsProfileTemplate3D::PlacementFacing::AgainstPath;
                const QSharedPointer<const ObjFile> startMesh = loadShape(
                        copyIndex++, objectIndex + span);
                if(startMesh != nullptr){
                    GeneratedPathFrame frame = samplePlacementFrame(
                            line, spanStart, pointSpan, false, true,
                            startRoll, endRoll, endExtension, endDrop,
                            pathTransform);
                    frame = frameForMode(frame, item.pathFrameMode);
                    appendRigidTemplate(target, *startMesh, frame,
                            source.offset, alpha, reverseFacing);
                }
                const bool finalNode = !line.isPointPath()
                        || std::abs(spanEnd - line.length) < 0.0001f;
                if(finalNode){
                    const QSharedPointer<const ObjFile> endMesh = loadShape(
                            copyIndex++, objectIndex + span + 1);
                    if(endMesh != nullptr){
                        GeneratedPathFrame frame = samplePlacementFrame(
                                line, spanEnd, pointSpan, true, true,
                                startRoll, endRoll, endExtension, endDrop,
                                pathTransform);
                        frame = frameForMode(frame, item.pathFrameMode);
                        appendRigidTemplate(target, *endMesh, frame,
                                source.offset, alpha, reverseFacing);
                    }
                }
                continue;
            }
            const bool atEnd = placement.location
                    == OrtsProfileTemplate3D::PlacementLocation::End;
            const float distance = atEnd ? spanEnd : spanStart;
            bool reverseFacing = false;
            if(placement.facing
                    == OrtsProfileTemplate3D::PlacementFacing::AgainstPath)
                reverseFacing = true;
            else if(placement.facing
                    == OrtsProfileTemplate3D::PlacementFacing::Outward)
                reverseFacing = !atEnd;
            else if(placement.facing
                    == OrtsProfileTemplate3D::PlacementFacing::Inward)
                reverseFacing = atEnd;
            const QSharedPointer<const ObjFile> mesh = loadShape(
                    copyIndex++, objectIndex + span);
            if(mesh == nullptr)
                continue;
            GeneratedPathFrame frame = samplePlacementFrame(
                    line, distance, pointSpan, atEnd, false,
                    startRoll, endRoll, endExtension, endDrop,
                    pathTransform);
            frame = frameForMode(frame, item.pathFrameMode);
            appendRigidTemplate(target, *mesh, frame, source.offset,
                                alpha, reverseFacing);
        }
    }
    return copyIndex;
}

void appendVertex(QVector<float> &target, const GeneratedVertex &vertex) {
    for(float value : vertex.values)
        target.append(value);
}

void updateBounds(OrtsGeneratedProfileMesh &mesh) {
    if(mesh.vertices.isEmpty())
        return;
    mesh.bounds[0] = mesh.bounds[1] = mesh.vertices[0];
    mesh.bounds[2] = mesh.bounds[3] = mesh.vertices[1];
    mesh.bounds[4] = mesh.bounds[5] = mesh.vertices[2];
    for(int i = 0; i < mesh.vertices.size(); i += 9){
        mesh.bounds[0] = std::max(mesh.bounds[0], mesh.vertices[i]);
        mesh.bounds[1] = std::min(mesh.bounds[1], mesh.vertices[i]);
        mesh.bounds[2] = std::max(mesh.bounds[2], mesh.vertices[i + 1]);
        mesh.bounds[3] = std::min(mesh.bounds[3], mesh.vertices[i + 1]);
        mesh.bounds[4] = std::max(mesh.bounds[4], mesh.vertices[i + 2]);
        mesh.bounds[5] = std::min(mesh.bounds[5], mesh.vertices[i + 2]);
    }
}

void transformStaticPath(OrtsGeneratedProfileMesh &mesh,
        const TrackShape::SectionIdx &path) {
    float rotation[4];
    Quat::fill(rotation);
    Quat::rotateY(rotation, rotation, qDegreesToRadians(-path.rotDeg));
    const float translation[3] = {-path.pos[0], path.pos[1], path.pos[2]};

    for(int i = 0; i < mesh.vertices.size(); i += 9){
        float position[3] = {
            mesh.vertices[i], mesh.vertices[i + 1], mesh.vertices[i + 2]
        };
        float normal[3] = {
            mesh.vertices[i + 3], mesh.vertices[i + 4], mesh.vertices[i + 5]
        };
        Vec3::transformQuat(position, position, rotation);
        Vec3::transformQuat(normal, normal, rotation);
        mesh.vertices[i] = position[0] + translation[0];
        mesh.vertices[i + 1] = position[1] + translation[1];
        mesh.vertices[i + 2] = position[2] + translation[2];
        mesh.vertices[i + 3] = normal[0];
        mesh.vertices[i + 4] = normal[1];
        mesh.vertices[i + 5] = normal[2];
    }
    updateBounds(mesh);
}

void translateBakedStaticPath(OrtsGeneratedProfileMesh &mesh,
        const TrackShape::SectionIdx &path,
        const ProceduralPathTransform &objectTransform) {
    float translation[3] = {-path.pos[0], path.pos[1], path.pos[2]};
    Vec3::transformQuat(translation, translation,
            const_cast<float*>(objectTransform.rotation));
    for(int i = 0; i < mesh.vertices.size(); i += 9){
        mesh.vertices[i] += translation[0];
        mesh.vertices[i + 1] += translation[1];
        mesh.vertices[i + 2] += translation[2];
    }
    updateBounds(mesh);
}

QString texturePath(const QString &routePath, const QString &textureName) {
    QString normalizedName = textureName;
    normalizedName.replace('\\', '/');
    const QString routeTexture = QDir::cleanPath(routePath + "/TEXTURES/" + normalizedName);
    if(QFileInfo::exists(routeTexture))
        return routeTexture;
    const QDir routeDirectory(routePath);
    const QString globalTexture = QDir::cleanPath(
            routeDirectory.absoluteFilePath("../../GLOBAL/TEXTURES/" + normalizedName));
    if(QFileInfo::exists(globalTexture))
        return globalTexture;
    return routeTexture;
}

QString normalizedTextureId(QString path) {
    path.replace('\\', '/');
    path = ContentPath::normalize(path);
    return path;
}

bool textureIdMatches(int textureId, const Texture *texture) {
    const auto found = TexLib::mtex.find(textureId);
    return found != TexLib::mtex.end()
            && found->second != nullptr
            && found->second == texture;
}

int profileTextureId(const QString &routePath, const QString &textureName) {
    // ORTS profiles can emit many LOD/material objects which all reuse a
    // small set of textures. Resolving each new OglObj independently made
    // every live Flex rebuild repeat filesystem probes and linear TexLib
    // scans. Keep one validated TexLib reference per route/texture instead.
    static QHash<QString, int> textureIds;
    static QHash<QString, Texture*> textures;

    QString normalizedName = textureName;
    normalizedName.replace('\\', '/');
    QString normalizedRoute = QDir::cleanPath(routePath);
    const QString cacheKey = ContentPath::key(normalizedRoute) + "\n" + normalizedName.toLower();
    const auto cached = textureIds.constFind(cacheKey);
    const Texture *existing = textures.value(cacheKey);
    if(cached != textureIds.cend() && textureIdMatches(cached.value(), existing)
            && !existing->missing && !existing->error)
        return cached.value();

    const QString path = ContentPath::textureSource(normalizedTextureId(
            texturePath(routePath, normalizedName)));

    const int textureId = TexLib::addTex(path);
    textureIds.insert(cacheKey, textureId);
    const auto loaded = TexLib::mtex.find(textureId);
    textures.insert(cacheKey,
            loaded == TexLib::mtex.end() ? nullptr : loaded->second);
    return textureId;
}

}

static bool buildMeshesForLine(const OrtsTrackProfile &profile,
        ComplexLine &line, const QVector<float> &distances,
        QVector<OrtsGeneratedProfileMesh> &meshes,
        float startRoll, float endRoll, QStringList *diagnostics,
        float endExtension = 0, float endDrop = 0,
        const ProceduralPathTransform *pathTransform = nullptr,
        int objectIndex = 0,
        QVector<OrtsGeneratedProfileSharedMesh> *sharedMeshes = nullptr) {
    meshes.clear();
    if(sharedMeshes != nullptr)
        sharedMeshes->clear();
    if(!profile.valid || distances.size() < 2 || line.length <= 0){
        if(diagnostics != nullptr)
            diagnostics->append("ORTS profile or DynTrack path is empty");
        return false;
    }

    // Every profile vertex at a given distance shares the same centerline
    // position and orientation. Complex profiles can contain over a hundred
    // vertices across their LODs, so sample the path once per distance.
    QVector<GeneratedPathFrame> pathFrames;
    pathFrames.reserve(distances.size());
    for(float distance : distances)
        pathFrames.append(samplePathFrame(
                line, distance, startRoll, endRoll,
                endExtension, endDrop, pathTransform));

    float previousCutoff = -1;
    bool hasPositionControl = false;
    QSet<QString> materialDiagnostics;
    for(const OrtsProfileLod &lod : profile.lods){
        for(const OrtsProfileLodItem &item : lod.items){
            if(!item.shaderName.isEmpty()
                    && item.shaderName.compare("TexDiff", Qt::CaseInsensitive) != 0
                    && item.shaderName.compare("BlendATexDiff", Qt::CaseInsensitive) != 0)
                materialDiagnostics.insert("ShaderName " + item.shaderName
                                           + " mapped to TSRE textured material");
            if(!item.lightModelName.isEmpty())
                materialDiagnostics.insert("LightModelName " + item.lightModelName
                                           + " mapped to TSRE default lighting");
            if(item.alphaTestMode != 0 && item.alphaTestMode != 1)
                materialDiagnostics.insert("AlphaTestMode is not supported; "
                                           "using texture alpha");
            if(!item.textureAddressMode.isEmpty()
                    && item.textureAddressMode.compare("Wrap", Qt::CaseInsensitive) != 0)
                materialDiagnostics.insert("TexAddrModeName " + item.textureAddressMode
                                           + " is not supported; using TSRE default");
            if(item.alternativeTexture != 0)
                materialDiagnostics.insert("ESD_Alternative_Texture is only partially "
                                           "supported through route/global lookup");
            if(std::abs(item.mipMapLodBias) > 0.0001f)
                materialDiagnostics.insert("MipMapLevelOfDetailBias is not supported");
            OrtsGeneratedProfileMesh mesh;
            mesh.textureName = item.textureName;
            mesh.materialPass = materialPass(item);
            const float alpha = materialAlpha(item);
            if(profile.lodMethod == OrtsTrackProfile::LodMethod::CompleteReplacement)
                mesh.minimumDistance = previousCutoff;
            mesh.maximumDistance = lod.cutoffRadius;

            QVector<GeneratedPathFrame> itemPathFrames = pathFrames;
            for(GeneratedPathFrame &frame : itemPathFrames)
                frame = frameForMode(frame, item.pathFrameMode);

            auto appendPolylineSpan = [&](OrtsGeneratedProfileMesh &target,
                    const OrtsProfilePolyline &polyline,
                    const GeneratedPathFrame &previousFrame,
                    const GeneratedPathFrame &currentFrame) {
                if(polyline.vertices.size() < 2)
                    return;
                QVector<GeneratedVertex> previous;
                QVector<GeneratedVertex> current;
                previous.reserve(polyline.vertices.size());
                current.reserve(polyline.vertices.size());
                for(const OrtsProfileVertex &vertex : polyline.vertices){
                    if(vertex.positionControl
                            != OrtsProfileVertex::PositionControl::None)
                        hasPositionControl = true;
                    previous.append(transformVertex(
                            vertex, polyline, previousFrame, alpha));
                    current.append(transformVertex(
                            vertex, polyline, currentFrame, alpha));
                }
                for(int vertexIndex = 1; vertexIndex < current.size();
                        vertexIndex++){
                    // ORTS uses clockwise front faces. Reversing profile X
                    // above removes the reflection which previously made
                    // that order appear counter-clockwise in OpenGL.
                    appendVertex(target.vertices, current[vertexIndex]);
                    appendVertex(target.vertices, current[vertexIndex - 1]);
                    appendVertex(target.vertices, previous[vertexIndex - 1]);
                    appendVertex(target.vertices, current[vertexIndex]);
                    appendVertex(target.vertices, previous[vertexIndex - 1]);
                    appendVertex(target.vertices, previous[vertexIndex]);
                }
            };

            if(line.isPointPath()){
                QVector<OrtsGeneratedProfileMesh> spanMeshes;
                spanMeshes.resize(std::max(
                        0, (int)itemPathFrames.size() - 1));
                for(OrtsGeneratedProfileMesh &spanMesh : spanMeshes){
                    spanMesh.textureName = mesh.textureName;
                    spanMesh.materialPass = mesh.materialPass;
                    spanMesh.minimumDistance = mesh.minimumDistance;
                    spanMesh.maximumDistance = mesh.maximumDistance;
                }
                for(int span = 0; span < spanMeshes.size(); span++){
                    for(const OrtsProfilePolyline &polyline : item.polylines)
                        appendPolylineSpan(spanMeshes[span], polyline,
                                itemPathFrames[span],
                                itemPathFrames[span + 1]);
                }
                for(const OrtsProfileTemplate3D &template3D
                        : item.templates3D){
                    if(template3D.geometryMode
                            == OrtsProfileTemplate3D::GeometryMode::Shared
                            && sharedMeshes != nullptr){
                        appendTemplateGeometry(profile, item, template3D, line,
                                itemPathFrames, mesh.vertices, alpha,
                                startRoll, endRoll, endExtension, endDrop,
                                pathTransform, objectIndex, diagnostics,
                                sharedMeshes, mesh.minimumDistance,
                                mesh.maximumDistance);
                        continue;
                    }
                    int copyIndex = 0;
                    for(int span = 0; span < spanMeshes.size(); span++){
                        QVector<GeneratedPathFrame> spanFrames = {
                            itemPathFrames[span], itemPathFrames[span + 1]
                        };
                        if(template3D.generationMode
                                == OrtsProfileTemplate3D::GenerationMode::Repeat
                                && span + 1 < spanMeshes.size())
                            spanFrames[1].distance -= 0.0002f;
                        copyIndex = appendTemplateGeometry(
                                profile, item, template3D, line,
                                spanFrames, spanMeshes[span].vertices, alpha,
                                startRoll, endRoll, endExtension, endDrop,
                                pathTransform, objectIndex + span, diagnostics,
                                nullptr, mesh.minimumDistance,
                                mesh.maximumDistance, copyIndex);
                    }
                }
                for(OrtsGeneratedProfileMesh &spanMesh : spanMeshes){
                    if(spanMesh.vertices.isEmpty())
                        continue;
                    updateBounds(spanMesh);
                    meshes.append(spanMesh);
                }
            } else {
                for(const OrtsProfilePolyline &polyline : item.polylines){
                    for(int frameIndex = 1;
                            frameIndex < itemPathFrames.size(); frameIndex++)
                        appendPolylineSpan(mesh, polyline,
                                itemPathFrames[frameIndex - 1],
                                itemPathFrames[frameIndex]);
                }
                for(const OrtsProfileTemplate3D &template3D
                        : item.templates3D)
                    appendTemplateGeometry(profile, item, template3D, line,
                            itemPathFrames, mesh.vertices, alpha,
                            startRoll, endRoll, endExtension, endDrop,
                            pathTransform, objectIndex, diagnostics,
                            sharedMeshes, mesh.minimumDistance,
                            mesh.maximumDistance);
                if(!mesh.vertices.isEmpty()){
                    updateBounds(mesh);
                    meshes.append(mesh);
                }
            }
        }
        previousCutoff = lod.cutoffRadius;
    }

    if(hasPositionControl && diagnostics != nullptr)
        diagnostics->append("ORTS PositionControl parsed; profile-specific "
                            "superelevation deformation is not yet applied");
    if(diagnostics != nullptr){
        for(const QString &diagnostic : materialDiagnostics)
            diagnostics->append(diagnostic);
    }
    const bool hasShared = sharedMeshes != nullptr
            && !sharedMeshes->isEmpty();
    if(meshes.isEmpty() && !hasShared && diagnostics != nullptr)
        diagnostics->append("ORTS profile produced no renderable geometry");
    return !meshes.isEmpty() || hasShared;
}

static bool buildMeshesForPath(const OrtsTrackProfile &profile,
        const QVector<TSection> &sections,
        QVector<OrtsGeneratedProfileMesh> &meshes,
        float startRoll, float endRoll, QStringList *diagnostics,
        float endExtension = 0, float endDrop = 0,
        const ProceduralPathTransform *pathTransform = nullptr,
        int objectIndex = 0) {
    if(sections.isEmpty()){
        meshes.clear();
        if(diagnostics != nullptr)
            diagnostics->append("ORTS profile or DynTrack path is empty");
        return false;
    }
    ComplexLine line;
    line.init(sections);
    const QVector<float> distances = frameDistances(
            profile, sections, endExtension, diagnostics);
    return buildMeshesForLine(profile, line, distances, meshes,
            startRoll, endRoll, diagnostics, endExtension, endDrop,
            pathTransform, objectIndex);
}

bool OrtsTrackProfileRenderer::buildMeshes(const OrtsTrackProfile &profile,
        const QVector<TSection> &sections,
        QVector<OrtsGeneratedProfileMesh> &meshes,
        QStringList *diagnostics, float endExtension, float endDrop,
        const ProceduralPathTransform *pathTransform, int objectIndex) {
    return buildMeshesForPath(
            profile, sections, meshes, 0, 0, diagnostics,
            endExtension, endDrop, pathTransform, objectIndex);
}

bool OrtsTrackProfileRenderer::buildMeshes(const OrtsTrackProfile &profile,
        ComplexLine &line, QVector<OrtsGeneratedProfileMesh> &meshes,
        QStringList *diagnostics, int objectIndex,
        QVector<OrtsGeneratedProfileSharedMesh> *sharedMeshes) {
    if(!line.isPointPath()){
        meshes.clear();
        if(diagnostics != nullptr)
            diagnostics->append("ORTS profile point path is empty");
        return false;
    }
    const QVector<float> distances = frameDistances(line, diagnostics);
    return buildMeshesForLine(profile, line, distances, meshes,
            0, 0, diagnostics, 0, 0, nullptr, objectIndex, sharedMeshes);
}

bool OrtsTrackProfileRenderer::generate(const OrtsTrackProfile &profile,
        const QVector<TSection> &sections, QVector<OglObj*> &shape,
        const QString &routePath, QStringList *diagnostics,
        float endExtension, float endDrop,
        const ProceduralPathTransform *pathTransform, int objectIndex) {
    QVector<OrtsGeneratedProfileMesh> meshes;
    if(!buildMeshes(profile, sections, meshes, diagnostics,
            endExtension, endDrop, pathTransform, objectIndex))
        return false;

    QVector<OglObj*> generated;
    const OrtsGeneratedProfileMesh::MaterialPass passes[] = {
        OrtsGeneratedProfileMesh::MaterialPass::Opaque,
        OrtsGeneratedProfileMesh::MaterialPass::AlphaTest,
        OrtsGeneratedProfileMesh::MaterialPass::Blended
    };
    for(OrtsGeneratedProfileMesh::MaterialPass pass : passes){
        for(const OrtsGeneratedProfileMesh &mesh : meshes){
            if(mesh.materialPass != pass)
                continue;
            float *vertexData = new float[mesh.vertices.size()];
            std::copy(mesh.vertices.cbegin(), mesh.vertices.cend(), vertexData);
            OglObj *object = new OglObj();
            object->setMaterialTextureId(
                    profileTextureId(routePath, mesh.textureName));
            object->setDistanceRange(mesh.minimumDistance, mesh.maximumDistance);
            object->init(
                    vertexData, mesh.vertices.size(), RenderItem::VNTA, GL_TRIANGLES);
            object->setBound(const_cast<float*>(mesh.bounds));
            generated.append(object);
            delete[] vertexData;
        }
    }
    shape.append(generated);
    return !generated.isEmpty();
}

bool OrtsTrackProfileRenderer::generate(const OrtsTrackProfile &profile,
        ComplexLine &line, QVector<OglObj*> &shape,
        const QString &routePath, QStringList *diagnostics,
        int objectIndex) {
    QVector<OrtsGeneratedProfileMesh> meshes;
    if(!buildMeshes(profile, line, meshes, diagnostics, objectIndex))
        return false;

    QVector<OglObj*> generated;
    const OrtsGeneratedProfileMesh::MaterialPass passes[] = {
        OrtsGeneratedProfileMesh::MaterialPass::Opaque,
        OrtsGeneratedProfileMesh::MaterialPass::AlphaTest,
        OrtsGeneratedProfileMesh::MaterialPass::Blended
    };
    for(OrtsGeneratedProfileMesh::MaterialPass pass : passes){
        for(const OrtsGeneratedProfileMesh &mesh : meshes){
            if(mesh.materialPass != pass)
                continue;
            float *vertexData = new float[mesh.vertices.size()];
            std::copy(mesh.vertices.cbegin(), mesh.vertices.cend(), vertexData);
            OglObj *object = new OglObj();
            object->setMaterialTextureId(
                    profileTextureId(routePath, mesh.textureName));
            object->setDistanceRange(mesh.minimumDistance, mesh.maximumDistance);
            object->init(
                    vertexData, mesh.vertices.size(), RenderItem::VNTA,
                    GL_TRIANGLES);
            object->setBound(const_cast<float*>(mesh.bounds));
            generated.append(object);
            delete[] vertexData;
        }
    }
    shape.append(generated);
    return !generated.isEmpty();
}

bool OrtsTrackProfileRenderer::generateWithInstances(
        const OrtsTrackProfile &profile, ComplexLine &line,
        QVector<OglObj*> &shape,
        QVector<OrtsGeneratedProfileInstanceObject> &instances,
        const QString &routePath, QStringList *diagnostics,
        int objectIndex) {
    QVector<OrtsGeneratedProfileMesh> meshes;
    QVector<OrtsGeneratedProfileSharedMesh> sharedMeshes;
    if(!buildMeshes(profile, line, meshes, diagnostics,
                    objectIndex, &sharedMeshes))
        return false;

    QVector<OglObj*> generated;
    const OrtsGeneratedProfileMesh::MaterialPass passes[] = {
        OrtsGeneratedProfileMesh::MaterialPass::Opaque,
        OrtsGeneratedProfileMesh::MaterialPass::AlphaTest,
        OrtsGeneratedProfileMesh::MaterialPass::Blended
    };
    for(OrtsGeneratedProfileMesh::MaterialPass pass : passes){
        for(const OrtsGeneratedProfileMesh &mesh : meshes){
            if(mesh.materialPass != pass)
                continue;
            float *vertexData = new float[mesh.vertices.size()];
            std::copy(mesh.vertices.cbegin(), mesh.vertices.cend(), vertexData);
            OglObj *object = new OglObj();
            object->setMaterialTextureId(
                    profileTextureId(routePath, mesh.textureName));
            object->setDistanceRange(
                    mesh.minimumDistance, mesh.maximumDistance);
            object->init(vertexData, mesh.vertices.size(),
                         RenderItem::VNTA, GL_TRIANGLES);
            object->setBound(const_cast<float*>(mesh.bounds));
            generated.append(object);
            delete[] vertexData;
        }
        for(const OrtsGeneratedProfileSharedMesh &mesh : sharedMeshes){
            if(mesh.materialPass != pass || mesh.transforms.isEmpty())
                continue;
            float *vertexData = new float[mesh.vertices.size()];
            std::copy(mesh.vertices.cbegin(), mesh.vertices.cend(), vertexData);
            OrtsGeneratedProfileInstanceObject instance;
            instance.object = new OglObj();
            instance.object->setMaterialTextureId(
                    profileTextureId(routePath, mesh.textureName));
            instance.object->setDistanceRange(
                    mesh.minimumDistance, mesh.maximumDistance);
            instance.object->init(vertexData, mesh.vertices.size(),
                                  RenderItem::VNTA, GL_TRIANGLES);
            instance.object->setBound(const_cast<float*>(mesh.bounds));
            instance.transforms = mesh.transforms;
            instances.append(instance);
            delete[] vertexData;
        }
    }
    shape.append(generated);
    return !generated.isEmpty() || !instances.isEmpty();
}

bool OrtsTrackProfileRenderer::generate(const OrtsTrackProfile &profile,
        const TrackShape &trackShape, const QMap<int, float> &angles,
        QVector<OglObj*> &shape, const QString &routePath,
        QStringList *diagnostics, float endExtension, float endDrop,
        const ProceduralPathTransform *pathTransform,
        const QVector<QSharedPointer<const OrtsTrackProfile>> *pathProfiles,
        int objectIndex) {
    if(Game::currentRoute == nullptr || Game::currentRoute->tsection == nullptr)
        return false;

    QVector<OrtsGeneratedProfileMesh> meshes;
    for(int pathIndex = 0; pathIndex < trackShape.numpaths; pathIndex++){
        const TrackShape::SectionIdx &path = trackShape.path[pathIndex];
        QVector<TSection> sections;
        for(int sectionIndex = 0; sectionIndex < path.n; sectionIndex++){
            const auto found = Game::currentRoute->tsection->sekcja.find(
                    (int)path.sect[sectionIndex]);
            if(found != Game::currentRoute->tsection->sekcja.end()
                    && found->second != nullptr)
                sections.append(*found->second);
        }
        if(sections.isEmpty())
            continue;

        ProceduralPathTransform combinedTransform;
        const ProceduralPathTransform *activeTransform = nullptr;
        if(pathTransform != nullptr && pathTransform->enabled){
            float pathRotation[4];
            Quat::fill(pathRotation);
            Quat::rotateY(pathRotation, pathRotation,
                    qDegreesToRadians(-path.rotDeg));
            Quat::multiply(combinedTransform.rotation,
                    const_cast<float*>(pathTransform->rotation), pathRotation);
            combinedTransform.enabled = true;
            combinedTransform.uprightCrossSections =
                    pathTransform->uprightCrossSections;
            activeTransform = &combinedTransform;
        }

        QVector<OrtsGeneratedProfileMesh> pathMeshes;
        const OrtsTrackProfile *activeProfile = &profile;
        if(pathProfiles != nullptr && pathIndex < pathProfiles->size()
                && pathProfiles->at(pathIndex) != nullptr)
            activeProfile = pathProfiles->at(pathIndex).data();
        if(!buildMeshesForPath(*activeProfile, sections, pathMeshes,
                angles.value(pathIndex * 2, 0),
                angles.value(pathIndex * 2 + 1, 0), diagnostics,
                endExtension, endDrop, activeTransform, objectIndex))
            continue;
        for(OrtsGeneratedProfileMesh &mesh : pathMeshes){
            if(activeTransform != nullptr)
                translateBakedStaticPath(mesh, path, *pathTransform);
            else
                transformStaticPath(mesh, path);
            meshes.append(mesh);
        }
    }

    QVector<OglObj*> generated;
    const OrtsGeneratedProfileMesh::MaterialPass passes[] = {
        OrtsGeneratedProfileMesh::MaterialPass::Opaque,
        OrtsGeneratedProfileMesh::MaterialPass::AlphaTest,
        OrtsGeneratedProfileMesh::MaterialPass::Blended
    };
    for(OrtsGeneratedProfileMesh::MaterialPass pass : passes){
        for(const OrtsGeneratedProfileMesh &mesh : meshes){
            if(mesh.materialPass != pass)
                continue;
            float *vertexData = new float[mesh.vertices.size()];
            std::copy(mesh.vertices.cbegin(), mesh.vertices.cend(), vertexData);
            OglObj *object = new OglObj();
            object->setMaterialTextureId(
                    profileTextureId(routePath, mesh.textureName));
            object->setDistanceRange(mesh.minimumDistance, mesh.maximumDistance);
            object->init(
                    vertexData, mesh.vertices.size(), RenderItem::VNTA, GL_TRIANGLES);
            object->setBound(const_cast<float*>(mesh.bounds));
            generated.append(object);
            delete[] vertexData;
        }
    }
    shape.append(generated);
    return !generated.isEmpty();
}
