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
#include <tsre/procedural/ComplexTrackPlan.h>
#include <tsre/procedural/TrackProfileFilters.h>
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
#include <numeric>

namespace {

constexpr int MaximumFrames = 4096;
constexpr float OpaqueAlpha = 1.0f;
constexpr float BlendedTransparentCutoff = -1.0f / 255.0f;
constexpr float AlphaTestCutoff = -0.51f;

// BlendATexDiff, or BlendAPbrTexDiff (named, or upgraded from it).
bool blended(const OrtsProfileLodItem &item) {
    const QString shader = item.shader();
    return shader.compare("BlendATexDiff", Qt::CaseInsensitive) == 0
            || MstsMaterials::shader(shader) == MstsMaterials::Shader::PbrBlended;
}

// A PBR LODItem's material (task shapes 13); shader Other for another.
OrtsGeneratedProfilePbr profilePbr(const OrtsProfileLodItem &item) {
    OrtsGeneratedProfilePbr pbr;
    pbr.shader = MstsMaterials::shader(item.shader());
    if(!MstsMaterials::pbr(pbr.shader))
        return pbr;
    pbr.ormTextureName = item.ormTextureName;
    pbr.normalTextureName = item.normalTextureName;
    pbr.factors = MstsMaterials::Factors::read(item.materialFactors);
    pbr.alphaTest = item.alphaTestMode == 1;
    return pbr;
}

OrtsGeneratedProfileMesh::MaterialPass materialPass(
        const OrtsProfileLodItem &item) {
    if(item.alphaTestMode == 1)
        return OrtsGeneratedProfileMesh::MaterialPass::AlphaTest;
    if(blended(item))
        return OrtsGeneratedProfileMesh::MaterialPass::Blended;
    return OrtsGeneratedProfileMesh::MaterialPass::Opaque;
}

// The surface a material pass draws with (OglObj::setSurface): blended
// layers in the blended pass, in two, so their edges blend over what lies
// under them, drawn before.
RenderItem::Surface materialSurface(OrtsGeneratedProfileMesh::MaterialPass pass) {
    switch(pass){
    case OrtsGeneratedProfileMesh::MaterialPass::AlphaTest:
        return RenderItem::SURFACE_ALPHA_TEST;
    case OrtsGeneratedProfileMesh::MaterialPass::Blended:
        return RenderItem::SURFACE_BLENDED;
    default:
        return RenderItem::SURFACE_OPAQUE;
    }
}

// Whether a blended item is a flat layer of a track or road profile, drawn
// once, in order, at the end of the alpha-tested pass
// (OrtsGeneratedProfileMesh::blendInOrder): at least 80 % of its
// cross-section's length within 50 degrees of horizontal. Every blended item
// of the user's content is one (ballast, sleeper tops, rail bases,
// embankments: at most 12 % steeper, 0.17 m high); a fence, wall or glass
// barrier is not, nor is any item of a static profile (rulers' fences and
// walls), which keep the two-part draw. TSRE_PROFILE_LAYERS_IN_ORDER=0 draws
// every blended layer in two (read once; the meshes made after it count, so
// reload the track profiles or the route).
bool blendsInOrder(const OrtsTrackProfile &profile, const OrtsProfileLodItem &item) {
    constexpr float SteepDegrees = 50.0f;
    constexpr float FlatShare = 0.8f;
    static const bool enabled =
            qEnvironmentVariable("TSRE_PROFILE_LAYERS_IN_ORDER", "1") != "0";
    if(!enabled
            || profile.objectType == OrtsTrackProfile::ObjectType::Static
            || materialPass(item) != OrtsGeneratedProfileMesh::MaterialPass::Blended)
        return false;
    const float steepSlope = std::tan(qDegreesToRadians(SteepDegrees));
    float steep = 0.0f;
    float total = 0.0f;
    for(const OrtsProfilePolyline &line : item.polylines)
        for(int i = 1; i < line.vertices.size(); ++i){
            const float dx = std::abs(line.vertices[i].position[0] - line.vertices[i - 1].position[0]);
            const float dy = std::abs(line.vertices[i].position[1] - line.vertices[i - 1].position[1]);
            const float length = std::hypot(dx, dy);
            total += length;
            if(dy > dx * steepSlope)
                steep += length;
        }
    return total > 0.0f && steep <= (1.0f - FlatShare) * total;
}

float materialAlpha(const OrtsProfileLodItem &item) {
    // TSRE's VNTA alpha attribute encodes material behavior: positive one
    // forces an opaque fragment, while a negative value preserves texture
    // alpha and discards fragments below its absolute threshold. Blended
    // materials use the smallest meaningful cutoff so invisible fragments
    // cannot write depth before the opaque profile layers are rendered.
    if(item.alphaTestMode == 1)
        return AlphaTestCutoff;
    if(blended(item))
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

// A superelevated straight's segments, as Open Rails' SuperElevationPrimitive
// cuts it (LinearGen): about ChordSpan x 10 m each, the chord length if
// shorter; at least 2, at most 250, even.
int rolledStraightSegments(const OrtsTrackProfile &profile, float length) {
    const float span = profile.chordSpanDegrees * 10.0f;
    int segments = span > 0 ? (int)std::abs(length / span) : 2;
    if(profile.pitchControl == OrtsTrackProfile::PitchControl::ChordLength
            && profile.pitchControlScalar > 0 && profile.pitchControlScalar < span)
        segments = (int)std::abs(length / profile.pitchControlScalar);
    if(segments == 0)
        segments = 2;
    else if(segments > 250)
        segments = 250;
    if(segments % 2 == 1)
        segments++;
    return segments;
}

QVector<float> frameDistances(const OrtsTrackProfile &profile,
        const QVector<TSection> &sections, float endExtension,
        QStringList *diagnostics, bool *truncated,
        const OrtsProfileRoll *roll = nullptr) {
    if(truncated != nullptr)
        *truncated = false;
    QVector<float> distances;
    distances.append(0);
    float offset = 0;
    bool limitReached = false;
    for(TSection section : sections){
        const float length = section.getDlugosc();
        if(length <= 0)
            continue;
        const int segments = section.type == 1
                ? curveSegments(profile, section)
                : roll != nullptr && roll->rolls(offset, offset + length)
                ? rolledStraightSegments(profile, length) : 1;
        for(int i = 1; i <= segments; i++){
            const float distance = offset + length * (float)i / segments;
            if(distance > OrtsTrackProfileRenderer::MaximumGeneratedPathLength){
                if(distances.last()
                        < OrtsTrackProfileRenderer::MaximumGeneratedPathLength)
                    distances.append(
                            OrtsTrackProfileRenderer::MaximumGeneratedPathLength);
                limitReached = true;
                break;
            }
            distances.append(distance);
            if(distances.size() >= MaximumFrames){
                limitReached = true;
                break;
            }
        }
        offset += length;
        if(limitReached)
            break;
    }
    if(!limitReached && endExtension > 0
            && distances.last() + endExtension
                <= OrtsTrackProfileRenderer::MaximumGeneratedPathLength)
        distances.append(distances.last() + endExtension);
    if(limitReached){
        if(truncated != nullptr)
            *truncated = true;
        if(diagnostics != nullptr)
            diagnostics->append(
                    "ORTS profile geometry truncated to the 2048 m safety limit");
    }
    return distances;
}

QVector<float> frameDistances(const ComplexLine &line,
        QStringList *diagnostics, bool *truncated) {
    if(truncated != nullptr)
        *truncated = false;
    QVector<float> distances;
    for(float distance : line.getNodeDistances()){
        if(distance > OrtsTrackProfileRenderer::MaximumGeneratedPathLength){
            if(distances.isEmpty()
                    || distances.last()
                        < OrtsTrackProfileRenderer::MaximumGeneratedPathLength)
                distances.append(
                        OrtsTrackProfileRenderer::MaximumGeneratedPathLength);
            if(truncated != nullptr)
                *truncated = true;
            if(diagnostics != nullptr)
                diagnostics->append(
                        "ORTS profile point path truncated to the 2048 m safety limit");
            break;
        }
        distances.append(distance);
        if(distances.size() >= MaximumFrames){
            if(truncated != nullptr)
                *truncated = true;
            if(diagnostics != nullptr)
                diagnostics->append(
                        "ORTS profile point path truncated to the frame safety limit");
            break;
        }
    }
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
    // Superelevation (task tracks 16): the profile rolls about this point
    // (profile X, Y), on the side the curve turns (Open Rails' Direction:
    // -1, 0 none, 1).
    float pivot[2] = {0, 0};
    int side = 0;
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
        const OrtsProfileRoll *roll, float endExtension, float endDrop,
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
    int side = 0;
    const float angle = roll != nullptr ? roll->angle(sampledDistance, &side) : 0.0f;

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
    result.rollCosine = std::cos(angle);
    result.rollSine = std::sin(angle);
    // Open Rails' pivot: the rail that stays (offset to the curve's side),
    // 0.275 m up.
    result.side = side;
    result.pivot[0] = roll != nullptr ? roll->pivot * float(side) : 0.0f;
    result.pivot[1] = OrtsProfileRoll::PivotHeight;
    result.distance = distance;
    return result;
}

// Whether a profile vertex follows the superelevation (Open Rails'
// SuperElevationPrimitive): not with PositionControl None, not an Outside
// vertex on the side the curve turns, not an Inside one on the other.
bool rollsVertex(const OrtsProfileVertex &source, int side) {
    if(side == 0)
        return true;
    const int vertexSide = source.position[0] > 0 ? 1 : source.position[0] < 0 ? -1 : 0;
    switch(source.positionControl){
        case OrtsProfileVertex::PositionControl::None:
            return false;
        case OrtsProfileVertex::PositionControl::Outside:
            return vertexSide != side;
        case OrtsProfileVertex::PositionControl::Inside:
            return vertexSide != -side;
        case OrtsProfileVertex::PositionControl::All:
        default:
            return true;
    }
}

GeneratedVertex transformVertex(const OrtsProfileVertex &source,
        const OrtsProfilePolyline &polyline,
        const GeneratedPathFrame &frame, float alpha) {
    // Rolled about the frame's pivot (task tracks 16), or not at all.
    const bool rolls = rollsVertex(source, frame.side);
    const float cosine = rolls ? frame.rollCosine : 1.0f;
    const float sine = rolls ? frame.rollSine : 0.0f;
    const float x = source.position[0] - frame.pivot[0];
    const float y = source.position[1] - frame.pivot[1];
    const float rolledX = x * cosine - y * sine + frame.pivot[0];
    const float rolledY = x * sine + y * cosine + frame.pivot[1];
    const float rolledNormalX = source.normal[0] * cosine
            - source.normal[1] * sine;
    const float rolledNormalY = source.normal[0] * sine
            + source.normal[1] * cosine;
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
    frame.side = 0;
    frame.pivot[0] = 0;
    frame.pivot[1] = 0;
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

QHash<QString, QSharedPointer<const ObjFile>> &templateMeshCache() {
    static QHash<QString, QSharedPointer<const ObjFile>> cache;
    return cache;
}

QSharedPointer<const ObjFile> templateMesh(const OrtsTrackProfile &profile,
        const QString &shapeName, QStringList *diagnostics) {
    QHash<QString, QSharedPointer<const ObjFile>> &cache = templateMeshCache();
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
    // Rolled about the frame's pivot as a whole (Open Rails'
    // GetFullProfileFrame).
    const float x = source[0] + offset[0] - frame.pivot[0];
    const float y = source[1] + offset[1] - frame.pivot[1];
    const float rolledX = x * frame.rollCosine - y * frame.rollSine + frame.pivot[0];
    const float rolledY = x * frame.rollSine + y * frame.rollCosine + frame.pivot[1];
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
        const OrtsProfileRoll *roll, float endExtension, float endDrop,
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
            roll, endExtension, endDrop,
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
        const OrtsProfileRoll *roll, float endExtension, float endDrop,
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
                    line, distance, roll,
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
        const OrtsProfileRoll *roll, float endExtension, float endDrop,
        const ProceduralPathTransform *pathTransform, int objectIndex,
        QStringList *diagnostics, bool includesPathStart,
        bool includesPathEnd) {
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
        output.lightMaterial = item.lightMaterial();
        output.pbr = profilePbr(item);
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
                        averagedNode, roll,
                        endExtension, endDrop, pathTransform)
                : samplePathFrame(line, distance, roll,
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
                if(span + 1 == placementSpanCount && includesPathEnd)
                    appendOccurrence(spanEnd, copyIndex++,
                            objectIndex + span + 1, reverseFacing,
                            span, true, true);
                continue;
            }
            const bool pathStart = placement.location
                    == OrtsProfileTemplate3D::PlacementLocation::PathStart;
            const bool pathEnd = placement.location
                    == OrtsProfileTemplate3D::PlacementLocation::PathEnd;
            const bool spanEndPlacement = placement.location
                    == OrtsProfileTemplate3D::PlacementLocation::SpanEnd;
            if((pathStart && (!includesPathStart || span != 0))
                    || (pathEnd && (!includesPathEnd
                        || span + 1 != placementSpanCount))
                    || (spanEndPlacement && !includesPathEnd
                        && span + 1 == placementSpanCount))
                continue;
            const bool atEnd = placement.location
                    == OrtsProfileTemplate3D::PlacementLocation::SpanEnd
                    || pathEnd;
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

QVector<float> generatedChunkEnds(float pathLength) {
    QVector<float> ends;
    if(pathLength <= OrtsTrackProfileRenderer::GeneratedChunkingThreshold)
        return ends;
    const int count = std::max(2, (int)std::ceil(
            pathLength
            / OrtsTrackProfileRenderer::GeneratedChunkTargetLength));
    const float balancedLength = pathLength / count;
    ends.reserve(count);
    for(int chunk = 1; chunk <= count; chunk++)
        ends.append(chunk == count
                ? pathLength : balancedLength * chunk);
    return ends;
}

int chunkIndexForDistance(const QVector<float> &chunkEnds,
        float distance) {
    if(chunkEnds.isEmpty())
        return 0;
    const auto next = std::upper_bound(
            chunkEnds.cbegin(), chunkEnds.cend(), distance);
    return std::min((int)(next - chunkEnds.cbegin()),
                    (int)chunkEnds.size() - 1);
}

QVector<float> &verticesForDistance(QVector<float> &fallback,
        QVector<OrtsGeneratedProfileMesh> *chunkMeshes,
        const QVector<float> *chunkEnds, float distance) {
    if(chunkMeshes == nullptr || chunkEnds == nullptr
            || chunkMeshes->isEmpty() || chunkEnds->isEmpty())
        return fallback;
    return (*chunkMeshes)[chunkIndexForDistance(
            *chunkEnds, distance)].vertices;
}

int appendTemplateGeometry(const OrtsTrackProfile &profile,
        const OrtsProfileLodItem &item,
        const OrtsProfileTemplate3D &source, ComplexLine &line,
        const QVector<GeneratedPathFrame> &pathFrames,
        QVector<float> &target, float alpha,
        const OrtsProfileRoll *roll, float endExtension, float endDrop,
        const ProceduralPathTransform *pathTransform, int objectIndex,
        QStringList *diagnostics,
        QVector<OrtsGeneratedProfileSharedMesh> *sharedMeshes,
        float minimumDistance, float maximumDistance,
        int copyIndexBase = 0,
        QVector<OrtsGeneratedProfileMesh> *chunkMeshes = nullptr,
        const QVector<float> *chunkEnds = nullptr,
        // Baked point paths may arrive one span at a time. These flags retain
        // ownership of the complete requested path endpoints in that case.
        bool includesPathStart = true,
        bool includesPathEnd = true) {
    // Tracks task 17: a COMPLEX member's parts are the complex track
    // planner's, never swept along one path.
    if(source.complexPart())
        return copyIndexBase;
    if(source.geometryMode
            == OrtsProfileTemplate3D::GeometryMode::Shared
            && sharedMeshes != nullptr){
        appendSharedTemplateGeometry(profile, item, source, line,
                pathFrames, *sharedMeshes, alpha,
                minimumDistance, maximumDistance,
                roll, endExtension, endDrop,
                pathTransform, objectIndex, diagnostics,
                includesPathStart, includesPathEnd);
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
                QVector<float> &output = verticesForDistance(
                        target, chunkMeshes, chunkEnds,
                        0.5f * (start + end));
                appendDeformedTemplate(output, *mesh, line,
                        start, end,
                        roll, endExtension, endDrop,
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
                            roll, endExtension, endDrop,
                            pathTransform, item.pathFrameMode, false,
                            true,
                            source.offset, alpha);
            }
        } else {
            const QSharedPointer<const ObjFile> mesh = loadShape(0, objectIndex);
            if(mesh != nullptr)
                appendDeformedTemplate(target, *mesh, line,
                        pathFrames.first().distance, pathFrames.last().distance,
                        roll, endExtension, endDrop,
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
                    line, distance, roll,
                    endExtension, endDrop, pathTransform);
            frame = frameForMode(frame, item.pathFrameMode);
            QVector<float> &output = verticesForDistance(
                    target, chunkMeshes, chunkEnds, distance);
            appendRigidTemplate(output, *mesh, frame, source.offset,
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
                            roll, endExtension, endDrop,
                            pathTransform);
                    frame = frameForMode(frame, item.pathFrameMode);
                    QVector<float> &output = verticesForDistance(
                            target, chunkMeshes, chunkEnds, spanStart);
                    appendRigidTemplate(output, *startMesh, frame,
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
                                roll, endExtension, endDrop,
                                pathTransform);
                        frame = frameForMode(frame, item.pathFrameMode);
                        QVector<float> &output = verticesForDistance(
                                target, chunkMeshes, chunkEnds, spanEnd);
                        appendRigidTemplate(output, *endMesh, frame,
                                source.offset, alpha, reverseFacing);
                    }
                }
                continue;
            }
            const bool pathStart = placement.location
                    == OrtsProfileTemplate3D::PlacementLocation::PathStart;
            const bool pathEnd = placement.location
                    == OrtsProfileTemplate3D::PlacementLocation::PathEnd;
            const bool spanEndPlacement = placement.location
                    == OrtsProfileTemplate3D::PlacementLocation::SpanEnd;
            if((pathStart && (!includesPathStart || span != 0))
                    || (pathEnd && (!includesPathEnd
                        || span + 1 != placementSpanCount))
                    || (spanEndPlacement && !includesPathEnd
                        && span + 1 == placementSpanCount))
                continue;
            const bool atEnd = placement.location
                    == OrtsProfileTemplate3D::PlacementLocation::SpanEnd
                    || pathEnd;
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
                    roll, endExtension, endDrop,
                    pathTransform);
            frame = frameForMode(frame, item.pathFrameMode);
            QVector<float> &output = verticesForDistance(
                    target, chunkMeshes, chunkEnds, distance);
            appendRigidTemplate(output, *mesh, frame, source.offset,
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

int profileTextureId(const QString &routePath, const QString &textureName,
        TexLib::Use use = TexLib::Display) {
    // ORTS profiles can emit many LOD/material objects which all reuse a
    // small set of textures. Resolving each new OglObj independently made
    // every live Flex rebuild repeat filesystem probes and linear TexLib
    // scans. Keep one validated TexLib reference per route/texture instead.
    static QHash<QString, int> textureIds;
    static QHash<QString, Texture*> textures;

    QString normalizedName = textureName;
    normalizedName.replace('\\', '/');
    QString normalizedRoute = QDir::cleanPath(routePath);
    const QString cacheKey = ContentPath::key(normalizedRoute) + "\n" + normalizedName.toLower()
            + "\n" + QString::number(int(use));
    const auto cached = textureIds.constFind(cacheKey);
    const Texture *existing = textures.value(cacheKey);
    // A failed texture too: TexLib keeps its id until a reload tries it.
    if(cached != textureIds.cend() && textureIdMatches(cached.value(), existing))
        return cached.value();

    const QString path = ContentPath::textureSource(normalizedTextureId(
            texturePath(routePath, normalizedName)));

    const int textureId = TexLib::addTex(path, false, use);
    textureIds.insert(cacheKey, textureId);
    const auto loaded = TexLib::mtex.find(textureId);
    textures.insert(cacheKey,
            loaded == TexLib::mtex.end() ? nullptr : loaded->second);
    return textureId;
}

// A profile mesh's draw object: its texture, light material and LOD range;
// a PBR item's in the PBR layout with its maps (data textures) and factors,
// as S files draw theirs (task shapes 13).
template<typename Mesh>
OglObj *profileObject(const QString &routePath, const Mesh &mesh) {
    OglObj *object = new OglObj();
    object->setMaterialTextureId(profileTextureId(routePath, mesh.textureName));
    object->setLightMaterial(mesh.lightMaterial);
    object->setDistanceRange(mesh.minimumDistance, mesh.maximumDistance);
    object->setSurface(materialSurface(mesh.materialPass));
    object->setBlendInOrder(mesh.blendInOrder);
    const OrtsGeneratedProfilePbr &pbr = mesh.pbr;
    if(MstsMaterials::pbr(pbr.shader)){
        QVector<float> vertices = OrtsTrackProfileRenderer::pbrVertices(mesh.vertices);
        object->init(vertices.data(), vertices.size(), RenderItem::PBR, GL_TRIANGLES);
        const int orm = pbr.ormTextureName.isEmpty() ? -1
                : profileTextureId(routePath, pbr.ormTextureName, TexLib::Data);
        const int normal = pbr.normalTextureName.isEmpty() ? -1
                : profileTextureId(routePath, pbr.normalTextureName, TexLib::Data);
        object->setPbr(pbr.factors, orm, normal, pbr.shader, pbr.alphaTest);
    } else {
        object->init(const_cast<float*>(mesh.vertices.constData()), mesh.vertices.size(),
                     RenderItem::VNTA, GL_TRIANGLES);
    }
    object->setBound(const_cast<float*>(mesh.bounds));
    return object;
}

}

namespace {

// Tracks task 17: COMPLEX members laid on a ComplexTrackPlan. The plan is in
// MSTS shape space; a frame at one of its points is built as the renderer's
// own static paths are (x mirrored, transformStaticPath), so the template
// and polyline vertex transforms above apply unchanged: their winding,
// normals and texture coordinates.
GeneratedPathFrame complexFrame(float x, float z, float heading, float distance) {
    GeneratedPathFrame frame;
    frame.position[0] = -x;
    frame.position[1] = 0;
    frame.position[2] = z;
    frame.right[0] = std::cos(heading);
    frame.right[1] = 0;
    frame.right[2] = std::sin(heading);
    frame.forward[0] = -std::sin(heading);
    frame.forward[1] = 0;
    frame.forward[2] = std::cos(heading);
    frame.distance = distance;
    return frame;
}

struct MeshExtent {
    float minX = 0, maxX = 0, minZ = 0, maxZ = 0;
};

MeshExtent meshExtent(const ObjFile &mesh) {
    MeshExtent extent;
    extent.minX = extent.minZ = std::numeric_limits<float>::max();
    extent.maxX = extent.maxZ = -std::numeric_limits<float>::max();
    for(int i = 0; i + 7 < mesh.points.size(); i += 8){
        extent.minX = std::min(extent.minX, mesh.points[i]);
        extent.maxX = std::max(extent.maxX, mesh.points[i]);
        extent.minZ = std::min(extent.minZ, mesh.points[i + 2]);
        extent.maxZ = std::max(extent.maxZ, mesh.points[i + 2]);
    }
    return extent;
}

// A mesh's x laid from `a` to `b` (design section 2, StretchZone): x at or
// left of zone[0] keeps its distance from the left end, x right of zone[1]
// from the right end, the zone between spreads.
float stretchedX(float x, const MeshExtent &extent, const float *zone, float a, float b) {
    const float z0 = zone[0];
    const float z1 = zone[1];
    if(x <= z0)
        return a + (x - extent.minX);
    if(x >= z1 && x > z0)
        return b - (extent.maxX - x);
    const float left = a + (z0 - extent.minX);
    const float right = b - (extent.maxX - z1);
    return left + (right - left) * (x - z0) / std::max(1e-6f, z1 - z0);
}

// A tie or bearer mesh laid on a plan's tie, stretched to its length.
void appendTie(QVector<float> &target, const ObjFile &mesh, const OrtsProfileTemplate3D &source,
        const ComplexTrackPlan::Tie &tie, float alpha) {
    const MeshExtent extent = meshExtent(mesh);
    const GeneratedPathFrame frame = complexFrame(tie.centre[0], tie.centre[1], tie.heading, 0);
    const float offset[3] = {0, source.offset[1], source.offset[2]};
    const bool stretch = std::abs(tie.length - (extent.maxX - extent.minX)) > 1e-4f;
    float vertex[8];
    for(int i = 0; i + 7 < mesh.points.size(); i += 8){
        std::copy(mesh.points.constData() + i, mesh.points.constData() + i + 8, vertex);
        if(stretch)
            vertex[0] = stretchedX(vertex[0], extent, source.stretchZone, -tie.length / 2, tie.length / 2);
        const GeneratedVertex placed = transformTemplateVertex(vertex, frame, offset, alpha, false, false);
        appendVertex(target, placed);
    }
}

// A width factor along a line, from its points' values (none: 1).
float scaleAt(const ComplexTrackPlan::Line &line, const QVector<float> &scale, float distance) {
    if(scale.size() != line.size() || scale.isEmpty())
        return 1.0f;
    for(int i = 1; i < line.size(); i++)
        if(line[i].distance >= distance){
            const float span = std::max(1e-6f, line[i].distance - line[i - 1].distance);
            const float t = std::clamp((distance - line[i - 1].distance) / span, 0.0f, 1.0f);
            return scale[i - 1] + t * (scale[i] - scale[i - 1]);
        }
    return scale.last();
}

// A swept mesh (the Rail part, a conductor or rack rail) along a line
// between two distances, one mesh's depth at a time, as Sweep does; its
// x narrowed by `scale` (a switch blade's).
void appendSwept(QVector<float> &target, const ObjFile &mesh, const ComplexTrackPlan::Line &line,
        float from, float to, const float *sourceOffset, float alpha,
        const QVector<float> &scale = QVector<float>()) {
    const MeshExtent extent = meshExtent(mesh);
    const float depth = extent.maxZ - extent.minZ;
    if(depth <= 1e-6f || to - from <= 1e-6f)
        return;
    const float offset[3] = {0, sourceOffset[1], 0};
    for(float start = from; start < to - 1e-6f;){
        const float end = std::min(to, start + depth);
        for(int i = 0; i + 7 < mesh.points.size(); i += 8){
            const float *vertex = mesh.points.constData() + i;
            const float fraction = (extent.maxZ - vertex[2]) / depth;
            const float distance = start + fraction * (end - start);
            const TrackShapeAnalysis::Point p = TrackShapeAnalysis::at(line, distance);
            float narrowed[8];
            std::copy(vertex, vertex + 8, narrowed);
            narrowed[0] *= scaleAt(line, scale, distance);
            GeneratedVertex placed = transformTemplateVertex(narrowed,
                    complexFrame(p.x, p.z, p.heading, distance), offset, alpha, false, true);
            placed.values[7] = mesh.texYmin + (mesh.texYmax - mesh.texYmin) * distance;
            appendVertex(target, placed);
        }
        start = end;
    }
}

// A polyline across a bed between two cross-sections: vertices left of the
// profile's centre follow the leftmost track, right of it the rightmost
// (design section 6), as appendPolylineSpan's faces.
void appendBedSpan(QVector<float> &target, const OrtsProfilePolyline &polyline,
        const ComplexTrackPlan::BedSlice &previous, const ComplexTrackPlan::BedSlice &current,
        float alpha) {
    if(polyline.vertices.size() < 2)
        return;
    auto place = [&](const ComplexTrackPlan::BedSlice &slice) {
        const GeneratedPathFrame frame = complexFrame(slice.position[0], slice.position[1],
                slice.heading, slice.distance);
        QVector<GeneratedVertex> vertices;
        for(OrtsProfileVertex vertex : polyline.vertices){
            const float x = vertex.position[0];
            vertex.position[0] = x < 0 ? x + slice.left : x > 0 ? x + slice.right
                    : (slice.left + slice.right) / 2;
            vertices.append(transformVertex(vertex, polyline, frame, alpha));
        }
        return vertices;
    };
    const QVector<GeneratedVertex> before = place(previous);
    const QVector<GeneratedVertex> after = place(current);
    for(int i = 1; i < after.size(); i++){
        appendVertex(target, after[i]);
        appendVertex(target, after[i - 1]);
        appendVertex(target, before[i - 1]);
        appendVertex(target, after[i]);
        appendVertex(target, before[i - 1]);
        appendVertex(target, before[i]);
    }
}

// A bed polyline: every vertex at or below the rail foot (design section
// 6); others, such as a tunnel's tube, are stretched across the tracks.
bool bedPolyline(const OrtsProfilePolyline &polyline, float railFoot) {
    for(const OrtsProfileVertex &vertex : polyline.vertices)
        if(vertex.position[1] > railFoot + 1e-4f)
            return false;
    return polyline.vertices.size() >= 2;
}

// The bed's height at x (from a track's centre, signed): the highest of the
// bed polylines over x; none over it, the nearest vertex's.
float bedHeight(const QVector<const OrtsProfilePolyline*> &bed, float x) {
    float best = -std::numeric_limits<float>::max();
    float nearestGap = std::numeric_limits<float>::max();
    float nearestY = 0;
    for(const OrtsProfilePolyline *polyline : bed)
        for(int i = 0; i < polyline->vertices.size(); i++){
            const OrtsProfileVertex &b = polyline->vertices[i];
            if(std::abs(b.position[0] - x) < nearestGap){
                nearestGap = std::abs(b.position[0] - x);
                nearestY = b.position[1];
            }
            if(i == 0)
                continue;
            const OrtsProfileVertex &a = polyline->vertices[i - 1];
            const float span = b.position[0] - a.position[0];
            if(std::abs(span) < 1e-6f || (x - a.position[0]) * (x - b.position[0]) > 0)
                continue;
            best = std::max(best, a.position[1] + (b.position[1] - a.position[1]) * (x - a.position[0]) / span);
        }
    return best > -std::numeric_limits<float>::max() ? best : nearestY;
}

OrtsProfileVertex between(const OrtsProfileVertex &a, const OrtsProfileVertex &b, float f) {
    OrtsProfileVertex result = a;
    for(int c = 0; c < 3; c++){
        result.position[c] = a.position[c] + f * (b.position[c] - a.position[c]);
        result.normal[c] = a.normal[c] + f * (b.normal[c] - a.normal[c]);
    }
    const float length = std::sqrt(result.normal[0] * result.normal[0] + result.normal[1] * result.normal[1]
            + result.normal[2] * result.normal[2]);
    if(length > 1e-6f)
        for(float &c : result.normal)
            c /= length;
    for(int c = 0; c < 2; c++)
        result.texCoord[c] = a.texCoord[c] + f * (b.texCoord[c] - a.texCoord[c]);
    return result;
}

// A bed polyline laid on track k of a slice: its own cross-section, each
// side reaching to the midpoint between it and its neighbour there; past
// it a polyline is cut where it crosses the midpoint, or (wholly past it)
// pressed onto the midpoint at the bed's height.
QVector<GeneratedVertex> placeOnTrack(const OrtsProfilePolyline &polyline,
        const QVector<const OrtsProfilePolyline*> &bed, const ComplexTrackPlan::BedSlice &slice,
        int k, float alpha) {
    const int count = slice.tracks.size();
    const float offset = count == 0 ? slice.left : slice.tracks[k];
    const float open = std::numeric_limits<float>::max();
    const float leftLimit = k > 0 && k < count ? (offset - slice.tracks[k - 1]) / 2 : open;
    const float rightLimit = k + 1 < count ? (slice.tracks[k + 1] - offset) / 2 : open;
    // At a track's end its own part of the cross-section lies on its own
    // end plane (decision 40), so the next piece's bed on that track meets
    // it; what is cut or pressed at a midpoint stays on the slice, where
    // the neighbour's edge is.
    const ComplexTrackPlan::BedSlice::TrackEnd end = slice.trackEnds.value(k);
    const GeneratedPathFrame sliceFrame = complexFrame(slice.position[0], slice.position[1], slice.heading,
            slice.distance);
    const GeneratedPathFrame endFrame = end.valid
            ? complexFrame(end.position[0], end.position[1], end.heading, slice.distance) : sliceFrame;
    QVector<GeneratedVertex> vertices;
    const QVector<OrtsProfileVertex> &source = polyline.vertices;
    for(int i = 0; i < source.size(); i++){
        OrtsProfileVertex vertex = source[i];
        const float x = vertex.position[0];
        const float limit = x < 0 ? leftLimit : rightLimit;
        const bool clamped = std::abs(x) > limit;
        if(clamped){
            const float edge = x < 0 ? -limit : limit;
            bool cut = false;
            for(int j : {i - 1, i + 1}){
                if(j < 0 || j >= source.size())
                    continue;
                const float w = source[j].position[0];
                if((w - edge) * (x - edge) <= 0 && std::abs(x - w) > 1e-6f){
                    vertex = between(source[j], source[i], (edge - w) / (x - w));
                    cut = true;
                    break;
                }
            }
            if(!cut){
                vertex.position[0] = edge;
                vertex.position[1] = bedHeight(bed, edge);
            }
        }
        if(end.valid && !clamped){
            vertices.append(transformVertex(vertex, polyline, endFrame, alpha));
            continue;
        }
        vertex.position[0] += offset;
        vertices.append(transformVertex(vertex, polyline, sliceFrame, alpha));
    }
    return vertices;
}

// A bed polyline between two cross-sections, per track: with as many
// tracks on both, each to the one in its place across (crossing paths swap
// places, their strips don't); else each to the same path's (a path on one
// only lays none there).
void appendBedTracks(QVector<float> &target, const OrtsProfilePolyline &polyline,
        const QVector<const OrtsProfilePolyline*> &bed, const ComplexTrackPlan::BedSlice &previous,
        const ComplexTrackPlan::BedSlice &current, float alpha) {
    const int count = std::max(1, int(current.tracks.size()));
    const bool inPlace = current.tracks.size() == previous.tracks.size();
    for(int kc = 0; kc < count; kc++){
        const int kp = inPlace ? kc : previous.trackPaths.indexOf(current.trackPaths.value(kc, -2));
        if(kp < 0)
            continue;
        const QVector<GeneratedVertex> before = placeOnTrack(polyline, bed, previous, kp, alpha);
        const QVector<GeneratedVertex> after = placeOnTrack(polyline, bed, current, kc, alpha);
        for(int i = 1; i < after.size(); i++){
            appendVertex(target, after[i]);
            appendVertex(target, after[i - 1]);
            appendVertex(target, before[i - 1]);
            appendVertex(target, after[i]);
            appendVertex(target, before[i - 1]);
            appendVertex(target, before[i]);
        }
    }
}

// A polyline along a bed: bed polylines per track (bed holds the item's),
// others stretched across.
void appendBed(QVector<float> &target, const OrtsProfilePolyline &polyline,
        const QVector<ComplexTrackPlan::BedSlice> &slices, float alpha,
        const QVector<const OrtsProfilePolyline*> &bed) {
    const bool perTrack = bed.contains(&polyline);
    for(int i = 1; i < slices.size(); i++){
        if(perTrack)
            appendBedTracks(target, polyline, bed, slices[i - 1], slices[i], alpha);
        else
            appendBedSpan(target, polyline, slices[i - 1], slices[i], alpha);
    }
}

// A SwitchDrive or SwitchIndicator template as the plan places it (step 7):
// its Offset, Spacing (drives only) and mesh extents.
ComplexTrackPlan::SwitchPart switchPart(const OrtsProfileTemplate3D &source, const ObjFile &mesh) {
    ComplexTrackPlan::SwitchPart part;
    std::copy(source.offset, source.offset + 3, part.offset);
    part.spacing = source.generationMode == OrtsProfileTemplate3D::GenerationMode::SwitchDrive
            ? source.spacing : 0;
    const MeshExtent extent = meshExtent(mesh);
    part.minX = extent.minX;
    part.maxX = extent.maxX;
    part.minZ = extent.minZ;
    part.maxZ = extent.maxZ;
    return part;
}

const OrtsProfileTemplate3D *firstTemplate(const OrtsTrackProfile &profile,
        OrtsProfileTemplate3D::GenerationMode mode) {
    for(const OrtsProfileLod &lod : profile.lods)
        for(const OrtsProfileLodItem &item : lod.items)
            for(const OrtsProfileTemplate3D &source : item.templates3D)
                if(source.generationMode == mode && !source.shapes.isEmpty())
                    return &source;
    return nullptr;
}

}

ComplexTrackPlan::Settings OrtsTrackProfileRenderer::complexSettings(
        const OrtsTrackProfile &member, QStringList *diagnostics) {
    ComplexTrackPlan::Settings settings;
    settings.gauge = member.trackGauge;
    settings.tieSpacing = member.complexTrack.tieSpacing;
    settings.tiePhase = member.complexTrack.tiePhase;
    settings.tieGap = member.complexTrack.tieGap;
    settings.maxBearerLength = member.complexTrack.maxBearerLength;
    settings.maxSharedAngle = member.complexTrack.maxSharedAngle;
    settings.conductorClearance = member.complexTrack.conductorClearance;
    settings.flangeway = member.complexTrack.flangeway;
    settings.bladeOpening = member.complexTrack.bladeOpening;
    settings.checkRailLength = member.complexTrack.checkRailLength;
    if(const OrtsProfileTemplate3D *rail = firstTemplate(member, OrtsProfileTemplate3D::GenerationMode::Rail))
        settings.railOffset = rail->offset[0];
    if(const OrtsProfileTemplate3D *tie = firstTemplate(member, OrtsProfileTemplate3D::GenerationMode::Tie)){
        const QSharedPointer<const ObjFile> mesh = templateMesh(member, tie->shapes.first(), diagnostics);
        if(mesh != nullptr){
            const MeshExtent extent = meshExtent(*mesh);
            settings.tieLength = extent.maxX - extent.minX;
            settings.tieWidth = extent.maxZ - extent.minZ;
        }
    }
    // Step 7: the drives of the nearest LOD having any, motors and
    // hand-operated ones apart, for the ties under them.
    for(bool hand : {false, true}){
        QVector<ComplexTrackPlan::SwitchPart> &drives = hand ? settings.handDrives : settings.drives;
        for(const OrtsProfileLod &lod : member.lods){
            for(const OrtsProfileLodItem &item : lod.items)
                for(const OrtsProfileTemplate3D &source : item.templates3D){
                    if(source.generationMode != OrtsProfileTemplate3D::GenerationMode::SwitchDrive
                            || source.handOperated != hand || source.shapes.isEmpty())
                        continue;
                    const QSharedPointer<const ObjFile> mesh = templateMesh(member, source.shapes.first(), diagnostics);
                    if(mesh != nullptr)
                        drives.append(switchPart(source, *mesh));
                }
            if(!drives.isEmpty())
                break;
        }
    }
    return settings;
}

bool OrtsTrackProfileRenderer::buildComplexMeshes(const OrtsTrackProfile &member,
        const ComplexTrackPlan &plan, QVector<OrtsGeneratedProfileMesh> &meshes,
        QStringList *diagnostics) {
    meshes.clear();
    if(!member.valid || plan.groups.isEmpty())
        return false;
    using Mode = OrtsProfileTemplate3D::GenerationMode;
    // The rail foot (the Rail part's Offset y): polylines at or below it
    // are the bed.
    const OrtsProfileTemplate3D *railPart = firstTemplate(member, Mode::Rail);
    const float railFoot = railPart != nullptr ? railPart->offset[1] : std::numeric_limits<float>::max();
    float previousCutoff = -1;
    for(const OrtsProfileLod &lod : member.lods){
        // Bearers by the Bearer mesh where this LOD has one, else the Tie
        // mesh stretched (decision 5).
        bool bearerMesh = false;
        // Check rails by the CheckRail mesh where this LOD has one, else
        // the rail section.
        bool checkRailMesh = false;
        for(const OrtsProfileLodItem &item : lod.items)
            for(const OrtsProfileTemplate3D &source : item.templates3D){
                bearerMesh = bearerMesh || source.generationMode == Mode::Bearer;
                checkRailMesh = checkRailMesh || source.generationMode == Mode::CheckRail;
            }
        for(const OrtsProfileLodItem &item : lod.items){
            OrtsGeneratedProfileMesh mesh;
            mesh.textureName = item.textureName;
            mesh.materialPass = materialPass(item);
            mesh.blendInOrder = blendsInOrder(member, item);
            mesh.lightMaterial = item.lightMaterial();
            mesh.pbr = profilePbr(item);
            if(member.lodMethod == OrtsTrackProfile::LodMethod::CompleteReplacement)
                mesh.minimumDistance = previousCutoff;
            mesh.maximumDistance = lod.cutoffRadius;
            const float alpha = materialAlpha(item);

            // The item's bed polylines (at or below the rail foot) are laid
            // per track (design section 6).
            QVector<const OrtsProfilePolyline*> bed;
            for(const OrtsProfilePolyline &polyline : item.polylines)
                if(bedPolyline(polyline, railFoot))
                    bed.append(&polyline);
            for(const OrtsProfilePolyline &polyline : item.polylines)
                for(const ComplexTrackPlan::Group &group : plan.groups){
                    appendBed(mesh.vertices, polyline, group.bed, alpha, bed);
                    for(const QVector<ComplexTrackPlan::BedSlice> &slices : group.pathBeds)
                        appendBed(mesh.vertices, polyline, slices, alpha, bed);
                }

            for(const OrtsProfileTemplate3D &source : item.templates3D){
                if(!source.complexPart())
                    continue;
                const QSharedPointer<const ObjFile> part = templateMesh(member, source.shapes.first(), diagnostics);
                if(part == nullptr)
                    continue;
                switch(source.generationMode){
                case Mode::Rail:
                case Mode::CheckRail:
                    // Running and stock rails, blades, wing rails; check
                    // rails here without a CheckRail mesh.
                    for(const ComplexTrackPlan::RailPiece &piece : plan.railPieces){
                        const bool check = piece.kind == ComplexTrackPlan::RailPiece::Kind::CheckRail;
                        if(source.generationMode == Mode::CheckRail ? !check : (check && checkRailMesh))
                            continue;
                        appendSwept(mesh.vertices, *part, piece.line, piece.from, piece.to,
                                source.offset, alpha, piece.scale);
                    }
                    break;
                case Mode::ConductorRail:
                case Mode::RackRail: {
                    const unsigned bit = source.generationMode == Mode::ConductorRail
                            ? TrackProfileFilters::ConductorRail : TrackProfileFilters::RackRail;
                    for(const ComplexTrackPlan::RailLine &rail : plan.conductorLines(source.offset[0], bit))
                        for(const ComplexTrackPlan::Range &range : rail.drawn)
                            appendSwept(mesh.vertices, *part, rail.line, range.first, range.second,
                                    source.offset, alpha);
                    break;
                }
                case Mode::Tie:
                case Mode::Bearer:
                    for(const ComplexTrackPlan::Group &group : plan.groups)
                        for(const ComplexTrackPlan::Tie &tie : group.ties){
                            const bool bearer = tie.kind == ComplexTrackPlan::Tie::Kind::Bearer;
                            if(source.generationMode == Mode::Bearer ? !bearer : (bearer && bearerMesh))
                                continue;
                            appendTie(mesh.vertices, *part, source, tie, alpha);
                        }
                    break;
                case Mode::SwitchDrive:
                case Mode::SwitchIndicator: {
                    // Step 7: at every switch whose main path lays the
                    // part; a hand-operated switch's drives are the marked
                    // templates, others' the unmarked (decision 38). The
                    // indicator shows the main route (its first Shape)
                    // until the junction-state task.
                    const bool drive = source.generationMode == Mode::SwitchDrive;
                    if(drive && source.handOperated != plan.settings.handOperated)
                        break;
                    const unsigned bit = drive ? TrackProfileFilters::SwitchDrive
                                               : TrackProfileFilters::SwitchIndicator;
                    const ComplexTrackPlan::SwitchPart placed = switchPart(source, *part);
                    const float offset[3] = {std::abs(source.offset[0]), source.offset[1], 0};
                    for(const ComplexTrackPlan::Switch &sw : plan.switches){
                        if((plan.paths[sw.main].parts & bit) == 0)
                            continue;
                        for(const ComplexTrackPlan::SwitchPlacement &at : plan.switchPlacements(sw, placed))
                            appendRigidTemplate(mesh.vertices, *part,
                                    complexFrame(at.position[0], at.position[1], at.heading, 0),
                                    offset, alpha, at.side < 0);
                    }
                    break;
                }
                case Mode::Fastener: {
                    const float offset[3] = {0, source.offset[1], source.offset[2]};
                    for(const ComplexTrackPlan::Group &group : plan.groups)
                        for(const ComplexTrackPlan::Fastener &fastener : group.fasteners)
                            appendRigidTemplate(mesh.vertices, *part,
                                    complexFrame(fastener.position[0], fastener.position[1],
                                                 fastener.heading, 0),
                                    offset, alpha, false);
                    break;
                }
                default:
                    break;
                }
            }
            if(mesh.vertices.isEmpty())
                continue;
            updateBounds(mesh);
            meshes.append(mesh);
        }
        previousCutoff = lod.cutoffRadius;
    }
    return !meshes.isEmpty();
}

bool OrtsTrackProfileRenderer::generateComplex(const OrtsTrackProfile &member,
        const ComplexTrackPlan &plan, QVector<OglObj*> &objects, const QString &routePath,
        QStringList *diagnostics) {
    QVector<OrtsGeneratedProfileMesh> meshes;
    if(!buildComplexMeshes(member, plan, meshes, diagnostics))
        return false;
    const OrtsGeneratedProfileMesh::MaterialPass passes[] = {
        OrtsGeneratedProfileMesh::MaterialPass::Opaque,
        OrtsGeneratedProfileMesh::MaterialPass::AlphaTest,
        OrtsGeneratedProfileMesh::MaterialPass::Blended
    };
    for(OrtsGeneratedProfileMesh::MaterialPass pass : passes)
        for(const OrtsGeneratedProfileMesh &mesh : meshes)
            if(mesh.materialPass == pass)
                objects.append(profileObject(routePath, mesh));
    return true;
}

void OrtsTrackProfileRenderer::clearTemplateMeshCache() {
    templateMeshCache().clear();
}

QVector<float> OrtsTrackProfileRenderer::pbrVertices(const QVector<float> &vertices) {
    const int stride = RenderItem::VNTA;
    const int count = vertices.size() / stride;
    std::vector<int> triangles(size_t(count - count % 3));
    std::iota(triangles.begin(), triangles.end(), 0);
    auto source = [&](int vertex, float *position, float *normal, float *uv) {
        const float *v = vertices.constData() + size_t(vertex) * stride;
        std::copy_n(v, 3, position);
        std::copy_n(v + 3, 3, normal);
        std::copy_n(v + 6, 2, uv);
        return true;
    };
    std::vector<float> tangents;
    MstsMaterials::tangents(count, source, triangles, tangents);
    QVector<float> out(count * RenderItem::PBR, 0.0f);
    for(int vertex = 0; vertex < count; ++vertex){
        const float *v = vertices.constData() + size_t(vertex) * stride;
        float *p = out.data() + size_t(vertex) * RenderItem::PBR;
        std::copy_n(v, 8, p);
        p[8] = 1.0f;
        std::copy_n(&tangents[size_t(vertex) * 4], 4, p + 9);
        p[13] = v[6];
        p[14] = v[7];
        std::fill(p + 15, p + 19, 1.0f);
    }
    return out;
}

float OrtsTrackProfileRenderer::generatedPartLod(OglObj *object,
        const float *objectRotation, float objectX, float objectZ) {
    if(object == nullptr)
        return 0;
    float bounds[6];
    if(!object->getSimpleBorder(bounds))
        return std::hypot(objectX, objectZ);
    const float localCenter[3] = {
        (bounds[0] + bounds[1]) * 0.5f,
        (bounds[2] + bounds[3]) * 0.5f,
        (bounds[4] + bounds[5]) * 0.5f
    };
    float centerX = localCenter[0];
    float centerZ = localCenter[2];
    if(objectRotation != nullptr){
        centerX = objectRotation[0] * localCenter[0]
                + objectRotation[4] * localCenter[1]
                + objectRotation[8] * localCenter[2];
        centerZ = objectRotation[2] * localCenter[0]
                + objectRotation[6] * localCenter[1]
                + objectRotation[10] * localCenter[2];
    }
    const float halfX = (bounds[0] - bounds[1]) * 0.5f;
    const float halfY = (bounds[2] - bounds[3]) * 0.5f;
    const float halfZ = (bounds[4] - bounds[5]) * 0.5f;
    const float radius = std::sqrt(
            halfX * halfX + halfY * halfY + halfZ * halfZ);
    return std::max(0.0f,
            std::hypot(objectX + centerX, objectZ + centerZ) - radius);
}

static bool buildMeshesForLine(const OrtsTrackProfile &profile,
        ComplexLine &line, const QVector<float> &distances,
        QVector<OrtsGeneratedProfileMesh> &meshes,
        const OrtsProfileRoll *roll, QStringList *diagnostics,
        float endExtension = 0, float endDrop = 0,
        const ProceduralPathTransform *pathTransform = nullptr,
        int objectIndex = 0,
        QVector<OrtsGeneratedProfileSharedMesh> *sharedMeshes = nullptr,
        bool includesPathEnd = true) {
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
                line, distance, roll,
                endExtension, endDrop, pathTransform));
    const float chunkableLength = std::min(
            line.length, pathFrames.last().distance);
    const QVector<float> chunkEnds = generatedChunkEnds(chunkableLength);

    float previousCutoff = -1;
    QSet<QString> materialDiagnostics;
    for(const OrtsProfileLod &lod : profile.lods){
        for(const OrtsProfileLodItem &item : lod.items){
            bool upgraded = false;
            const QString shader = item.shader(&upgraded);
            if(upgraded)
                materialDiagnostics.insert("ShaderName " + item.shaderName
                                           + " with TSREOrmTexName, TSRENormalTexName and "
                                           "ESD_TSREMaterial drawn as " + shader);
            if(!item.shaderName.isEmpty()
                    && item.shaderName.compare("TexDiff", Qt::CaseInsensitive) != 0
                    && item.shaderName.compare("BlendATexDiff", Qt::CaseInsensitive) != 0
                    && !MstsMaterials::pbr(MstsMaterials::shader(shader)))
                materialDiagnostics.insert("ShaderName " + item.shaderName
                                           + " mapped to TSRE textured material");
            bool knownLightModel = true;
            item.lightMaterial(&knownLightModel);
            if(!knownLightModel)
                materialDiagnostics.insert("LightModelName " + item.lightModelName
                                           + " is unknown; using normal lighting");
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
            mesh.blendInOrder = blendsInOrder(profile, item);
            mesh.lightMaterial = item.lightMaterial();
            mesh.pbr = profilePbr(item);
            const float alpha = materialAlpha(item);
            if(profile.lodMethod == OrtsTrackProfile::LodMethod::CompleteReplacement)
                mesh.minimumDistance = previousCutoff;
            mesh.maximumDistance = lod.cutoffRadius;

            QVector<GeneratedPathFrame> itemPathFrames = pathFrames;
            for(GeneratedPathFrame &frame : itemPathFrames)
                frame = frameForMode(frame, item.pathFrameMode);

            QVector<GeneratedPathFrame> polylineFrames;
            if(!chunkEnds.isEmpty()){
                polylineFrames = itemPathFrames;
                for(int chunk = 0; chunk + 1 < chunkEnds.size(); chunk++){
                    const float boundary = chunkEnds[chunk];
                    bool exists = false;
                    for(const GeneratedPathFrame &frame : polylineFrames){
                        if(std::abs(frame.distance - boundary) < 0.0001f){
                            exists = true;
                            break;
                        }
                    }
                    if(exists)
                        continue;
                    GeneratedPathFrame frame = samplePathFrame(
                            line, boundary, roll,
                            endExtension, endDrop, pathTransform);
                    polylineFrames.append(frameForMode(
                            frame, item.pathFrameMode));
                }
                std::sort(polylineFrames.begin(), polylineFrames.end(),
                        [](const GeneratedPathFrame &left,
                           const GeneratedPathFrame &right) {
                    return left.distance < right.distance;
                });
            }

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

            if(!chunkEnds.isEmpty()){
                // Chunking is a render-storage policy, not path editing.
                // Continuous polylines receive exact boundary frames, while
                // complete template occurrences are assigned to one part.
                // Stretch is emitted separately below so its authored span
                // and UV mapping are never changed by the LOD policy.
                QVector<OrtsGeneratedProfileMesh> chunkMeshes;
                chunkMeshes.resize(chunkEnds.size());
                for(OrtsGeneratedProfileMesh &chunkMesh : chunkMeshes){
                    chunkMesh.textureName = mesh.textureName;
                    chunkMesh.materialPass = mesh.materialPass;
                    chunkMesh.blendInOrder = mesh.blendInOrder;
                    chunkMesh.lightMaterial = mesh.lightMaterial;
                    chunkMesh.pbr = mesh.pbr;
                    chunkMesh.minimumDistance = mesh.minimumDistance;
                    chunkMesh.maximumDistance = mesh.maximumDistance;
                }
                for(int frameIndex = 1;
                        frameIndex < polylineFrames.size(); frameIndex++){
                    const GeneratedPathFrame &previousFrame =
                            polylineFrames[frameIndex - 1];
                    const GeneratedPathFrame &currentFrame =
                            polylineFrames[frameIndex];
                    OrtsGeneratedProfileMesh &chunkMesh = chunkMeshes[
                            chunkIndexForDistance(chunkEnds,
                                0.5f * (previousFrame.distance
                                        + currentFrame.distance))];
                    for(const OrtsProfilePolyline &polyline : item.polylines)
                        appendPolylineSpan(chunkMesh, polyline,
                                previousFrame, currentFrame);
                }

                QVector<OrtsGeneratedProfileMesh> stretchMeshes;
                auto initializeStretchMeshes = [&]() {
                    if(!stretchMeshes.isEmpty())
                        return;
                    stretchMeshes.resize(line.isPointPath()
                            ? std::max(0,
                                (int)itemPathFrames.size() - 1)
                            : 1);
                    for(OrtsGeneratedProfileMesh &stretchMesh
                            : stretchMeshes){
                        stretchMesh.textureName = mesh.textureName;
                        stretchMesh.materialPass = mesh.materialPass;
                        stretchMesh.blendInOrder = mesh.blendInOrder;
                        stretchMesh.lightMaterial = mesh.lightMaterial;
                        stretchMesh.pbr = mesh.pbr;
                        stretchMesh.minimumDistance = mesh.minimumDistance;
                        stretchMesh.maximumDistance = mesh.maximumDistance;
                    }
                };

                for(const OrtsProfileTemplate3D &template3D
                        : item.templates3D){
                    if(template3D.geometryMode
                            == OrtsProfileTemplate3D::GeometryMode::Shared
                            && sharedMeshes != nullptr){
                        appendTemplateGeometry(profile, item, template3D, line,
                                itemPathFrames, mesh.vertices, alpha,
                                roll, endExtension, endDrop,
                                pathTransform, objectIndex, diagnostics,
                                sharedMeshes, mesh.minimumDistance,
                                mesh.maximumDistance, 0,
                                nullptr, nullptr, true, includesPathEnd);
                        continue;
                    }
                    if(template3D.generationMode
                            == OrtsProfileTemplate3D::GenerationMode::Stretch){
                        initializeStretchMeshes();
                        if(line.isPointPath()){
                            int copyIndex = 0;
                            for(int span = 0;
                                    span < stretchMeshes.size(); span++){
                                const QVector<GeneratedPathFrame> spanFrames = {
                                    itemPathFrames[span],
                                    itemPathFrames[span + 1]
                                };
                                copyIndex = appendTemplateGeometry(
                                        profile, item, template3D, line,
                                        spanFrames,
                                        stretchMeshes[span].vertices, alpha,
                                        roll,
                                        endExtension, endDrop,
                                        pathTransform, objectIndex + span,
                                        diagnostics, nullptr,
                                        mesh.minimumDistance,
                                        mesh.maximumDistance, copyIndex);
                            }
                        } else {
                            appendTemplateGeometry(
                                    profile, item, template3D, line,
                                    itemPathFrames,
                                    stretchMeshes[0].vertices, alpha,
                                    roll,
                                    endExtension, endDrop,
                                    pathTransform, objectIndex,
                                    diagnostics, nullptr,
                                    mesh.minimumDistance,
                                    mesh.maximumDistance);
                        }
                        continue;
                    }

                    if(line.isPointPath()){
                        int copyIndex = 0;
                        for(int span = 0;
                                span + 1 < itemPathFrames.size(); span++){
                            QVector<GeneratedPathFrame> spanFrames = {
                                itemPathFrames[span], itemPathFrames[span + 1]
                            };
                            if(template3D.generationMode
                                    == OrtsProfileTemplate3D::GenerationMode::Repeat
                                    && span + 2 < itemPathFrames.size())
                                spanFrames[1].distance -= 0.0002f;
                            copyIndex = appendTemplateGeometry(
                                    profile, item, template3D, line,
                                    spanFrames, mesh.vertices, alpha,
                                    roll,
                                    endExtension, endDrop,
                                    pathTransform, objectIndex + span,
                                    diagnostics, nullptr,
                                    mesh.minimumDistance,
                                    mesh.maximumDistance, copyIndex,
                                    &chunkMeshes, &chunkEnds,
                                    span == 0,
                                    includesPathEnd
                                        && span + 2 == itemPathFrames.size());
                        }
                    } else {
                        appendTemplateGeometry(
                                profile, item, template3D, line,
                                itemPathFrames, mesh.vertices, alpha,
                                roll, endExtension, endDrop,
                                pathTransform, objectIndex,
                                diagnostics, nullptr,
                                mesh.minimumDistance,
                                mesh.maximumDistance, 0,
                                &chunkMeshes, &chunkEnds,
                                true, includesPathEnd);
                    }
                }

                for(OrtsGeneratedProfileMesh &chunkMesh : chunkMeshes){
                    if(chunkMesh.vertices.isEmpty())
                        continue;
                    updateBounds(chunkMesh);
                    meshes.append(chunkMesh);
                }
                for(OrtsGeneratedProfileMesh &stretchMesh : stretchMeshes){
                    if(stretchMesh.vertices.isEmpty())
                        continue;
                    updateBounds(stretchMesh);
                    meshes.append(stretchMesh);
                }
            } else if(line.isPointPath()){
                QVector<OrtsGeneratedProfileMesh> spanMeshes;
                spanMeshes.resize(std::max(
                        0, (int)itemPathFrames.size() - 1));
                for(OrtsGeneratedProfileMesh &spanMesh : spanMeshes){
                    spanMesh.textureName = mesh.textureName;
                    spanMesh.materialPass = mesh.materialPass;
                    spanMesh.blendInOrder = mesh.blendInOrder;
                    spanMesh.lightMaterial = mesh.lightMaterial;
                    spanMesh.pbr = mesh.pbr;
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
                                roll, endExtension, endDrop,
                                pathTransform, objectIndex, diagnostics,
                                sharedMeshes, mesh.minimumDistance,
                                mesh.maximumDistance, 0,
                                nullptr, nullptr, true, includesPathEnd);
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
                                roll, endExtension, endDrop,
                                pathTransform, objectIndex + span, diagnostics,
                                nullptr, mesh.minimumDistance,
                                mesh.maximumDistance, copyIndex,
                                nullptr, nullptr,
                                span == 0,
                                includesPathEnd
                                    && span + 1 == spanMeshes.size());
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
                            roll, endExtension, endDrop,
                            pathTransform, objectIndex, diagnostics,
                            sharedMeshes, mesh.minimumDistance,
                            mesh.maximumDistance, 0,
                            nullptr, nullptr, true, includesPathEnd);
                if(!mesh.vertices.isEmpty()){
                    updateBounds(mesh);
                    meshes.append(mesh);
                }
            }
        }
        previousCutoff = lod.cutoffRadius;
    }

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
        const OrtsProfileRoll *roll, QStringList *diagnostics,
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
    bool truncated = false;
    // A profile without superelevation never rolls (Open Rails'
    // SuperElevationMethod None).
    if(roll != nullptr && (roll->empty()
            || profile.superElevationMethod == OrtsTrackProfile::SuperElevationMethod::None))
        roll = nullptr;
    const QVector<float> distances = frameDistances(
            profile, sections, endExtension, diagnostics, &truncated, roll);
    return buildMeshesForLine(profile, line, distances, meshes,
            roll, diagnostics, endExtension, endDrop,
            pathTransform, objectIndex, nullptr, !truncated);
}

bool OrtsTrackProfileRenderer::buildMeshes(const OrtsTrackProfile &profile,
        const QVector<TSection> &sections,
        QVector<OrtsGeneratedProfileMesh> &meshes,
        QStringList *diagnostics, float endExtension, float endDrop,
        const ProceduralPathTransform *pathTransform, int objectIndex,
        const OrtsProfileRoll *roll) {
    return buildMeshesForPath(
            profile, sections, meshes, roll, diagnostics,
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
    bool truncated = false;
    const QVector<float> distances = frameDistances(
            line, diagnostics, &truncated);
    return buildMeshesForLine(profile, line, distances, meshes,
            nullptr, diagnostics, 0, 0, nullptr, objectIndex, sharedMeshes,
            !truncated);
}

bool OrtsTrackProfileRenderer::generate(const OrtsTrackProfile &profile,
        const QVector<TSection> &sections, QVector<OglObj*> &shape,
        const QString &routePath, QStringList *diagnostics,
        float endExtension, float endDrop,
        const ProceduralPathTransform *pathTransform, int objectIndex,
        const OrtsProfileRoll *roll) {
    QVector<OrtsGeneratedProfileMesh> meshes;
    OrtsProfileRoll pivoted;
    if(roll != nullptr){
        pivoted = *roll;
        pivoted.pivot = OrtsProfileRoll::pivotOf(profile);
    }
    if(!buildMeshes(profile, sections, meshes, diagnostics,
            endExtension, endDrop, pathTransform, objectIndex,
            roll != nullptr ? &pivoted : nullptr))
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
            generated.append(profileObject(routePath, mesh));
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
            generated.append(profileObject(routePath, mesh));
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
            generated.append(profileObject(routePath, mesh));
        }
        for(const OrtsGeneratedProfileSharedMesh &mesh : sharedMeshes){
            if(mesh.materialPass != pass || mesh.transforms.isEmpty())
                continue;
            OrtsGeneratedProfileInstanceObject instance;
            instance.object = profileObject(routePath, mesh);
            instance.transforms = mesh.transforms;
            instances.append(instance);
        }
    }
    shape.append(generated);
    return !generated.isEmpty() || !instances.isEmpty();
}

bool OrtsTrackProfileRenderer::generate(const OrtsTrackProfile &profile,
        const TrackShape &trackShape, const QVector<OrtsProfileRoll> &pathRolls,
        QVector<OglObj*> &shape, const QString &routePath,
        QStringList *diagnostics, float endExtension, float endDrop,
        const ProceduralPathTransform *pathTransform,
        const QVector<QSharedPointer<const OrtsTrackProfile>> *pathProfiles,
        int objectIndex, const TSectionDAT *tsection) {
    if(tsection == nullptr && Game::currentRoute != nullptr)
        tsection = Game::currentRoute->tsection;
    if(tsection == nullptr)
        return false;
    // Every path rolls about the pivot of the object's own profile (Open
    // Rails' DecomposeStaticSuperElevation).
    const float pivot = OrtsProfileRoll::pivotOf(profile);

    QVector<OrtsGeneratedProfileMesh> meshes;
    for(int pathIndex = 0; pathIndex < trackShape.numpaths; pathIndex++){
        const TrackShape::SectionIdx &path = trackShape.path[pathIndex];
        QVector<TSection> sections;
        for(int sectionIndex = 0; sectionIndex < path.n; sectionIndex++){
            const auto found = tsection->sekcja.find(
                    (int)path.sect[sectionIndex]);
            if(found != tsection->sekcja.end()
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
        OrtsProfileRoll roll;
        if(pathIndex < pathRolls.size()){
            roll = pathRolls[pathIndex];
            roll.pivot = pivot;
        }
        const OrtsProfileRoll *pathRoll = roll.empty() ? nullptr : &roll;
        if(!buildMeshesForPath(*activeProfile, sections, pathMeshes,
                pathRoll, diagnostics,
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
            generated.append(profileObject(routePath, mesh));
        }
    }
    shape.append(generated);
    return !generated.isEmpty();
}
