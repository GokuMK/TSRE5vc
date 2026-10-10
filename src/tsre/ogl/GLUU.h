/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef GLUU_H
#define	GLUU_H

#include <QOpenGLFunctions>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QHash>
#include <QStringList>
#include <tsre/math3d/Vector4f.h>
#include <tsre/math3d/Vector3f.h>
#include "Shader.h"
#include <tsre/Game.h>
//QT_FORWARD_DECLARE_CLASS(QOpenGLShaderProgram)


class GLUU {
public:
    enum RenderMode {
        RENDER_DEFAULT = 0,
        RENDER_SELECTION = 1
    };
    
    Shader *currentShader;
    QHash<QString, Shader*> shaders;
    
    float alpha;
    float alphaTest;
    float currentAlphaTest;
    float currentBrightness = 1.0;
    
    float fogDensity = Game::fogDensity;
    float shadow1Res = Game::shadow1Res;
    float shadow2Res = Game::shadow2Res;
    float shadow2Bias = Game::shadow2Bias;
    // Tap spread (x near, y middle) and depth-bias (z near, w middle) scales
    // of the near and middle shadow maps, which keep the blur and the bias
    // of surfaces without normals the same in world space in every map.
    float shadowMapScale[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    // Normal offset in metres of the near, middle and far shadow lookups, the
    // depth bias of the near and middle maps for surfaces with normals, and
    // the direction towards the shadow-casting sun. A zero offset keeps the
    // tuned slope bias.
    float shadowNormalOffset[3] = {0.0f, 0.0f, 0.0f};
    float shadowDepthBias[2] = {0.0f, 0.0f};
    float shadowLightDirection[3] = {0.0f, 1.0f, 0.0f};
    // Camera position in the submission space, for view-dependent shading,
    // and mipmap levels of the bound environment map (0: none bound).
    float cameraPosition[3] = {0.0f, 0.0f, 0.0f};
    int environmentMapLevels = 0;
    // Planar water reflection: inverse viewport size and mipmap levels in w
    // (0: none bound), and the mirror plane (n . p + d = 0).
    float waterReflectionView[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float waterReflectionPlane[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    // Clip plane of the water reflection pass; the default keeps everything.
    float clipPlane[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    //float fogColor[4]{0.5, 0.75, 1.0, 1.0};
    float fogColor[4] = {Game::fogColor[0], Game::fogColor[1], Game::fogColor[2], Game::fogColor[3]};
    float skyColor[4] = {Game::skyColor[0], Game::skyColor[1], Game::skyColor[2], Game::skyColor[3]};
    // The sun's (diffuse) and the ambient light; time of day changes them.
    float diffuseColor[4] = {0.7f, 0.7f, 0.7f, 0.7f};
    float ambientColor[4] = {0.3f, 0.3f, 0.3f, 0.3f};
    // Scale of local lights from the time of day (Daylight::localLights).
    float localLightAdaptation = 1.0f;
    // Scale of signal lights' glow from the time of day (Daylight::signalLights).
    float signalLightAdaptation = 1.0f;
    //float skyc[4]{200.0/255.0,218.0/255,225.0/255.0, 1.0};
    float sky[3]{1.0, 1.0, 1.0};
    
    //QMatrix4x4 m_proj;
    //QMatrix4x4 m_camera;
    //QMatrix4x4 m_world;
    float* pMatrix;
    float* fMatrix;
    float* pShadowMatrix;
    float* pShadowMatrix2;
    // Near shadow map, covering the camera's surroundings in more detail.
    float* pShadowMatrix0;
    float* mvMatrix;
    float* objStrMatrix;
    static GLUU *get();
    GLUU();
    virtual ~GLUU();
    void initShader();
    void setMatrixUniforms();
    void disableTextures(Vector4f* color);
    void disableTextures(Vector3f* color);
    void disableTextures(float x, float y, float z, float a);
    void setSelectionId(quint32 selectionId);
    void enableTextures();
    void disableNormals();
    void enableNormals();
    void setBrightness(float val);
    void bindTexture(QOpenGLFunctions *f, unsigned int texAddr);
    long long int getMatrixHash(float *matrix);
    void makeShadowFramebuffer(unsigned int &frameBuffer, unsigned int &texture, int texSize, GLenum ATEX );
    // Program that draws terrain packets in place of the given one: its
    // terrain variant, or the program itself when it has none.
    Shader *terrainVariant(Shader *shader) const;
    // Program that draws overlay and UI packets in place of the given one:
    // its unlit variant (no lighting, shadows or fog), or the program itself.
    Shader *unlitVariant(Shader *shader) const;
    // Program that draws metallic-roughness (PBR) packets in place of the
    // given one, or the program itself when it has none.
    Shader *pbrVariant(Shader *shader) const;
    // Program that draws water surfaces in place of the given one, or the
    // program itself when it has none.
    Shader *waterVariant(Shader *shader) const;
    // Seconds for shader animation, constant within a frame; 0 while
    // Game::animationFrozen is set.
    static float animationSeconds();
    // Shader source with #include "file" lines expanded from the same
    // directory and the given names defined after the #version line.
    static QByteArray shaderSource(const QString &directory, const QString &name,
                                   const QString &type, const QStringList &defines = {});
    // Directory the shader sources are loaded from on this platform.
    static QString shaderDirectory();
    bool textureEnabled;
    bool normalsEnabled;
private:
    QHash<Shader*, Shader*> terrainVariants;
    QHash<Shader*, Shader*> unlitVariants;
    QHash<Shader*, Shader*> pbrVariants;
    QHash<Shader*, Shader*> waterVariants;

    int currentTexture = -1;
    Vector4f shapeColor;
};

#endif	/* GLUU_H */

