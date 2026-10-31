/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/shape/SFileLegacy.h>
#include <tsre/renderer/Mesh.h>


#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/texture/TexLib.h>
#include <tsre/shape/ShapeLoader.h>
#include <tsre/world/TerrainSeason.h>
#include <tsre/shape/NightTextures.h>
#include <tsre/texture/Texture.h>
#include <QDebug>
#include <QtCore>
#include <iostream>
#include <QOpenGLShaderProgram>
#include <tsre/ogl/GLUU.h>
#include <tsre/math3d/GLMatrix.h>
#include <algorithm>
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
#include <tsre/renderer/SelectionId.h>
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
    this->textureRoot = texp;
    state.push_back(State());
}


SFileLegacy::~SFileLegacy() {
    ShapeLoader::cancel(this);
    clearData();
}

bool SFileLegacy::setLoadOptions(const ShapeLoadOptions &options) {
    doubleSided = options.value(QString::fromLatin1(ShapeLoadOption::DoubleSided), false).toBool();
    return true;
}

void SFileLegacy::load() {
    if (loadData()) initGL();
}

ComplexShape *SFileLegacy::detachedCopy() const {
    SFileLegacy *copy = new SFileLegacy(pathid, nazwa, textureRoot);
    copy->doubleSided = doubleSided;
    return copy;
}

void SFileLegacy::loadDetached() {
    load();
}

void SFileLegacy::adopt(ComplexShape &copy) {
    auto *loadedCopy = dynamic_cast<SFileLegacy *>(&copy);
    if (loadedCopy == nullptr || loaded != 0)
        return;
    clearData();
    // Everything loadData(), loadSd() and initGL() fill; the copy takes this
    // shape's empty data and is deleted.
    SFileLegacy &o = *loadedCopy;
    std::swap(animations, o.animations);
    std::swap(texPath, o.texPath);
    std::swap(sdName, o.sdName);
    std::swap(loadedSd, o.loadedSd);
    std::swap(texloaded, o.texloaded);
    std::swap(esdDetailLevel, o.esdDetailLevel);
    std::swap(esdAlternativeTexture, o.esdAlternativeTexture);
    std::swap(esdBoundingBox, o.esdBoundingBox);
    std::swap(snapable, o.snapable);
    std::swap(pbrFactors, o.pbrFactors);
    std::swap(primFactors, o.primFactors);
    std::swap(tpoints, o.tpoints);
    std::swap(iloscd, o.iloscd);
    std::swap(distancelevel, o.distancelevel);
    std::swap(iloscm, o.iloscm);
    std::swap(macierz, o.macierz);
    std::swap(ilosci, o.ilosci);
    std::swap(image, o.image);
    std::swap(ilosct, o.ilosct);
    std::swap(texture, o.texture);
    std::swap(iloscv, o.iloscv);
    std::swap(vtxstate, o.vtxstate);
    std::swap(iloscps, o.iloscps);
    std::swap(primstate, o.primstate);
    std::swap(ishaders, o.ishaders);
    std::swap(shader, o.shader);
    std::swap(size, o.size);
    std::swap(bound, o.bound);
    std::swap(glReady, o.glReady);
    // A missing file leaves the copy at 0; a failed load is 2 here.
    loaded = o.loaded == 1 ? 1 : 2;
    o.loaded = 0;
    requiresUpdate = true;
    ++invalidations;
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
        for(Animation &a : animations)
            for(ShapeAnimation::Channel &c : a.node)
                ShapeAnimation::sortKeys(c);
    loadSd();
    if (loaded == 1)
        resolvePbrFactors();
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
                node.push_back(ShapeAnimation::Channel());
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
                            // TCB keys' tension, continuity and bias left
                            // out, as Open Rails leaves them.
                            if (controller.id == TS::tcb_rot && (key.id == TS::tcb_key || key.id == TS::slerp_rot)) {
                                ShapeAnimation::RotationKey value;
                                value.frame = data->getUint();
                                float msts[4];
                                for (float &c : msts) c = data->getFloat();
                                ShapeAnimation::fromMsts(msts, value.rotation);
                                node.back().rotations.push_back(value);
                            } else if (controller.id == TS::linear_pos && key.id == TS::linear_key) {
                                ShapeAnimation::PositionKey value;
                                value.frame = data->getUint();
                                for (float &c : value.position) c = data->getFloat();
                                node.back().positions.push_back(value);
                            }
                            data->off = key.end;
                        }
                    }
                    data->off = controller.end;
                }
                ShapeAnimation::sortKeys(node.back());
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
                    node.push_back(ShapeAnimation::Channel());
                    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                        if(sh == "controllers"){
                            count = ParserX::GetNumber(data);
                            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                                if(sh == "tcb_rot"){
                                    count =  ParserX::GetNumber(data);
                                    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                                        // TCB parameters left out (as in loadC).
                                        if(sh == "tcb_key" || sh == "slerp_rot"){
                                            ShapeAnimation::RotationKey value;
                                            value.frame = ParserX::GetUInt(data);
                                            float msts[4];
                                            for(float &c : msts)
                                                c = ParserX::GetNumber(data);
                                            ShapeAnimation::fromMsts(msts, value.rotation);
                                            node.back().rotations.push_back(value);
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
                                            ShapeAnimation::PositionKey value;
                                            value.frame = ParserX::GetUInt(data);
                                            for(float &c : value.position)
                                                c = ParserX::GetNumber(data);
                                            node.back().positions.push_back(value);
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
    QFile file(ContentPath::withExtension(pathid, "sd"));
    if (!file.open(QIODevice::ReadOnly)){
        qDebug() << "Sd Shape: not exist "<<ContentPath::withExtension(pathid, "sd");
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
                // PBR material factors (task shapes 13).
                if (sh == ("esd_tsrematerial")) {
                    const QString key = MstsMaterials::key(ParserX::GetString(data));
                    QVector<float> values;
                    bool ok = true;
                    while (values.size() < MstsMaterials::Factors::Count) {
                        const float value = ParserX::GetNumberInside(data, &ok);
                        if (!ok)
                            break;
                        values.push_back(value);
                    }
                    if (!key.isEmpty())
                        pbrFactors.insert(key, MstsMaterials::Factors::read(values));
                    ParserX::SkipToken(data);
                    continue;
                }
                ParserX::SkipToken(data);
            }
            ParserX::SkipToken(data);
            continue;
        }
    }
    // An .sd without ESD_Alternative_Texture (-1 here) has no alternatives.
    texPath += TerrainSeason::shapeTextureDirectory(std::max(0, esdAlternativeTexture), Game::season);
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
    ShapeLoader::cancel(this);
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
    for(size_t i = 1; i < state.size(); ++i)
        if(state[i].released){
            state[i] = State();
            return i;
        }
    state.push_back(State());
    return state.size() - 1;
}

void SFileLegacy::releaseState(unsigned int stateId){
    if(stateId == 0 || stateId >= state.size())
        return;
    // Its packets stay in its pool (the renderer may still hold them this
    // frame) for the state that takes its number.
    state[stateId] = State();
    state[stateId].released = true;
    auto pool = animatedPackets.find(stateId);
    if (pool != animatedPackets.end())
        pool->count = -1;
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

int SFileLegacy::distanceLevelCount() const {
    return loaded == 1 && distancelevel ? iloscd : 0;
}

float SFileLegacy::distanceLevelDistance(int level) const {
    return level >= 0 && level < distanceLevelCount() ? float(distancelevel[level].levelSelection) : 0.0f;
}

void SFileLegacy::clearRenderItems(quint64 key) {
    for (RenderItem *item : renderItems[key])
        Renderer::retirePacket(item);
    renderItems[key].clear();
}

quint64 SFileLegacy::packetKey(unsigned int stateId) const {
    return (quint64(quint32(state[stateId].distanceLevel)) << 32)
            | quint32(state[stateId].enabledSubObjs);
}

void SFileLegacy::invalidateRenderState(bool invalidateMatrixCache){
    requiresUpdate = true;
    ++invalidations;
    for(auto it = renderItems.begin(); it != renderItems.end(); ++it){
        clearRenderItems(it.key());
    }

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

    if(state[stateId].animated && animations.size() > 0){
        if(animations[0].frames == 0)
            return;
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

// Mirrors the per-vertex alpha mode written in initGL(): texdiff is opaque,
// alpha test mode 1 cuts out below 0.51, other materials blend.
unsigned char SFileLegacy::gatherSurface(int primState) const {
    const primst &material = primstate[primState];
    if (material.arg2 >= 0 && material.arg2 < ishaders && shader[material.arg2].alpha == 1)
        return RenderItem::SURFACE_OPAQUE;
    if (material.arg6 == 1)
        return RenderItem::SURFACE_ALPHA_TEST;
    return RenderItem::SURFACE_BLENDED;
}

// Gather packets reference the TexLib texture; the renderer resolves it when
// drawing, so packets survive asynchronous texture loading unchanged.
void SFileLegacy::setGatherTexture(RenderItem *item, int primState, bool texEnabled) {
    const int textureIndex = primstate[primState].arg4;
    if (textureIndex == -1 || !texEnabled) {
        item->disableTextures(1.0, 0.0, 1.0, 1.0);
        return;
    }
    auto &img = image[texture[textureIndex].image];
    if (img.tex == -1)
        img.tex = TexLib::addTex(texPath, img.name);
    if (img.tex < 0) {
        item->disableTextures(1.0, 0.0, 1.0, 1.0);
        return;
    }
    item->enableTextureId(img.tex);
    // Its night texture (task 35) while the shape holds them.
    if (nightHeld) {
        if (img.night == 0) {
            img.nightFile = NightTextures::find(texPath, img.name);
            img.night = img.nightFile.isEmpty() ? -1 : 1;
        }
        if (img.night > 0) {
            if (img.nightTex < 0)
                img.nightTex = TexLib::addTex(img.nightFile);
            item->material.nightTextureId = img.nightTex;
        }
    }
}

// PBR materials (task shapes 13): PbrTexDiff or BlendAPbrTexDiff.
bool SFileLegacy::pbrPrimState(int primState) const {
    if (primState < 0 || primState >= iloscps)
        return false;
    const int s = primstate[primState].arg2;
    return s >= 0 && s < ishaders && MstsMaterials::pbr(shader[s].pbr);
}

// Each PBR primitive state's factors: the .sd's line for its base colour
// texture.
void SFileLegacy::resolvePbrFactors() {
    primFactors = QVector<MstsMaterials::Factors>(std::max(iloscps, 0));
    for (int i = 0; i < iloscps; ++i) {
        const int t = primstate[i].arg4;
        if (!pbrPrimState(i) || t < 0 || t >= ilosct || texture[t].image < 0 || texture[t].image >= ilosci)
            continue;
        primFactors[i] = pbrFactors.value(MstsMaterials::key(image[texture[t].image].name));
    }
}

// A PBR map's TexLib texture (its texels are data), -1 for none.
int SFileLegacy::dataTexture(int textureIndex) {
    if (textureIndex < 0 || textureIndex >= ilosct || texture[textureIndex].image < 0
            || texture[textureIndex].image >= ilosci)
        return -1;
    auto &img = image[texture[textureIndex].image];
    if (img.tex == -1)
        img.tex = TexLib::addTex(texPath, img.name, false, TexLib::Data);
    return img.tex;
}

// A PBR primitive's packet: its PBR mesh range and material.
void SFileLegacy::usePbr(RenderItem *item, int primState, const sub &object, const czes &part) {
    if (!object.pbrMesh.valid() || part.pbrOffset < 0)
        return;
    const primst &material = primstate[primState];
    item->mesh.handle = object.pbrMesh;
    item->mesh.first = part.pbrOffset;
    item->mesh.layout = RenderItem::PBR;
    MstsMaterials::fill(*item, primFactors.value(primState), dataTexture(material.ormTexture),
                        dataTexture(material.normalTexture), shader[material.arg2].pbr, material.arg6 == 1);
}

void SFileLegacy::releaseNightTextures() {
    for (int i = 0; image != nullptr && i < ilosci; ++i)
        if (image[i].nightTex >= 0) {
            TexLib::delRef(image[i].nightTex);
            image[i].nightTex = -1;
        }
}

void SFileLegacy::pushRenderItem(RenderQueue &queue){
    pushRenderItem(queue, 0,0);
}

void SFileLegacy::pushRenderItem(RenderQueue &queue, quint32 selectionId, unsigned int stateId){
    if (loaded == 1 && !isGLReady() && !initGL()) return;
    if (isinit != 1 || loaded == 2)
        return;
    if (loaded == 0) {
        const ShapeLoader::Request request = ShapeLoader::request(this);
        if (request == ShapeLoader::Request::Wait) {
            queue.noteShapeWaiting();
            return;
        }
        if (request == ShapeLoader::Request::LoadHere) {
            loaded = 2;
            load();
            return;
        }
        if (loaded != 1)
            return;
    }

    // Night textures (task 35): held while the frame wants them; the packets
    // pick them up or drop them.
    if (esdAlternativeTexture > 0 && (esdAlternativeTexture & NightTextures::AlternativeFlag) != 0
            && NightTextures::frame().wanted != nightHeld) {
        nightHeld = !nightHeld;
        if (!nightHeld)
            releaseNightTextures();
        requiresUpdate = true;
        ++invalidations;
    }

    if(state[stateId].enableSubObjQueue.size() > 0){
        for (auto it = state[stateId].enableSubObjQueue.begin(); it != state[stateId].enableSubObjQueue.end();){
            enableSubObjByName(stateId, it.key(), it.value());
            it = state[stateId].enableSubObjQueue.erase(it);
        }
    }

    // Parts at rest draw with the shared packets (instanced across objects);
    // a posed or looping state's moving parts with packets of its own.
    const QVector<RenderItem *> &shared = staticPackets(stateId);
    if (movesParts(stateId))
        pushPosed(queue, selectionId, stateId, shared);
    else if (SelectionIdCodec::isKind(selectionId, SelectionIdCodec::Kind::ShapePart)) {
        // Picking primitives (task shapes 13): each with its number.
        const std::vector<int> numbers = primitiveNumbers(stateId);
        for (int k = 0; k < shared.size() && k < int(numbers.size()); ++k)
            queue.submit(shared[k], SelectionIdCodec::forShapePart(selectionId, numbers[k]));
    } else if (shared.size() > 0)
        queue.submit(shared, selectionId);
}

std::vector<int> SFileLegacy::primitiveNumbers(unsigned int stateId) const {
    std::vector<int> numbers;
    const State &st = state[stateId];
    const dist &level = distancelevel[st.distanceLevel];
    int number = 0;
    for (int i = 0; i < level.iloscs; i++) {
        const int first = number;
        number += level.subobiekty[i].iloscc;
        if (((st.enabledSubObjs >> i) & 1) == 0)
            continue;
        for (int j = 0; j < level.subobiekty[i].iloscc; j++)
            numbers.push_back(first + j);
    }
    return numbers;
}

int SFileLegacy::drawnPrimitiveCount(unsigned int stateId) const {
    if (loaded != 1 || stateId >= unsigned(state.size()) || distancelevel == nullptr)
        return 0;
    const dist &level = distancelevel[state[stateId].distanceLevel];
    int count = 0;
    for (int i = 0; i < level.iloscs; i++)
        count += level.subobiekty[i].iloscc;
    return count;
}

int SFileLegacy::drawnPrimitiveMaterial(unsigned int stateId, int primitive) const {
    if (loaded != 1 || stateId >= unsigned(state.size()) || distancelevel == nullptr || primitive < 0)
        return -1;
    const dist &level = distancelevel[state[stateId].distanceLevel];
    for (int i = 0; i < level.iloscs; i++) {
        if (primitive < level.subobiekty[i].iloscc)
            return level.subobiekty[i].czesci[primitive].prim_state_idx;
        primitive -= level.subobiekty[i].iloscc;
    }
    return -1;
}

void SFileLegacy::pushPrimitiveHighlight(RenderQueue &queue, unsigned int stateId,
                                         const QVector<int> &primitives, const float *color) {
    if (primitives.isEmpty() || !isGLReady() || stateId >= unsigned(state.size()))
        return;
    const State &st = state[stateId];
    const int currentDlevel = st.distanceLevel;
    const bool posed = movesParts(stateId);
    if (posed)
        posedMatrices(stateId, currentDlevel, posedScratch);
    int number = 0;
    for (int i = 0; i < distancelevel[currentDlevel].iloscs; i++) {
        const sub &object = distancelevel[currentDlevel].subobiekty[i];
        for (int j = 0; j < object.iloscc; j++, number++) {
            if (!primitives.contains(number) || object.czesci[j].iloscv <= 0)
                continue;
            const int matrix = vtxstate[primstate[object.czesci[j].prim_state_idx].vtx_state].matrix;
            float m[16];
            const float *transform;
            if (posed) {
                transform = posedScratch[matrix].data();
            } else {
                Mat4::identity(m);
                transform = getPmatrix(currentDlevel, m, matrix);
            }
            auto *item = new RenderItem();
            item->mesh.handle = object.mesh;
            item->mesh.first = object.czesci[j].offset;
            item->mesh.count = object.czesci[j].iloscv;
            item->mesh.primitive = RenderItem::PRIMITIVE_TRIANGLES;
            item->mesh.layout = RenderItem::VNTA;
            item->msMatrix = queue.frameMatrix(transform);
            item->material.lit = false;
            item->disableTextures(color[0], color[1], color[2], 1.0f);
            item->material.opacity = color[3];
            item->material.surface = RenderItem::SURFACE_BLENDED;
            item->material.decal = true;
            item->material.doubleSided = true;
            queue.submitFrameItem(item);
        }
    }
}

const QVector<RenderItem *> &SFileLegacy::staticPackets(unsigned int stateId) {
    const quint64 key = packetKey(stateId);
    // Made already: found without operator[], which checks for an insert
    // (twice for every object of a frame: 0.75 ms at Prudnik's freight
    // station).
    if (!requiresUpdate) {
        const auto found = renderItems.constFind(key);
        if (found != renderItems.cend() && !found->isEmpty())
            return found.value();
    }
    if(renderItems[key].size() == 0 || requiresUpdate){
        // `requiresUpdate` is shape-wide (all keys).
        // If any state requested refresh, invalidate other cached keys too.
        bool globalInvalidateRequested = requiresUpdate;
        requiresUpdate = false;
        clearRenderItems(key);

        RenderItem * r;// = new RenderItem();
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

                if (!macierz[matrix].isFixed) {
                    Mat4::identity(m);
                    memcpy(macierz[matrix].fixed, getPmatrix(currentDlevel, m, matrix), sizeof (float) * 16);
                    macierz[matrix].isFixed = true;
                }
                r->msMatrix = (float*)&macierz[matrix].fixed;

                MstsMaterials::applyLight(*r, vtxstate[vtx_state].arg2);

                r->material.surface = gatherSurface(prim_state);
                r->material.doubleSided = doubleSided;
                setGatherTexture(r, prim_state, texEnabled);

                r->mesh.handle = distancelevel[currentDlevel].subobiekty[i].mesh;
                //r->mvMatrix = Mat4::clone(queue.transform());
                r->mesh.first = distancelevel[currentDlevel].subobiekty[i].czesci[j].offset;
                r->mesh.count = distancelevel[currentDlevel].subobiekty[i].czesci[j].iloscv;
                r->mesh.primitive = RenderItem::PRIMITIVE_TRIANGLES;
                r->mesh.layout = RenderItem::VNTA;
                // PBR materials (task shapes 13).
                if (pbrPrimState(prim_state))
                    usePbr(r, prim_state, distancelevel[currentDlevel].subobiekty[i],
                           distancelevel[currentDlevel].subobiekty[i].czesci[j]);
                r->shared = true;
                // Static packets keep their part matrix, so their bounds hold.
                const auto &part = distancelevel[currentDlevel].subobiekty[i].czesci[j];
                r->setBounds(part.boundCenter, part.boundRadius, r->msMatrix);
                renderItems[key].push_back(r);
            }
        }
        if(globalInvalidateRequested){
            // Keep this key's items, but force rebuild of the other keys.
            for(auto it = renderItems.begin(); it != renderItems.end(); ++it){
                if(it.key() == key)
                    continue;
                clearRenderItems(it.key());
            }
        }
    }

    return renderItems[key];
}

// A posed (or looping) state: its packets for the primitives whose parts
// move (a part posed, looping, or under one), each frame's matrices written
// into them; the shared packets for the rest. Kept while the state's packet
// key, selection, the shape's invalidations and the parts that move are as
// they were; only the matrices written again for a new pose. A state
// submitted again in the same frame falls back to frame-owned items.
void SFileLegacy::pushPosed(RenderQueue &queue, quint32 selectionId, unsigned int stateId,
                            const QVector<RenderItem *> &shared) {
    static const RenderItem blank;
    const State &st = state[stateId];
    const int currentDlevel = st.distanceLevel;
    const bool looping = st.animated && !animations.empty() && animations[0].frames > 0;
    AnimatedPackets &pool = animatedPackets[stateId];
    const quint64 frame = Renderer::frameNumber();
    const bool reusePool = !pool.used || pool.frame != frame;
    pool.frame = frame;
    pool.used = true;
    const quint64 key = packetKey(stateId);
    const bool built = reusePool && pool.key == key && pool.selectionId == selectionId
            && pool.invalidation == invalidations && pool.count >= 0;
    // Picking primitives (task shapes 13): each with its number.
    std::vector<int> numbers;
    if (SelectionIdCodec::isKind(selectionId, SelectionIdCodec::Kind::ShapePart))
        numbers = primitiveNumbers(stateId);
    auto numbered = [&](int index) {
        return index < int(numbers.size()) ? SelectionIdCodec::forShapePart(selectionId, numbers[index])
                                           : selectionId;
    };
    auto submitBuilt = [&]() {
        for (int k : pool.sharedIndices)
            if (k < shared.size())
                queue.submit(shared[k], numbered(k));
        for (int k = 0; k < pool.count; ++k)
            queue.submit(pool.items[k], 0, RenderQueue::SUBMIT_ORDERED);
    };
    if (built && !looping && pool.version == st.pose.version) {
        submitBuilt();
        return;
    }
    posedMatrices(stateId, currentDlevel, posedScratch, &posedAffected);
    if (built && posedAffected == pool.affected) {
        for (int k = 0; k < pool.count; ++k)
            pool.matrices[k] = posedScratch[pool.matrixOf[k]];
        pool.version = st.pose.version;
        submitBuilt();
        return;
    }
    if (reusePool) {
        pool.key = key;
        pool.selectionId = selectionId;
        pool.invalidation = invalidations;
        pool.version = st.pose.version;
        pool.affected = posedAffected;
        pool.matrixOf.clear();
        pool.sharedIndices.clear();
    }
    int slot = 0, index = 0;
    for (int i = 0; i < distancelevel[currentDlevel].iloscs; i++) {
        if(((st.enabledSubObjs >> i) & 1) == 0)
            continue;
        for (int j = 0; j < distancelevel[currentDlevel].subobiekty[i].iloscc; j++, index++) {
            int prim_state = distancelevel[currentDlevel].subobiekty[i].czesci[j].prim_state_idx;
            int vtx_state = primstate[prim_state].vtx_state;
            int matrix = vtxstate[vtx_state].matrix;
            bool texEnabled = distancelevel[currentDlevel].subobiekty[i].czesci[j].enabled;
            if (matrix >= 0 && matrix < int(posedAffected.size()) && !posedAffected[matrix] && index < shared.size()) {
                if (reusePool)
                    pool.sharedIndices.push_back(index);
                queue.submit(shared[index], numbered(index));
                continue;
            }
            const float *m = posedScratch[matrix].data();
            RenderItem *r;
            if(reusePool){
                if(slot == pool.items.size()){
                    pool.items.push_back(new RenderItem());
                    pool.matrices.emplace_back();
                }
                r = pool.items[slot];
                *r = blank;
                std::copy(m, m + 16, pool.matrices[slot].begin());
                r->msMatrix = pool.matrices[slot].data();
                pool.matrixOf.push_back(matrix);
                slot++;
            } else {
                r = new RenderItem();
                r->msMatrix = queue.frameMatrix(m);
            }

            MstsMaterials::applyLight(*r, vtxstate[vtx_state].arg2);

            r->setSelectionId(numbered(index));
            r->material.surface = gatherSurface(prim_state);
            r->material.doubleSided = doubleSided;
            if (selectionId == 0)
                setGatherTexture(r, prim_state, texEnabled);

            r->mesh.handle = distancelevel[currentDlevel].subobiekty[i].mesh;
            r->mesh.first = distancelevel[currentDlevel].subobiekty[i].czesci[j].offset;
            r->mesh.count = distancelevel[currentDlevel].subobiekty[i].czesci[j].iloscv;
            r->mesh.primitive = RenderItem::PRIMITIVE_TRIANGLES;
            r->mesh.layout = RenderItem::VNTA;
            // PBR materials (task shapes 13).
            if (pbrPrimState(prim_state))
                usePbr(r, prim_state, distancelevel[currentDlevel].subobiekty[i],
                       distancelevel[currentDlevel].subobiekty[i].czesci[j]);
            if(reusePool){
                r->shared = true;
                queue.submit(r, 0, RenderQueue::SUBMIT_ORDERED);
            } else {
                r->shared = false;
                queue.submitFrameItem(r);
            }
        }
    }
    if (reusePool)
        pool.count = slot;
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

bool SFileLegacy::matrixByName(const QString &name, float *out) const {
    if (!isLoaded() || iloscd < 1 || distancelevel == nullptr || distancelevel[0].hierarchia == nullptr)
        return false;
    int id = -1;
    for (int i = 0; i < iloscm; i++)
        if (macierz[i].name.compare(name, Qt::CaseInsensitive) == 0) {
            id = i;
            break;
        }
    if (id < 0)
        return false;
    // As getPmatrix(): the mirrored root, then the parents down to the
    // matrix; a malformed hierarchy stops after iloscm steps.
    QVector<int> chain;
    for (int m = id; m > 0 && m < iloscm && chain.size() < iloscm; m = distancelevel[0].hierarchia[m])
        chain.push_back(m);
    Mat4::identity(out);
    out[0] = -1;
    for (int i = chain.size() - 1; i >= 0; --i)
        Mat4::multiply(out, out, const_cast<float*>(macierz[chain[i]].param));
    return true;
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

bool SFileLegacy::movesParts(unsigned int stateId) const {
    const State &st = state[stateId];
    return st.pose.posed || (st.animated && !animations.empty() && animations[0].frames > 0);
}

// As getPmatrix() for every matrix: the mirrored root, then each part as
// posed (its own transform, its keys at its frame or the state's looping
// one, or its rest) after its parent. A malformed hierarchy stops after
// iloscm steps.
void SFileLegacy::posedMatrices(unsigned int stateId, int currentDlevel,
                                std::vector<std::array<float, 16>> &out,
                                std::vector<unsigned char> *affected) const {
    const State &st = state[stateId];
    const dist &level = distancelevel[currentDlevel];
    const ShapeAnimation::Channel *channels = animations.empty() ? nullptr : animations[0].node.data();
    const int channelCount = animations.empty() ? 0 : animations[0].node.size();
    out.resize(iloscm);
    if (affected != nullptr)
        affected->assign(iloscm, 0);
    std::vector<unsigned char> &done = posedDone;
    done.assign(iloscm, 0);
    std::vector<int> &chain = posedChain;
    for (int matrix = 0; matrix < iloscm; ++matrix) {
        chain.clear();
        for (int m = matrix; m > 0 && m < iloscm && !done[m] && int(chain.size()) < iloscm;
             m = m < level.ilosch ? level.hierarchia[m] : -1)
            chain.push_back(m);
        // Its first posed ancestor, or the root.
        const int top = chain.empty() ? matrix : chain.back();
        const int above = top > 0 && top < level.ilosch ? level.hierarchia[top] : -1;
        float base[16];
        if (above > 0 && above < iloscm && done[above]) {
            std::copy(out[above].begin(), out[above].end(), base);
        } else {
            Mat4::identity(base);
            base[0] = -1;
        }
        if (matrix == 0) {
            std::copy(base, base + 16, out[0].begin());
            done[0] = 1;
            continue;
        }
        const bool looping = st.animated && channelCount > 0 && animations[0].frames > 0;
        bool moving = affected != nullptr && above > 0 && above < iloscm && (*affected)[above];
        for (int i = int(chain.size()) - 1; i >= 0; --i) {
            const int m = chain[i];
            float local[16];
            const ShapeAnimation::Channel *channel = m < channelCount ? &channels[m] : nullptr;
            st.pose.local(m, channel, looping, st.frameCount, macierz[m].param, local);
            Mat4::multiply(base, base, local);
            std::copy(base, base + 16, out[m].begin());
            done[m] = 1;
            if (affected != nullptr) {
                moving = moving || (size_t(m) < st.pose.parts.size()
                                    && st.pose.parts[m].mode != ShapeAnimation::Pose::Part::Rest)
                        || (looping && channel != nullptr && !channel->empty());
                (*affected)[m] = moving ? 1 : 0;
            }
        }
    }
}

int SFileLegacy::partCount() const {
    return loaded == 1 ? iloscm : 0;
}

QString SFileLegacy::partName(int part) const {
    return part >= 0 && part < partCount() ? macierz[part].name : QString();
}

int SFileLegacy::partParent(int part) const {
    if (part <= 0 || part >= partCount() || distancelevel == nullptr || part >= distancelevel[0].ilosch)
        return -1;
    return distancelevel[0].hierarchia[part];
}

bool SFileLegacy::partRest(int part, float *out) const {
    if (part < 0 || part >= partCount())
        return false;
    std::copy(macierz[part].param, macierz[part].param + 16, out);
    return true;
}

float SFileLegacy::partLastFrame(int part) const {
    if (part < 0 || part >= partCount() || animations.empty() || part >= animations[0].node.size()
            || animations[0].node[part].empty())
        return -1.0f;
    return animations[0].node[part].lastFrame();
}

bool SFileLegacy::partAtFrame(int part, float frame, float *out) const {
    if (part < 0 || part >= partCount())
        return false;
    if (animations.empty() || part >= animations[0].node.size())
        std::copy(macierz[part].param, macierz[part].param + 16, out);
    else
        ShapeAnimation::pose(animations[0].node[part], frame, macierz[part].param, out);
    return true;
}

float SFileLegacy::animationFrames() const {
    return loaded == 1 && !animations.empty() ? float(animations[0].frames) : 0.0f;
}

float SFileLegacy::animationRate() const {
    return loaded == 1 && !animations.empty() ? animations[0].fps : 0.0f;
}

void SFileLegacy::setPartFrame(unsigned int stateId, int part, float frame) {
    if (stateId < unsigned(state.size()))
        state[stateId].pose.setFrame(part, frame);
}

void SFileLegacy::setPartTransform(unsigned int stateId, int part, const float *local) {
    if (stateId < unsigned(state.size()))
        state[stateId].pose.setTransform(part, local);
}

bool SFileLegacy::partMatrix(unsigned int stateId, int part, float *out) const {
    if (stateId >= unsigned(state.size()) || part < 0 || part >= partCount() || iloscd <= 0)
        return false;
    std::vector<std::array<float, 16>> matrices;
    posedMatrices(stateId, 0, matrices);
    std::copy(matrices[part].begin(), matrices[part].end(), out);
    return true;
}

void SFileLegacy::clearPose(unsigned int stateId) {
    if (stateId < unsigned(state.size()))
        state[stateId].pose.clear();
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
        shape->shader[i].pbr = MstsMaterials::shader(shape->shader[i].name);
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
                if (j == 1) shape->primstate[i].ormTexture = index;
                if (j == 2) shape->primstate[i].normalTexture = index;
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
        pliks->shader[i].pbr = MstsMaterials::shader(pliks->shader[i].name);
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
            const int index = ParserX::GetNumber(bufor);
            if (j == 0) pliks->primstate[i].arg4 = index;
            if (j == 1) pliks->primstate[i].ormTexture = index;
            if (j == 2) pliks->primstate[i].normalTexture = index;
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
    return glReady;
}

// Sphere around a part's uploaded positions (9 floats per vertex).
static void partBounds(SFileLegacy::czes &part, const float *vertices) {
    if (part.iloscv <= 0) {
        part.boundRadius = -1.0f;
        return;
    }
    float low[3] = {vertices[0], vertices[1], vertices[2]};
    float high[3] = {vertices[0], vertices[1], vertices[2]};
    for (int v = 1; v < part.iloscv; ++v)
        for (int i = 0; i < 3; ++i) {
            low[i] = std::min(low[i], vertices[v * 9 + i]);
            high[i] = std::max(high[i], vertices[v * 9 + i]);
        }
    float radius = 0.0f;
    for (int i = 0; i < 3; ++i)
        part.boundCenter[i] = 0.5f * (low[i] + high[i]);
    for (int v = 0; v < part.iloscv; ++v) {
        float distance = 0.0f;
        for (int i = 0; i < 3; ++i) {
            const float d = vertices[v * 9 + i] - part.boundCenter[i];
            distance += d * d;
        }
        radius = std::max(radius, distance);
    }
    part.boundRadius = std::sqrt(radius);
}

// The sub-object's PBR parts in the PBR layout (task shapes 13): position,
// normal, texture coordinates, alpha, tangent with handedness (from the
// texture coordinates of their triangles, per source vertex), the texture
// coordinates again and a white colour; in the VNTA mesh's vertex order.
void SFileLegacy::buildPbrMesh(sub &object) {
    std::vector<int> triangles;
    int count = 0;
    for (int k = 0; k < object.iloscc; ++k) {
        czes &part = object.czesci[k];
        part.pbrOffset = -1;
        if (!pbrPrimState(part.prim_state_idx) || part.iloscv <= 0 || part.idx == nullptr)
            continue;
        triangles.insert(triangles.end(), part.idx, part.idx + part.iloscv);
        count += part.iloscv;
    }
    if (count == 0) {
        Meshes::release(object.pbrMesh);
        return;
    }
    const auto &vert = object.vertices;
    auto source = [&](int v, float *position, float *normal, float *uv) {
        if (v < 0 || v >= int(vert.size()))
            return false;
        const int p = vert[v].point, n = vert[v].normal, t = vert[v].uvpoint;
        if (p < 0 || p >= tpoints.ipoints || n < 0 || n >= tpoints.inormals || t < 0 || t >= tpoints.iuv_points)
            return false;
        position[0] = tpoints.points[p].x;
        position[1] = tpoints.points[p].y;
        position[2] = tpoints.points[p].z;
        normal[0] = tpoints.normals[n].x;
        normal[1] = tpoints.normals[n].y;
        normal[2] = tpoints.normals[n].z;
        uv[0] = tpoints.uv_points[t].x;
        uv[1] = tpoints.uv_points[t].y;
        return true;
    };
    std::vector<float> tangents;
    MstsMaterials::tangents(int(vert.size()), source, triangles, tangents);
    const int stride = RenderItem::PBR;
    MeshData data;
    data.layout = RenderItem::PBR;
    data.vertices.assign(size_t(count) * stride, 0.0f);
    int offset = 0;
    for (int k = 0; k < object.iloscc; ++k) {
        czes &part = object.czesci[k];
        if (!pbrPrimState(part.prim_state_idx) || part.iloscv <= 0 || part.idx == nullptr)
            continue;
        part.pbrOffset = offset;
        for (int i = 0; i < part.iloscv; ++i) {
            float *vertex = &data.vertices[size_t(offset + i) * stride];
            const int v = part.idx[i];
            if (!source(v, vertex, vertex + 3, vertex + 6))
                continue;
            vertex[8] = 1.0f;
            std::copy_n(&tangents[size_t(v) * 4], 4, vertex + 9);
            vertex[13] = vertex[6];
            vertex[14] = vertex[7];
            std::fill(vertex + 15, vertex + 19, 1.0f);
        }
        offset += part.iloscv;
    }
    Meshes::update(object.pbrMesh, std::move(data));
}

bool SFileLegacy::initGL() {
    // Builds renderer-owned meshes; no GL context is needed. The indexed
    // source is released afterwards, so only reload() rebuilds them.
    if (glReady) return true;
    if (loaded != 1) return false;
    bool uploaded = false;
    const auto restore = qScopeGuard([&] {
        if (!uploaded) {
            releaseMeshes();
            glReady = false;
        }
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

                MeshData meshData;
                meshData.layout = RenderItem::VNTA;
                meshData.vertices.resize(size_t(iloscv) * 9);

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
                    std::copy_n(wierzcholki, size_t(part.iloscv) * 9,
                                meshData.vertices.begin() + size_t(offset) * 9);
                    partBounds(object.czesci[jj], wierzcholki);

                    pliks->distancelevel[j].subobiekty[ii].czesci[jj].offset = offset;
                    offset += pliks->distancelevel[j].subobiekty[ii].czesci[jj].iloscv;

                }
                Meshes::update(object.mesh, std::move(meshData));
                buildPbrMesh(object);
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
    glReady = true;
    uploaded = true;
    return true;
}

void SFileLegacy::clearAnimatedPackets() {
    for (auto it = animatedPackets.begin(); it != animatedPackets.end(); ++it)
        for (RenderItem *item : it.value().items)
            Renderer::retirePacket(item);
    animatedPackets.clear();
}

void SFileLegacy::releaseMeshes() {
    for (int j = 0; distancelevel && j < iloscd; ++j)
        for (int i = 0; distancelevel[j].subobiekty && i < distancelevel[j].iloscs; ++i)
        {
            Meshes::release(distancelevel[j].subobiekty[i].mesh);
            Meshes::release(distancelevel[j].subobiekty[i].pbrMesh);
        }
}

void SFileLegacy::clearData() {
    invalidateRenderState();
    clearAnimatedPackets();
    releaseNightTextures();
    nightHeld = false;
    releaseMeshes();
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
    primFactors.clear();
    delete[] shader; shader = nullptr; ishaders = 0;
    animations.clear();
    glReady = false;
    texloaded = 0;
}
