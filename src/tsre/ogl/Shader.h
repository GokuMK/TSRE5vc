/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SHADER_H
#define	SHADER_H

#include <QOpenGLShaderProgram>

class Shader : public QOpenGLShaderProgram {
public:
    Shader();
    virtual ~Shader();
    unsigned int shaderProgram;
    unsigned int vertexPositionAttribute;
    unsigned int textureCoordAttribute;
    unsigned int pShadowMatrixUniform;
    unsigned int pShadow2MatrixUniform;
    unsigned int pShadow0MatrixUniform;
    unsigned int pMatrixUniform;
    unsigned int fMatrixUniform;
    unsigned int mvMatrixUniform;
    unsigned int msMatrixUniform;
    unsigned int samplerUniform;
    unsigned int lod;
    unsigned int sun;
    unsigned int brightness;
    unsigned int skyColor;
    unsigned int skyLight;
    unsigned int shaderAlpha;
    unsigned int shaderAlphaTest;
    unsigned int shaderTextureEnabled;
    unsigned int shaderShapeColor;
    int shaderSelectionId = -1;
    unsigned int shaderEnableNormals;
    unsigned int shaderDiffuseColor;
    unsigned int shaderAmbientColor;
    unsigned int shaderSpecularColor;
    unsigned int shaderLightDirection;
    unsigned int shaderSecondTexEnabled;
    int terrainTextureRemap = -1;
    unsigned int shaderShadowsEnabled;
    unsigned int shaderBrightness;
    unsigned int shaderFogDensity;
    unsigned int shaderTransparency;
    unsigned int shadow1Res;
    unsigned int shadow2Res;
    unsigned int shadow2Bias;
    int shadowMapScale = -1;
    int shadowNormalOffset = -1;
    int shadowDepthBias = -1;
    int shadowLightDirection = -1;
    int terrainPaged;
    int terrainVerticesPerPatch;
    int terrainPatchSide;
    int terrainSampleSpacing;
    int terrainApplyGaps;
    int terrainMapPass;
    int terrainMaterialEnabled;
    int terrainMaterialMap;
    int terrainMaterialTextures;
    int terrainMaterialDetails;
    int terrainMaterialParams;
    int instanced;
    int instanceBase;
    int instanceMatrices;
    int terrainMaterialMapRemap;
    int terrainMaterialMapSide;
    int terrainMaterialNoiseScale;
    // Metallic-roughness materials (PBR variant) and image-based light.
    int pbrBaseColor = -1;
    int pbrMetallicRoughness = -1;
    int pbrEmissive = -1;
    int pbrNormalScale = -1;
    int pbrOcclusionStrength = -1;
    int pbrAlphaCutoff = -1;
    int pbrBlend = -1;
    int pbrUnlit = -1;
    int pbrTextures = -1;
    int pbrTexCoords = -1;
    int pbrUvTransform = -1;
    int pbrUvTransforms = -1;
    int pbrClearcoat = -1;
    int pbrSpecular = -1;
    int pbrIor = -1;
    int pbrTransmission = -1;
    int pbrAttenuationColor = -1;
    int cameraPosition = -1;
    int environmentMapLevels = -1;
    int waterTime = -1;
    int waterLayers = -1;
    int waterReflectionView = -1;
    int waterReflectionPlane = -1;
    int clipPlane = -1;
private:

};

#endif	/* SHADER_H */

