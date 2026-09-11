/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/SFileLegacy.h>


#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/texture/TexLib.h>
#include <QDebug>
#include <QtCore>
#include <iostream>
#include <QOpenGLShaderProgram>
#include <tsre/ogl/GLUU.h>
#include <tsre/math3d/GLMatrix.h>
#include <cmath>
#include <QString>
#include <tsre/Game.h>
#include <tsre/fileFunctions/TS.h>
#include <tsre/fileFunctions/SimisReader.h>
#include <shapeViewer/ShapeTextureInfo.h>
#include <shapeViewer/ShapeHierarchyInfo.h>
#include <shapeViewer/ContentHierarchyInfo.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/Renderer.h>
#include <vector>
#include <tsre/fileFunctions/FileBuffer.h>
#include <limits>
#include <memory>
#include <QScopeGuard>
#include <QOpenGLExtraFunctions>

SFileLegacy::SFileLegacy() {
    pathid = "";
    this->loaded = 2;
}

SFileLegacy::SFileLegacy(QString pathid, QString name, QString texp ) {
    this->pathid = pathid;
    this->nazwa = name;
    this->isinit = 1;
    this->loaded = 0;
    this->texPath = texp;
    state.push_back(State());
}


SFileLegacy::~SFileLegacy() {
    clearData();
}

void SFileLegacy::load() {
    if (loadData() && QOpenGLContext::currentContext()) initGL();
}

bool SFileLegacy::loadData() {
    if (loaded == 1) return true;
    clearData();
    //unsigned long long int timeNow = QDateTime::currentMSecsSinceEpoch();
    QFile source(pathid);
    QFile *file = &source;
    if (!file->open(QIODevice::ReadOnly)){
        qDebug() << "S Shape: not exist "<<pathid;
        file->close();
        return false;
    }
    FileBuffer* data = ReadFile::read(file);
    int loadingCount = 0;
    //qDebug() << "--" << pathid << "--" << data->length;

    data->off = 32;
    if (data->isBinarySimis()) {
        try {
        data->off = 32;
        Simis::Block root(data, TS::shape);

        while (data->off < data->readEnd()) {
            const auto block = data->readBlock();
            FileBuffer::ScopedLimit scope(*data, block.end);
            const auto pozycja = block.id;

            switch (pozycja) {
                case TS::shape_header:
                    break;
                case TS::volumes:
                    break;
                case TS::shader_names:
                    loadingCount++;
                    SFileLegacy::odczytajshadersc(data, this);
                    break;
                case TS::points:
                    loadingCount++;
                    SFileLegacy::odczytajpunktyc(data, this);
                    getSize();
                    break;
                case TS::uv_points:
                    loadingCount++;
                    SFileLegacy::odczytajuvpunktyc(data, this);
                    break;
                case TS::normals:
                    loadingCount++;
                    SFileLegacy::odczytajnormalnec(data, this);
                    break;
                case TS::matrices:
                    loadingCount++;
                    SFileLegacy::odczytajmatricesc(data, this);
                    break;
                case TS::images:
                    loadingCount++;
                    SFileLegacy::odczytajimagesc(data, this);
                    break;
                case TS::textures:
                    loadingCount++;
                    SFileLegacy::odczytajtexturesc(data, this);
                    break;
                case TS::vtx_states:
                    loadingCount++;
                    SFileLegacy::odczytajvtx_statesc(data, this);
                    break;
                case TS::prim_states:
                    loadingCount++;
                    SFileLegacy::odczytajprim_statesc(data, this);
                    break;
                case TS::texture_filter_names:
                    break;
                case TS::sort_vectors:
                    break;
                case TS::colours:
                    break;
                case TS::light_materials:
                    break;
                case TS::light_model_cfgs:
                    break;
                case TS::lod_controls:
                    if(loadingCount < 9){
                        throw FileBuffer::ParseError("Shape LOD appears before required data");
                    }
                    SFileLegacy::odczytajloddc(data, this);
                    loaded = 1;
                    break;
                case TS::animations: {
                    data->skipLabel();
                    const int count = Simis::count(data);
                    for (int i = 0; i < count; ++i) {
                        const auto animation = data->readBlock();
                        FileBuffer::ScopedLimit animationScope(*data, animation.end);
                        if (animation.id == TS::animation) {
                            animations.push_back(Animation());
                            animations.back().loadC(data, animation.end);
                        }
                        data->off = animation.end;
                    }
                    break;
                }
                default:
                    qDebug() << "#SFileLegacy - unknown token: "<< pozycja << TS::name(pozycja);
                    break;
            }
            data->off = block.end;
        }
        } catch (const FileBuffer::ParseError& error) {
            loaded = 2;
            qWarning() << "Invalid shape SIMIS data" << pathid << data->off << error.what();
        }
    } else {
        try {
        //qDebug() << "plik xml:";
        //wczytanie plku xml
        data->off = 0;
        ParserX::NextLine(data);

        QString sh = "";
        while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
            if(sh == "shape"){
                while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                    //qDebug() << sh;
                    if(sh == "shader_names"){
                        SFileLegacy::odczytajshaders(data, this);
                        loadingCount++;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "points"){
                        SFileLegacy::odczytajpunkty(data, this);
                        getSize();
                        loadingCount++;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "uv_points"){
                        SFileLegacy::odczytajuvpunkty(data, this);
                        loadingCount++;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "normals"){
                        SFileLegacy::odczytajnormalne(data, this);
                        loadingCount++;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "matrices"){
                        SFileLegacy::odczytajmatrices(data, this);
                        loadingCount++;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "images"){
                        SFileLegacy::odczytajimages(data, this);
                        loadingCount++;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "textures"){
                        SFileLegacy::odczytajtextures(data, this);
                        loadingCount++;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "vtx_states"){
                        SFileLegacy::odczytajvtx_states(data, this);
                        loadingCount++;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "prim_states"){
                        SFileLegacy::odczytajprim_states(data, this);
                        loadingCount++;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "shape_header"){
                        //qDebug() << "shape_header" <<
                        ParserX::GetAlternativeTokenName(data);
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "volumes"){
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "texture_filter_names"){
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "sort_vectors"){
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "colours"){
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "light_materials"){
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "light_model_cfgs"){
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "lod_controls"){
                        if(loadingCount < 9){
                            qDebug() << "#shape - loading error" << sh;
                            throw FileBuffer::ParseError("Shape LOD appears before required data");
                        }
                        SFileLegacy::odczytajlodd(data, this);
                        loaded = 1;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if(sh == "animations"){
                        while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                            if(sh == "animation"){
                                animations.push_back(Animation());
                                animations.back().loadX(data);
                                if(animations.size() > 0){
                                    qDebug() << animations[0].node.size();

                                }
                                ParserX::SkipToken(data);
                                continue;
                            }
                            ParserX::SkipToken(data);
                        }
                        ParserX::SkipToken(data);
                        continue;
                    }
                    qDebug() << "#shape - undefined token" << sh;
                    ParserX::SkipToken(data);
                }

                ParserX::SkipToken(data);
                continue;
            }
            qDebug() << "#SFileLegacy - undefined token" << sh;
            ParserX::SkipToken(data);
        }
            } catch (const FileBuffer::ParseError& error) {
            loaded = 2;
            qWarning() << "Invalid legacy text shape" << pathid << error.what();
        }
    }
    delete data;
    file->close();
    if(loaded == 1)
        buildFrameIds();
    loadSd();
    //qDebug() <<this->pathid << QDateTime::currentMSecsSinceEpoch() - timeNow;
    return loaded == 1;
}

void SFileLegacy::Animation::loadC(FileBuffer* data, int length) {
    FileBuffer::ScopedLimit animationScope(*data, length);
    data->skipLabel();
    frames = data->getInt();
    fps = data->getInt();
    while (data->off < length) {
        const auto child = data->readBlock();
        FileBuffer::ScopedLimit childScope(*data, child.end);
        if (child.id == TS::anim_nodes) {
            data->skipLabel();
            const int nodeCount = Simis::count(data);
            for (int i = 0; i < nodeCount; ++i) {
                Simis::Block animNode(data, TS::anim_node);
                node.push_back(AnimNode());
                Simis::Block controllers(data, TS::controllers);
                const int controllerCount = Simis::count(data);
                for (int j = 0; j < controllerCount; ++j) {
                    const auto controller = data->readBlock();
                    FileBuffer::ScopedLimit controllerScope(*data, controller.end);
                    data->skipLabel();
                    if (controller.id == TS::tcb_rot || controller.id == TS::linear_pos) {
                        const int keyCount = Simis::count(data);
                        for (int k = 0; k < keyCount; ++k) {
                            const auto key = data->readBlock();
                            FileBuffer::ScopedLimit keyScope(*data, key.end);
                            data->skipLabel();
                            if (controller.id == TS::tcb_rot && key.id == TS::tcb_key) {
                                node.back().tcbKey.push_back(AnimNode::TcbKey());
                                auto& value = node.back().tcbKey.back();
                                value.frame = data->getUint();
                                value.quat[0] = -data->getFloat();
                                value.quat[1] = data->getFloat();
                                value.quat[2] = -data->getFloat();
                                value.quat[3] = data->getFloat();
                                for (int a = 0; a < 5; ++a) value.param[a] = data->getFloat();
                            } else if (controller.id == TS::tcb_rot && key.id == TS::slerp_rot) {
                                node.back().slerpRot.push_back(AnimNode::SlerpRot());
                                auto& value = node.back().slerpRot.back();
                                value.frame = data->getUint();
                                value.quat[0] = -data->getFloat();
                                value.quat[1] = data->getFloat();
                                value.quat[2] = -data->getFloat();
                                value.quat[3] = data->getFloat();
                            } else if (controller.id == TS::linear_pos && key.id == TS::linear_key) {
                                node.back().linearKey.push_back(AnimNode::LinearKey());
                                auto& value = node.back().linearKey.back();
                                value.frame = data->getUint();
                                for (int a = 0; a < 3; ++a) value.pos[a] = data->getFloat();
                            }
                            data->off = key.end;
                        }
                    }
                    data->off = controller.end;
                }
            }
        }
        data->off = child.end;
    }
}

void SFileLegacy::Animation::loadX(FileBuffer* data){
    frames = ParserX::GetNumber(data);
    fps = ParserX::GetNumber(data);

    QString sh;
    int count;
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        if(sh == "anim_nodes"){
            count = ParserX::GetNumber(data);
            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                if(sh == "anim_node"){
                    //qDebug() <<
                    ParserX::NextTokenInside(data);
                    node.push_back(AnimNode());
                    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                        if(sh == "controllers"){
                            count = ParserX::GetNumber(data);
                            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                                if(sh == "tcb_rot"){
                                    count =  ParserX::GetNumber(data);
                                    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                                        if(sh == "tcb_key"){
                                            node.back().tcbKey.push_back(AnimNode::TcbKey());
                                            node.back().tcbKey.back().frame = ParserX::GetUInt(data);
                                            node.back().tcbKey.back().quat[0] = -ParserX::GetNumber(data);
                                            node.back().tcbKey.back().quat[1] = ParserX::GetNumber(data);
                                            node.back().tcbKey.back().quat[2] = -ParserX::GetNumber(data);
                                            node.back().tcbKey.back().quat[3] = ParserX::GetNumber(data);
                                            for(int i = 0; i < 5; i++){
                                                node.back().tcbKey.back().param[i] = ParserX::GetNumber(data);
                                            }
                                            ParserX::SkipToken(data);
                                            continue;
                                        }
                                        if(sh == "slerp_rot"){
                                            node.back().slerpRot.push_back(AnimNode::SlerpRot());
                                            node.back().slerpRot.back().frame = ParserX::GetUInt(data);
                                            node.back().slerpRot.back().quat[0] = -ParserX::GetNumber(data);
                                            node.back().slerpRot.back().quat[1] = ParserX::GetNumber(data);
                                            node.back().slerpRot.back().quat[2] = -ParserX::GetNumber(data);
                                            node.back().slerpRot.back().quat[3] = ParserX::GetNumber(data);
                                            ParserX::SkipToken(data);
                                            continue;
                                        }
                                        ParserX::SkipToken(data);
                                    }
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                if(sh == "linear_pos"){
                                    count = ParserX::GetNumber(data);
                                    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                                        if(sh == "linear_key"){
                                            node.back().linearKey.push_back(AnimNode::LinearKey());
                                            node.back().linearKey.back().frame = ParserX::GetUInt(data);
                                            for(int i = 0; i < 3; i++){
                                                node.back().linearKey.back().pos[i] = ParserX::GetNumber(data);
                                            }
                                            ParserX::SkipToken(data);
                                            continue;
                                        }
                                        ParserX::SkipToken(data);
                                    }
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                ParserX::SkipToken(data);
                            }
                            ParserX::SkipToken(data);
                            continue;
                        }
                        ParserX::SkipToken(data);
                    }
                    ParserX::SkipToken(data);
                    continue;
                }
                ParserX::SkipToken(data);
            }
            ParserX::SkipToken(data);
            continue;
        }
        ParserX::SkipToken(data);
    }
}

void SFileLegacy::loadSd() {
    if(loadedSd == true)
        return;
    QFile file(pathid+"d");
    if (!file.open(QIODevice::ReadOnly)){
        qDebug() << "Sd Shape: not exist "<<pathid+"d";
        return;
    }
    FileBuffer* data = ReadFile::read(&file);
    file.close();
    data->toUtf16();
    data->skipBOM();
    QString sh;

    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        //qDebug() << sh;
        if (sh == ("simisa@@@@@@@@@@jinx0t1t______")) {
            continue;
        }
        if (sh == ("shape")) {
            sdName = ParserX::GetString(data).trimmed();
            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                if (sh == ("esd_detail_level")) {
                    esdDetailLevel = ParserX::GetNumber(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("esd_alternative_texture")) {
                    esdAlternativeTexture = ParserX::GetNumber(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("esd_bounding_box")) {
                    esdBoundingBox << EsdBoundingBox();
                    bool ok = false;
                    for(int i = 0; i < 6; i++){
                        esdBoundingBox.back().shape[i] = ParserX::GetNumberInside(data, &ok);
                        if(!ok) break;
                    }
                    if(ok == false)
                        esdBoundingBox.pop_back();
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("esd_complex")) {
                    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                        if (sh == ("esd_complex_box")) {
                            esdBoundingBox << EsdBoundingBox();
                            for(int i = 0; i < 3; i++){
                                esdBoundingBox.back().rotation[i] = ParserX::GetNumber(data);
                            }
                            for(int i = 0; i < 3; i++){
                                esdBoundingBox.back().translation[i] = ParserX::GetNumber(data);
                            }
                            for(int i = 0; i < 6; i++){
                                esdBoundingBox.back().shape[i] = ParserX::GetNumber(data);
                            }
                            ParserX::SkipToken(data);
                            continue;
                        }
                        ParserX::SkipToken(data);
                    }
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("esd_snapable")) {
                    snapable = true;
                    ParserX::SkipToken(data);
                    continue;
                }
                ParserX::SkipToken(data);
            }
            ParserX::SkipToken(data);
            continue;
        }
    }
    QString seasonPath;
    //qDebug() << esdAlternativeTexture << this->TextureFlags[Game::season];
    //qDebug() << (esdAlternativeTexture & this->TextureFlags[Game::season]);
    if((esdAlternativeTexture & Game::TextureFlags[Game::season]) != 0)
        seasonPath = "/" + Game::season.toLower();

    if(Game::season == "Winter" || Game::season == "AutumnSnow" || Game::season == "WinterSnow" || Game::season == "SpringSnow" ){
        if(esdAlternativeTexture & Game::TextureFlags["Snow"] != 0)
            seasonPath = "/snow";
        if(esdAlternativeTexture & Game::TextureFlags["SnowTrack"] != 0)
            seasonPath = "/snow";
    }
    texPath += seasonPath;
    loadedSd = true;
    delete data;
}

void SFileLegacy::getSize() {
        float tx[2]{-9999, 9999}, ty[2]{-9999, 9999}, tz[2]{-9999, 9999};
        for (int i = 0; i < tpoints.ipoints ; i++) {
            if(tpoints.points[i].x < tx[1]) tx[1] = tpoints.points[i].x;
            if(tpoints.points[i].y < ty[1]) ty[1] = tpoints.points[i].y;
            if(tpoints.points[i].z < tz[1]) tz[1] = tpoints.points[i].z;
            if(tpoints.points[i].x > tx[0]) tx[0] = tpoints.points[i].x;
            if(tpoints.points[i].y > ty[0]) ty[0] = tpoints.points[i].y;
            if(tpoints.points[i].z > tz[0]) tz[0] = tpoints.points[i].z;
        }
        bound[0] = tx[0];
        bound[1] = tx[1];
        bound[2] = ty[0];
        bound[3] = ty[1];
        bound[4] = tz[0];
        bound[5] = tz[1];
        tx[0] = tx[0] - tx[1];
        ty[0] = ty[0] - ty[1];
        tz[0] = tz[0] - tz[1];
        size = sqrt(tx[0]*tx[0] + ty[0]*ty[0] + tz[0]*tz[0]);
        //console.log(this.size);
    }

bool SFileLegacy::getBoxPoints(QVector<float>& points){
    if(this->esdBoundingBox.size() == 0){
        for(int i=0; i<2; i++)
            for(int j=4; j<6; j++){
                points.push_back(-bound[i]);
                points.push_back(bound[2]);
                points.push_back(bound[j]);
                points.push_back(-bound[i]);
                points.push_back(bound[3]);
                points.push_back(bound[j]);
            }
        for(int i=0; i<2; i++)
            for(int j=2; j<4; j++){
                points.push_back(-bound[i]);
                points.push_back(bound[j]);
                points.push_back(bound[4]);
                points.push_back(-bound[i]);
                points.push_back(bound[j]);
                points.push_back(bound[5]);
            }
        for(int i=4; i<6; i++)
            for(int j=2; j<4; j++){
                points.push_back(-bound[0]);
                points.push_back(bound[j]);
                points.push_back(bound[i]);
                points.push_back(-bound[1]);
                points.push_back(bound[j]);
                points.push_back(bound[i]);
            }
    } else {
        float tbound[6];
        for(int u = 0; u < this->esdBoundingBox.size(); u++ ){
            tbound[0] = this->esdBoundingBox[u].shape[0];
            tbound[1] = this->esdBoundingBox[u].shape[3];
            tbound[2] = this->esdBoundingBox[u].shape[1];
            tbound[3] = this->esdBoundingBox[u].shape[4];
            tbound[4] = this->esdBoundingBox[u].shape[2];
            tbound[5] = this->esdBoundingBox[u].shape[5];

            //int pointsBeg = points.size();
            ///////////
            for(int i=0; i<2; i++)
                for(int j=4; j<6; j++){
                    points.push_back(-tbound[i]);
                    points.push_back(tbound[2]);
                    points.push_back(tbound[j]);
                    points.push_back(-tbound[i]);
                    points.push_back(tbound[3]);
                    points.push_back(tbound[j]);
                }
            for(int i=0; i<2; i++)
                for(int j=2; j<4; j++){
                    points.push_back(-tbound[i]);
                    points.push_back(tbound[j]);
                    points.push_back(tbound[4]);
                    points.push_back(-tbound[i]);
                    points.push_back(tbound[j]);
                    points.push_back(tbound[5]);
                }
            for(int i=4; i<6; i++)
                for(int j=2; j<4; j++){
                    points.push_back(-tbound[0]);
                    points.push_back(tbound[j]);
                    points.push_back(tbound[i]);
                    points.push_back(-tbound[1]);
                    points.push_back(tbound[j]);
                    points.push_back(tbound[i]);
                }
            //int pointsEnd = points.size();
            //for(int i = pointsBeg; i < pointsEnd; i++){
            //    //Vec3::add(&points[i], &points[i], this->esdBoundingBox[u].translation);
            //}
        }
    }
    return true;
}

void SFileLegacy::getFloorBorderLinePoints(float *&punkty){
    if(this->esdBoundingBox.size() == 0){
        float h = bound[2];
        if(bound[3] < bound[2])
            h = bound[3];
//0 1 / 4 5
        float vec1[3], vec2[3], dist;
        Vec3::set(vec1, bound[0], h, bound[4]);
        Vec3::set(vec2, bound[0], h, bound[5]);
        dist = Vec3::distance(vec1, vec2);
        Vec3::sub(vec2, vec2, vec1);
        Vec3::normalize(vec2, vec2);
        for(float i = 0; i < dist + 2; i+=4 ){
            *punkty++ = vec1[0] + i*vec2[0];
            *punkty++ = h;
            *punkty++ = vec1[2] + i*vec2[2];
        }
        Vec3::set(vec1, bound[0], h, bound[5]);
        Vec3::set(vec2, bound[1], h, bound[5]);
        dist = Vec3::distance(vec1, vec2);
        Vec3::sub(vec2, vec2, vec1);
        Vec3::normalize(vec2, vec2);
        for(float i = 0; i < dist + 2; i+=4 ){
            *punkty++ = vec1[0] + i*vec2[0];
            *punkty++ = h;
            *punkty++ = vec1[2] + i*vec2[2];
        }
        Vec3::set(vec1, bound[1], h, bound[4]);
        Vec3::set(vec2, bound[0], h, bound[4]);
        dist = Vec3::distance(vec1, vec2);
        Vec3::sub(vec2, vec2, vec1);
        Vec3::normalize(vec2, vec2);
        for(float i = 0; i < dist + 2; i+=4 ){
            *punkty++ = vec1[0] + i*vec2[0];
            *punkty++ = h;
            *punkty++ = vec1[2] + i*vec2[2];
        }
        Vec3::set(vec1, bound[1], h, bound[5]);
        Vec3::set(vec2, bound[1], h, bound[4]);
        dist = Vec3::distance(vec1, vec2);
        Vec3::sub(vec2, vec2, vec1);
        Vec3::normalize(vec2, vec2);
        for(float i = 0; i < dist + 2; i+=4 ){
            *punkty++ = vec1[0] + i*vec2[0];
            *punkty++ = h;
            *punkty++ = vec1[2] + i*vec2[2];
        }
    } else {
        float tbound[6];
        for(int u = 0; u < this->esdBoundingBox.size(); u++ ){
            tbound[0] = this->esdBoundingBox[u].shape[0];
            tbound[1] = this->esdBoundingBox[u].shape[3];
            tbound[2] = this->esdBoundingBox[u].shape[1];
            tbound[3] = this->esdBoundingBox[u].shape[4];
            tbound[4] = this->esdBoundingBox[u].shape[2];
            tbound[5] = this->esdBoundingBox[u].shape[5];

        float h = tbound[2];
        if(tbound[3] < tbound[2])
            h = tbound[3];
        float vec1[3], vec2[3], dist;
        Vec3::set(vec1, bound[0], h, bound[4]);
        Vec3::set(vec2, bound[0], h, bound[5]);
        dist = Vec3::distance(vec1, vec2);
        Vec3::sub(vec2, vec2, vec1);
        Vec3::normalize(vec2, vec2);
        for(float i = 0; i < dist + 2; i+=4 ){
            *punkty++ = vec1[0] + i*vec2[0];
            *punkty++ = h;
            *punkty++ = vec1[2] + i*vec2[2];
        }
        Vec3::set(vec1, bound[0], h, bound[5]);
        Vec3::set(vec2, bound[1], h, bound[5]);
        dist = Vec3::distance(vec1, vec2);
        Vec3::sub(vec2, vec2, vec1);
        Vec3::normalize(vec2, vec2);
        for(float i = 0; i < dist + 2; i+=4 ){
            *punkty++ = vec1[0] + i*vec2[0];
            *punkty++ = h;
            *punkty++ = vec1[2] + i*vec2[2];
        }
        Vec3::set(vec1, bound[1], h, bound[4]);
        Vec3::set(vec2, bound[0], h, bound[4]);
        dist = Vec3::distance(vec1, vec2);
        Vec3::sub(vec2, vec2, vec1);
        Vec3::normalize(vec2, vec2);
        for(float i = 0; i < dist + 2; i+=4 ){
            *punkty++ = vec1[0] + i*vec2[0];
            *punkty++ = h;
            *punkty++ = vec1[2] + i*vec2[2];
        }
        Vec3::set(vec1, bound[1], h, bound[5]);
        Vec3::set(vec2, bound[1], h, bound[4]);
        dist = Vec3::distance(vec1, vec2);
        Vec3::sub(vec2, vec2, vec1);
        Vec3::normalize(vec2, vec2);
        for(float i = 0; i < dist + 2; i+=4 ){
            *punkty++ = vec1[0] + i*vec2[0];
            *punkty++ = h;
            *punkty++ = vec1[2] + i*vec2[2];
        }
        }
    }
}

bool SFileLegacy::isSnapable() const{
    return snapable;
}

void SFileLegacy::addSnapablePoints(QVector<float> &out){
    //if(!this->loadedSd)
    //    loadSd();
    if(esdBoundingBox.size() > 0){
        const float sizeX = std::fabs(esdBoundingBox[0].shape[3] - esdBoundingBox[0].shape[0]);
        const float sizeZ = std::fabs(esdBoundingBox[0].shape[5] - esdBoundingBox[0].shape[2]);
        if(sizeZ > sizeX * 4.0f){
            const float avgX = (esdBoundingBox[0].shape[0] + esdBoundingBox[0].shape[3]) / 2.0f;
            out.push_back(avgX);
            out.push_back(0);
            out.push_back(esdBoundingBox[0].shape[2]);
            out.push_back(avgX);
            out.push_back(0);
            out.push_back(esdBoundingBox[0].shape[5]);
        } else {
            const float avgZ = (esdBoundingBox[0].shape[2] + esdBoundingBox[0].shape[5]) / 2.0f;
            out.push_back(esdBoundingBox[0].shape[0]);
            out.push_back(0);
            out.push_back(avgZ);
            out.push_back(esdBoundingBox[0].shape[3]);
            out.push_back(0);
            out.push_back(avgZ);
        }
    } else {

        const float sizeX = std::fabs(bound[1] - bound[0]);
        const float sizeZ = std::fabs(bound[5] - bound[4]);
        if(sizeZ > sizeX * 4.0f){
            const float avgX = (bound[0] + bound[1]) / 2.0f;
            out.push_back(avgX);
            out.push_back(0);
            out.push_back(-bound[4]);
            out.push_back(avgX);
            out.push_back(0);
            out.push_back(-bound[5]);
        } else {
            const float avgZ = (bound[4] + bound[5]) / 2.0f;
            out.push_back(bound[0]);
            out.push_back(0);
            out.push_back(-avgZ);
            out.push_back(bound[1]);
            out.push_back(0);
            out.push_back(-avgZ);
        }
    }
}

void SFileLegacy::reload() {
    loaded = 0;
    glReady = false;
    if (!distancelevel || iloscd == 0) return;
    qDebug() << "reload";
    QStringList list;

    for (int i = 0; i < distancelevel[0].iloscs; i++) {
        for (int j = 0; j < distancelevel[0].subobiekty[i].iloscc; j++) {
            int prim_state = distancelevel[0].subobiekty[i].czesci[j].prim_state_idx;

            if (prim_state < 0 || prim_state >= iloscps) continue;
            const int textureIndex = primstate[prim_state].arg4;
            if (textureIndex < 0 || textureIndex >= ilosct ||
                texture[textureIndex].image < 0 || texture[textureIndex].image >= ilosci) continue;
            //if (image[texture[primstate[prim_state].arg4].image].tex == -2)
            //    continue;
            list.push_back(image[texture[primstate[prim_state].arg4].image].name);
       }
    }

    list.removeDuplicates();
    for(QString s : list){
        qDebug() << s;
        TexLib::addTex(texPath, s, true);
    }
}

unsigned int SFileLegacy::newState(){
    state.push_back(State());
    return state.size() - 1;
}

void SFileLegacy::setAnimated(unsigned int stateId, bool animated){
    state[stateId].animated = animated;
}

void SFileLegacy::setEnabledSubObjs(unsigned int stateId, unsigned int enabledSubObjs){
    state[stateId].enabledSubObjs = enabledSubObjs;
}

void SFileLegacy::setCurrentDistanceLevel(unsigned int stateId, int level){
    if(level < 0)
        level = 0;
    if(level >= iloscd)
        level = 0;
    state[stateId].distanceLevel = level;
}

unsigned long long SFileLegacy::getTextureStateHash() const{
    // Keep the cache fingerprint lightweight: texture slot id + resolved GL id per image.
    unsigned long long hash = 1469598103934665603ULL; // FNV-1a offset basis
    hash ^= (unsigned long long)(unsigned int)ilosci;
    hash *= 1099511628211ULL; // FNV-1a prime

    if(ilosci <= 0 || image == NULL)
        return hash;

    for(int i = 0; i < ilosci; i++){
        hash ^= (unsigned long long)(unsigned int)(image[i].tex + 0x9e3779b9);
        hash *= 1099511628211ULL;
        hash ^= (unsigned long long)(unsigned int)(image[i].texAddr + 0x9e3779b9);
        hash *= 1099511628211ULL;
    }

    return hash;
}

void SFileLegacy::clearRenderItems(unsigned int id) {
    renderItems[id].clear();
    renderItemOwners.remove(id);
}

void SFileLegacy::invalidateRenderState(bool invalidateMatrixCache){
    requiresUpdate = true;
    for(auto it = renderItems.begin(); it != renderItems.end(); ++it){
        clearRenderItems(it.key());
    }
    renderItemsTextureHash.clear();

    if(!invalidateMatrixCache)
        return;
    if(iloscm <= 0 || macierz == NULL)
        return;

    for(int i = 0; i < iloscm; i++){
        macierz[i].isFixed = false;
        macierz[i].hash = 0;
    }
}

void SFileLegacy::enableSubObjByNameQueue(unsigned int stateId, const QString& name, bool val){
    if(stateId > state.size() - 1)
        return;
    state[stateId].enableSubObjQueue[name] = val;
}

void SFileLegacy::enableSubObjByName(unsigned int stateId, const QString& name, bool val){
    // Find matrix id
    int matrixId = -1;
    for(int i = 0; i < iloscm; i++) {
        if(macierz[i].name.toLower() == name.toLower() ) {
            matrixId = i;
            break;
        }
    }
    if(matrixId < 0)
        return;

    // check if can be disabled/enabled
    if(distancelevel[0].iloscs < 2)
        return;
    //qDebug() << matrixId << distancelevel[0].subobiekty[0].header.geometryNodeMap.size() ;
    if(matrixId >= distancelevel[0].subobiekty[0].header.geometryNodeMap.size())
        return;
    if(distancelevel[0].subobiekty[0].header.geometryNodeMap[matrixId] > 0 )
        return;

    // enable / disable subobjs
    for(int j = 1; j < distancelevel[0].iloscs; j++){
        if(distancelevel[0].subobiekty[j].header.geometryNodeMap[matrixId] == 0){
            if(val)
                state[stateId].enabledSubObjs = state[stateId].enabledSubObjs | (1 << j);
            else
                state[stateId].enabledSubObjs = state[stateId].enabledSubObjs & ~(1 << j);
        }
    }
}

void SFileLegacy::updateSim(float deltaTime, unsigned int stateId){
    if (isinit != 1 || loaded == 2)
        return;

    animated = false;

    if(state[stateId].animated && animations.size() > 0){
        if(animations[0].frames == 0)
            return;
        animated = true;
        if(deltaTime <= 0){
            state[stateId].frameCount = 0;
        } else {
            state[stateId].frameCount += animations[0].fps * deltaTime;
            if(state[stateId].frameCount > animations[0].frames)
                while(state[stateId].frameCount > animations[0].frames)
                    state[stateId].frameCount -= animations[0].frames;
            if(state[stateId].frameCount < 0)
                state[stateId].frameCount = 0;
        }
    }
}

void SFileLegacy::render() {
    render(0,0);
}

void SFileLegacy::pushRenderItem(){
    pushRenderItem(0,0);
}

void SFileLegacy::pushRenderItem(quint32 selectionId, unsigned int stateId){
    if (loaded == 1 && !isGLReady() && !initGL()) return;
    if (isinit != 1 || loaded == 2)
        return;
    if(Game::currentRenderer == NULL)
        return;
    if (loaded == 0) {
        if(Game::objectLoadingTokens < 1) return;

        Game::objectLoadingTokens-=2;
        loaded = 2;
        load();
        return;
    }

    if(state[stateId].enableSubObjQueue.size() > 0){
        for (auto it = state[stateId].enableSubObjQueue.begin(); it != state[stateId].enableSubObjQueue.end();){
            enableSubObjByName(stateId, it.key(), it.value());
            it = state[stateId].enableSubObjQueue.erase(it);
        }
    }

    if(!animated && selectionId == 0 && renderItems[stateId].size() > 0){
        // Keep cached packets in sync with asynchronous texture loading.
        // Without this, some instances can stay on fallback material.
        bool textureAddressChanged = false;
        for(int i = 0; i < ilosci; i++){
            if(image[i].tex < 0)
                continue;
            if(TexLib::mtex[image[i].tex] == NULL){
                requiresUpdate = true;
                continue;
            }
            if(!TexLib::mtex[image[i].tex]->loaded){
                requiresUpdate = true;
                continue;
            }
            if(!TexLib::mtex[image[i].tex]->glLoaded){
                TexLib::mtex[image[i].tex]->GLTextures();
                requiresUpdate = true;
                continue;
            }
            unsigned int newTexAddr = TexLib::mtex[image[i].tex]->tex[0];
            if(image[i].texAddr != (int)newTexAddr){
                image[i].texAddr = (int)newTexAddr;
                requiresUpdate = true;
                textureAddressChanged = true;
            }
        }

        unsigned long long textureStateHash = getTextureStateHash();
        if(renderItemsTextureHash.value(stateId, 0ULL) != textureStateHash){
            requiresUpdate = true;
        }

        if(textureAddressChanged){
            // Render cache is keyed by stateId. If one state gets refreshed texture ids,
            // all other cached states for this shape must be rebuilt as well.
            for(auto it = renderItems.begin(); it != renderItems.end(); ++it){
                clearRenderItems(it.key());
            }
            renderItemsTextureHash.clear();
        }
    }

    // Animated sub-object matrices are frame-local. Avoid caching shared render items
    // with dangling matrix pointers by submitting per-frame owned packets instead.
    if(animated){
        RenderItem *r;
        float m[16];
        int currentDlevel = state[stateId].distanceLevel;
        for (int i = 0; i < distancelevel[currentDlevel].iloscs; i++) {
            if(((state[stateId].enabledSubObjs >> i) & 1) == 0)
                continue;

            for (int j = 0; j < distancelevel[currentDlevel].subobiekty[i].iloscc; j++) {
                r = new RenderItem();

                int prim_state = distancelevel[currentDlevel].subobiekty[i].czesci[j].prim_state_idx;
                int vtx_state = primstate[prim_state].vtx_state;
                int matrix = vtxstate[vtx_state].matrix;
                bool texEnabled = distancelevel[currentDlevel].subobiekty[i].czesci[j].enabled;

                Mat4::identity(m);
                getPmatrixAnimated(currentDlevel, m, matrix, state[stateId].frameCount);
                r->msMatrix = Mat4::clone(m);
                Game::currentRenderer->mvMatrixDelete.push_back(r->msMatrix);

                if( vtxstate[vtx_state].arg2 < -7 )
                    r->normalsEnabled = 0;
                else
                    r->normalsEnabled = 1;

                if( vtxstate[vtx_state].arg2 == -12 )
                    r->brightness = 0.5;
                else
                    r->brightness = 1.0;

                r->setSelectionId(selectionId);
                if (selectionId == 0) {
                    if(primstate[prim_state].arg4 == -1 || !texEnabled || TexLib::disabledTextures[image[texture[primstate[prim_state].arg4].image].texAddr] == 1){
                        r->disableTextures(1.0, 0.0, 1.0, 1.0);
                    } else if (image[texture[primstate[prim_state].arg4].image].texAddr >= 0) {
                        r->enableTextures(image[texture[primstate[prim_state].arg4].image].texAddr);
                    } else if (image[texture[primstate[prim_state].arg4].image].tex == -2) {
                        r->disableTextures(1.0, 0.0, 1.0, 1.0);
                    } else if (image[texture[primstate[prim_state].arg4].image].tex == -1) {
                        image[texture[primstate[prim_state].arg4].image].tex = TexLib::addTex(
                                texPath,
                                image[texture[primstate[prim_state].arg4].image].name
                                );
                        r->disableTextures(1.0, 0.0, 1.0, 1.0);
                    } else if (TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex] == NULL) {
                        r->disableTextures(1.0, 0.0, 1.0, 1.0);
                    } else if (TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->glLoaded) {
                        image[texture[primstate[prim_state].arg4].image].texAddr = TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->tex[0];
                        r->enableTextures(image[texture[primstate[prim_state].arg4].image].texAddr);
                    } else if (TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->loaded) {
                        TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->GLTextures();
                        if (TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->glLoaded) {
                            image[texture[primstate[prim_state].arg4].image].texAddr = TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->tex[0];
                            r->enableTextures(image[texture[primstate[prim_state].arg4].image].texAddr);
                        } else {
                            r->disableTextures(1.0, 0.0, 1.0, 1.0);
                        }
                    } else {
                        r->disableTextures(1.0, 0.0, 1.0, 1.0);
                    }
                }

                r->VBO = &distancelevel[currentDlevel].subobiekty[i].VBO;
                r->VAO = &distancelevel[currentDlevel].subobiekty[i].VAO;
                r->vertOffset = distancelevel[currentDlevel].subobiekty[i].czesci[j].offset;
                r->vertCount = distancelevel[currentDlevel].subobiekty[i].czesci[j].iloscv;
                r->itemType = GL_TRIANGLES;
                r->vertexAttr = RenderItem::VNTA;
                r->shared = false;
                Game::currentRenderer->pushItem(r, Game::currentRenderer->mvMatrix);
            }
        }
        return;
    }

    if(renderItems[stateId].size() == 0 || requiresUpdate){
        // `requiresUpdate` is shape-wide (all stateIds).
        // If any state requested refresh, invalidate other cached states too.
        bool globalInvalidateRequested = requiresUpdate;
        requiresUpdate = false;
        renderItemsTextureHash.remove(stateId);
        clearRenderItems(stateId);

        RenderItem * r;// = new RenderItem();
        float m[16];
        int currentDlevel = state[stateId].distanceLevel;
        bool textureAddressChangedDuringBuild = false;
        for (int i = 0; i < distancelevel[currentDlevel].iloscs; i++) {

            if(((state[stateId].enabledSubObjs >> i) & 1) == 0)
                continue;

            for (int j = 0; j < distancelevel[currentDlevel].subobiekty[i].iloscc; j++) {
                r = new RenderItem();

                int prim_state = distancelevel[currentDlevel].subobiekty[i].czesci[j].prim_state_idx;
                int vtx_state = primstate[prim_state].vtx_state;
                int matrix = vtxstate[vtx_state].matrix;
                bool texEnabled = distancelevel[currentDlevel].subobiekty[i].czesci[j].enabled;

                if(animated){
                    Mat4::identity(m);
                    getPmatrixAnimated(currentDlevel, m, matrix, state[stateId].frameCount);
                    r->msMatrix = (float*)&m;
                } else {
                    if (!macierz[matrix].isFixed) {
                        Mat4::identity(m);
                        memcpy(macierz[matrix].fixed, getPmatrix(currentDlevel, m, matrix), sizeof (float) * 16);
                        macierz[matrix].isFixed = true;
                    }
                    r->msMatrix = (float*)&macierz[matrix].fixed;
                }

                if( vtxstate[vtx_state].arg2 < -7 )
                    r->normalsEnabled = 0;
                else
                    r->normalsEnabled = 1;

                if( vtxstate[vtx_state].arg2 == -12 )
                    r->brightness = 0.5;
                else
                    r->brightness = 1.0;

                if(primstate[prim_state].arg4 == -1 || !texEnabled || TexLib::disabledTextures[image[texture[primstate[prim_state].arg4].image].texAddr] == 1){
                    r->disableTextures(1.0, 0.0, 1.0, 1.0);
                } else if (image[texture[primstate[prim_state].arg4].image].texAddr >= 0) {
                    r->enableTextures(image[texture[primstate[prim_state].arg4].image].texAddr);
                } else if (image[texture[primstate[prim_state].arg4].image].tex == -2) {
                    r->disableTextures(1.0, 0.0, 1.0, 1.0);
                } else if (image[texture[primstate[prim_state].arg4].image].tex == -1) {
                    image[texture[primstate[prim_state].arg4].image].tex = TexLib::addTex(
                            texPath,
                            image[texture[primstate[prim_state].arg4].image].name
                            );
                    requiresUpdate = true;
                    r->disableTextures(1.0, 0.0, 1.0, 1.0);
                } else if (TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex] == NULL) {
                    requiresUpdate = true;
                    r->disableTextures(1.0, 0.0, 1.0, 1.0);
                } else if (TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->glLoaded) {
                    unsigned int newTexAddr = TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->tex[0];
                    if(image[texture[primstate[prim_state].arg4].image].texAddr != (int)newTexAddr){
                        image[texture[primstate[prim_state].arg4].image].texAddr = (int)newTexAddr;
                        textureAddressChangedDuringBuild = true;
                    }
                    requiresUpdate = true;
                    r->enableTextures(image[texture[primstate[prim_state].arg4].image].texAddr);
                } else if (TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->loaded) {
                    TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->GLTextures();
                    requiresUpdate = true;
                    r->disableTextures(1.0, 0.0, 1.0, 1.0);
                } else {
                    requiresUpdate = true;
                    r->disableTextures(1.0, 0.0, 1.0, 1.0);
                }

                r->VBO = &distancelevel[currentDlevel].subobiekty[i].VBO;
                r->VAO = &distancelevel[currentDlevel].subobiekty[i].VAO;
                //r->mvMatrix = Mat4::clone(Game::currentRenderer->mvMatrix);
                r->vertOffset = distancelevel[currentDlevel].subobiekty[i].czesci[j].offset;
                r->vertCount = distancelevel[currentDlevel].subobiekty[i].czesci[j].iloscv;
                r->itemType = GL_TRIANGLES;
                r->vertexAttr = RenderItem::VNTA;
                r->shared = true;
                QSharedPointer<RenderItem> owner(r);
                r->cacheOwner = owner.toWeakRef();
                renderItemOwners[stateId].push_back(owner);
                renderItems[stateId].push_back(r);
                //Game::currentRenderer->pushItemVNTA(r);
            }
        }
        if(globalInvalidateRequested || textureAddressChangedDuringBuild){
            // Keep current state items, but force rebuild of other cached states.
            for(auto it = renderItems.begin(); it != renderItems.end(); ++it){
                if(it.key() == stateId)
                    continue;
                clearRenderItems(it.key());
                renderItemsTextureHash.remove(it.key());
            }
        }
        renderItemsTextureHash[stateId] = getTextureStateHash();
    }

    if(selectionId != 0){
        for(int i = 0; i < renderItems[stateId].size(); i++){
            RenderItem *baseItem = renderItems[stateId][i];
            if(baseItem == NULL)
                continue;

            RenderItem *selectionItem = new RenderItem(*baseItem);
            selectionItem->shared = false;
            selectionItem->setSelectionId(selectionId);
            selectionItem->lineWidth = 0;
            Game::currentRenderer->pushItem(selectionItem, Game::currentRenderer->mvMatrix);
        }
        return;
    }

    if(renderItems[stateId].size() > 0){
        //for(int i = 0; i < renderItems[stateId].size(); i++){
        //    Mat4::identity(renderItems[stateId][i]->mvMatrix);
        //    RenderItem *r;

            //renderItems[stateId][i]->mvMatrix =
        //            Mat4::copy(renderItems[stateId][i]->mvMatrix, Game::currentRenderer->mvMatrix);
        //    Game::currentRenderer->pushItemVNTA(r);
        //}
        //     Mat4::copy(Game::currentRenderer->mvMatrix, renderItems[stateId][i]->mvMatrix);

        Game::currentRenderer->pushItemsVNTA(renderItems[stateId], Game::currentRenderer->mvMatrix);
    }
}

void SFileLegacy::render(quint32 selectionId, unsigned int stateId) {
    if (loaded == 1 && !isGLReady() && !initGL()) return;

    if (isinit != 1 || loaded == 2)
        return;
    if (loaded == 0) {
        if(Game::objectLoadingTokens < 1)  return;

        Game::objectLoadingTokens-=2;
        loaded = 2;
        load();
        return;
    }

    if(state[stateId].enableSubObjQueue.size() > 0){
        //qDebug() << "queue"<<state[stateId].enableSubObjQueue.size();
        for (auto it = state[stateId].enableSubObjQueue.begin(); it != state[stateId].enableSubObjQueue.end();){
            enableSubObjByName(stateId, it.key(), it.value());
            it = state[stateId].enableSubObjQueue.erase(it);
        }
    }

    int oldmatrix = -2;
    float m[16];
    //var tex;
    //var oldtex = -3;
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    GLUU *gluu = GLUU::get();
    gluu->setSelectionId(selectionId);
    //gluu->enableTextures();
    //for(int iii = 0; iii < 200; iii++)
    //float talpha = gluu->alpha;
    //float talphatest = gluu->alphaTest;

    int currentDlevel = state[stateId].distanceLevel;
    for (int i = 0; i < distancelevel[currentDlevel].iloscs; i++) {
        QOpenGLVertexArrayObject::Binder vaoBinder(&distancelevel[currentDlevel].subobiekty[i].VAO);

        if(((state[stateId].enabledSubObjs >> i) & 1) == 0)
            continue;

        for (int j = 0; j < distancelevel[currentDlevel].subobiekty[i].iloscc; j++) {

            int prim_state = distancelevel[currentDlevel].subobiekty[i].czesci[j].prim_state_idx;
            int vtx_state = primstate[prim_state].vtx_state;
            int matrix = vtxstate[vtx_state].matrix;
            bool texEnabled = distancelevel[currentDlevel].subobiekty[i].czesci[j].enabled;

            if(animated){
                Mat4::identity(m);
                getPmatrixAnimated(currentDlevel, m, matrix, state[stateId].frameCount);
                oldmatrix = -1;
                gluu->currentMsMatrinxHash = 0;
                gluu->currentShader->setUniformValue(gluu->currentShader->msMatrixUniform, *reinterpret_cast<float(*)[4][4]>(&m));
            } else {
                if (oldmatrix != matrix) {
                    oldmatrix = matrix;
                    if (!macierz[matrix].isFixed) {
                        Mat4::identity(m);
                        memcpy(macierz[matrix].fixed, getPmatrix(currentDlevel, m, matrix), sizeof (float) * 16);
                        macierz[matrix].isFixed = true;
                        macierz[matrix].hash = gluu->getMatrixHash(macierz[matrix].fixed);
                    }
                    if(macierz[matrix].hash != gluu->currentMsMatrinxHash){
                        gluu->currentMsMatrinxHash = macierz[matrix].hash;
                        gluu->currentShader->setUniformValue(gluu->currentShader->msMatrixUniform, *reinterpret_cast<float(*)[4][4]>(&macierz[matrix].fixed));
                    }
                }
            }

            if( vtxstate[vtx_state].arg2 < -7 )
                gluu->disableNormals();
            else
                gluu->enableNormals();

            if( vtxstate[vtx_state].arg2 == -12 )
                gluu->setBrightness(0.5);
            else
                gluu->setBrightness(1.0);


            //if(gluu->textureEnabled)
            if(primstate[prim_state].arg4 == -1 || !texEnabled || TexLib::disabledTextures[image[texture[primstate[prim_state].arg4].image].texAddr] == 1){
                if(selectionId == 0)
                    gluu->disableTextures(1.0, 0.0, 1.0, 1.0);
                //glDisable(GL_TEXTURE_2D);
            } else if (image[texture[primstate[prim_state].arg4].image].texAddr >= 0) {
                //glEnable(GL_TEXTURE_2D);
                gluu->bindTexture(f, image[texture[primstate[prim_state].arg4].image].texAddr);
                //f->glBindTexture(GL_TEXTURE_2D, image[texture[primstate[prim_state].arg4].image].texAddr);
            } else if (image[texture[primstate[prim_state].arg4].image].tex == -2) {
                if(selectionId == 0)
                    gluu->disableTextures(1.0, 0.0, 1.0, 1.0);
                //glDisable(GL_TEXTURE_2D);
            } else if (image[texture[primstate[prim_state].arg4].image].tex == -1) {
                //image[texture[primstate[prim_state].arg4].image].tex = -2;
                //qDebug() << this->nazwa;
                //if(this->nazwa.contains("pared1_I.s", Qt::CaseInsensitive)){
                //    qDebug() << vtxstate[vtx_state].arg2;
                //    qDebug() << "=========" << image[texture[primstate[prim_state].arg4].image].name;
                //}
                image[texture[primstate[prim_state].arg4].image].tex = TexLib::addTex(
                        texPath,
                        image[texture[primstate[prim_state].arg4].image].name
                        );
                if(selectionId == 0)
                    gluu->disableTextures(1.0, 0.0, 1.0, 1.0);
                //glDisable(GL_TEXTURE_2D);
            } else if (TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->glLoaded) {
                image[texture[primstate[prim_state].arg4].image].texAddr = TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->tex[0];
                if(selectionId == 0)
                    gluu->bindTexture(f, image[texture[primstate[prim_state].arg4].image].texAddr);
            } else if (TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->loaded) {
                //if(allowLag) {
                //    allowLag = false;
                //if(TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->missing || TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->error){
                //    image[texture[primstate[prim_state].arg4].image].tex = -2;
                //} else {
                TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex]->GLTextures();
                //}
                //glDisable(GL_TEXTURE_2D);
                //}
            } else {
                if(selectionId == 0)
                    gluu->disableTextures(1.0, 0.0, 1.0, 1.0);
                //glDisable(GL_TEXTURE_2D);
            }

            //QOpenGLVertexArrayObject::Binder vaoBinder(&distancelevel[0].subobiekty[i].czesci[j].VAO);
            f->glDrawArrays(GL_TRIANGLES, distancelevel[currentDlevel].subobiekty[i].czesci[j].offset, distancelevel[currentDlevel].subobiekty[i].czesci[j].iloscv);

            if(selectionId == 0)
                gluu->enableTextures();
        }
    }
    //gluu->currentShader->setUniformValue(gluu->currentShader->shaderAlphaTest, gluu->alphaTest);
    gluu->setBrightness(1.0);
}

void SFileLegacy::fillContentHierarchyInfo(QVector<ContentHierarchyInfo*>& list, int parent){
    if (isinit != 1 || loaded != 1)
        return;

    ContentHierarchyInfo *info = new ContentHierarchyInfo();
    info->parent = parent;
    info->name = nazwa;
    info->distanceLevelId = -1; // Default
    info->shape = this;
    info->type = "shape";
    list.push_back(info);
    parent = list.size()-1;

    for (int i = 0; i < iloscd; i++) {
        ContentHierarchyInfo *info = new ContentHierarchyInfo();
        info->parent = parent;
        info->name = "Distance Level: " + QString::number(distancelevel[i].levelSelection) + " m";
        info->distanceLevelId = i;
        info->shape = this;
        info->type = "shape";
        list.push_back(info);
    }
}

void SFileLegacy::enablePart(unsigned int uid, unsigned int stateId){
    if (isinit != 1 || loaded != 1)
        return;

    int currentDlevel = state[stateId].distanceLevel;

    unsigned int i = uid / 1000;
    unsigned int j = uid - i*1000;
    distancelevel[currentDlevel].subobiekty[i].czesci[j].enabled = true;
}

void SFileLegacy::disablePart(unsigned int uid, unsigned int stateId){
    if (isinit != 1 || loaded != 1)
        return;

    int currentDlevel = state[stateId].distanceLevel;

    unsigned int i = uid / 1000;
    unsigned int j = uid - i*1000;
    distancelevel[currentDlevel].subobiekty[i].czesci[j].enabled = false;
}

void SFileLegacy::fillShapeHierarchyInfo(ShapeHierarchyInfo* info, unsigned int stateId){
    if(info == NULL)
        return;

    if (isinit != 1 || loaded != 1)
        return;

    int currentDlevel = state[stateId].distanceLevel;

    for(int i = 0; i < distancelevel[currentDlevel].ilosch; i++){
        info->hierarchy.push_back(distancelevel[currentDlevel].hierarchia[i]);
    }

    for(int i = 0; i < iloscm; i++){
        info->matrices.push_back(macierz[i].name);
    }

    for (int i = 0; i < distancelevel[currentDlevel].iloscs; i++) {
        for (int j = 0; j < distancelevel[currentDlevel].subobiekty[i].iloscc; j++) {
            int prim_state = distancelevel[currentDlevel].subobiekty[i].czesci[j].prim_state_idx;
            int vtx_state = primstate[prim_state].vtx_state;
            int matrix = vtxstate[vtx_state].matrix;

            info->parts.push_back(ShapeHierarchyInfo::ShapePart());
            info->parts.last().matrixId = matrix;
            if(primstate[prim_state].arg4 >= 0)
                info->parts.last().textureName = image[texture[primstate[prim_state].arg4].image].name;
            info->parts.last().polyCount = distancelevel[currentDlevel].subobiekty[i].czesci[j].iloscv/3;
            info->parts.last().uid = i*1000 + j;
            info->parts.last().enabled = distancelevel[currentDlevel].subobiekty[i].czesci[j].enabled;
        }
    }
}

void SFileLegacy::fillShapeTextureInfo(QHash<int, ShapeTextureInfo*>& list, unsigned int stateId){
    if (isinit != 1 || loaded != 1)
        return;

    int currentDlevel = state[stateId].distanceLevel;

    for (int i = 0; i < distancelevel[currentDlevel].iloscs; i++) {
        for (int j = 0; j < distancelevel[currentDlevel].subobiekty[i].iloscc; j++) {
            int prim_state = distancelevel[currentDlevel].subobiekty[i].czesci[j].prim_state_idx;

            if(primstate[prim_state].arg4 == -1){
                continue;
            }
            ShapeTextureInfo *tInfo = new ShapeTextureInfo();
            tInfo->enabled = true;
            tInfo->textureName = image[texture[primstate[prim_state].arg4].image].name;
            tInfo->loaded = "NONE";
            list[image[texture[primstate[prim_state].arg4].image].tex] = tInfo;

            if (image[texture[primstate[prim_state].arg4].image].tex == -2){
                continue;
            }
            if (image[texture[primstate[prim_state].arg4].image].tex == -1){
                tInfo->loading = true;
                continue;
            }

            Texture* ttex = TexLib::mtex[image[texture[primstate[prim_state].arg4].image].tex];
            if(ttex == NULL)
                continue;

            if (!ttex->loaded && !ttex->missing && !ttex->error ) {
                tInfo->loading = true;
                continue;
            }

            if(ttex->missing){
                tInfo->loaded = "MISSING";
                continue;
            }
            if(ttex->error){
                tInfo->loaded = "ERROR";
                continue;
            }

            if (!ttex->glLoaded) {
                tInfo->loading = true;
                continue;
            }

            tInfo->loaded = "YES";
            tInfo->resolution = QString::number(ttex->width) + "x" + QString::number(ttex->height);
            tInfo->textureId = image[texture[primstate[prim_state].arg4].image].tex;
        }
    }
}



float* SFileLegacy::getPmatrix(int currentDlevel, float* pmatrix, int matrix) {
    if (matrix == -1 || matrix == 0) {
        Mat4::identity(pmatrix);
        pmatrix[0] = -1;
        return pmatrix;
    } else {
        pmatrix = getPmatrix(currentDlevel, pmatrix, distancelevel[currentDlevel].hierarchia[matrix]);
    }
    Mat4::multiply(pmatrix, pmatrix, macierz[matrix].param);
    return pmatrix;
}

float* SFileLegacy::getPmatrixAnimated(int currentDlevel, float* pmatrix, int matrix, float frame){
    if (matrix == -1 || matrix == 0) {
        Mat4::identity(pmatrix);
        pmatrix[0] = -1;
        return pmatrix;
    } else {
        pmatrix = getPmatrixAnimated(currentDlevel, pmatrix, distancelevel[currentDlevel].hierarchia[matrix], frame);
    }
    float m[16];
    memcpy(m, macierz[matrix].param, sizeof (float) * 16);
    if(animations[0].node.size() > matrix){
        AnimNode *n = &animations[0].node[matrix];
        if(frame + 1 < n->tcbId.size()){
            float m1[16];
            float q1[4];
            float q2[4];
            float q3[4];
            Quat::slerp(q1, n->tcbKey[n->tcbId[(int)frame].id1].quat, n->tcbKey[n->tcbId[(int)frame].id2].quat, n->tcbId[(int)frame].offset);
            Quat::slerp(q2, n->tcbKey[n->tcbId[(int)frame+1].id1].quat, n->tcbKey[n->tcbId[(int)frame+1].id2].quat, n->tcbId[(int)frame+1].offset);
            Quat::slerp(q3, q1, q2, frame - (int)frame);
            Mat4::fromQuat(m1, q3);
            Mat4::multiply(m, m, m1);
        }
        if(frame + 1 < n->linearId.size()){
            float v1[3], v2[3], v3[3];
            Vec3::lerp(v1, n->linearKey[n->linearId[(int)frame].id1].pos, n->linearKey[n->linearId[(int)frame].id2].pos, n->linearId[(int)frame].offset);
            Vec3::lerp(v2, n->linearKey[n->linearId[(int)frame+1].id1].pos, n->linearKey[n->linearId[(int)frame+1].id2].pos, n->linearId[(int)frame+1].offset);
            Vec3::lerp(v3, v1, v2, frame - (int)frame);

            m[12] = v3[0];
            m[13] = v3[1];
            m[14] = v3[2];
        }
        //slerprot
    }
    Mat4::multiply(pmatrix, pmatrix, m);
    return pmatrix;
}

void SFileLegacy::buildFrameIds(){
    for(int i = 0; i < animations.size(); i++){
        for(int j = 0; j < animations[i].node.size(); j++){
            int frameCount = animations[i].frames;
            AnimNode *n = &animations[i].node[j];

            for(int y = 0; y < frameCount + 1; y++){
                if(n->tcbKey.size() > 0){
                    n->tcbId.push_back(AnimFrameId());
                    n->tcbId.back().id1 = 0;
                    n->tcbId.back().id2 = 0;
                    n->tcbId.back().offset = 0;
                    //qDebug() << "n->tcbKey.size()"<< n->tcbKey.size();
                    for(int u = 0; u < n->tcbKey.size() - 1; u++){
                        if(n->tcbKey[u+1].frame > y){
                            n->tcbId.back().id1 = u;
                            n->tcbId.back().id2 = u+1;
                            n->tcbId.back().offset = ((float)y - (float)n->tcbKey[u].frame)/((float)n->tcbKey[u+1].frame - (float)n->tcbKey[u].frame);
                            break;
                        }
                    }
                }
                if(n->linearKey.size() > 0){
                    n->linearId.push_back(AnimFrameId());
                    n->linearId.back().id1 = 0;
                    n->linearId.back().id2 = 0;
                    n->linearId.back().offset = 0;
                    //qDebug() << "n->tcbKey.size()"<< n->tcbKey.size();
                    for(int u = 0; u < n->linearKey.size() - 1; u++){
                        if(n->linearKey[u+1].frame > y){
                            n->linearId.back().id1 = u;
                            n->linearId.back().id2 = u+1;
                            n->linearId.back().offset = ((float)y - (float)n->linearKey[u].frame)/((float)n->linearKey[u+1].frame - (float)n->linearKey[u].frame);
                            break;
                        }
                    }
                }
                //qDebug() << y << n->tcbId.back().id1 << n->tcbId.back().id2 << n->tcbId.back().offset;

            }
        }
    }
}

// Binary shape loading

void SFileLegacy::odczytajshadersc(FileBuffer* data, SFileLegacy* shape) {
    data->skipLabel();
    shape->ishaders = Simis::count(data);
    shape->shader = new SFileLegacy::fshader[shape->ishaders];
    for (int i = 0; i < shape->ishaders; ++i) {
        Simis::Block item(data, TS::named_shader);
        shape->shader[i].name = data->readString().toLower();
        shape->shader[i].alpha = shape->shader[i].name == "texdiff" ? 1 : 0;
    }
}

void SFileLegacy::odczytajpunktyc(FileBuffer* data, SFileLegacy* shape) {
    data->skipLabel();
    shape->tpoints.ipoints = Simis::count(data);
    shape->tpoints.points = new SFileLegacy::fpoint[shape->tpoints.ipoints + 1];
    for (int i = 0; i < shape->tpoints.ipoints; ++i) {
        Simis::Block item(data, TS::point);
        shape->tpoints.points[i].x = data->getFloat();
        shape->tpoints.points[i].y = data->getFloat();
        shape->tpoints.points[i].z = data->getFloat();
    }
}

void SFileLegacy::odczytajuvpunktyc(FileBuffer* data, SFileLegacy* shape) {
    data->skipLabel();
    shape->tpoints.iuv_points = Simis::count(data);
    shape->tpoints.uv_points = new SFileLegacy::fpoint[shape->tpoints.iuv_points + 1];
    for (int i = 0; i < shape->tpoints.iuv_points; ++i) {
        Simis::Block item(data, TS::uv_point);
        shape->tpoints.uv_points[i].x = data->getFloat();
        shape->tpoints.uv_points[i].y = data->getFloat();
    }
}

void SFileLegacy::odczytajnormalnec(FileBuffer* data, SFileLegacy* shape) {
    data->skipLabel();
    shape->tpoints.inormals = Simis::count(data);
    shape->tpoints.normals = new SFileLegacy::fpoint[shape->tpoints.inormals + 1];
    for (int i = 0; i < shape->tpoints.inormals; ++i) {
        Simis::Block item(data, TS::vector);
        shape->tpoints.normals[i].x = data->getFloat();
        shape->tpoints.normals[i].y = data->getFloat();
        shape->tpoints.normals[i].z = data->getFloat();
    }
}

void SFileLegacy::odczytajmatricesc(FileBuffer* data, SFileLegacy* shape) {
    data->skipLabel();
    shape->iloscm = Simis::count(data);
    shape->macierz = new SFileLegacy::matrt[shape->iloscm + 1];
    for (int i = 0; i < shape->iloscm; ++i) {
        Simis::Block item(data, TS::matrix);
        shape->macierz[i].name = item.label();
        for (int j = 0; j < 16; ++j)
            shape->macierz[i].param[j] = j == 15 ? 1
                    : (j == 3 || j == 7 || j == 11) ? 0 : data->getFloat();
    }
}

void SFileLegacy::odczytajimagesc(FileBuffer* data, SFileLegacy* shape) {
    data->skipLabel();
    shape->ilosci = Simis::count(data);
    shape->image = new SFileLegacy::imgs[shape->ilosci + 1];
    for (int i = 0; i < shape->ilosci; ++i) {
        Simis::Block item(data, TS::image);
        shape->image[i].name = data->readString();
        shape->image[i].tex = -1;
    }
}

void SFileLegacy::odczytajtexturesc(FileBuffer* data, SFileLegacy* shape) {
    data->skipLabel();
    shape->ilosct = Simis::count(data);
    shape->texture = new SFileLegacy::text[shape->ilosct];
    for (int i = 0; i < shape->ilosct; ++i) {
        Simis::Block item(data, TS::texture);
        shape->texture[i].image = data->getInt();
        shape->texture[i].arg1 = data->getInt();
        shape->texture[i].arg2 = data->getInt();
        shape->texture[i].arg3 = data->getInt();
    }
}

void SFileLegacy::odczytajvtx_statesc(FileBuffer* data, SFileLegacy* shape) {
    data->skipLabel();
    shape->iloscv = Simis::count(data);
    shape->vtxstate = new SFileLegacy::vtxs[shape->iloscv];
    for (int i = 0; i < shape->iloscv; ++i) {
        Simis::Block item(data, TS::vtx_state);
        shape->vtxstate[i].arg1 = data->getInt();
        shape->vtxstate[i].matrix = data->getInt();
        shape->vtxstate[i].arg2 = data->getInt();
        shape->vtxstate[i].arg3 = data->getInt();
        shape->vtxstate[i].arg4 = data->getInt();
    }
}

void SFileLegacy::odczytajprim_statesc(FileBuffer* data, SFileLegacy* shape) {
    data->skipLabel();
    shape->iloscps = Simis::count(data);
    shape->primstate = new SFileLegacy::primst[shape->iloscps];
    for (int i = 0; i < shape->iloscps; ++i) {
        Simis::Block item(data, TS::prim_state);
        shape->primstate[i].arg1 = data->getInt();
        shape->primstate[i].arg2 = data->getInt();
        {
            Simis::Block textures(data, TS::tex_idxs);
            shape->primstate[i].arg3 = Simis::count(data, 4);
            shape->primstate[i].arg4 = -1;
            for (int j = 0; j < shape->primstate[i].arg3; ++j) {
                const int index = data->getInt();
                if (j == 0) shape->primstate[i].arg4 = index;
            }
        }
        shape->primstate[i].arg5 = data->getInt();
        shape->primstate[i].vtx_state = data->getInt();
        shape->primstate[i].arg6 = data->getInt();
        shape->primstate[i].arg7 = data->getInt();
        shape->primstate[i].arg8 = data->getInt();
    }
}

void SFileLegacy::odczytajloddc(FileBuffer* bufor, SFileLegacy* pliks) {
    std::vector<fvertex> vert;


    bufor->skipLabel();
    const int controls = Simis::count(bufor);
    if (!controls) return;
    // Preserve TSRE's single-LOD-control policy.
    Simis::Block control(bufor, TS::lod_control);
    {
        Simis::Block header(bufor, TS::distance_levels_header);
    }
    Simis::Block levels(bufor, TS::distance_levels);
    pliks->iloscd = Simis::count(bufor);
    pliks->distancelevel = new SFileLegacy::dist[pliks->iloscd];

    for (int j = 0; j < pliks->iloscd; ++j) {
        Simis::Block level(bufor, TS::distance_level);
        {
            Simis::Block header(bufor, TS::distance_level_header);
            {
                Simis::Block selection(bufor, TS::dlevel_selection);
                pliks->distancelevel[j].levelSelection = bufor->getFloat();
            }
            {
                Simis::Block hierarchy(bufor, TS::hierarchy);
                pliks->distancelevel[j].ilosch = Simis::count(bufor, 4);
                pliks->distancelevel[j].hierarchia = new int[pliks->distancelevel[j].ilosch + 1];
                for (int i = 0; i < pliks->distancelevel[j].ilosch; ++i)
                    pliks->distancelevel[j].hierarchia[i] = bufor->getInt();
            }
        }
        Simis::Block subObjects(bufor, TS::sub_objects);
        pliks->distancelevel[j].iloscs = Simis::count(bufor);
        pliks->distancelevel[j].subobiekty = new SFileLegacy::sub[pliks->distancelevel[j].iloscs + 1];
        for (int ii = 0; ii < pliks->distancelevel[j].iloscs; ++ii) {
            Simis::Block object(bufor, TS::sub_object);
            {
                Simis::Block header(bufor, TS::sub_object_header);
                bufor->require(20);
                bufor->off += 20;
                while (bufor->off < bufor->readEnd()) {
                    // The optional final SubObjID is a scalar, not a child block
                    // (newshape.bnf: sub_object_header). TSRE does not use it.
                    if (bufor->readEnd() - bufor->off == 4) {
                        bufor->getUint();
                        break;
                    }
                    const auto child = bufor->readBlock();
                    FileBuffer::ScopedLimit childScope(*bufor, child.end);
                    if (child.id == TS::geometry_info) {
                        bufor->skipLabel();
                        bufor->require(40);
                        bufor->off += 40;
                        while (bufor->off < child.end) {
                            const auto geometry = bufor->readBlock();
                            FileBuffer::ScopedLimit geometryScope(*bufor, geometry.end);
                            if (geometry.id == TS::geometry_node_map) {
                                bufor->skipLabel();
                                const int count = Simis::count(bufor, 4);
                                for (int i = 0; i < count; ++i)
                                    pliks->distancelevel[j].subobiekty[ii].header.geometryNodeMap.push_back(bufor->getInt());
                            }
                            bufor->off = geometry.end;
                        }
                    }
                    bufor->off = child.end;
                }
            }
            {
                Simis::Block vertices(bufor, TS::vertices);
                const int count = Simis::count(bufor);
                vert.clear();
                vert.resize(count);
                for (int i = 0; i < count; ++i) {
                    Simis::Block vertex(bufor, TS::vertex);
                    vert[i].arg1 = short(bufor->getInt());
                    vert[i].point = bufor->getUint();
                    vert[i].normal = bufor->getUint();
                    vert[i].arg2 = short(bufor->getInt());
                    vert[i].arg3 = short(bufor->getInt());
                    Simis::Block uvs(bufor, TS::vertex_uvs);
                    const int uvCount = Simis::count(bufor, 4);
                    vert[i].material = short(uvCount);
                    for (int u = 0; u < uvCount; ++u) {
                        const auto index = bufor->getUint();
                        if (!u) vert[i].uvpoint = index;
                    }
                }
            }
            int czilosc = 0, aktidx = 0;
            {
                Simis::Block primitives(bufor, TS::primitives);
                const int count = Simis::count(bufor);
                pliks->distancelevel[j].subobiekty[ii].czesci = new SFileLegacy::czes[count + 1];
                for (int i = 0; i < count; ++i) {
                    const auto primitive = bufor->readBlock();
                    FileBuffer::ScopedLimit primitiveScope(*bufor, primitive.end);
                    bufor->skipLabel();
                    if (primitive.id == TS::prim_state_idx) {
                        aktidx = bufor->getInt();
                    } else if (primitive.id == TS::indexed_trilist) {
                        // Remaining normal/flag children are not rendered by TSRE.
                        Simis::Block indices(bufor, TS::vertex_idxs);
                        auto& part = pliks->distancelevel[j].subobiekty[ii].czesci[czilosc++];
                        // Publish ownership before any subsequent read can throw.
                        pliks->distancelevel[j].subobiekty[ii].iloscc = czilosc;
                        part.prim_state_idx = aktidx;
                        part.iloscv = Simis::count(bufor, 4);
                        part.idx = new int[part.iloscv];
                        for (int k = part.iloscv - 1; k >= 0; --k)
                            part.idx[k] = bufor->getInt();
                    }
                    bufor->off = primitive.end;
                }
            }
            pliks->distancelevel[j].subobiekty[ii].iloscc = czilosc;
                pliks->distancelevel[j].subobiekty[ii].vertices = std::move(vert);
            }
        }
        return;
    }

// UTF-16 shape loading

void SFileLegacy::odczytajshaders(FileBuffer* bufor, SFileLegacy* pliks) {
    pliks->ishaders = ParserX::GetNumber(bufor);

    pliks->shader = new SFileLegacy::fshader[pliks->ishaders];
    for (int i = 0; i < pliks->ishaders; i++) {
        while (bufor->get() != 40) {
            bufor->off++;
        }
        bufor->off++;
        bufor->off += 2;
        pliks->shader[i].name = ParserX::GetString(bufor).toLower();
        if(pliks->shader[i].name == "texdiff")
            pliks->shader[i].alpha = 1;
        else
            pliks->shader[i].alpha = 0;
    }
    ParserX::SkipToken(bufor);
}

//-----------------------------------
//Oczytanie i zapisanie sekcji points
//-----------------------------------

void SFileLegacy::odczytajpunkty(FileBuffer* bufor, SFileLegacy* pliks) {
    pliks->tpoints.ipoints = ParserX::GetNumber(bufor);
    pliks->tpoints.points = new SFileLegacy::fpoint[pliks->tpoints.ipoints + 1];
    for (int i = 0; i < pliks->tpoints.ipoints; i++) {
        pliks->tpoints.points[i].x = ParserX::GetNumber(bufor);
        pliks->tpoints.points[i].y = ParserX::GetNumber(bufor);
        pliks->tpoints.points[i].z = ParserX::GetNumber(bufor);
    }
    ParserX::SkipToken(bufor);
    return;
};
//-----------------------------------
//Oczytanie i zapisanie sekcji uv_points
//-----------------------------------

void SFileLegacy::odczytajuvpunkty(FileBuffer* bufor, SFileLegacy* pliks) {
    pliks->tpoints.iuv_points = ParserX::GetNumber(bufor);
    pliks->tpoints.uv_points = new SFileLegacy::fpoint[ pliks->tpoints.iuv_points + 1];
    for (int i = 0; i < pliks->tpoints.iuv_points; i++) {
        pliks->tpoints.uv_points[i].x = ParserX::GetNumber(bufor);
        pliks->tpoints.uv_points[i].y = ParserX::GetNumber(bufor);
    }
    ParserX::SkipToken(bufor);
    return;
};
//-----------------------------------
//Oczytanie i zapisanie sekcji normals
//-----------------------------------

void SFileLegacy::odczytajnormalne(FileBuffer* bufor, SFileLegacy* pliks) {
    pliks->tpoints.inormals = ParserX::GetNumber(bufor);
    pliks->tpoints.normals = new SFileLegacy::fpoint[pliks->tpoints.inormals + 1];
    for (int i = 0; i < pliks->tpoints.inormals; i++) {
        pliks->tpoints.normals[i].x = ParserX::GetNumber(bufor);
        pliks->tpoints.normals[i].y = ParserX::GetNumber(bufor);
        pliks->tpoints.normals[i].z = ParserX::GetNumber(bufor);
    }
    ParserX::SkipToken(bufor);
    return;
};
//-----------------------------------
//Oczytanie i zapisanie sekcji matrices
//-----------------------------------

void SFileLegacy::odczytajmatrices(FileBuffer* bufor, SFileLegacy* pliks) {
    pliks->iloscm = ParserX::GetNumber(bufor);
    pliks->macierz = new SFileLegacy::matrt[pliks->iloscm + 1];

    for (int i = 0; i < pliks->iloscm; i++) {
        ParserX::NextTokenInside(bufor);
        pliks->macierz[i].name = ParserX::GetString(bufor);
        while (bufor->get() != 40) {
            bufor->off++;
        }
        bufor->off++;
        for (int j = 0; j < 16; j++) {
            if (j == 3 || j == 7 || j == 11) {
                pliks->macierz[i].param[j] = 0;
                continue;
            }
            if (j == 15) {
                pliks->macierz[i].param[j] = 1;
                continue;
            }
            pliks->macierz[i].param[j] = ParserX::GetNumber(bufor);
        }
        ParserX::SkipToken(bufor);
    }
    return;
};
//-----------------------------------
//Oczytanie i zapisanie sekcji images
//-----------------------------------

void SFileLegacy::odczytajimages(FileBuffer* bufor, SFileLegacy* pliks) {
    pliks->ilosci = ParserX::GetNumber(bufor);
    pliks->image = new SFileLegacy::imgs[pliks->ilosci + 1];
    for (int i = 0; i < pliks->ilosci; i++) {
        while (bufor->get() != 40) {
            bufor->off++;
        }
        bufor->off++;
        bufor->off += 2;
        int j = 0;
        pliks->image[i].name = ParserX::GetString(bufor);
        pliks->image[i].tex = -1;
    }
    ParserX::SkipToken(bufor);
    return;
};
//-----------------------------------
//Oczytanie i zapisanie sekcji textures
//-----------------------------------

void SFileLegacy::odczytajtextures(FileBuffer* bufor, SFileLegacy* pliks) {
    pliks->ilosct = ParserX::GetNumber(bufor);
    pliks->texture = new SFileLegacy::text[pliks->ilosct];
    for (int i = 0; i < pliks->ilosct; i++) {
        while (bufor->get() != 40) {
            bufor->off++;
        }
        bufor->off++;
        pliks->texture[i].image = ParserX::GetNumber(bufor);
        pliks->texture[i].arg1 = ParserX::GetNumber(bufor);
        pliks->texture[i].arg2 = ParserX::GetNumber(bufor);
        pliks->texture[i].arg3 = ParserX::GetNumber(bufor);
    }
    ParserX::SkipToken(bufor);
    return;
};
//-----------------------------------
//Oczytanie i zapisanie sekcji vtx_states
//-----------------------------------

void SFileLegacy::odczytajvtx_states(FileBuffer* bufor, SFileLegacy* pliks) {
    pliks->iloscv = ParserX::GetNumber(bufor);
    pliks->vtxstate = new SFileLegacy::vtxs[pliks->iloscv];
    for (int i = 0; i < pliks->iloscv; i++) {
        while (bufor->get() != 40) {
            bufor->off++;
        }
        bufor->off++;
        pliks->vtxstate[i].arg1 = ParserX::GetHex(bufor);
        pliks->vtxstate[i].matrix = ParserX::GetNumber(bufor);
        pliks->vtxstate[i].arg2 = ParserX::GetNumber(bufor);
        pliks->vtxstate[i].arg3 = ParserX::GetNumber(bufor);

        while (bufor->get() != 41) {
            bufor->off++;
        }
        bufor->off++;
        pliks->vtxstate[i].arg4 = 0;
    }
    //ParserX::SkipToken(bufor);
    return;
};
//-----------------------------------
//Oczytanie i zapisanie sekcji prim_states
//-----------------------------------

void SFileLegacy::odczytajprim_states(FileBuffer* bufor, SFileLegacy* pliks) {
    pliks->iloscps = ParserX::GetNumber(bufor);
    pliks->primstate = new SFileLegacy::primst[pliks->iloscps];
    for (int i = 0; i < pliks->iloscps; i++) {
        while (bufor->get() != 40) {
            bufor->off++;
        }
        bufor->off++;
        pliks->primstate[i].arg1 = ParserX::GetHex(bufor);
        pliks->primstate[i].arg2 = ParserX::GetNumber(bufor);
        pliks->primstate[i].arg3 = ParserX::GetNumber(bufor);
        pliks->primstate[i].arg4 = -1;
        for (int j = 0; j < pliks->primstate[i].arg3; j++) {
            if (j == 0) pliks->primstate[i].arg4 = ParserX::GetNumber(bufor);
            else ParserX::GetNumber(bufor);
        }
        pliks->primstate[i].arg5 = ParserX::GetNumber(bufor);
        pliks->primstate[i].vtx_state = ParserX::GetNumber(bufor);
        pliks->primstate[i].arg6 = ParserX::GetNumber(bufor);
        pliks->primstate[i].arg7 = ParserX::GetNumber(bufor);
        pliks->primstate[i].arg8 = ParserX::GetNumber(bufor);
    }
    ParserX::SkipToken(bufor);
    return;
};
//-----------------------------------
//Oczytanie i zapisanie sekcji loddetail
//-----------------------------------

void SFileLegacy::odczytajlodd(FileBuffer* bufor, SFileLegacy* pliks) {
    QString sh = "";
    int i, j, ii, jj;
    int iloscs, iloscv;
    int aktidx, iloscp, w, czilosc = 0, iii;
    int n, p, txt, nul;
    int v_ilosc;

    std::vector<fvertex> vert;
    i = 0; // odczytujemy jeden lodcontrol=0;
    sh = "lod_control";
    ParserX::FindTokenDomIgnore(sh, bufor);
    //szukamy sekcji distance levels
    sh = "distance_levels";
    ParserX::FindTokenDomIgnore(sh, bufor);
    // wczytujemy ilosc distancelevels
    pliks->iloscd = ParserX::GetNumber(bufor);
    pliks->distancelevel = new SFileLegacy::dist[pliks->iloscd];

    // wczytujemy distancelevls
    for (j = 0; j < pliks->iloscd; j++) {

        sh = "distance_level";
        ParserX::FindTokenDomIgnore(sh, bufor);
        //wczytanie hierarchii
        sh = "distance_level_header";
        ParserX::FindTokenDomIgnore(sh, bufor);
        pliks->distancelevel[j].levelSelection = ParserX::GetNumber(bufor); //dlevel_selection//
        pliks->distancelevel[j].ilosch = ParserX::GetNumber(bufor);
        pliks->distancelevel[j].hierarchia = new int[pliks->distancelevel[j].ilosch + 1];
        for (ii = 0; ii < pliks->distancelevel[j].ilosch; ii++) {
            pliks->distancelevel[j].hierarchia[ii] = ParserX::GetNumber(bufor);
        }

        //szukamy subobjektow
        sh = "sub_objects";
        ParserX::FindTokenDomIgnore(sh, bufor);
        //ilosc subobjektow
        pliks->distancelevel[j].iloscs = ParserX::GetNumber(bufor);
        // przydzielenie im pamieci
        pliks->distancelevel[j].subobiekty = new SFileLegacy::sub[pliks->distancelevel[j].iloscs + 1];

        //wczytujemy subobjekty
        iloscs = pliks->distancelevel[j].iloscs;
        for (ii = 0; ii < iloscs; ii++) {
            sh = "sub_object";
            ParserX::FindTokenDomIgnore(sh, bufor);

            while (!((sh = ParserX::NextTokenInside(bufor).toLower()) == "")) {
                if(sh == "sub_object_header"){
                    ParserX::GetHex(bufor);
                    ParserX::GetNumber(bufor);
                    ParserX::GetNumber(bufor);
                    ParserX::GetHex(bufor);
                    ParserX::GetHex(bufor);
                    while (!((sh = ParserX::NextTokenInside(bufor).toLower()) == "")) {
                        if(sh == "geometry_info"){
                            while (!((sh = ParserX::NextTokenInside(bufor).toLower()) == "")) {
                                if(sh == "geometry_nodes"){
                                    ParserX::SkipToken(bufor);
                                    continue;
                                }
                                if(sh == "geometry_node_map"){
                                    int count = ParserX::GetNumber(bufor);
                                    for(int ignm = 0; ignm < count; ignm++ ){
                                        pliks->distancelevel[j].subobiekty[ii].header.geometryNodeMap.push_back(ParserX::GetNumber(bufor));
                                    }
                                    ParserX::SkipToken(bufor);
                                    continue;
                                }
                                qDebug() << "#SFile geometry_info - undefined token: " << sh;
                                ParserX::SkipToken(bufor);
                            }
                            ParserX::SkipToken(bufor);
                            continue;
                        }
                        if(sh == "subobject_shaders"){
                            ParserX::SkipToken(bufor);
                            continue;
                        }
                        if(sh == "subobject_light_cfgs"){
                            ParserX::SkipToken(bufor);
                            continue;
                        }
                        qDebug() << "#SFile sub_object_header - undefined token: " << sh;
                        ParserX::SkipToken(bufor);
                    }
                    ParserX::SkipToken(bufor);
                    continue;
                }
                if(sh == "vertices"){
                    v_ilosc = ParserX::GetNumber(bufor);
                    iloscv = v_ilosc;
                    if (v_ilosc < 0 || v_ilosc > bufor->length / 2)
                        throw FileBuffer::ParseError("Invalid shape vertex count");
                    vert.clear();
                    vert.resize(v_ilosc);
                    for (jj = 0; jj < iloscv; jj++) {
                        vert[jj].arg1 = (short) ParserX::GetHex(bufor);
                        vert[jj].point = (unsigned int) ParserX::GetNumber(bufor);
                        vert[jj].normal = (unsigned int) ParserX::GetNumber(bufor);
                        vert[jj].arg2 = (short) ParserX::GetHex(bufor);
                        vert[jj].arg3 = (short) ParserX::GetHex(bufor);

                        //vert[jj].arg2 =
                        //vert[jj].arg3 =
                        vert[jj].material = (short) ParserX::GetNumber(bufor);

                        for (int jjjj = 0; jjjj < vert[jj].material; jjjj++) {
                            if (jjjj == 0) vert[jj].uvpoint = (unsigned int) ParserX::GetNumber(bufor);
                            else ParserX::GetNumber(bufor);
                        }
                    }
                    ParserX::SkipToken(bufor);
                    ParserX::SkipToken(bufor);
                    ParserX::SkipToken(bufor);
                    continue;
                }
                if(sh == "vertex_sets"){
                    ParserX::SkipToken(bufor);
                    continue;
                }
                if(sh == "primitives"){
                    pliks->distancelevel[j].subobiekty[ii].iloscc = ParserX::GetNumber(bufor);
                    // przydzielenie im pamieci
                    pliks->distancelevel[j].subobiekty[ii].czesci = new SFileLegacy::czes[pliks->distancelevel[j].subobiekty[ii].iloscc + 1];

                    //wczytaj czesci
                    czilosc = 0;
                    aktidx = 0;

                    for (jj = 0; jj < pliks->distancelevel[j].subobiekty[ii].iloscc; jj++) {
                        //wybor sekcji lista czy indeks
                        //w = ParserX::sekcjap(bufor);
                        sh = ParserX::NextTokenDomIgnore(bufor).toLower();
                        //w =
                        if (sh == "indexed_trilist") {
                            //jesli lista
                            pliks->distancelevel[j].subobiekty[ii].czesci[czilosc].prim_state_idx = aktidx;
                            //wczytanie indeksow wierzcholkow
                            sh = "vertex_idxs";
                            ParserX::FindTokenDomIgnore(sh, bufor);
                            pliks->distancelevel[j].subobiekty[ii].czesci[czilosc].iloscv = ParserX::GetNumber(bufor);

                            pliks->distancelevel[j].subobiekty[ii].czesci[czilosc].idx = new int[pliks->distancelevel[j].subobiekty[ii].czesci[czilosc].iloscv];
                            //lista wierzcholkow
                            for (int iii = pliks->distancelevel[j].subobiekty[ii].czesci[czilosc].iloscv - 1; iii >= 0; iii--) {
                                //w = ParserX::GetNumber(bufor);
                                pliks->distancelevel[j].subobiekty[ii].czesci[czilosc].idx[iii] = ParserX::GetNumber(bufor);

                            }

                            //pominiecie normals i flags
                            sh = "normal_idxs";
                            ParserX::FindTokenDomIgnore(sh, bufor);
                            sh = "flags";
                            ParserX::FindTokenDomIgnore(sh, bufor);
                            ParserX::SkipToken(bufor);
                            czilosc++;
                        } else {
                            aktidx = ParserX::GetNumber(bufor);
                        }
                    }
                    pliks->distancelevel[j].subobiekty[ii].iloscc = czilosc;
                pliks->distancelevel[j].subobiekty[ii].vertices = std::move(vert);

                    ParserX::SkipToken(bufor);
                    ParserX::SkipToken(bufor);
                    continue;
                }
                qDebug() << "#SFile subobject - undefined token: " << sh;
                ParserX::SkipToken(bufor);
            }
        }
    }
    ParserX::SkipToken(bufor);
    ParserX::SkipToken(bufor);
    ParserX::SkipToken(bufor);
    ParserX::SkipToken(bufor);
    ParserX::SkipToken(bufor);
    //ParserX::SkipToken(bufor);
    //ParserX::SkipToken(bufor);
    return;
};

// GL buffer initialization

bool SFileLegacy::isGLReady() const {
    return glReady && glContext && glContext == QOpenGLContext::currentContext();
}

bool SFileLegacy::initGL() {
    // Uploaded source has been released. Another/lost context requires reload().
    if (glReady) return isGLReady();
    if (loaded != 1 || !QOpenGLContext::currentContext()) return false;
    auto *f = QOpenGLContext::currentContext()->functions();
    auto *extra = QOpenGLContext::currentContext()->extraFunctions();
    GLint previousVao = 0, previousBuffer = 0;
    f->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    f->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    bool uploaded = false;
    const auto restore = qScopeGuard([&] {
        if (!uploaded) {
            for (int l = 0; l < iloscd; ++l)
                for (int o = 0; o < distancelevel[l].iloscs; ++o) {
                    auto &object = distancelevel[l].subobiekty[o];
                    object.VAO.destroy();
                    object.VBO.destroy();
                }
            glContext.clear();
            glReady = false;
        }
        extra->glBindVertexArray(GLuint(previousVao));
        f->glBindBuffer(GL_ARRAY_BUFFER, GLuint(previousBuffer));
    });
    auto *gluu = GLUU::get();
    auto *pliks = this;
    int w, n, p, txt;
    std::unique_ptr<float[]> expanded;
    size_t expandedCapacity = 0;
    try {
        for (int j = 0; j < iloscd; ++j) {
            for (int ii = 0; ii < distancelevel[j].iloscs; ++ii) {
                auto &object = distancelevel[j].subobiekty[ii];
                const auto &vert = object.vertices;
                size_t count = 0;
                for (int k = 0; k < object.iloscc; ++k) {
                    if (object.czesci[k].iloscv < 0) return false;
                    count += size_t(object.czesci[k].iloscv);
                }
                if (count > size_t(std::numeric_limits<int>::max()) / (9 * sizeof(float)))
                    return false;
                /////////////////////////
                int iloscv = 0;
                int offset = 0;
                for (int jj = 0; jj < pliks->distancelevel[j].subobiekty[ii].iloscc; jj++) {
                    iloscv += pliks->distancelevel[j].subobiekty[ii].czesci[jj].iloscv;
                }

                if (!object.VAO.isCreated()) object.VAO.create();
                QOpenGLVertexArrayObject::Binder vaoBinder(&pliks->distancelevel[j].subobiekty[ii].VAO);

                if (!pliks->distancelevel[j].subobiekty[ii].VAO.isCreated() ||
                    !pliks->distancelevel[j].subobiekty[ii].VBO.create()) return false;
                if (!pliks->distancelevel[j].subobiekty[ii].VBO.bind()) return false;
                pliks->distancelevel[j].subobiekty[ii].VBO.allocate(iloscv * 9 * sizeof(GLfloat));
                if (object.VBO.size() != int(iloscv * 9 * sizeof(GLfloat))) {
                    object.VBO.release();
                    return false;
                }
                f->glEnableVertexAttribArray(0);
                f->glEnableVertexAttribArray(1);
                f->glEnableVertexAttribArray(2);
                f->glEnableVertexAttribArray(3);
                f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), 0);
                f->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), reinterpret_cast<void *>(3 * sizeof(GLfloat)));
                f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), reinterpret_cast<void *>(6 * sizeof(GLfloat)));
                f->glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), reinterpret_cast<void *>(8 * sizeof(GLfloat)));

                for (int jj = 0; jj < pliks->distancelevel[j].subobiekty[ii].iloscc; jj++) {
                    const auto &part = object.czesci[jj];
                    const size_t required = size_t(part.iloscv) * 9;
                    if (required > expandedCapacity) {
                        expanded.reset();
                        expanded.reset(new float[required]);
                        expandedCapacity = required;
                    }
                    float *wierzcholki = expanded.get();
                    float alphaTest = 0;
                    if (part.iloscv > 0) {
                        const int prim_state = part.prim_state_idx;
                        if (prim_state < 0 || prim_state >= iloscps)
                            throw FileBuffer::ParseError("Invalid shape vertex or primitive-state index");
                        const auto &material = primstate[prim_state];
                        alphaTest = material.arg6 == 1 ? -0.51f : -gluu->alphaTest;
                        if (material.arg2 >= 0 && material.arg2 < ishaders &&
                            shader[material.arg2].alpha == 1)
                            alphaTest = 1.0f;
                    }

                    for (int iii = pliks->distancelevel[j].subobiekty[ii].czesci[jj].iloscv - 1; iii >= 0; iii--) {
                            //pliks->distancelevel[j].subobiekty[ii].czesci[czilosc].wierzcholki[iii] = new SFileLegacy::wie();

                        w = pliks->distancelevel[j].subobiekty[ii].czesci[jj].idx[iii];
                        if (w < 0 || w >= int(vert.size()))
                            throw FileBuffer::ParseError("Invalid shape vertex or primitive-state index");
                        n = vert[w].normal;
                        txt = vert[w].uvpoint;
                        p = vert[w].point;
                        if (p < 0 || p >= pliks->tpoints.ipoints || n < 0 || n >= pliks->tpoints.inormals
                                || txt < 0 || txt >= pliks->tpoints.iuv_points)
                            throw FileBuffer::ParseError("Invalid shape point/normal/UV index");

                        wierzcholki[iii*9+0] = pliks->tpoints.points[p].x;
                        wierzcholki[iii*9+1] = pliks->tpoints.points[p].y;
                        wierzcholki[iii*9+2] = pliks->tpoints.points[p].z;
                        wierzcholki[iii*9+3] = pliks->tpoints.normals[n].x;
                        wierzcholki[iii*9+4] = pliks->tpoints.normals[n].y;
                        wierzcholki[iii*9+5] = pliks->tpoints.normals[n].z;
                        wierzcholki[iii*9+6] = pliks->tpoints.uv_points[txt].x;
                        wierzcholki[iii*9+7] = pliks->tpoints.uv_points[txt].y;
                        wierzcholki[iii*9+8] = alphaTest;
                            //directxSmierdzi-=2;
                            //if(directxSmierdzi<-2) directxSmierdzi = 2;
                    }
                    pliks->distancelevel[j].subobiekty[ii].VBO.write(offset * 9 * sizeof(GLfloat), wierzcholki, pliks->distancelevel[j].subobiekty[ii].czesci[jj].iloscv * 9 * sizeof(GLfloat));

                    pliks->distancelevel[j].subobiekty[ii].czesci[jj].offset = offset;
                    offset += pliks->distancelevel[j].subobiekty[ii].czesci[jj].iloscv;

                }
                pliks->distancelevel[j].subobiekty[ii].VBO.release();
            }
        }
    } catch (const FileBuffer::ParseError &error) {
        qWarning() << "Cannot initialize legacy shape GL" << pathid << error.what();
        return false;
    }
    // Keep indexed source until every upload succeeds, allowing a retry on failure.
    for (int j = 0; j < iloscd; ++j)
        for (int i = 0; i < distancelevel[j].iloscs; ++i) {
            auto &object = distancelevel[j].subobiekty[i];
            std::vector<fvertex>().swap(object.vertices);
            for (int k = 0; k < object.iloscc; ++k) {
                delete[] object.czesci[k].idx;
                object.czesci[k].idx = nullptr;
            }
        }
    delete[] tpoints.points; tpoints.points = nullptr;
    delete[] tpoints.normals; tpoints.normals = nullptr;
    delete[] tpoints.uv_points; tpoints.uv_points = nullptr;
    glContext = QOpenGLContext::currentContext();
    glReady = true;
    uploaded = true;
    return true;
}

void SFileLegacy::clearData() {
    invalidateRenderState();
    for (int j = 0; distancelevel && j < iloscd; ++j) {
        auto &level = distancelevel[j];
        for (int i = 0; level.subobiekty && i < level.iloscs; ++i) {
            auto &object = level.subobiekty[i];
            for (int k = 0; object.czesci && k < object.iloscc; ++k)
                delete[] object.czesci[k].idx;
            delete[] object.czesci;
        }
        delete[] level.subobiekty;
        delete[] level.hierarchia;
    }
    delete[] distancelevel; distancelevel = nullptr; iloscd = 0;
    delete[] tpoints.points; delete[] tpoints.normals; delete[] tpoints.uv_points;
    tpoints = {};
    delete[] macierz; macierz = nullptr; iloscm = 0;
    delete[] image; image = nullptr; ilosci = 0;
    delete[] texture; texture = nullptr; ilosct = 0;
    delete[] vtxstate; vtxstate = nullptr; iloscv = 0;
    delete[] primstate; primstate = nullptr; iloscps = 0;
    delete[] shader; shader = nullptr; ishaders = 0;
    animations.clear();
    glReady = false;
    glContext.clear();
    texloaded = 0;
}
