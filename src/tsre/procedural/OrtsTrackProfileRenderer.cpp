/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/procedural/OrtsTrackProfileRenderer.h>

#include <tsre/ogl/OglObj.h>
#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/procedural/ComplexLine.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/tdb/TrackShape.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/world/Route.h>

#include <QDir>
#include <QFileInfo>
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

struct GeneratedVertex {
    float values[9];
};

GeneratedVertex transformVertex(const OrtsProfileVertex &source,
        const OrtsProfilePolyline &polyline, ComplexLine &line, float distance,
        float alpha, float startRoll = 0, float endRoll = 0,
        float endExtension = 0, float endDrop = 0) {
    float frame[6] = {0, 0, 0, 0, 0, 0};
    const float sampledDistance = std::min(distance, line.length);
    line.getDrawPosition(frame, sampledDistance);
    const float yaw = frame[4];
    const float overflow = std::max(0.0f, distance - line.length);
    if(overflow > 0){
        frame[0] += std::sin(yaw) * overflow;
        frame[2] += std::cos(yaw) * overflow;
        if(endExtension > 0)
            frame[1] -= endDrop * overflow / endExtension;
    }
    const float cosine = std::cos(yaw);
    const float sine = std::sin(yaw);
    const float fraction = line.length > 0 ? sampledDistance / line.length : 0;
    const float roll = startRoll * (1.0f - fraction) + endRoll * fraction;
    const float rollCosine = std::cos(roll);
    const float rollSine = std::sin(roll);
    const float rolledX = source.position[0] * rollCosine
            - source.position[1] * rollSine;
    const float rolledY = source.position[0] * rollSine
            + source.position[1] * rollCosine;
    const float rolledNormalX = source.normal[0] * rollCosine
            - source.normal[1] * rollSine;
    const float rolledNormalY = source.normal[0] * rollSine
            + source.normal[1] * rollCosine;

    GeneratedVertex result;
    // ComplexLine exposes the TSRE object yaw: its sign is opposite to the
    // mathematical Y rotation used below. This boundary is easy to miss
    // because ORTS/MSTS profiles are generated in the DirectX -Z-forward
    // space while TSRE sweeps them along its OpenGL +Z-forward path. Applying
    // yaw directly twists a cross-section on curves, moving an outside X
    // vertex progressively to the inside. Rotate by the inverse yaw so each
    // profile side keeps a constant curve radius.
    result.values[0] = frame[0] + rolledX * cosine
            + source.position[2] * sine;
    result.values[1] = frame[1] + rolledY;
    result.values[2] = frame[2] - rolledX * sine
            + source.position[2] * cosine;
    result.values[3] = rolledNormalX * cosine + source.normal[2] * sine;
    result.values[4] = rolledNormalY;
    result.values[5] = -rolledNormalX * sine + source.normal[2] * cosine;
    result.values[6] = source.texCoord[0] + polyline.deltaTexCoord[0] * distance;
    result.values[7] = source.texCoord[1] + polyline.deltaTexCoord[1] * distance;
    result.values[8] = alpha;
    return result;
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

QString texturePath(const QString &routePath, const QString &textureName) {
    QString normalizedName = textureName;
    normalizedName.replace('\\', '/');
    const QString routeTexture = QDir::cleanPath(routePath + "/textures/" + normalizedName);
    if(QFileInfo::exists(routeTexture))
        return routeTexture;
    const QDir routeDirectory(routePath);
    const QString globalTexture = QDir::cleanPath(
            routeDirectory.absoluteFilePath("../../global/textures/" + normalizedName));
    if(QFileInfo::exists(globalTexture))
        return globalTexture;
    return routeTexture;
}

}

static bool buildMeshesForPath(const OrtsTrackProfile &profile,
        const QVector<TSection> &sections,
        QVector<OrtsGeneratedProfileMesh> &meshes,
        float startRoll, float endRoll, QStringList *diagnostics,
        float endExtension = 0, float endDrop = 0) {
    meshes.clear();
    if(!profile.valid || sections.isEmpty()){
        if(diagnostics != nullptr)
            diagnostics->append("ORTS profile or DynTrack path is empty");
        return false;
    }

    ComplexLine line;
    line.init(sections);
    const QVector<float> distances = frameDistances(
            profile, sections, endExtension, diagnostics);
    if(distances.size() < 2)
        return false;

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

            for(const OrtsProfilePolyline &polyline : item.polylines){
                if(polyline.vertices.size() < 2)
                    continue;
                QVector<QVector<GeneratedVertex>> frames;
                frames.reserve(distances.size());
                for(float distance : distances){
                    QVector<GeneratedVertex> vertices;
                    vertices.reserve(polyline.vertices.size());
                    for(const OrtsProfileVertex &vertex : polyline.vertices){
                        if(vertex.positionControl != OrtsProfileVertex::PositionControl::None)
                            hasPositionControl = true;
                        vertices.append(transformVertex(
                                vertex, polyline, line, distance, alpha,
                                startRoll, endRoll, endExtension, endDrop));
                    }
                    frames.append(vertices);
                }

                for(int frameIndex = 1; frameIndex < frames.size(); frameIndex++){
                    const QVector<GeneratedVertex> &previous = frames[frameIndex - 1];
                    const QVector<GeneratedVertex> &current = frames[frameIndex];
                    for(int vertexIndex = 1; vertexIndex < current.size(); vertexIndex++){
                        appendVertex(mesh.vertices, current[vertexIndex]);
                        appendVertex(mesh.vertices, previous[vertexIndex - 1]);
                        appendVertex(mesh.vertices, current[vertexIndex - 1]);
                        appendVertex(mesh.vertices, current[vertexIndex]);
                        appendVertex(mesh.vertices, previous[vertexIndex]);
                        appendVertex(mesh.vertices, previous[vertexIndex - 1]);
                    }
                }
            }

            if(!mesh.vertices.isEmpty()){
                updateBounds(mesh);
                meshes.append(mesh);
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
    if(meshes.isEmpty() && diagnostics != nullptr)
        diagnostics->append("ORTS profile produced no renderable geometry");
    return !meshes.isEmpty();
}

bool OrtsTrackProfileRenderer::buildMeshes(const OrtsTrackProfile &profile,
        const QVector<TSection> &sections,
        QVector<OrtsGeneratedProfileMesh> &meshes,
        QStringList *diagnostics, float endExtension, float endDrop) {
    return buildMeshesForPath(
            profile, sections, meshes, 0, 0, diagnostics,
            endExtension, endDrop);
}

bool OrtsTrackProfileRenderer::generate(const OrtsTrackProfile &profile,
        const QVector<TSection> &sections, QVector<OglObj*> &shape,
        const QString &routePath, QStringList *diagnostics,
        float endExtension, float endDrop) {
    QVector<OrtsGeneratedProfileMesh> meshes;
    if(!buildMeshes(profile, sections, meshes, diagnostics,
            endExtension, endDrop))
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
            QString *materialPath =
                    new QString(texturePath(routePath, mesh.textureName));
            object->setMaterial(materialPath);
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
        const TrackShape &trackShape, const QMap<int, float> &angles,
        QVector<OglObj*> &shape, const QString &routePath,
        QStringList *diagnostics) {
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

        QVector<OrtsGeneratedProfileMesh> pathMeshes;
        if(!buildMeshesForPath(profile, sections, pathMeshes,
                angles.value(pathIndex * 2, 0),
                angles.value(pathIndex * 2 + 1, 0), diagnostics))
            continue;
        for(OrtsGeneratedProfileMesh &mesh : pathMeshes){
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
            QString *materialPath =
                    new QString(texturePath(routePath, mesh.textureName));
            object->setMaterial(materialPath);
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
