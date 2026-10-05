/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "GLUU.h"
#include <tsre/math3d/GLMatrix.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/Game.h>
#include <tsre/math3d/Vector4f.h>
#include <QDebug>
#include <QFile>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions_3_0>
#include <QOpenGLVersionFunctionsFactory>
#include <QElapsedTimer>
#include <cmath>
#include <tsre/renderer/Renderer.h>
#ifndef __APPLE__
#include <GL/gl.h>
#else
#include <OpenGL/gl.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

GLUU* GLUU::get() {
    static GLUU* gluu = new GLUU();
    return gluu;
}

GLUU::GLUU() {
    alphaTest = 0.3;
    currentAlphaTest = 0.3;
    pMatrix = new float[16];
    fMatrix = new float[16];
    pShadowMatrix = new float[16];
    pShadowMatrix2 = new float[16];
    pShadowMatrix0 = new float[16];
    Mat4::identity(pShadowMatrix0);
    mvMatrix = new float[16];
    objStrMatrix = new float[16];
}

GLUU::~GLUU() {

}

// GLSL 3.30 sources; OpenGL 3.3 is the minimum on every platform.
QString GLUU::shaderDirectory() {
    return QString("appdata/")+Game::AppDataVersion+"/shaders330";
}

namespace {

QByteArray readShaderFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Shader file not found" << path;
        return QByteArray();
    }
    return file.readAll();
}

QByteArray expandIncludes(const QString &directory, const QByteArray &source, int depth) {
    if (depth > 8) {
        qWarning() << "Shader includes nested too deeply in" << directory;
        return source;
    }
    QByteArray out;
    for (const QByteArray &line : source.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.startsWith("#include")) {
            const int open = trimmed.indexOf('"');
            const int close = trimmed.lastIndexOf('"');
            if (open >= 0 && close > open) {
                const QString name = QString::fromUtf8(trimmed.mid(open + 1, close - open - 1));
                out += expandIncludes(directory, readShaderFile(directory + "/" + name), depth + 1);
                out += '\n';
                continue;
            }
        }
        out += line;
        out += '\n';
    }
    return out;
}

}

QByteArray GLUU::shaderSource(const QString &directory, const QString &name,
                              const QString &type, const QStringList &defines) {
    QByteArray source = expandIncludes(directory,
                                       readShaderFile(directory + "/" + name + "." + type), 0);
    if (defines.isEmpty())
        return source;
    // Defines go right after #version, which must stay the first statement.
    QByteArray defineLines;
    for (const QString &define : defines)
        defineLines += "#define " + define.toUtf8() + " 1\n";
    const int version = source.indexOf("#version");
    const int lineEnd = version >= 0 ? source.indexOf('\n', version) : -1;
    if (lineEnd < 0)
        return defineLines + source;
    return source.left(lineEnd + 1) + defineLines + source.mid(lineEnd + 1);
}

Shader *GLUU::terrainVariant(Shader *shader) const {
    return terrainVariants.value(shader, shader);
}

Shader *GLUU::unlitVariant(Shader *shader) const {
    return unlitVariants.value(shader, shader);
}

Shader *GLUU::pbrVariant(Shader *shader) const {
    return pbrVariants.value(shader, shader);
}

Shader *GLUU::waterVariant(Shader *shader) const {
    return waterVariants.value(shader, shader);
}

float GLUU::animationSeconds() {
    static QElapsedTimer clock;
    static quint64 frame = 0;
    static float seconds = 0.0f;
    if (Game::animationFrozen)
        return 0.0f;
    if (!clock.isValid())
        clock.start();
    if (frame != Renderer::frameNumber() || seconds == 0.0f) {
        frame = Renderer::frameNumber();
        // Wrapped hourly to keep the wave phases precise.
        seconds = float(std::fmod(clock.elapsed() / 1000.0, 3600.0));
    }
    return seconds;
}

void GLUU::initShader() {
    QOpenGLContext *context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions *extra = context->extraFunctions();
    QOpenGLFunctions_3_0 *functions30 =
            QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_0>(context);
    struct ShaderDefinition {
        QString name;
        QString vertexSource;
        QString fragmentSource;
        QStringList defines;
    };
    // Standard programs leave out terrain code; each has a "<name>Terrain"
    // variant built with TSRE_TERRAIN that draws terrain packets, and a
    // "<name>Unlit" variant built with TSRE_UNLIT for overlays and UI.
    // Selection draws terrain and objects with one program, so it always has
    // the terrain code.
    const QStringList terrain{"TSRE_TERRAIN"};
    const QStringList unlit{"TSRE_UNLIT"};
    QVector<ShaderDefinition> shaderDefinitions;
    shaderDefinitions.push_back({"StandardFog", "StandardFog", "StandardFog", {}});
    shaderDefinitions.push_back({"StandardFast", "StandardFog", "StandardFast", {}});
    shaderDefinitions.push_back({"StandardFogStoredCoords", "StandardFogStoredCoords", "StandardFogStoredCoords", {}});
    shaderDefinitions.push_back({"StandardFogTerrain", "StandardFog", "StandardFog", terrain});
    shaderDefinitions.push_back({"StandardFastTerrain", "StandardFog", "StandardFast", terrain});
    shaderDefinitions.push_back({"StandardFogStoredCoordsTerrain", "StandardFogStoredCoords", "StandardFogStoredCoords", terrain});
    shaderDefinitions.push_back({"StandardFogUnlit", "StandardFog", "StandardFog", unlit});
    shaderDefinitions.push_back({"StandardFastUnlit", "StandardFog", "StandardFast", unlit});
    shaderDefinitions.push_back({"StandardFogStoredCoordsUnlit", "StandardFogStoredCoords", "StandardFogStoredCoords", unlit});
    shaderDefinitions.push_back({"Shadows", "Shadows", "Shadows", {}});
    shaderDefinitions.push_back({"Selection", "StandardFog", "Selection", terrain});
    // Metallic-roughness materials (glTF) for the main program only.
    shaderDefinitions.push_back({"StandardFogPbr", "StandardFog", "StandardFog", {"TSRE_PBR"}});
    // Water surfaces, for the main and the fast program.
    shaderDefinitions.push_back({"StandardFogWater", "StandardFog", "StandardFog", {"TSRE_WATER"}});

    const QString directory = shaderDirectory();
    for(int i = 0; i < shaderDefinitions.size(); i++ ){
        const ShaderDefinition &definition = shaderDefinitions[i];
        shaders[definition.name] = new Shader();
        if(!shaders[definition.name]->addShaderFromSourceCode(QOpenGLShader::Vertex,
                shaderSource(directory, definition.vertexSource, "vs", definition.defines))){
            qDebug() << "Loading shader .vs file failed.";
        }
        if(!shaders[definition.name]->addShaderFromSourceCode(QOpenGLShader::Fragment,
                shaderSource(directory, definition.fragmentSource, "fs", definition.defines))){
            qDebug() << "Loading shader .fs file failed.";
        }
        currentShader = shaders[definition.name];
        currentShader->bindAttributeLocation("vertex", 0);
        currentShader->bindAttributeLocation("aTextureCoord", 1);
        currentShader->bindAttributeLocation("normal", 2);
        currentShader->bindAttributeLocation("alpha", 3);
        currentShader->bindAttributeLocation("tangent", 4);
        currentShader->bindAttributeLocation("aTextureCoord1", 5);
        currentShader->bindAttributeLocation("vertexColor", 6);
        if(definition.name == "Selection" && functions30 != nullptr)
            functions30->glBindFragDataLocation(currentShader->programId(), 0,
                                                "selectionResult");
        if(!currentShader->link()){
            qDebug() << "Shader link failed.";
        }
        if(definition.name == "Selection"
                && extra->glGetFragDataLocation(currentShader->programId(),
                                                "selectionResult") != 0){
            qWarning() << "Selection shader output is not bound to color attachment 0";
        }
        if(!currentShader->bind()){
            qDebug() << "Shader bind failed.";
        }
        currentShader->pMatrixUniform = currentShader->uniformLocation("uPMatrix");
        currentShader->fMatrixUniform = currentShader->uniformLocation("uFMatrix");
        currentShader->pShadowMatrixUniform = currentShader->uniformLocation("uShadowPMatrix");
        currentShader->pShadow2MatrixUniform = currentShader->uniformLocation("uShadow2PMatrix");
        currentShader->pShadow0MatrixUniform = currentShader->uniformLocation("uShadow0PMatrix");
        currentShader->mvMatrixUniform = currentShader->uniformLocation("uMVMatrix");
        currentShader->msMatrixUniform = currentShader->uniformLocation("uMSMatrix");
        currentShader->lod = currentShader->uniformLocation("lod");
        currentShader->sun = currentShader->uniformLocation("sun");

        currentShader->skyColor = currentShader->uniformLocation("skyColor");

        currentShader->shaderAlpha = currentShader->uniformLocation("isAlpha");
        currentShader->shaderAlphaTest = currentShader->uniformLocation("alphaTest");
        currentShader->shaderTextureEnabled = currentShader->uniformLocation("textureEnabled");
        currentShader->shaderShapeColor = currentShader->uniformLocation("shapeColor");
        currentShader->shaderSelectionId = currentShader->uniformLocation("selectionId");
        currentShader->shaderEnableNormals = currentShader->uniformLocation("enableNormals");
        currentShader->shaderDiffuseColor = currentShader->uniformLocation("diffuseColor");
        currentShader->shaderAmbientColor = currentShader->uniformLocation("ambientColor");
        currentShader->shaderSpecularColor = currentShader->uniformLocation("specularColor");
        currentShader->shaderLightDirection = currentShader->uniformLocation("lightDirection");
        currentShader->shaderSecondTexEnabled = currentShader->uniformLocation("secondTexEnabled");
        currentShader->terrainTextureRemap = currentShader->uniformLocation("terrainTextureRemap");
        currentShader->shaderShadowsEnabled = currentShader->uniformLocation("shadowsEnabled");
        currentShader->shaderBrightness = currentShader->uniformLocation("colorBrightness");
        currentShader->shaderFogDensity = currentShader->uniformLocation("fogDensity");
        currentShader->shadow1Res = currentShader->uniformLocation("shadow1Res");
        currentShader->shadow2Res = currentShader->uniformLocation("shadow2Res");
        currentShader->shadow2Bias = currentShader->uniformLocation("shadow2Bias");
        currentShader->shadowMapScale = currentShader->uniformLocation("shadowMapScale");
        currentShader->shadowNormalOffset = currentShader->uniformLocation("shadowNormalOffset");
        currentShader->shadowDepthBias = currentShader->uniformLocation("shadowDepthBias");
        currentShader->shadowLightDirection = currentShader->uniformLocation("shadowLightDirection");
        currentShader->terrainPaged = currentShader->uniformLocation("terrainPaged");
        currentShader->terrainVerticesPerPatch = currentShader->uniformLocation("terrainVerticesPerPatch");
        currentShader->terrainPatchSide = currentShader->uniformLocation("terrainPatchSide");
        currentShader->terrainSampleSpacing = currentShader->uniformLocation("terrainSampleSpacing");
        currentShader->terrainApplyGaps = currentShader->uniformLocation("terrainApplyGaps");
        currentShader->terrainMapPass = currentShader->uniformLocation("terrainMapPass");
        currentShader->terrainMaterialEnabled = currentShader->uniformLocation("terrainMaterialEnabled");
        currentShader->terrainMaterialMap = currentShader->uniformLocation("terrainMaterialMap");
        currentShader->terrainMaterialTextures = currentShader->uniformLocation("terrainMaterialTextures");
        currentShader->terrainMaterialDetails = currentShader->uniformLocation("terrainMaterialDetails");
        currentShader->terrainMaterialParams = currentShader->uniformLocation("terrainMaterialParams");
        currentShader->instanced = currentShader->uniformLocation("instanced");
        currentShader->instanceBase = currentShader->uniformLocation("instanceBase");
        currentShader->instanceMatrices = currentShader->uniformLocation("instanceMatrices");
        currentShader->terrainMaterialMapRemap = currentShader->uniformLocation("terrainMaterialMapRemap");
        currentShader->terrainMaterialMapSide = currentShader->uniformLocation("terrainMaterialMapSide");
        currentShader->terrainMaterialNoiseScale = currentShader->uniformLocation("terrainMaterialNoiseScale");
        currentShader->pbrBaseColor = currentShader->uniformLocation("pbrBaseColor");
        currentShader->pbrMetallicRoughness = currentShader->uniformLocation("pbrMetallicRoughness");
        currentShader->pbrEmissive = currentShader->uniformLocation("pbrEmissive");
        currentShader->pbrNormalScale = currentShader->uniformLocation("pbrNormalScale");
        currentShader->pbrOcclusionStrength = currentShader->uniformLocation("pbrOcclusionStrength");
        currentShader->pbrAlphaCutoff = currentShader->uniformLocation("pbrAlphaCutoff");
        currentShader->pbrBlend = currentShader->uniformLocation("pbrBlend");
        currentShader->pbrUnlit = currentShader->uniformLocation("pbrUnlit");
        currentShader->pbrTextures = currentShader->uniformLocation("pbrTextures");
        currentShader->pbrTexCoords = currentShader->uniformLocation("pbrTexCoords");
        currentShader->pbrUvTransform = currentShader->uniformLocation("pbrUvTransform");
        currentShader->pbrUvTransforms = currentShader->uniformLocation("pbrUvTransforms");
        currentShader->pbrClearcoat = currentShader->uniformLocation("pbrClearcoat");
        currentShader->cameraPosition = currentShader->uniformLocation("cameraPosition");
        currentShader->environmentMapLevels = currentShader->uniformLocation("environmentMapLevels");
        currentShader->waterTime = currentShader->uniformLocation("waterTime");
        currentShader->waterLayers = currentShader->uniformLocation("waterLayers");
        currentShader->waterReflectionView = currentShader->uniformLocation("waterReflectionView");
        currentShader->clipPlane = currentShader->uniformLocation("clipPlane");

        const GLuint terrainBlock = extra->glGetUniformBlockIndex(
                    currentShader->programId(), "TerrainPatchBlock");
        if (terrainBlock != GL_INVALID_INDEX)
            extra->glUniformBlockBinding(currentShader->programId(), terrainBlock, 0);
        if (currentShader->terrainPaged >= 0)
            currentShader->setUniformValue(currentShader->terrainPaged, 0);
        if (currentShader->shaderSelectionId >= 0){
            const quint32 selectionUniformProbe = 0xdaa55aa5u;
            quint32 selectionUniformValue = 0;
            setSelectionId(selectionUniformProbe);
            extra->glGetUniformuiv(currentShader->programId(),
                                   currentShader->shaderSelectionId,
                                   &selectionUniformValue);
            if(selectionUniformValue != selectionUniformProbe)
                qWarning() << "Selection uint uniform test failed";
            setSelectionId(0);
        }

        unsigned int tex1 = currentShader->uniformLocation("uSampler");
        currentShader->setUniformValue(tex1, 0);
        unsigned int tex2 = currentShader->uniformLocation("uSampler2");
        currentShader->setUniformValue(tex2, 1);
        unsigned int tex3 = currentShader->uniformLocation("shadow1");
        currentShader->setUniformValue(tex3, 2);
        unsigned int tex4 = currentShader->uniformLocation("shadow2");
        currentShader->setUniformValue(tex4, 3);
        // Unit 9 holds the near shadow map.
        unsigned int tex0 = currentShader->uniformLocation("shadow0");
        currentShader->setUniformValue(tex0, 9);
        // Units 4-7 hold the procedural terrain map, material and detail
        // arrays and the per-material parameters.
        if (currentShader->terrainMaterialMap >= 0)
            currentShader->setUniformValue(currentShader->terrainMaterialMap, 4);
        if (currentShader->terrainMaterialTextures >= 0)
            currentShader->setUniformValue(currentShader->terrainMaterialTextures, 5);
        if (currentShader->terrainMaterialDetails >= 0)
            currentShader->setUniformValue(currentShader->terrainMaterialDetails, 6);
        if (currentShader->terrainMaterialParams >= 0)
            currentShader->setUniformValue(currentShader->terrainMaterialParams, 7);
        // Unit 8 holds the instance matrix buffer.
        if (currentShader->instanceMatrices >= 0)
            currentShader->setUniformValue(currentShader->instanceMatrices, 8);
        // Unit 10 holds the environment map, units 11-14 the metallic-roughness,
        // normal, occlusion and emissive maps, and units 4-6 (terrain-only
        // elsewhere) the clearcoat maps. Water uses units 4 and 5 for its
        // lower layers, unit 6 for the planar reflection and unit 15 for the
        // wave map.
        const struct { const char *name; int unit; } pbrSamplers[] = {
            {"environmentMap", 10}, {"pbrMetallicRoughnessMap", 11}, {"pbrNormalMap", 12},
            {"pbrOcclusionMap", 13}, {"pbrEmissiveMap", 14}, {"pbrClearcoatMap", 4},
            {"pbrClearcoatRoughnessMap", 5}, {"pbrClearcoatNormalMap", 6},
            {"waterBottomMap", 4}, {"waterMiddleMap", 5}, {"waterReflectionMap", 6},
            {"waterNormalMap", 15}};
        for (const auto &sampler : pbrSamplers) {
            const int location = currentShader->uniformLocation(sampler.name);
            if (location >= 0)
                currentShader->setUniformValue(location, sampler.unit);
        }
        if (currentShader->instanced >= 0)
            currentShader->setUniformValue(currentShader->instanced, 0);
        if (currentShader->terrainMaterialEnabled >= 0)
            currentShader->setUniformValue(currentShader->terrainMaterialEnabled, 0);
        currentShader->release();
    }
    
    for (const QString &name : {QString("StandardFog"), QString("StandardFast"),
                                QString("StandardFogStoredCoords")})
    {
        terrainVariants[shaders[name]] = shaders[name + "Terrain"];
        unlitVariants[shaders[name]] = shaders[name + "Unlit"];
    }
    pbrVariants[shaders["StandardFog"]] = shaders["StandardFogPbr"];
    waterVariants[shaders["StandardFog"]] = shaders["StandardFogWater"];
    waterVariants[shaders["StandardFast"]] = shaders["StandardFogWater"];
    currentShader = shaders["StandardFog"];
}

void GLUU::setMatrixUniforms() {
    currentShader->setUniformValue(currentShader->pMatrixUniform, *reinterpret_cast<float(*)[4][4]> (pMatrix));
    currentShader->setUniformValue(currentShader->fMatrixUniform, *reinterpret_cast<float(*)[4][4]> (fMatrix));
    currentShader->setUniformValue(currentShader->pShadowMatrixUniform, *reinterpret_cast<float(*)[4][4]> (pShadowMatrix));
    currentShader->setUniformValue(currentShader->pShadow2MatrixUniform, *reinterpret_cast<float(*)[4][4]> (pShadowMatrix2));
    currentShader->setUniformValue(currentShader->pShadow0MatrixUniform, *reinterpret_cast<float(*)[4][4]> (pShadowMatrix0));
    currentShader->setUniformValue(currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (mvMatrix));
    currentShader->setUniformValue(currentShader->msMatrixUniform, *reinterpret_cast<float(*)[4][4]> (objStrMatrix));
    currentTexture = -1;
    
    currentShader->setUniformValue(currentShader->lod, Game::objectLod);
    currentShader->setUniformValue(currentShader->skyColor, fogColor[0],fogColor[1],fogColor[2],fogColor[3]);
    currentShader->setUniformValue(currentShader->shaderDiffuseColor, 0.7,0.7,0.7,0.7);
    currentShader->setUniformValue(currentShader->shaderAmbientColor, 0.3,0.3,0.3,0.3);
    currentShader->setUniformValue(currentShader->shaderSpecularColor, 1.0,1.0,1.0,1.0);
    currentShader->setUniformValue(currentShader->shaderLightDirection, Game::sunLightDirection[0], Game::sunLightDirection[1], Game::sunLightDirection[2]);
    currentShader->setUniformValue(currentShader->shaderAlpha, alpha);
    currentShader->setUniformValue(currentShader->shaderAlphaTest, alphaTest);
    textureEnabled = true;
    normalsEnabled = true;
    currentShader->setUniformValue(currentShader->shaderTextureEnabled, 1.0f);
    currentShader->setUniformValue(currentShader->shaderEnableNormals, 1.0f);
    currentShader->setUniformValue(currentShader->shaderSecondTexEnabled, 0.0f);
    currentShader->setUniformValue(currentShader->terrainTextureRemap, QVector3D());
    currentShader->setUniformValue(currentShader->shaderShadowsEnabled, Game::shadowsEnabled);
    currentShader->setUniformValue(currentShader->shaderBrightness, currentBrightness);
    currentShader->setUniformValue(currentShader->shaderFogDensity, fogDensity);
    if(currentShader->shaderSelectionId >= 0)
        setSelectionId(0);
    
    currentShader->setUniformValue(currentShader->shadow1Res, shadow1Res);
    currentShader->setUniformValue(currentShader->shadow2Res, shadow2Res);
    currentShader->setUniformValue(currentShader->shadow2Bias, shadow2Bias);
    currentShader->setUniformValue(currentShader->shadowMapScale, shadowMapScale[0],
            shadowMapScale[1], shadowMapScale[2], shadowMapScale[3]);
    currentShader->setUniformValue(currentShader->shadowNormalOffset, shadowNormalOffset[0],
            shadowNormalOffset[1], shadowNormalOffset[2]);
    currentShader->setUniformValue(currentShader->shadowDepthBias, shadowDepthBias[0],
            shadowDepthBias[1]);
    currentShader->setUniformValue(currentShader->shadowLightDirection, shadowLightDirection[0],
            shadowLightDirection[1], shadowLightDirection[2]);
    currentShader->setUniformValue(currentShader->cameraPosition, cameraPosition[0],
            cameraPosition[1], cameraPosition[2]);
    currentShader->setUniformValue(currentShader->environmentMapLevels, float(environmentMapLevels));
    if (currentShader->waterTime >= 0)
        currentShader->setUniformValue(currentShader->waterTime, animationSeconds());
    if (currentShader->waterReflectionView >= 0)
        currentShader->setUniformValue(currentShader->waterReflectionView, waterReflectionView[0],
                waterReflectionView[1], waterReflectionView[2], waterReflectionView[3]);
    if (currentShader->clipPlane >= 0)
        currentShader->setUniformValue(currentShader->clipPlane, clipPlane[0], clipPlane[1],
                clipPlane[2], clipPlane[3]);
};

void GLUU::disableTextures(Vector4f* color){
    currentShader->setUniformValue(currentShader->shaderShapeColor, color->x, color->y, color->z, color->c);
    if(!this->textureEnabled) return;
    this->textureEnabled = false;
    currentShader->setUniformValue(currentShader->shaderTextureEnabled, 0.0f);
}

void GLUU::disableTextures(Vector3f* color){
    currentShader->setUniformValue(currentShader->shaderShapeColor, color->x, color->y, color->z, 1.0);
    if(!this->textureEnabled) return;
    this->textureEnabled = false;
    currentShader->setUniformValue(currentShader->shaderTextureEnabled, 0.0f);
}

void GLUU::disableTextures(float x, float y, float z, float a){
    currentShader->setUniformValue(currentShader->shaderShapeColor, x, y, z, a);
    if(!this->textureEnabled) 
        return;
    this->textureEnabled = false;
    currentShader->setUniformValue(currentShader->shaderTextureEnabled, 0.0f);
}

void GLUU::setSelectionId(quint32 selectionId){
    if(currentShader == nullptr || currentShader->shaderSelectionId < 0)
        return;
    QOpenGLContext::currentContext()->extraFunctions()->glUniform1ui(
                currentShader->shaderSelectionId, selectionId);
}

/*bool GLUU::disableTexturesOptional(float x, float y, float z, float a){
    if(!this->textureEnabled) 
        return false;
    currentShader->setUniformValue(currentShader->shaderShapeColor, x, y, z, a);
    this->textureEnabled = false;
    currentShader->setUniformValue(currentShader->shaderTextureEnabled, 0.0f);
    return true;
}*/

void GLUU::enableTextures(){
    if(this->textureEnabled)
        return;
    this->textureEnabled = true;
    currentShader->setUniformValue(currentShader->shaderTextureEnabled, 1.0f);
}

void GLUU::disableNormals(){
    if(!this->normalsEnabled) 
        return;
    this->normalsEnabled = false;
    currentShader->setUniformValue(currentShader->shaderEnableNormals, 0.0f);
}

void GLUU::setBrightness(float val){
    if(currentBrightness == val)
        return;
    currentBrightness = val;
    currentShader->setUniformValue(currentShader->shaderBrightness, currentBrightness);
}

void GLUU::enableNormals(){
    if(this->normalsEnabled) return;
    this->normalsEnabled = true;
    currentShader->setUniformValue(currentShader->shaderEnableNormals, 1.0f);
}

void GLUU::bindTexture(QOpenGLFunctions *f, unsigned int texAddr){
    if(this->currentTexture == texAddr)
        return;
    this->currentTexture = texAddr;
    f->glBindTexture(GL_TEXTURE_2D, texAddr);
}

long long int GLUU::getMatrixHash(float* matrix){
    long long int out = 0;
    for(int i = 0; i < 16; i++){
        out *= 10;
        out += matrix[i]*1000.0;
    }
    return out;
}

void GLUU::makeShadowFramebuffer(unsigned int& frameBuffer, unsigned int& texture, int texSize, GLenum ATEX){
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    f->glGenFramebuffers(1, &frameBuffer);
    f->glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
    f->glActiveTexture(ATEX);
    f->glGenTextures(1, &texture);
    f->glBindTexture(GL_TEXTURE_2D, texture);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, texSize, texSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
    glDrawBuffer(GL_NONE); 
    if(f->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        qDebug() << "shadowbuffer1 fail";
}
