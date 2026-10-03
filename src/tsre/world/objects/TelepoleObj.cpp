/* This file is part of TSRE5. Licensed under GPL-3.0-or-later; see LICENSE.md. */
#include <tsre/world/objects/TelepoleObj.h>

#include <tsre/Game.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/ogl/TrackItemObj.h>
#include <tsre/procedural/ComplexLine.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <tsre/procedural/OrtsTrackProfileRenderer.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/world/TelepoleData.h>
#include <tsre/world/TerrainLib.h>
#include <QMap>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr TS::TokenId fields[] = {
    TS::UiD, TS::Population, TS::StartPosition, TS::EndPosition,
    TS::StartType, TS::EndType, TS::StartDirection, TS::EndDirection,
    TS::Config, TS::Quality, TS::Position, TS::Direction, TS::MaxVisDistance,
    TS::VDbId
};
constexpr float WireRadius = 0.008f;
constexpr float SagPerMetre = 0.02f;
constexpr int MaximumRenderedPoles = 10000;
// Route terrain helpers currently provide a fixed 10,000-float destination.
constexpr int MaximumTerrainLinePoints = 3333;

QString number(float value) {
    // Independent of the surrounding stream's precision/locale; round-trip
    // finite floats exactly enough for binary32 world data.
    return QString::number(value, 'g', std::numeric_limits<float>::max_digits10);
}
QString number(unsigned int value) { return QString::number(value); }
QString number(const std::array<float, 3>& value) {
    return number(value[0]) + " " + number(value[1]) + " " + number(value[2]);
}
template<typename T>
void writeField(QTextStream& out, const char* name, const T& value) {
    out << "\t\t" << name << " ( " << number(value) << " )\n";
}
template<typename T>
void writeOptional(QTextStream& out, const char* name,
        const std::optional<T>& value) {
    if (value) writeField(out, name, *value);
}

QString numericAtom(FileBuffer* data) {
    // ParserX still traverses the world blocks. Read this form's plain numeric
    // values with checked Qt conversions: ParserX::GetNumber accumulates in
    // float precision and mishandles positive exponent signs on reload.
    QString atom;
    for (;;) {
        const QChar c(data->getShort());
        if (c.isSpace()) {
            if (atom.isEmpty()) continue;
            break;
        }
        if (c == '(' || c == ')') {
            data->off -= 2;
            break;
        }
        atom += c;
        if (atom.size() > 128)
            throw FileBuffer::ParseError("Telepole numeric value too long");
    }
    return atom;
}

float distance(const TelepoleObj::Vector &a, const TelepoleObj::Vector &b) {
    const double dx = (double)b[0] - a[0];
    const double dy = (double)b[1] - a[1];
    const double dz = (double)b[2] - a[2];
    return (float)std::sqrt(dx * dx + dy * dy + dz * dz);
}

void appendProfileVertex(OrtsProfilePolyline &line, float x, float y,
        float normalX, float normalY) {
    OrtsProfileVertex vertex;
    vertex.position[0] = x;
    vertex.position[1] = y;
    vertex.normal[0] = normalX;
    vertex.normal[1] = normalY;
    vertex.normal[2] = 0;
    vertex.positionControl = OrtsProfileVertex::PositionControl::None;
    line.vertices.append(vertex);
}

OrtsTrackProfile wireProfile(const TelepoleData::Config &config) {
    OrtsTrackProfile profile;
    profile.id = "__telepole_wire";
    profile.name = profile.id;
    profile.objectType = OrtsTrackProfile::ObjectType::Static;
    profile.superElevationMethod = OrtsTrackProfile::SuperElevationMethod::None;
    profile.valid = true;

    OrtsProfileLod lod;
    lod.cutoffRadius = 4000.0f;
    OrtsProfileLodItem item;
    item.name = "wire";
    item.pathFrameMode = OrtsProfileLodItem::PathFrameMode::Upright;
    for(const std::array<float, 3> &wire : config.wires){
        OrtsProfilePolyline line;
        line.name = "wire";
        // TPoleConfig Wire is XYZ. Its X is a longitudinal attachment offset
        // (zero in stock data); cross-section coordinates are lateral Z/Y.
        // The ORTS profile backend performs its own MSTS-to-TSRE X conversion.
        const float centerX = wire[2];
        const float centerY = wire[1];
        appendProfileVertex(line, centerX - WireRadius,
                centerY - WireRadius, -1, -1);
        appendProfileVertex(line, centerX - WireRadius,
                centerY + WireRadius, -1, 1);
        appendProfileVertex(line, centerX + WireRadius,
                centerY + WireRadius, 1, 1);
        appendProfileVertex(line, centerX + WireRadius,
                centerY - WireRadius, 1, -1);
        appendProfileVertex(line, centerX - WireRadius,
                centerY - WireRadius, -1, -1);
        item.polylines.append(line);
    }
    lod.items.append(item);
    profile.lods.append(lod);
    return profile;
}

}

TelepoleObj::TelepoleObj() {
    type = "telepole";
    typeID = telepole;
    size = -1;
    skipLevel = 1;
    internalLodControl = true;
    x = y = 0;
    std::fill_n(position, 3, 0);
    std::fill_n(placedAtPosition, 3, 0);
    std::fill_n(firstPosition, 3, 0);
    std::fill_n(qDirection, 4, 0);
    qDirection[3] = 1;
    Mat4::identity(wireTransform.data());
    setMartix();
}

TelepoleObj::TelepoleObj(const TelepoleObj &other)
    : WorldObj(other),
      population(other.population), startType(other.startType),
      endType(other.endType), config(other.config), quality(other.quality),
      startPosition(other.startPosition), endPosition(other.endPosition),
      direction(other.direction), startDirection(other.startDirection),
      endDirection(other.endDirection), maxVisDistance(other.maxVisDistance),
      hasUid(other.hasUid), hasPosition(other.hasPosition),
      hasVdbId(other.hasVdbId),
      coordinatesConverted(other.coordinatesConverted),
      newObjectRequested(other.newObjectRequested),
      selectionValue(other.selectionValue) {
    internalLodControl = true;
    Mat4::identity(wireTransform.data());
}

TelepoleObj::~TelepoleObj() {
    invalidateGeneratedGeometry(true);
    delete endpointMarker;
    delete selectedEndpointMarker;
    delete guideLine;
}

WorldObj* TelepoleObj::clone() { return new TelepoleObj(*this); }

void TelepoleObj::load(int tileX, int tileY) {
    x = tileX;
    y = tileY;
    if (!coordinatesConverted) {
        position[2] = -position[2];
        coordinatesConverted = true;
    }
    if(resPath.isEmpty())
        resPath = ContentPath::join(
                Game::root + "/ROUTES/" + Game::route, "SHAPES");
    if(newObjectRequested){
        initializeNewObject();
        newObjectRequested = false;
    }
    setMartix();
    loaded = true;
    modified = false;
    invalidateGeneratedGeometry();
}

void TelepoleObj::set(TS::TokenId token, FileBuffer* data) {
    if (std::find(std::begin(fields), std::end(fields), token)
            == std::end(fields)) {
        WorldObj::set(token, data);
        return;
    }
    data->skipLabel();
    readField(token, data, true);
}

void TelepoleObj::set(QString token, FileBuffer* data) {
    for (auto id : fields) {
        if (token.compare(QLatin1String(TS::name(id)),
                          Qt::CaseInsensitive) == 0) {
            readField(id, data, false);
            return;
        }
    }
    WorldObj::set(token, data);
}

void TelepoleObj::set(QString token, QString value) {
    if(token.compare("ref_class", Qt::CaseInsensitive) == 0)
        newObjectRequested = true;
    WorldObj::set(token, value);
}

void TelepoleObj::set(QString token, long long value) {
    if(token.compare("ref_value", Qt::CaseInsensitive) == 0 && value >= 0)
        config = (unsigned int)value;
    WorldObj::set(token, value);
}

void TelepoleObj::readField(TS::TokenId token, FileBuffer* data, bool binary) {
    const auto readUint = [&]() {
        if (binary) return data->getUint();
        bool ok;
        const auto value = numericAtom(data).toUInt(&ok);
        if (!ok)
            throw FileBuffer::ParseError("Invalid Telepole unsigned integer");
        return value;
    };
    const auto scalar = [&]() {
        if (binary) return data->getFloat();
        bool ok;
        const auto value = numericAtom(data).toFloat(&ok);
        if (!ok || !std::isfinite(value))
            throw FileBuffer::ParseError("Invalid Telepole float");
        return value;
    };
    const auto vector = [&]() { return Vector{scalar(), scalar(), scalar()}; };
    switch (token) {
    case TS::UiD: UiD = readUint(); hasUid = true; break;
    case TS::Population: population = readUint(); break;
    case TS::StartPosition: startPosition = vector(); break;
    case TS::EndPosition: endPosition = vector(); break;
    case TS::StartType: startType = readUint(); break;
    case TS::EndType: endType = readUint(); break;
    case TS::StartDirection: startDirection = scalar(); break;
    case TS::EndDirection: endDirection = scalar(); break;
    case TS::Config: config = readUint(); break;
    case TS::Quality: quality = readUint(); break;
    case TS::Position: {
        const auto value = vector();
        std::copy(value.begin(), value.end(), position);
        if (coordinatesConverted) position[2] = -position[2];
        if (!hasPosition) ++jestPQ;
        hasPosition = true;
        break;
    }
    case TS::Direction: direction = vector(); break;
    case TS::MaxVisDistance: maxVisDistance = scalar(); break;
    case TS::VDbId: vDbId = readUint(); hasVdbId = true; break;
    }
    invalidateGeneratedGeometry(token == TS::Config);
}

void TelepoleObj::initializeNewObject() {
    hasUid = true;
    hasPosition = true;
    hasVdbId = true;
    jestPQ = std::max(jestPQ, 1);
    const Vector source = {position[0], position[1], -position[2]};
    startPosition = source;
    endPosition = source;
    population = 2;
    startType = 0;
    endType = 0;
    startDirection = 90.0f;
    endDirection = 90.0f;
    if(!config) config = 0;
    direction = Vector{0, 0, 0};
    vDbId = std::numeric_limits<unsigned int>::max();
}

TelepoleObj::Vector TelepoleObj::internalPoint(const Vector &source) const {
    return {source[0], source[1], -source[2]};
}

TelepoleObj::Vector TelepoleObj::sourcePoint(
        int tileX, int tileZ, const float *internal) const {
    // Keep MSTS tiles separate from local floats. Only the small tile delta
    // is converted, avoiding the precision loss of one global float space.
    const double localX = ((double)tileX - x) * 2048.0 + internal[0];
    const double localZ = ((double)tileZ - y) * 2048.0 + internal[2];
    return {(float)localX, internal[1], (float)-localZ};
}

float TelepoleObj::spanLength() const {
    if(!startPosition || !endPosition)
        return 0;
    return distance(*startPosition, *endPosition);
}

float TelepoleObj::separation() const {
    const QString routePath = Game::root + "/ROUTES/" + Game::route;
    const TelepoleData::Config *entry = TelepoleData::routeData(routePath)
            .config(configIndex());
    return entry != nullptr && entry->valid ? entry->separation : 10.0f;
}

void TelepoleObj::recalculatePopulation() {
    const float spacing = separation();
    const float length = spanLength();
    if(!std::isfinite(length) || length <= 0 || !std::isfinite(spacing)
            || spacing <= 0){
        population = 2;
        return;
    }
    const double intervals = std::ceil((double)length / spacing);
    const double calculated = std::max(2.0, intervals + 1.0);
    population = (unsigned int)std::min(calculated,
            (double)std::numeric_limits<unsigned int>::max());
}

void TelepoleObj::updateMidpoint() {
    if(!startPosition || !endPosition)
        return;
    const Vector start = internalPoint(*startPosition);
    const Vector end = internalPoint(*endPosition);
    for(int axis = 0; axis < 3; axis++)
        position[axis] = (start[axis] + end[axis]) * 0.5f;
    std::copy(position, position + 3, placedAtPosition);
    hasPosition = true;
    jestPQ = std::max(jestPQ, 1);
    setMartix();
}

bool TelepoleObj::setEndPosition(
        int tileX, int tileZ, const float *newPosition) {
    if(newPosition == nullptr || !startPosition)
        return false;
    for(int axis = 0; axis < 3; axis++)
        if(!std::isfinite(newPosition[axis]))
            return false;
    endPosition = sourcePoint(tileX, tileZ, newPosition);
    recalculatePopulation();
    updateMidpoint();
    setModified();
    invalidateGeneratedGeometry();
    return true;
}

unsigned int TelepoleObj::populationValue() const {
    return population.value_or(0);
}

int TelepoleObj::configIndex() const {
    return (int)config.value_or(0);
}

void TelepoleObj::setConfigIndex(int index) {
    if(index < 0 || configIndex() == index)
        return;
    config = (unsigned int)index;
    recalculatePopulation();
    setModified();
    invalidateGeneratedGeometry(true);
}

void TelepoleObj::invalidateGeneratedGeometry(bool resetShape) {
    for(OglObj *object : wireObjects){
        if(object != nullptr){
            object->deleteVBO();
            delete object;
        }
    }
    wireObjects.clear();
    polePoints.clear();
    wirePathPoints.clear();
    poleTransforms.clear();
    generatedGeometryValid = false;
    wireGeometryInitialized = false;
    if(guideLine != nullptr)
        guideLine->deleteVBO();
    if(resetShape){
        poleShape = nullptr;
        poleShapeState = 0;
        loadedPoleShape.clear();
    }
}

void TelepoleObj::ensureDerivedPoints() {
    if(generatedGeometryValid)
        return;
    polePoints.clear();
    wirePathPoints.clear();
    poleTransforms.clear();
    if(!startPosition || !endPosition)
        return;

    const Vector start = internalPoint(*startPosition);
    const Vector end = internalPoint(*endPosition);
    unsigned int count = population.value_or(0);
    if(count < 2){
        const float spacing = separation();
        count = spacing > 0
                ? (unsigned int)std::ceil(spanLength() / spacing) + 1 : 2;
    }
    count = std::max(2u, std::min(count,
            (unsigned int)MaximumRenderedPoles));
    polePoints.reserve((int)count);
    for(unsigned int index = 0; index < count; index++){
        const float t = count > 1
                ? (float)index / (float)(count - 1) : 0;
        Vector point = {
            start[0] + (end[0] - start[0]) * t,
            start[1] + (end[1] - start[1]) * t,
            start[2] + (end[2] - start[2]) * t
        };
        // Native Telepole poles are terrain-grounded, including both ends.
        // Keep the authored endpoint heights for persistence/population math,
        // but use terrain height for every rendered pole and wire attachment.
        if(Game::terrainLib != nullptr){
            float terrainHeight = point[1];
            if(Game::terrainLib->tryGetHeight(
                    x, y, point[0], point[2], terrainHeight))
                point[1] = terrainHeight;
        }
        polePoints.append(point);
    }

    for(int span = 0; span + 1 < polePoints.size(); span++){
        const Vector &a = polePoints[span];
        const Vector &b = polePoints[span + 1];
        const float spanDistance = distance(a, b);
        const int subdivisions = std::max(
                4, (int)std::ceil(spanDistance));
        const float sag = spanDistance * SagPerMetre;
        for(int sample = span == 0 ? 0 : 1;
                sample <= subdivisions; sample++){
            const float t = (float)sample / subdivisions;
            Vector point = {
                a[0] + (b[0] - a[0]) * t,
                a[1] + (b[1] - a[1]) * t
                    - 4.0f * sag * t * (1.0f - t),
                a[2] + (b[2] - a[2]) * t
            };
            wirePathPoints.append(point);
        }
    }
    if(!wirePathPoints.isEmpty()){
        Mat4::identity(wireTransform.data());
        Mat4::translate(wireTransform.data(), wireTransform.data(),
                wirePathPoints.first()[0], wirePathPoints.first()[1],
                wirePathPoints.first()[2]);
    }
    generatedGeometryValid = true;
}

void TelepoleObj::ensurePoleShape() {
    const QString routePath = Game::root + "/ROUTES/" + Game::route;
    const TelepoleData::Config *entry = TelepoleData::routeData(routePath)
            .config(configIndex());
    const QString wanted = entry != nullptr && entry->valid
            ? entry->fileName : QString();
    if(wanted.compare(loadedPoleShape, Qt::CaseInsensitive) != 0){
        poleShape = nullptr;
        poleShapeState = 0;
        loadedPoleShape = wanted;
    }
    if(poleShape != nullptr || wanted.isEmpty()
            || Game::currentShapeLib == nullptr)
        return;
    const int shapeId = Game::currentShapeLib->addShape(
            ContentPath::join(resPath, wanted));
    const auto found = Game::currentShapeLib->shape.find(shapeId);
    if(found == Game::currentShapeLib->shape.end()
            || found->second == nullptr)
        return;
    poleShape = found->second;
    poleShapeState = poleShape->newState();
    poleShape->setAnimated(poleShapeState, isAnimated());
}

void TelepoleObj::ensurePoleTransforms() {
    ensureDerivedPoints();
    if(!poleTransforms.isEmpty() || polePoints.isEmpty())
        return;
    poleTransforms.reserve(polePoints.size());
    float forward[3] = {
        polePoints.last()[0] - polePoints.first()[0],
        0,
        polePoints.last()[2] - polePoints.first()[2]
    };
    const float forwardLength = std::hypot(forward[0], forward[2]);
    if(forwardLength > 0.000001f){
        forward[0] /= forwardLength;
        forward[2] /= forwardLength;
    } else {
        Vec3::set(forward, 0, 0, 1);
    }
    const float right[3] = {forward[2], 0, -forward[0]};
    for(const Vector &point : polePoints){
        std::array<float, 16> transform = {
            right[0], right[1], right[2], 0,
            0, 1, 0, 0,
            forward[0], forward[1], forward[2], 0,
            point[0], point[1], point[2], 1
        };
        // TPoleConfig Wire uses X as the run direction and Z as the lateral
        // cross-arm coordinate. Align the pole shape's local +X with the
        // Telepole span; ordinary world-shape -Z-forward alignment would turn
        // the pole by 90 degrees.
        Mat4::rotateY(transform.data(), transform.data(), -M_PI_2);
        poleTransforms.append(transform);
    }
}

void TelepoleObj::ensureWireGeometry() {
    ensureDerivedPoints();
    if(wireGeometryInitialized)
        return;
    wireGeometryInitialized = true;
    if(wirePathPoints.size() < 2)
        return;
    const QString routePath = Game::root + "/ROUTES/" + Game::route;
    const TelepoleData::Config *entry = TelepoleData::routeData(routePath)
            .config(configIndex());
    if(entry == nullptr || !entry->valid || entry->wires.isEmpty())
        return;

    QVector<ComplexLinePoint> linePoints;
    linePoints.reserve(wirePathPoints.size());
    for(const Vector &point : wirePathPoints){
        ComplexLinePoint linePoint;
        Vec3::copy(linePoint.position, point.data());
        linePoints.append(linePoint);
    }
    ComplexLine line;
    line.init(linePoints);
    QVector<OrtsGeneratedProfileMesh> meshes;
    QStringList diagnostics;
    if(!OrtsTrackProfileRenderer::buildMeshes(
            wireProfile(*entry), line, meshes, &diagnostics))
        return;

    // Point-backed profile generation normally returns one mesh per semantic
    // path span. Telepole samples are only a smooth wire curve, not rendering
    // ownership boundaries, so combine them into one draw object per LOD.
    QMap<QString, OrtsGeneratedProfileMesh> combined;
    for(const OrtsGeneratedProfileMesh &mesh : meshes){
        const QString key = QString::number((int)mesh.materialPass) + ":"
                + QString::number(mesh.minimumDistance) + ":"
                + QString::number(mesh.maximumDistance);
        auto found = combined.find(key);
        if(found == combined.end()){
            combined.insert(key, mesh);
            continue;
        }
        found->vertices += mesh.vertices;
        // Generated bounds use [maximum, minimum] pairs. Merge by union;
        // reversing these operations computes the intersection and can put a
        // future LOD/culling center inside only one span of a long Telepole.
        found->bounds[0] = std::max(found->bounds[0], mesh.bounds[0]);
        found->bounds[1] = std::min(found->bounds[1], mesh.bounds[1]);
        found->bounds[2] = std::max(found->bounds[2], mesh.bounds[2]);
        found->bounds[3] = std::min(found->bounds[3], mesh.bounds[3]);
        found->bounds[4] = std::max(found->bounds[4], mesh.bounds[4]);
        found->bounds[5] = std::min(found->bounds[5], mesh.bounds[5]);
    }
    for(const OrtsGeneratedProfileMesh &mesh : combined){
        if(mesh.vertices.isEmpty())
            continue;
        OglObj *object = new OglObj();
        object->setMaterial(0.12f, 0.12f, 0.12f);
        object->setDistanceRange(mesh.minimumDistance, mesh.maximumDistance);
        object->init(const_cast<float*>(mesh.vertices.constData()),
                mesh.vertices.size(), RenderItem::VNTA, GL_TRIANGLES);
        object->setBound(const_cast<float*>(mesh.bounds));
        wireObjects.append(object);
    }
}

void TelepoleObj::ensureInteractiveGeometry() {
    ensureDerivedPoints();
    if(endpointMarker == nullptr){
        const float vertices[] = {0, 0, 0, 0, 10, 0};
        endpointMarker = new OglObj();
        endpointMarker->setLineWidth(8);
        endpointMarker->setMaterial(1, 1, 1);
        endpointMarker->init(const_cast<float*>(vertices), 6,
                RenderItem::V, GL_LINES);
        selectedEndpointMarker = new OglObj();
        selectedEndpointMarker->setLineWidth(8);
        selectedEndpointMarker->setMaterial(0.5f, 0.5f, 0.5f);
        selectedEndpointMarker->init(const_cast<float*>(vertices), 6,
                RenderItem::V, GL_LINES);
    }
    if(guideLine == nullptr){
        guideLine = new OglObj();
        guideLine->setLineWidth(2);
        guideLine->setMaterial(1, 1, 1);
    }
    if(!guideLine->loaded && polePoints.size() >= 2){
        QVector<float> vertices;
        vertices.reserve((polePoints.size() - 1) * 6);
        for(int index = 0; index + 1 < polePoints.size(); index++){
            for(int endpoint = 0; endpoint < 2; endpoint++){
                const Vector &point = polePoints[index + endpoint];
                vertices.append(point[0]);
                vertices.append(point[1] + 1.0f);
                vertices.append(point[2]);
            }
        }
        guideLine->init(vertices.data(), vertices.size(),
                RenderItem::V, GL_LINES);
    }
}

void TelepoleObj::pushRenderItems(float lod, float posx, float posz,
        float*, float*, float, quint32 selectionId) {
    Q_UNUSED(posx);
    Q_UNUSED(posz);
    if(!loaded || !startPosition || !endPosition
            || Game::currentRenderer == nullptr)
        return;
    ensureDerivedPoints();
    ensurePoleShape();
    ensurePoleTransforms();
    ensureWireGeometry();

    if(poleShape != nullptr){
        for(const std::array<float, 16> &transform : poleTransforms){
            Game::currentRenderer->mvPushMatrix();
            Mat4::multiply(Game::currentRenderer->mvMatrix,
                    Game::currentRenderer->mvMatrix,
                    const_cast<float*>(transform.data()));
            poleShape->pushRenderItem(selectionId, poleShapeState);
            Game::currentRenderer->mvPopMatrix();
        }
    }
    Game::currentRenderer->mvPushMatrix();
    Mat4::multiply(Game::currentRenderer->mvMatrix,
            Game::currentRenderer->mvMatrix, wireTransform.data());
    for(OglObj *object : wireObjects)
        object->pushRenderItem(selectionId, lod);
    Game::currentRenderer->mvPopMatrix();

    if(!Game::viewInteractives)
        return;
    ensureInteractiveGeometry();
    if(guideLine != nullptr)
        guideLine->pushRenderItem(selectionId);
    if(polePoints.size() >= 2){
        const int indices[2] = {0, (int)polePoints.size() - 1};
        for(int endpoint = 0; endpoint < 2; endpoint++){
            Game::currentRenderer->mvPushMatrix();
            const Vector &point = polePoints[indices[endpoint]];
            Mat4::translate(Game::currentRenderer->mvMatrix,
                    Game::currentRenderer->mvMatrix,
                    point[0], point[1], point[2]);
            OglObj *marker = selected && selectionValue == endpoint + 1
                    ? selectedEndpointMarker : endpointMarker;
            marker->pushRenderItem(SelectionIdCodec::withPart(
                    selectionId, endpoint + 1));
            Game::currentRenderer->mvPopMatrix();
        }
    }
}

void TelepoleObj::updateSim(float deltaTime) {
    if(loaded && poleShape != nullptr)
        poleShape->updateSim(deltaTime, poleShapeState);
}

void TelepoleObj::deleteVBO() {
    invalidateGeneratedGeometry();
    if(endpointMarker != nullptr){
        endpointMarker->deleteVBO();
        delete endpointMarker;
        endpointMarker = nullptr;
    }
    if(selectedEndpointMarker != nullptr){
        selectedEndpointMarker->deleteVBO();
        delete selectedEndpointMarker;
        selectedEndpointMarker = nullptr;
    }
}

bool TelepoleObj::select(int value) {
    selectionValue = value == 1 || value == 2 ? value : 0;
    selected = true;
    return true;
}

void TelepoleObj::setPosition(int tileX, int tileZ, float *newPosition) {
    if(newPosition == nullptr)
        return;
    bool endpointChanged = false;
    if(selectionValue == 1 || selectionValue == 2){
        Vector value = sourcePoint(tileX, tileZ, newPosition);
        if(selectionValue == 1)
            startPosition = value;
        else
            endPosition = value;
        endpointChanged = true;
    } else if(startPosition && endPosition){
        const Vector target = sourcePoint(tileX, tileZ, newPosition);
        const Vector current = {position[0], position[1], -position[2]};
        const Vector delta = {
            target[0] - current[0],
            target[1] - current[1],
            target[2] - current[2]
        };
        for(int axis = 0; axis < 3; axis++){
            (*startPosition)[axis] += delta[axis];
            (*endPosition)[axis] += delta[axis];
        }
    } else {
        WorldObj::setPosition(tileX, tileZ, newPosition);
        return;
    }
    if(endpointChanged)
        recalculatePopulation();
    updateMidpoint();
    setModified();
    invalidateGeneratedGeometry();
}

void TelepoleObj::translate(float dx, float dy, float dz) {
    if(startPosition && endPosition){
        for(Vector *point : {&*startPosition, &*endPosition}){
            (*point)[0] += dx;
            (*point)[1] += dy;
            (*point)[2] -= dz;
        }
    }
    WorldObj::translate(dx, dy, dz);
    invalidateGeneratedGeometry();
}

bool TelepoleObj::hasLinePoints() {
    return startPosition && endPosition;
}

void TelepoleObj::getLinePoints(float *&output) {
    ensureDerivedPoints();
    const int outputCount = std::min(
            (int)polePoints.size(), MaximumTerrainLinePoints);
    for(int outputIndex = 0; outputIndex < outputCount; outputIndex++){
        const int pointIndex = outputCount == polePoints.size()
                ? outputIndex
                : (int)std::llround((double)outputIndex
                    * (polePoints.size() - 1) / (outputCount - 1));
        const Vector &point = polePoints[pointIndex];
        *output++ = point[0];
        *output++ = point[1];
        *output++ = point[2];
    }
}

void TelepoleObj::save(QTextStream* out) {
    if (!loaded) return;
    *out << "\tTelepole (\n";
    if (hasUid) writeField(*out, "UiD", UiD);
    writeOptional(*out, "Population", population);
    writeOptional(*out, "StartPosition", startPosition);
    writeOptional(*out, "EndPosition", endPosition);
    writeOptional(*out, "StartType", startType);
    writeOptional(*out, "EndType", endType);
    writeOptional(*out, "StartDirection", startDirection);
    writeOptional(*out, "EndDirection", endDirection);
    writeOptional(*out, "Config", config);
    writeOptional(*out, "Quality", quality);
    if (hasPosition) writeField(*out, "Position", Vector{
        position[0], position[1], coordinatesConverted
                ? -position[2] : position[2]});
    writeOptional(*out, "Direction", direction);
    writeOptional(*out, "MaxVisDistance", maxVisDistance);
    if (hasVdbId) writeField(*out, "VDbId", vDbId);
    *out << "\t)\n";
}
