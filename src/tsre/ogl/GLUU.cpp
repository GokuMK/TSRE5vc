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
    imvMatrixStack = 0;
    alphaTest = 0.3;
    currentAlphaTest = 0.3;
    pMatrix = new float[16];
    fMatrix = new float[16];
    pShadowMatrix = new float[16];
    pShadowMatrix2 = new float[16];
    mvMatrix = new float[16];
    objStrMatrix = new float[16];
}

GLUU::~GLUU() {

}

const char* GLUU::getShader(QString shaderScript, QString type) {
#ifdef __APPLE__
    QFile* shaderData = new QFile(QString("appdata/")+Game::AppDataVersion+"/shaders330/"+shaderScript+"."+type);
#else
    QFile* shaderData = new QFile(QString("appdata/")+Game::AppDataVersion+"/shaders/"+shaderScript+"."+type);
#endif
    if (!shaderData->open(QIODevice::ReadOnly)){
        qDebug() << "Shader file not found " << shaderData->fileName();
        return "";
    }
    return (const char*) ReadFile::readRAW(shaderData)->data;
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
    };
    QVector<ShaderDefinition> shaderDefinitions;
    shaderDefinitions.push_back({"StandardFog", "StandardFog", "StandardFog"});
    shaderDefinitions.push_back({"StandardFogStoredCoords", "StandardFogStoredCoords", "StandardFogStoredCoords"});
    shaderDefinitions.push_back({"StandardBloom", "StandardBloom", "StandardBloom"});
    shaderDefinitions.push_back({"Shadows", "Shadows", "Shadows"});
    shaderDefinitions.push_back({"Selection", "StandardFog", "Selection"});

    for(int i = 0; i < shaderDefinitions.size(); i++ ){
        const ShaderDefinition &definition = shaderDefinitions[i];
        shaders[definition.name] = new Shader();
        if(!shaders[definition.name]->addShaderFromSourceCode(QOpenGLShader::Vertex, getShader(definition.vertexSource, "vs"))){
            qDebug() << "Loading shader .vs file failed.";
        }
        if(!shaders[definition.name]->addShaderFromSourceCode(QOpenGLShader::Fragment, getShader(definition.fragmentSource, "fs"))){
            qDebug() << "Loading shader .fs file failed.";
        }
        currentShader = shaders[definition.name];
        currentShader->bindAttributeLocation("vertex", 0);
        currentShader->bindAttributeLocation("aTextureCoord", 1);
        currentShader->bindAttributeLocation("normal", 2);
        currentShader->bindAttributeLocation("alpha", 3);
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
        currentShader->shaderShadowsEnabled = currentShader->uniformLocation("shadowsEnabled");
        currentShader->shaderBrightness = currentShader->uniformLocation("colorBrightness");
        currentShader->shaderFogDensity = currentShader->uniformLocation("fogDensity");
        currentShader->shadow1Res = currentShader->uniformLocation("shadow1Res");
        currentShader->shadow1Bias = currentShader->uniformLocation("shadow1Bias");
        currentShader->shadow2Res = currentShader->uniformLocation("shadow2Res");
        currentShader->shadow2Bias = currentShader->uniformLocation("shadow2Bias");
        currentShader->terrainPaged = currentShader->uniformLocation("terrainPaged");
        currentShader->terrainVerticesPerPatch = currentShader->uniformLocation("terrainVerticesPerPatch");
        currentShader->terrainPatchSide = currentShader->uniformLocation("terrainPatchSide");
        currentShader->terrainSampleSpacing = currentShader->uniformLocation("terrainSampleSpacing");
        currentShader->terrainApplyGaps = currentShader->uniformLocation("terrainApplyGaps");
        currentShader->terrainMapPass = currentShader->uniformLocation("terrainMapPass");

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
        currentShader->release();
    }
    
    //currentShader = shaders["StandardFog"];
    currentShader = shaders["StandardBloom"];
}

void GLUU::mvPushMatrix() {
    mvMatrixStack[imvMatrixStack++] = Mat4::clone(mvMatrix);
}

void GLUU::mvPopMatrix() {
    if (--imvMatrixStack < 0) return;
    delete[] mvMatrix;
    mvMatrix = mvMatrixStack[imvMatrixStack];
}

void GLUU::setMatrixUniforms() {
    currentShader->setUniformValue(currentShader->pMatrixUniform, *reinterpret_cast<float(*)[4][4]> (pMatrix));
    currentShader->setUniformValue(currentShader->fMatrixUniform, *reinterpret_cast<float(*)[4][4]> (fMatrix));
    currentShader->setUniformValue(currentShader->pShadowMatrixUniform, *reinterpret_cast<float(*)[4][4]> (pShadowMatrix));
    currentShader->setUniformValue(currentShader->pShadow2MatrixUniform, *reinterpret_cast<float(*)[4][4]> (pShadowMatrix2));
    currentShader->setUniformValue(currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (mvMatrix));
    currentShader->setUniformValue(currentShader->msMatrixUniform, *reinterpret_cast<float(*)[4][4]> (objStrMatrix));
    currentMsMatrinxHash = 0;
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
    currentShader->setUniformValue(currentShader->shaderShadowsEnabled, Game::shadowsEnabled);
    currentShader->setUniformValue(currentShader->shaderBrightness, currentBrightness);
    currentShader->setUniformValue(currentShader->shaderFogDensity, fogDensity);
    if(currentShader->shaderSelectionId >= 0)
        setSelectionId(0);
    
    currentShader->setUniformValue(currentShader->shadow1Res, shadow1Res);
    currentShader->setUniformValue(currentShader->shadow1Bias, shadow1Bias);
    currentShader->setUniformValue(currentShader->shadow2Res, shadow2Res);
    currentShader->setUniformValue(currentShader->shadow2Bias, shadow2Bias);
};

float GLUU::degToRad(float degrees) {
    return degrees * M_PI / 180.0;
}

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
