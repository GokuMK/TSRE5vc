/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */


#include <cmath>

#include <tsre/renderer/OpenGL3Renderer.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/math3d/GLMatrix.h>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <tsre/ogl/GLUU.h>
#include <tsre/Game.h>
#ifndef __APPLE__
#include <GL/gl.h>
#else
#include <OpenGL/gl.h>
#endif

namespace {

void cleanupMatrixList(QVector<float*> &matrixList){
    for(int i = 0; i < matrixList.size(); i++){
        delete[] matrixList[i];
    }
    matrixList.clear();
}

void cleanupRenderItems(QVector<RenderItem*> &items){
    for(int i = 0; i < items.size(); i++){
        if(items[i] == NULL)
            continue;
        if(!items[i]->shared)
            delete items[i];
    }
    items.clear();
}

void applyItemState(GLUU *gluu, QOpenGLFunctions *f, RenderItem *item){
    if(item == NULL)
        return;

    if(item->normalsEnabled)
        gluu->enableNormals();
    else
        gluu->disableNormals();

    gluu->setBrightness(item->brightness);

    if(item->texturesEnabled){
        gluu->enableTextures();
        gluu->bindTexture(f, item->texAddr);
    } else {
        gluu->disableTextures(item->colorX, item->colorY, item->colorZ, item->colorA);
    }
}

unsigned int getItemDrawType(const RenderItem *item){
    if(item == NULL)
        return GL_TRIANGLES;
    if(item->itemType == 0)
        return GL_TRIANGLES;
    return item->itemType;
}

bool requiresWireframe(const RenderItem *item){
    if(item == NULL)
        return false;
    return item->polygonMode != 0;
}

struct TerrainStateCache {
    bool valid = false;
    bool paged = false;
    QOpenGLBuffer *params = NULL;
    int verticesPerPatch = 0;
    int patchSide = 0;
    float sampleSpacing = 0.0f;
    bool applyGaps = false;
    bool mapPass = false;
};

void applyTerrainState(GLUU *gluu, RenderItem *item,
                       TerrainStateCache &cache){
    if(gluu == NULL || gluu->currentShader == NULL || item == NULL)
        return;
    Shader *shader = gluu->currentShader;
    if(!cache.valid || cache.paged != item->terrainPaged)
        shader->setUniformValue(shader->terrainPaged, item->terrainPaged ? 1 : 0);
    if(!item->terrainPaged){
        cache.valid = true;
        cache.paged = false;
        cache.params = NULL;
        return;
    }
    if(!cache.valid || !cache.paged
            || cache.verticesPerPatch != item->terrainVerticesPerPatch)
        shader->setUniformValue(shader->terrainVerticesPerPatch,
                                item->terrainVerticesPerPatch);
    if(!cache.valid || !cache.paged
            || cache.patchSide != item->terrainPatchSide)
        shader->setUniformValue(shader->terrainPatchSide,
                                item->terrainPatchSide);
    if(!cache.valid || !cache.paged
            || cache.sampleSpacing != item->terrainSampleSpacing)
        shader->setUniformValue(shader->terrainSampleSpacing,
                                item->terrainSampleSpacing);
    if(!cache.valid || !cache.paged || cache.applyGaps != item->terrainApplyGaps)
        shader->setUniformValue(shader->terrainApplyGaps,
                                item->terrainApplyGaps ? 1 : 0);
    if(!cache.valid || !cache.paged || cache.mapPass != item->terrainMapPass)
        shader->setUniformValue(shader->terrainMapPass,
                                item->terrainMapPass ? 1 : 0);
    if(!cache.valid || !cache.paged || cache.params != item->terrainParamsBuffer)
        QOpenGLContext::currentContext()->extraFunctions()->glBindBufferBase(
                    GL_UNIFORM_BUFFER, 0,
                    item->terrainParamsBuffer == NULL ? 0
                    : item->terrainParamsBuffer->bufferId());
    cache.valid = true;
    cache.paged = true;
    cache.params = item->terrainParamsBuffer;
    cache.verticesPerPatch = item->terrainVerticesPerPatch;
    cache.patchSide = item->terrainPatchSide;
    cache.sampleSpacing = item->terrainSampleSpacing;
    cache.applyGaps = item->terrainApplyGaps;
    cache.mapPass = item->terrainMapPass;
}

void drawItem(QOpenGLFunctions *f, RenderItem *item){
    if(item->indexed){
        QOpenGLContext::currentContext()->extraFunctions()->glDrawElementsBaseVertex(
                    getItemDrawType(item), item->vertCount, item->indexType,
                    reinterpret_cast<void*>(static_cast<quintptr>(item->indexOffset)),
                    item->baseVertex);
    } else {
        f->glDrawArrays(getItemDrawType(item), item->vertOffset, item->vertCount);
    }
}

}

OpenGL3Renderer::OpenGL3Renderer() {
    mvMatrix = new float[16];
    objStrMatrix = new float[16];
    Mat4::identity(objStrMatrix);

    //VAO.create();
    //QOpenGLVertexArrayObject::Binder vaoBinder(&VAO);
    //f = QOpenGLContext::currentContext()->functions();
    /*f->glEnableVertexAttribArray(0);
    f->glEnableVertexAttribArray(1);
    f->glEnableVertexAttribArray(2);
    f->glEnableVertexAttribArray(3);
    f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), 0);
    f->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), reinterpret_cast<void *>(3 * sizeof(GLfloat)));
    f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), reinterpret_cast<void *>(6 * sizeof(GLfloat)));
    f->glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), reinterpret_cast<void *>(8 * sizeof(GLfloat)));
*/
}

OpenGL3Renderer::OpenGL3Renderer(const OpenGL3Renderer& orig) {
}

OpenGL3Renderer::~OpenGL3Renderer() {
}

void OpenGL3Renderer::pushItem(RenderItem* r, float* mvmatrix){
    if(r == NULL)
        return;

    RenderItem *queuedItem = r;
    if(r->shared){
        queuedItem = new RenderItem();
        *queuedItem = *r;
        queuedItem->shared = false;
    }

    if(mvmatrix == NULL)
        mvmatrix = mvMatrix;

    if(mvmatrix != NULL){
        queuedItem->mvMatrix = Mat4::clone(mvmatrix);
        mvMatrixs.push_back(queuedItem->mvMatrix);
    } else {
        queuedItem->mvMatrix = NULL;
    }

    items.push_back(queuedItem);
}

void OpenGL3Renderer::pushItemsVNTA(QVector<RenderItem*>& r, float* mvmatrix){
    for(int i = 0; i < r.size(); i++){
        //r[i]->mvMatrix = Mat4::clone(mvmatrix);
        //itemsVNTA[r[i]->texAddr].push_back(r[i]);
        if(itemsVNTA[r[i]->texAddr][(unsigned long long int)r[i]] == NULL){
            itemsVNTA[r[i]->texAddr][(unsigned long long int)r[i]] = r[i];
            r[i]->mvMatrixList.clear();
        }
        r[i]->mvMatrixList.push_back(mvmatrix);
        /*RenderItem *rr = new RenderItem();
        rr->VBO = r[i]->VBO;
        rr->VAO = r[i]->VAO;
        rr->mvMatrix = Mat4::clone(mvmatrix);
        rr->vertOffset = r[i]->vertOffset;
        rr->vertCount = r[i]->vertCount;
        rr->itemType = r[i]->itemType;
        rr->vertexAttr = r[i]->vertexAttr;
        rr->msMatrix = r[i]->msMatrix;
        rr->texAddr = r[i]->texAddr;
        rr->texturesEnabled = r[i]->texturesEnabled;
        rr->shared = false;
        itemsVNTA[r[i]->texAddr].push_back(rr);*/
    }
}

void OpenGL3Renderer::pushItemVNTA(RenderItem* r, float* mvmatrix){
    //r->mvMatrix = Mat4::clone(mvmatrix);
    //if(itemsVNTA[r->texAddr][(unsigned long long int)r] == NULL){
    //    itemsVNTA[r->texAddr][(unsigned long long int)r] = r;
    //    r->mvMatrixList.clear();
    //}
    //r->mvMatrixList.push_back(mvmatrix);
    //itemsVNTA[r->texAddr].push_back(r);
}

void OpenGL3Renderer::renderFrame(){
    GLUU *gluu = GLUU::get();
    f = QOpenGLContext::currentContext()->functions();

    if(gluu == NULL || f == NULL){
        cleanupRenderItems(items);
        itemsVNTA.clear();
        cleanupMatrixList(mvMatrixs);
        cleanupMatrixList(mvMatrixDelete);
        return;
    }
    TerrainStateCache terrainState;

    // Generic frame-owned queue.
    for(int i = 0; i < items.size(); i++){
        RenderItem *item = items[i];
        if(item == NULL)
            continue;
        if(item->VAO == NULL)
            continue;

        applyItemState(gluu, f, item);
        applyTerrainState(gluu, item, terrainState);

        if(item->msMatrix != NULL){
            gluu->currentShader->setUniformValue(gluu->currentShader->msMatrixUniform, *reinterpret_cast<float(*)[4][4]>(item->msMatrix));
        }
        if(item->mvMatrix != NULL){
            gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]>(item->mvMatrix));
        }

        bool customLineWidth = item->lineWidth > 0 && item->lineWidth != Game::oglDefaultLineWidth;
        if(customLineWidth)
            f->glLineWidth(item->lineWidth);

        bool wireframe = requiresWireframe(item);
        if(wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        QOpenGLVertexArrayObject::Binder vaoBinder(item->VAO);
        drawItem(f, item);

        if(wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        if(customLineWidth)
            f->glLineWidth(Game::oglDefaultLineWidth);
    }

    // Keep defaults predictable for the grouped VNTA pass.
    gluu->setBrightness(1.0f);
    gluu->enableTextures();
    gluu->enableNormals();

    QHashIterator<unsigned int, QHash<unsigned long long int, RenderItem*>> it(itemsVNTA);
    while (it.hasNext()) {
        it.next();
        gluu->bindTexture(f, it.key());

        QHashIterator<unsigned long long int, RenderItem*> it2(itemsVNTA[it.key()]);
        while (it2.hasNext()) {
            it2.next();
            RenderItem *item = it2.value();
            if(item == NULL)
                continue;
            if(item->VAO == NULL)
                continue;

            applyItemState(gluu, f, item);
            applyTerrainState(gluu, item, terrainState);
            QOpenGLVertexArrayObject::Binder vaoBinder(item->VAO);

            if(item->msMatrix != NULL){
                gluu->currentShader->setUniformValue(gluu->currentShader->msMatrixUniform, *reinterpret_cast<float(*)[4][4]>(item->msMatrix));
            }

            bool customLineWidth = item->lineWidth > 0 && item->lineWidth != Game::oglDefaultLineWidth;
            if(customLineWidth)
                f->glLineWidth(item->lineWidth);

            bool wireframe = requiresWireframe(item);
            if(wireframe)
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

            for(int i = 0; i < item->mvMatrixList.size(); i++){
                if(item->mvMatrixList[i] != NULL){
                    gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]>(item->mvMatrixList[i]));
                }
                drawItem(f, item);
            }

            if(wireframe)
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

            if(customLineWidth)
                f->glLineWidth(Game::oglDefaultLineWidth);
        }
        itemsVNTA[it.key()].clear();
    }

    itemsVNTA.clear();
    cleanupRenderItems(items);
    cleanupMatrixList(mvMatrixs);
    cleanupMatrixList(mvMatrixDelete);

    gluu->setBrightness(1.0f);
    gluu->enableTextures();
    gluu->enableNormals();
    if(gluu->currentShader != NULL)
        gluu->currentShader->setUniformValue(
                    gluu->currentShader->terrainPaged, 0);
    QOpenGLContext::currentContext()->extraFunctions()->glBindBufferBase(
                GL_UNIFORM_BUFFER, 0, 0);
}

