/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/procedural/OrtsTrackProfile.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>

QString OrtsTrackProfileCatalog::loadedRoutePath;
QMap<QString, QSharedPointer<OrtsTrackProfile>> OrtsTrackProfileCatalog::profiles;
QStringList OrtsTrackProfileCatalog::loadDiagnostics;

namespace {

struct StfNode {
    QString name;
    QStringList values;
    QVector<StfNode> children;
};

QStringList tokenizeStf(const QString &text, QStringList &diagnostics) {
    QStringList tokens;
    int i = 0;
    while(i < text.size()){
        const QChar c = text[i];
        if(c.isSpace()){
            i++;
            continue;
        }
        if(c == '/' && i + 1 < text.size() && text[i + 1] == '/'){
            while(i < text.size() && text[i] != '\n')
                i++;
            continue;
        }
        if(c == '#'){
            while(i < text.size() && text[i] != '\n')
                i++;
            continue;
        }
        if(c == '(' || c == ')'){
            tokens.append(QString(c));
            i++;
            continue;
        }
        if(c == '"'){
            QString value;
            i++;
            while(i < text.size() && text[i] != '"'){
                if(text[i] == '\\' && i + 1 < text.size()){
                    i++;
                    value.append(text[i++]);
                } else {
                    value.append(text[i++]);
                }
            }
            if(i >= text.size())
                diagnostics.append("Unterminated quoted string");
            else
                i++;
            tokens.append(value);
            continue;
        }
        const int start = i;
        while(i < text.size() && !text[i].isSpace()
                && text[i] != '(' && text[i] != ')')
            i++;
        tokens.append(text.mid(start, i - start));
    }
    return tokens;
}

bool parseStfNode(const QStringList &tokens, int &index, const QString &name,
        StfNode &node, QStringList &diagnostics) {
    node.name = name;
    if(index >= tokens.size() || tokens[index] != "("){
        diagnostics.append("Expected '(' after " + name);
        return false;
    }
    index++;
    while(index < tokens.size() && tokens[index] != ")"){
        const QString token = tokens[index++];
        if(index < tokens.size() && tokens[index] == "("){
            StfNode child;
            if(!parseStfNode(tokens, index, token, child, diagnostics))
                return false;
            node.children.append(child);
        } else {
            node.values.append(token);
        }
    }
    if(index >= tokens.size()){
        diagnostics.append("Unclosed block " + name);
        return false;
    }
    index++;
    return true;
}

const StfNode *child(const StfNode &node, const QString &name) {
    for(const StfNode &candidate : node.children){
        if(candidate.name.compare(name, Qt::CaseInsensitive) == 0)
            return &candidate;
    }
    return nullptr;
}

QVector<const StfNode*> children(const StfNode &node, const QString &name) {
    QVector<const StfNode*> result;
    for(const StfNode &candidate : node.children){
        if(candidate.name.compare(name, Qt::CaseInsensitive) == 0)
            result.append(&candidate);
    }
    return result;
}

QString firstValue(const StfNode &node, const QString &name,
        const QString &fallback = QString()) {
    const StfNode *valueNode = child(node, name);
    if(valueNode == nullptr || valueNode->values.isEmpty())
        return fallback;
    return valueNode->values.first();
}

float numberValue(QString value, bool *ok = nullptr) {
    value = value.trimmed();
    static const QRegularExpression numberPattern(
            "^([-+]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][-+]?\\d+)?)([a-zA-Z]*)$");
    const QRegularExpressionMatch match = numberPattern.match(value);
    if(!match.hasMatch()){
        if(ok != nullptr)
            *ok = false;
        return 0;
    }
    bool parsed = false;
    float result = match.captured(1).toFloat(&parsed);
    const QString unit = match.captured(2).toLower();
    if(unit == "cm")
        result *= 0.01f;
    else if(unit == "mm")
        result *= 0.001f;
    else if(unit == "km")
        result *= 1000.0f;
    else if(unit == "ft")
        result *= 0.3048f;
    else if(!unit.isEmpty() && unit != "m")
        parsed = false;
    if(ok != nullptr)
        *ok = parsed;
    return parsed ? result : 0;
}

float floatValue(const StfNode &node, const QString &name, float fallback) {
    bool ok = false;
    const float result = numberValue(firstValue(node, name), &ok);
    return ok ? result : fallback;
}

int intValue(const StfNode &node, const QString &name, int fallback) {
    bool ok = false;
    const int result = firstValue(node, name).toInt(&ok);
    return ok ? result : fallback;
}

QStringList splitFilters(const QString &value) {
    QStringList result = value.split(',', Qt::SkipEmptyParts);
    for(QString &filter : result)
        filter = filter.trimmed();
    return result;
}

QString profileRoleSuffix(OrtsTrackProfile::ObjectRole role) {
    switch(role){
        case OrtsTrackProfile::ObjectRole::Single:
            return "_single";
        case OrtsTrackProfile::ObjectRole::Left:
            return "_left";
        case OrtsTrackProfile::ObjectRole::Middle:
            return "_middle";
        case OrtsTrackProfile::ObjectRole::Right:
            return "_right";
        case OrtsTrackProfile::ObjectRole::Main:
        default:
            return QString();
    }
}

QString objectRoleKey(OrtsTrackProfile::ObjectRole role) {
    if(role == OrtsTrackProfile::ObjectRole::Main)
        return "main";
    return profileRoleSuffix(role).mid(1);
}

QString objectTypeKey(OrtsTrackProfile::ObjectType objectType) {
    switch(objectType){
        case OrtsTrackProfile::ObjectType::Road:
            return "road";
        case OrtsTrackProfile::ObjectType::Static:
            return "static";
        case OrtsTrackProfile::ObjectType::Track:
        default:
            return "track";
    }
}

QString catalogKey(OrtsTrackProfile::ObjectType objectType,
        const QString &profileId) {
    return objectTypeKey(objectType) + ":" + profileId.trimmed().toLower();
}

bool parseObjectTypeValues(const QStringList &values,
        OrtsTrackProfile &profile) {
    if(values.isEmpty()){
        profile.objectType = OrtsTrackProfile::ObjectType::Track;
        profile.objectRole = OrtsTrackProfile::ObjectRole::Main;
        profile.objectTypeExplicit = false;
        return true;
    }

    profile.objectTypeExplicit = true;
    const QString object = values[0].trimmed();
    if(object.compare("TRACK", Qt::CaseInsensitive) == 0)
        profile.objectType = OrtsTrackProfile::ObjectType::Track;
    else if(object.compare("ROAD", Qt::CaseInsensitive) == 0)
        profile.objectType = OrtsTrackProfile::ObjectType::Road;
    else if(object.compare("STATIC", Qt::CaseInsensitive) == 0)
        profile.objectType = OrtsTrackProfile::ObjectType::Static;
    else {
        profile.diagnostics.append("Invalid ObjectType object: " + object);
        return false;
    }

    profile.objectRole = OrtsTrackProfile::ObjectRole::Main;
    if(values.size() >= 2){
        const QString role = values[1].trimmed();
        if(role.compare("MAIN", Qt::CaseInsensitive) == 0)
            profile.objectRole = OrtsTrackProfile::ObjectRole::Main;
        else if(role.compare("SINGLE", Qt::CaseInsensitive) == 0)
            profile.objectRole = OrtsTrackProfile::ObjectRole::Single;
        else if(role.compare("LEFT", Qt::CaseInsensitive) == 0)
            profile.objectRole = OrtsTrackProfile::ObjectRole::Left;
        else if(role.compare("MIDDLE", Qt::CaseInsensitive) == 0)
            profile.objectRole = OrtsTrackProfile::ObjectRole::Middle;
        else if(role.compare("RIGHT", Qt::CaseInsensitive) == 0)
            profile.objectRole = OrtsTrackProfile::ObjectRole::Right;
        else {
            profile.diagnostics.append("Invalid ObjectType role: " + role);
            return false;
        }
    }
    if(values.size() > 2){
        profile.diagnostics.append("ObjectType accepts an object and optional role");
        return false;
    }
    return true;
}

OrtsTrackProfile::LodMethod lodMethod(const QString &value) {
    if(value.compare("CompleteReplacement", Qt::CaseInsensitive) == 0)
        return OrtsTrackProfile::LodMethod::CompleteReplacement;
    return OrtsTrackProfile::LodMethod::ComponentAdditive;
}

OrtsTrackProfile::PitchControl pitchControl(const QString &value) {
    if(value.compare("ChordLength", Qt::CaseInsensitive) == 0)
        return OrtsTrackProfile::PitchControl::ChordLength;
    if(value.compare("ChordDisplacement", Qt::CaseInsensitive) == 0)
        return OrtsTrackProfile::PitchControl::ChordDisplacement;
    return OrtsTrackProfile::PitchControl::None;
}

OrtsTrackProfile::SuperElevationMethod elevationMethod(const QString &value) {
    if(value.compare("None", Qt::CaseInsensitive) == 0)
        return OrtsTrackProfile::SuperElevationMethod::None;
    if(value.compare("Both", Qt::CaseInsensitive) == 0)
        return OrtsTrackProfile::SuperElevationMethod::Both;
    if(value.compare("Inside", Qt::CaseInsensitive) == 0)
        return OrtsTrackProfile::SuperElevationMethod::Inside;
    return OrtsTrackProfile::SuperElevationMethod::Outside;
}

OrtsProfileVertex::PositionControl positionControl(const QString &value) {
    if(value.compare("None", Qt::CaseInsensitive) == 0)
        return OrtsProfileVertex::PositionControl::None;
    if(value.compare("Inside", Qt::CaseInsensitive) == 0)
        return OrtsProfileVertex::PositionControl::Inside;
    if(value.compare("Outside", Qt::CaseInsensitive) == 0)
        return OrtsProfileVertex::PositionControl::Outside;
    return OrtsProfileVertex::PositionControl::All;
}

bool values(const QStringList &input, float *output, int count) {
    if(input.size() < count)
        return false;
    for(int i = 0; i < count; i++){
        bool ok = false;
        output[i] = numberValue(input[i], &ok);
        if(!ok)
            return false;
    }
    return true;
}

bool attributeValues(const QXmlStreamAttributes &attributes, const QString &name,
        float *output, int count) {
    QString value;
    for(const QXmlStreamAttribute &attribute : attributes){
        if(attribute.name().compare(name, Qt::CaseInsensitive) == 0){
            value = attribute.value().toString();
            break;
        }
    }
    return values(value.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts),
                  output, count);
}

QString attribute(const QXmlStreamAttributes &attributes, const QString &name,
        const QString &fallback = QString()) {
    for(const QXmlStreamAttribute &candidate : attributes){
        if(candidate.name().compare(name, Qt::CaseInsensitive) == 0)
            return candidate.value().toString();
    }
    return fallback;
}

void validateProfile(OrtsTrackProfile &profile) {
    bool renderable = false;
    bool structureValid = !profile.lods.isEmpty();
    for(const OrtsProfileLod &lod : profile.lods){
        if(lod.cutoffRadius <= 0){
            profile.diagnostics.append("LOD has invalid CutoffRadius");
            structureValid = false;
        }
        if(lod.items.isEmpty()){
            profile.diagnostics.append("LOD has no LODItem");
            structureValid = false;
        }
        for(const OrtsProfileLodItem &item : lod.items){
            if(item.polylines.isEmpty()){
                profile.diagnostics.append("LODItem has no Polyline: " + item.name);
                structureValid = false;
            }
            for(const OrtsProfilePolyline &polyline : item.polylines){
                if(polyline.vertices.size() < 2){
                    profile.diagnostics.append("Polyline has fewer than two vertices: "
                                               + polyline.name);
                    structureValid = false;
                } else
                    renderable = true;
                for(const OrtsProfileVertex &vertex : polyline.vertices){
                    if(!vertex.valid){
                        profile.diagnostics.append("Polyline has an invalid vertex: "
                                                   + polyline.name);
                        structureValid = false;
                    }
                }
            }
        }
    }
    if(profile.chordSpanDegrees <= 0)
        profile.chordSpanDegrees = 1.0f;
    if(profile.trackGauge <= 0)
        profile.trackGauge = 1.435f;
    if(profile.lods.isEmpty())
        profile.diagnostics.append("Profile has no LOD");
    profile.valid = renderable && structureValid && profile.objectTypeValid;
}

QSharedPointer<OrtsTrackProfile> profileFromStfNode(
        const StfNode &root, const QString &familyId) {
    QSharedPointer<OrtsTrackProfile> profile(new OrtsTrackProfile());
    profile->familyId = familyId;
    profile->name = firstValue(root, "Name", familyId);
    const StfNode *objectTypeNode = child(root, "ObjectType");
    profile->objectTypeValid = parseObjectTypeValues(
            objectTypeNode == nullptr ? QStringList() : objectTypeNode->values,
            *profile);
    profile->id = familyId + profileRoleSuffix(profile->objectRole);
    profile->lodMethod = lodMethod(firstValue(root, "LODMethod"));
    profile->chordSpanDegrees = floatValue(root, "ChordSpan", 1.0f);
    profile->pitchControl = pitchControl(firstValue(root, "PitchControl"));
    profile->pitchControlScalar = floatValue(root, "PitchControlScalar", 0);
    profile->includedShapes = splitFilters(firstValue(root, "IncludedShapes"));
    profile->excludedShapes = splitFilters(firstValue(root, "ExcludedShapes"));
    profile->includedTextures = splitFilters(firstValue(root, "IncludedTextures"));
    profile->excludedTextures = splitFilters(firstValue(root, "ExcludedTextures"));
    profile->trackGauge = floatValue(root, "TrackGauge", 1.435f);
    profile->superElevationMethod = elevationMethod(
            firstValue(root, "SuperElevationMethod", "Outside"));

    for(const StfNode *lodNode : children(root, "LOD")){
        OrtsProfileLod lod;
        lod.cutoffRadius = floatValue(*lodNode, "CutoffRadius", 0);
        for(const StfNode *itemNode : children(*lodNode, "LODItem")){
            OrtsProfileLodItem item;
            item.name = firstValue(*itemNode, "Name");
            item.textureName = firstValue(*itemNode, "TexName");
            item.shaderName = firstValue(*itemNode, "ShaderName");
            item.lightModelName = firstValue(*itemNode, "LightModelName");
            item.alphaTestMode = intValue(*itemNode, "AlphaTestMode", 0);
            item.textureAddressMode = firstValue(*itemNode, "TexAddrModeName");
            item.alternativeTexture = intValue(
                    *itemNode, "ESD_Alternative_Texture", 0);
            item.mipMapLodBias = floatValue(
                    *itemNode, "MipMapLevelOfDetailBias", 0);
            for(const StfNode *polylineNode : children(*itemNode, "Polyline")){
                OrtsProfilePolyline polyline;
                polyline.name = firstValue(*polylineNode, "Name");
                const StfNode *deltaNode = child(*polylineNode, "DeltaTexCoord");
                if(deltaNode != nullptr)
                    values(deltaNode->values, polyline.deltaTexCoord, 2);
                for(const StfNode *vertexNode : children(*polylineNode, "Vertex")){
                    OrtsProfileVertex vertex;
                    const StfNode *positionNode = child(*vertexNode, "Position");
                    const StfNode *normalNode = child(*vertexNode, "Normal");
                    const StfNode *textureNode = child(*vertexNode, "TexCoord");
                    const bool positionValid = positionNode != nullptr
                            && values(positionNode->values, vertex.position, 2);
                    if(positionValid && positionNode->values.size() >= 3)
                        vertex.valid = values(
                                positionNode->values, vertex.position, 3);
                    const bool normalValid = normalNode != nullptr
                            && values(normalNode->values, vertex.normal, 3)
                            && (std::abs(vertex.normal[0]) > 0.000001f
                                || std::abs(vertex.normal[1]) > 0.000001f
                                || std::abs(vertex.normal[2]) > 0.000001f);
                    const bool textureValid = textureNode != nullptr
                            && values(textureNode->values, vertex.texCoord, 2);
                    vertex.valid = vertex.valid && positionValid
                            && normalValid && textureValid;
                    vertex.positionControl = positionControl(
                            firstValue(*vertexNode, "PositionControl", "All"));
                    polyline.vertices.append(vertex);
                }
                item.polylines.append(polyline);
            }
            lod.items.append(item);
        }
        profile->lods.append(lod);
    }
    validateProfile(*profile);
    return profile;
}

}

QVector<QSharedPointer<OrtsTrackProfile>>
OrtsTrackProfileParser::parseFileProfiles(
        const QString &path, QStringList *diagnostics) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)){
        if(diagnostics != nullptr)
            diagnostics->append("Unable to open " + path);
        return {};
    }
    QTextStream stream(&file);
    stream.setAutoDetectUnicode(true);
    const QString text = stream.readAll();
    const QString id = QFileInfo(path).completeBaseName();
    QVector<QSharedPointer<OrtsTrackProfile>> parsedProfiles;
    if(QFileInfo(path).suffix().compare("xml", Qt::CaseInsensitive) == 0)
        parsedProfiles = parseXmlProfiles(text, id, diagnostics);
    else
        parsedProfiles = parseStfProfiles(text, id, diagnostics);
    for(const QSharedPointer<OrtsTrackProfile> &profile : parsedProfiles)
        profile->sourcePath = path;
    return parsedProfiles;
}

QSharedPointer<OrtsTrackProfile> OrtsTrackProfileParser::parseFile(
        const QString &path, QStringList *diagnostics) {
    const QVector<QSharedPointer<OrtsTrackProfile>> parsedProfiles =
            parseFileProfiles(path, diagnostics);
    return parsedProfiles.isEmpty() ? QSharedPointer<OrtsTrackProfile>()
                                    : parsedProfiles.first();
}

QVector<QSharedPointer<OrtsTrackProfile>>
OrtsTrackProfileParser::parseStfProfiles(
        const QString &text, const QString &id, QStringList *diagnostics) {
    QStringList localDiagnostics;
    QString normalized = text;
    if(normalized.startsWith(QChar(0xfeff)))
        normalized.remove(0, 1);
    if(!normalized.startsWith("SIMISA@@@@@@@@@@JINX0p0t______")){
        localDiagnostics.append("Invalid STF SIMISA signature");
        if(diagnostics != nullptr)
            diagnostics->append(localDiagnostics);
        return {};
    }

    const QStringList tokens = tokenizeStf(normalized, localDiagnostics);
    QVector<QSharedPointer<OrtsTrackProfile>> parsedProfiles;
    QSet<QString> memberKeys;
    for(int rootIndex = 0; rootIndex + 1 < tokens.size(); rootIndex++){
        if(tokens[rootIndex].compare("TrProfile", Qt::CaseInsensitive) != 0
                || tokens[rootIndex + 1] != "(")
            continue;

        int index = rootIndex + 1;
        StfNode root;
        if(!parseStfNode(tokens, index, tokens[rootIndex], root,
                         localDiagnostics))
            break;
        rootIndex = index - 1;

        QSharedPointer<OrtsTrackProfile> profile =
                profileFromStfNode(root, id);
        const QString memberKey = objectTypeKey(profile->objectType) + ":"
                + QString::number((int)profile->objectRole);
        if(profile->objectTypeValid && memberKeys.contains(memberKey)){
            localDiagnostics.append(
                    "Duplicate ObjectType in one profile family ignored: "
                    + objectTypeKey(profile->objectType) + " "
                    + objectRoleKey(profile->objectRole));
            continue;
        }
        if(profile->objectTypeValid)
            memberKeys.insert(memberKey);
        parsedProfiles.append(profile);
    }
    if(parsedProfiles.isEmpty()){
        localDiagnostics.append("Missing TrProfile block");
        if(diagnostics != nullptr)
            diagnostics->append(localDiagnostics);
        return {};
    }
    if(diagnostics != nullptr){
        diagnostics->append(localDiagnostics);
        for(const QSharedPointer<OrtsTrackProfile> &profile : parsedProfiles)
            diagnostics->append(profile->diagnostics);
    }
    return parsedProfiles;
}

QSharedPointer<OrtsTrackProfile> OrtsTrackProfileParser::parseStf(
        const QString &text, const QString &id, QStringList *diagnostics) {
    const QVector<QSharedPointer<OrtsTrackProfile>> parsedProfiles =
            parseStfProfiles(text, id, diagnostics);
    return parsedProfiles.isEmpty() ? QSharedPointer<OrtsTrackProfile>()
                                    : parsedProfiles.first();
}

QSharedPointer<OrtsTrackProfile> OrtsTrackProfileParser::parseXml(
        const QString &text, const QString &id, QStringList *diagnostics) {
    QSharedPointer<OrtsTrackProfile> profile(new OrtsTrackProfile());
    profile->familyId = id;
    profile->id = id;
    QXmlStreamReader xml(text);
    bool sawRoot = false;
    OrtsProfileLod *lod = nullptr;
    OrtsProfileLodItem *item = nullptr;
    OrtsProfilePolyline *polyline = nullptr;

    while(!xml.atEnd()){
        xml.readNext();
        if(!xml.isStartElement())
            continue;
        const QString element = xml.name().toString();
        const QXmlStreamAttributes attributes = xml.attributes();
        if(element.compare("TrProfile", Qt::CaseInsensitive) == 0){
            sawRoot = true;
            profile->name = attribute(attributes, "Name", id);
            const QString objectTypeValue = attribute(attributes, "ObjectType");
            profile->objectTypeValid = parseObjectTypeValues(
                    objectTypeValue.split(
                        QRegularExpression("\\s+"), Qt::SkipEmptyParts),
                    *profile);
            profile->id = id + profileRoleSuffix(profile->objectRole);
            profile->lodMethod = lodMethod(attribute(attributes, "LODMethod"));
            profile->chordSpanDegrees =
                    numberValue(attribute(attributes, "ChordSpan", "1"));
            profile->pitchControl = pitchControl(attribute(attributes, "PitchControl"));
            profile->pitchControlScalar =
                    numberValue(attribute(attributes, "PitchControlScalar", "0"));
            profile->includedShapes = splitFilters(attribute(attributes, "IncludedShapes"));
            profile->excludedShapes = splitFilters(attribute(attributes, "ExcludedShapes"));
            profile->includedTextures = splitFilters(attribute(attributes, "IncludedTextures"));
            profile->excludedTextures = splitFilters(attribute(attributes, "ExcludedTextures"));
            profile->trackGauge = numberValue(attribute(attributes, "TrackGauge", "1.435"));
            profile->superElevationMethod =
                    elevationMethod(attribute(attributes, "SuperElevationMethod", "Outside"));
        } else if(element.compare("LOD", Qt::CaseInsensitive) == 0){
            OrtsProfileLod newLod;
            newLod.cutoffRadius = numberValue(attribute(attributes, "CutoffRadius"));
            profile->lods.append(newLod);
            lod = &profile->lods.last();
            item = nullptr;
            polyline = nullptr;
        } else if(element.compare("LODItem", Qt::CaseInsensitive) == 0 && lod != nullptr){
            OrtsProfileLodItem newItem;
            newItem.name = attribute(attributes, "Name");
            newItem.textureName = attribute(attributes, "TexName");
            newItem.shaderName = attribute(attributes, "ShaderName");
            newItem.lightModelName = attribute(attributes, "LightModelName");
            newItem.alphaTestMode = attribute(attributes, "AlphaTestMode").toInt();
            newItem.textureAddressMode = attribute(attributes, "TexAddrModeName");
            newItem.alternativeTexture = attribute(attributes, "ESD_Alternative_Texture").toInt();
            newItem.mipMapLodBias =
                    numberValue(attribute(attributes, "MipMapLevelOfDetailBias", "0"));
            lod->items.append(newItem);
            item = &lod->items.last();
            polyline = nullptr;
        } else if(element.compare("Polyline", Qt::CaseInsensitive) == 0 && item != nullptr){
            OrtsProfilePolyline newPolyline;
            newPolyline.name = attribute(attributes, "Name");
            attributeValues(attributes, "DeltaTexCoord", newPolyline.deltaTexCoord, 2);
            item->polylines.append(newPolyline);
            polyline = &item->polylines.last();
        } else if(element.compare("Vertex", Qt::CaseInsensitive) == 0 && polyline != nullptr){
            OrtsProfileVertex vertex;
            const bool positionValid =
                    attributeValues(attributes, "Position", vertex.position, 3);
            const bool normalValid =
                    attributeValues(attributes, "Normal", vertex.normal, 3)
                    && (std::abs(vertex.normal[0]) > 0.000001f
                        || std::abs(vertex.normal[1]) > 0.000001f
                        || std::abs(vertex.normal[2]) > 0.000001f);
            const bool textureValid =
                    attributeValues(attributes, "TexCoord", vertex.texCoord, 2);
            vertex.valid = positionValid && normalValid && textureValid;
            vertex.positionControl =
                    positionControl(attribute(attributes, "PositionControl", "All"));
            polyline->vertices.append(vertex);
        }
    }

    if(xml.hasError())
        profile->diagnostics.append("XML error: " + xml.errorString());
    if(!sawRoot)
        profile->diagnostics.append("Missing TrProfile root element");
    validateProfile(*profile);
    if(xml.hasError() || !sawRoot)
        profile->valid = false;
    if(diagnostics != nullptr)
        diagnostics->append(profile->diagnostics);
    return profile;
}

QVector<QSharedPointer<OrtsTrackProfile>>
OrtsTrackProfileParser::parseXmlProfiles(
        const QString &text, const QString &id, QStringList *diagnostics) {
    const QSharedPointer<OrtsTrackProfile> profile =
            parseXml(text, id, diagnostics);
    if(profile == nullptr)
        return {};
    return {profile};
}

void OrtsTrackProfileCatalog::load(const QString &routePath, bool forceReload) {
    const QString normalizedPath = QDir::cleanPath(routePath);
    if(!forceReload && loadedRoutePath == normalizedPath)
        return;

    loadedRoutePath = normalizedPath;
    profiles.clear();
    loadDiagnostics.clear();

    QDir directory(normalizedPath + "/TRACKPROFILES");
    if(!directory.exists())
        return;

    QMap<QString, QString> selectedFiles;
    const QFileInfoList entries = directory.entryInfoList(
            QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for(const QFileInfo &entry : entries){
        const QString suffix = entry.suffix().toLower();
        const QString stem = entry.completeBaseName();
        if(suffix != "stf" && suffix != "xml")
            continue;
        const QString key = stem.toLower();
        if(!selectedFiles.contains(key) || suffix == "xml")
            selectedFiles[key] = entry.absoluteFilePath();
    }

    QStringList keys = selectedFiles.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &left, const QString &right){
        if(left == right)
            return false;
        if(left == "trprofile")
            return true;
        if(right == "trprofile")
            return false;
        return left < right;
    });

    for(const QString &sourceKey : keys){
        const QString sourceFile = selectedFiles[sourceKey];
        QStringList diagnostics;
        const QVector<QSharedPointer<OrtsTrackProfile>> parsedProfiles =
                OrtsTrackProfileParser::parseFileProfiles(
                    sourceFile, &diagnostics);
        for(const QString &diagnostic : diagnostics)
            loadDiagnostics.append(
                    QFileInfo(sourceFile).fileName() + ": " + diagnostic);
        for(const QSharedPointer<OrtsTrackProfile> &profile : parsedProfiles){
            if(profile == nullptr || !profile->valid)
                continue;
            const QString profileKey = catalogKey(
                    profile->objectType, profile->id);
            if(profiles.contains(profileKey)){
                loadDiagnostics.append(
                        QFileInfo(sourceFile).fileName()
                        + ": duplicate profile identity ignored: "
                        + profile->id);
                continue;
            }
            profiles.insert(profileKey, profile);
        }
    }
}

QStringList OrtsTrackProfileCatalog::profileIds() {
    QStringList result;
    for(const QSharedPointer<OrtsTrackProfile> &profile : profiles){
        if(!result.contains(profile->id, Qt::CaseInsensitive))
            result.append(profile->id);
    }
    return result;
}

QStringList OrtsTrackProfileCatalog::profileIds(
        OrtsTrackProfile::ObjectType objectType, bool selectableOnly) {
    QStringList result;
    for(const QSharedPointer<OrtsTrackProfile> &profile : profiles){
        if(profile->objectType != objectType)
            continue;
        if(selectableOnly
                && profile->objectRole != OrtsTrackProfile::ObjectRole::Main
                && profile->objectRole != OrtsTrackProfile::ObjectRole::Single)
            continue;
        if(!result.contains(profile->id, Qt::CaseInsensitive))
            result.append(profile->id);
    }
    std::sort(result.begin(), result.end(), [](const QString &left,
            const QString &right){
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QStringList OrtsTrackProfileCatalog::familyIds(
        OrtsTrackProfile::ObjectType objectType) {
    QStringList result;
    for(const QSharedPointer<OrtsTrackProfile> &profile : profiles){
        if(profile->objectType == objectType
                && !result.contains(profile->familyId, Qt::CaseInsensitive))
            result.append(profile->familyId);
    }
    std::sort(result.begin(), result.end(), [](const QString &left,
            const QString &right){
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QVector<OrtsTrackProfile::ObjectRole>
OrtsTrackProfileCatalog::familyRoles(
        const QString &familyId,
        OrtsTrackProfile::ObjectType objectType) {
    const OrtsTrackProfile::ObjectRole roles[] = {
        OrtsTrackProfile::ObjectRole::Main,
        OrtsTrackProfile::ObjectRole::Single,
        OrtsTrackProfile::ObjectRole::Left,
        OrtsTrackProfile::ObjectRole::Middle,
        OrtsTrackProfile::ObjectRole::Right
    };
    QVector<OrtsTrackProfile::ObjectRole> result;
    for(OrtsTrackProfile::ObjectRole role : roles){
        const QSharedPointer<const OrtsTrackProfile> profile = find(
                profileId(familyId, role), objectType);
        if(profile != nullptr && profile->familyId.compare(
                familyId, Qt::CaseInsensitive) == 0)
            result.append(role);
    }
    return result;
}

bool OrtsTrackProfileCatalog::hasFamily(
        const QString &familyId,
        OrtsTrackProfile::ObjectType objectType) {
    for(const QSharedPointer<OrtsTrackProfile> &profile : profiles){
        if(profile->objectType == objectType
                && profile->familyId.compare(
                    familyId, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QStringList OrtsTrackProfileCatalog::selectionNames() {
    return profileIds();
}

QStringList OrtsTrackProfileCatalog::selectionNames(
        OrtsTrackProfile::ObjectType objectType, bool selectableOnly) {
    return profileIds(objectType, selectableOnly);
}

QStringList OrtsTrackProfileCatalog::diagnostics() {
    return loadDiagnostics;
}

QSharedPointer<const OrtsTrackProfile> OrtsTrackProfileCatalog::find(
        const QString &name) {
    const OrtsTrackProfile::ObjectType types[] = {
        OrtsTrackProfile::ObjectType::Track,
        OrtsTrackProfile::ObjectType::Road,
        OrtsTrackProfile::ObjectType::Static
    };
    for(OrtsTrackProfile::ObjectType objectType : types){
        const QSharedPointer<const OrtsTrackProfile> profile =
                find(name, objectType);
        if(profile != nullptr)
            return profile;
    }
    return {};
}

QSharedPointer<const OrtsTrackProfile> OrtsTrackProfileCatalog::find(
        const QString &name, OrtsTrackProfile::ObjectType objectType) {
    return profiles.value(catalogKey(objectType, name));
}

QSharedPointer<const OrtsTrackProfile> OrtsTrackProfileCatalog::findRole(
        const QString &name, OrtsTrackProfile::ObjectType objectType,
        OrtsTrackProfile::ObjectRole role) {
    const QSharedPointer<const OrtsTrackProfile> selected =
            find(name, objectType);
    if(selected == nullptr)
        return {};
    if(selected->objectRole == OrtsTrackProfile::ObjectRole::Single)
        return selected;
    if(selected->objectRole != OrtsTrackProfile::ObjectRole::Main)
        return selected;

    const QSharedPointer<const OrtsTrackProfile> requested = find(
            profileId(selected->familyId, role), objectType);
    return requested == nullptr ? selected : requested;
}

QVector<QSharedPointer<const OrtsTrackProfile>>
OrtsTrackProfileCatalog::profilesForPaths(
        const QString &name, OrtsTrackProfile::ObjectType objectType,
        const QVector<float> &pathRotations) {
    QVector<QSharedPointer<const OrtsTrackProfile>> result;
    const int pathCount = pathRotations.size();
    if(pathCount <= 0)
        return result;
    const QSharedPointer<const OrtsTrackProfile> selected =
            find(name, objectType);
    if(selected == nullptr)
        return result;
    result.reserve(pathCount);

    int groupStart = 0;
    while(groupStart < pathCount){
        int groupEnd = groupStart + 1;
        while(groupEnd < pathCount
                && std::abs(std::remainder(
                    pathRotations[groupEnd] - pathRotations[groupStart],
                    360.0f)) <= 0.1f)
            groupEnd++;

        for(int pathIndex = groupStart; pathIndex < groupEnd; pathIndex++){
            OrtsTrackProfile::ObjectRole role =
                    OrtsTrackProfile::ObjectRole::Main;
            if(selected->objectRole == OrtsTrackProfile::ObjectRole::Main
                    && groupEnd - groupStart > 1){
                if(pathIndex == groupStart)
                    role = OrtsTrackProfile::ObjectRole::Left;
                else if(pathIndex == groupEnd - 1)
                    role = OrtsTrackProfile::ObjectRole::Right;
                else
                    role = OrtsTrackProfile::ObjectRole::Middle;
            }
            result.append(findRole(name, objectType, role));
        }
        groupStart = groupEnd;
    }
    return result;
}

QString OrtsTrackProfileCatalog::profileId(
        const QString &familyId, OrtsTrackProfile::ObjectRole role) {
    return familyId + profileRoleSuffix(role);
}

QString OrtsTrackProfileCatalog::routePath() {
    return loadedRoutePath;
}
