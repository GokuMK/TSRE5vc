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
#include <QTextStream>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>

QString OrtsTrackProfileCatalog::loadedRoutePath;
QMap<QString, QSharedPointer<OrtsTrackProfile>> OrtsTrackProfileCatalog::profiles;
QMap<QString, QString> OrtsTrackProfileCatalog::aliases;
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
    profile.valid = renderable && structureValid;
}

}

QSharedPointer<OrtsTrackProfile> OrtsTrackProfileParser::parseFile(
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
    QSharedPointer<OrtsTrackProfile> profile;
    if(QFileInfo(path).suffix().compare("xml", Qt::CaseInsensitive) == 0)
        profile = parseXml(text, id, diagnostics);
    else
        profile = parseStf(text, id, diagnostics);
    if(profile != nullptr)
        profile->sourcePath = path;
    return profile;
}

QSharedPointer<OrtsTrackProfile> OrtsTrackProfileParser::parseStf(
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
    int rootIndex = -1;
    for(int i = 0; i + 1 < tokens.size(); i++){
        if(tokens[i].compare("TrProfile", Qt::CaseInsensitive) == 0
                && tokens[i + 1] == "("){
            rootIndex = i;
            break;
        }
    }
    if(rootIndex < 0){
        localDiagnostics.append("Missing TrProfile block");
        if(diagnostics != nullptr)
            diagnostics->append(localDiagnostics);
        return {};
    }

    int index = rootIndex + 1;
    StfNode root;
    if(!parseStfNode(tokens, index, tokens[rootIndex], root, localDiagnostics)){
        if(diagnostics != nullptr)
            diagnostics->append(localDiagnostics);
        return {};
    }

    QSharedPointer<OrtsTrackProfile> profile(new OrtsTrackProfile());
    profile->id = id;
    profile->name = firstValue(root, "Name", id);
    profile->lodMethod = lodMethod(firstValue(root, "LODMethod"));
    profile->chordSpanDegrees = floatValue(root, "ChordSpan", 1.0f);
    profile->pitchControl = pitchControl(firstValue(root, "PitchControl"));
    profile->pitchControlScalar = floatValue(root, "PitchControlScalar", 0);
    profile->includedShapes = splitFilters(firstValue(root, "IncludedShapes"));
    profile->excludedShapes = splitFilters(firstValue(root, "ExcludedShapes"));
    profile->includedTextures = splitFilters(firstValue(root, "IncludedTextures"));
    profile->excludedTextures = splitFilters(firstValue(root, "ExcludedTextures"));
    profile->trackGauge = floatValue(root, "TrackGauge", 1.435f);
    profile->superElevationMethod =
            elevationMethod(firstValue(root, "SuperElevationMethod", "Outside"));

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
            item.alternativeTexture = intValue(*itemNode, "ESD_Alternative_Texture", 0);
            item.mipMapLodBias = floatValue(*itemNode, "MipMapLevelOfDetailBias", 0);
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
                        vertex.valid = values(positionNode->values, vertex.position, 3);
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
    profile->diagnostics.append(localDiagnostics);
    validateProfile(*profile);
    if(diagnostics != nullptr)
        diagnostics->append(profile->diagnostics);
    return profile;
}

QSharedPointer<OrtsTrackProfile> OrtsTrackProfileParser::parseXml(
        const QString &text, const QString &id, QStringList *diagnostics) {
    QSharedPointer<OrtsTrackProfile> profile(new OrtsTrackProfile());
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

void OrtsTrackProfileCatalog::load(const QString &routePath, bool forceReload) {
    const QString normalizedPath = QDir::cleanPath(routePath);
    if(!forceReload && loadedRoutePath.compare(normalizedPath, Qt::CaseInsensitive) == 0)
        return;

    loadedRoutePath = normalizedPath;
    profiles.clear();
    aliases.clear();
    loadDiagnostics.clear();

    QDir directory(normalizedPath + "/TrackProfiles");
    if(!directory.exists())
        return;

    QMap<QString, QString> selectedFiles;
    const QFileInfoList entries = directory.entryInfoList(
            QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for(const QFileInfo &entry : entries){
        const QString suffix = entry.suffix().toLower();
        const QString stem = entry.completeBaseName();
        if((!stem.startsWith("TrProfile", Qt::CaseInsensitive)
                && !stem.startsWith("default_", Qt::CaseInsensitive))
                || (suffix != "stf" && suffix != "xml"))
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

    QMap<QString, int> aliasCounts;
    for(const QString &key : keys){
        QStringList diagnostics;
        QSharedPointer<OrtsTrackProfile> profile =
                OrtsTrackProfileParser::parseFile(selectedFiles[key], &diagnostics);
        for(const QString &diagnostic : diagnostics)
            loadDiagnostics.append(QFileInfo(selectedFiles[key]).fileName() + ": " + diagnostic);
        if(profile == nullptr || !profile->valid)
            continue;
        profiles.insert(profile->id.toLower(), profile);
        if(!profile->name.trimmed().isEmpty())
            aliasCounts[profile->name.trimmed().toLower()]++;
    }

    for(auto iterator = profiles.cbegin(); iterator != profiles.cend(); ++iterator){
        const QString alias = iterator.value()->name.trimmed().toLower();
        if(!alias.isEmpty() && aliasCounts.value(alias) == 1
                && !profiles.contains(alias))
            aliases.insert(alias, iterator.key());
    }
}

QStringList OrtsTrackProfileCatalog::profileIds() {
    QStringList result;
    for(const QSharedPointer<OrtsTrackProfile> &profile : profiles)
        result.append(profile->id);
    return result;
}

QStringList OrtsTrackProfileCatalog::selectionNames() {
    QStringList result = profileIds();
    for(auto iterator = aliases.cbegin(); iterator != aliases.cend(); ++iterator){
        const QSharedPointer<OrtsTrackProfile> profile = profiles.value(iterator.value());
        if(profile != nullptr && !result.contains(profile->name, Qt::CaseInsensitive))
            result.append(profile->name);
    }
    return result;
}

QStringList OrtsTrackProfileCatalog::diagnostics() {
    return loadDiagnostics;
}

QSharedPointer<const OrtsTrackProfile> OrtsTrackProfileCatalog::find(
        const QString &nameOrAlias) {
    QString key = nameOrAlias.trimmed().toLower();
    if(aliases.contains(key))
        key = aliases.value(key);
    return profiles.value(key);
}

QString OrtsTrackProfileCatalog::routePath() {
    return loadedRoutePath;
}
