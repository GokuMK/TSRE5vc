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
#include <tsre/shape/ShapeLib.h>
#include <tsre/Game.h>
#include <QDebug>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/shape/SFile.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/shape/SFileLegacy.h>

//int ShapeLib::jestshape;
//std::unordered_map<int, SFile*> ShapeLib::shape;

ShapeLib::ShapeLib() {
    mstsBackend = qEnvironmentVariable("TSRE_MSTS_SHAPE_BACKEND");
    firstMstsLodOnly = qEnvironmentVariableIntValue("TSRE_MSTS_FIRST_LOD_ONLY") == 1;
}

ShapeLib::ShapeLib(const ShapeLib& orig) {
}

ShapeLib::~ShapeLib() {
}

void ShapeLib::reset() {
    jestshape = 0;
    shape.clear();
    contexts.clear();
    pathKeys.clear();
}
        
void ShapeLib::delRef(int texx) {
    /*if(!mtex.containsKey(texx)) return;
    mtex.get(texx).ref--;
    if(mtex.get(texx).ref<=0){

        if(mtex.get(texx).glLoaded){
            mtex.get(texx).delVBO(gl);
            mtex.remove(texx);
        }
    }*/
}
        
void ShapeLib::addRef(int texx) {
    //if(!mtex.containsKey(texx)) return;
    //mtex.get(texx).ref++;
}

int ShapeLib::addShape(QString path){
    return addShape(path, Game::root+"/ROUTES/"+Game::route+"/TEXTURES");
}       

int ShapeLib::addShape(QString path, QString texPath) {
    QString pathid = path;//(path + "/" + name);
    pathid.replace("\\", "/");
    pathid = ContentPath::normalize(pathid);
    texPath = ContentPath::normalize(texPath);
    const QString context = ContentPath::key(texPath) + "\n" + Game::season;
    const QString pathKey = ContentPath::key(pathid);
    for (const auto &entry : shape) {
        if(entry.second && !entry.second->hasLoadFailed() && pathKeys[entry.first] == pathKey
                && contexts[entry.first] == context)
            return entry.first;
    }
    qDebug() << "Nowy " << jestshape << " shape: " << pathid;

    QString ext = pathid.toLower().split(".").last();
    ComplexShape *asset = nullptr;
    if(ext == "gltf" || ext == "glb"){
        asset = new GltfShape(pathid, path.split("/").last(), texPath);
    } else if(ext != "s"){
        qDebug() << "ShapeLib: unknown extension, treating as MSTS shape:" << ext << "path:" << pathid;
    }
    if(asset == nullptr) {
        if(mstsBackend == "complex" || mstsBackend == "complex-compact")
            asset = new SFileComplex(pathid, path.split("/").last(), texPath);
        else if(mstsBackend == "old")
            asset = new SFile(pathid, path.split("/").last(), texPath);
        else
            asset = new SFileLegacy(pathid, path.split("/").last(), texPath);
    }
    asset->setLoadOptions({
        {QString::fromLatin1(ShapeLoadOption::FirstLodOnly), firstMstsLodOnly},
        {QString::fromLatin1(ShapeLoadOption::Compact), mstsBackend == "complex-compact"}
    });
    shape[jestshape] = asset;
    contexts[jestshape] = context;
    pathKeys[jestshape] = pathKey;

    return jestshape++;
}

void ShapeLib::invalidateRendererCaches(bool invalidateMatrixCache){
    for (auto it = shape.begin(); it != shape.end(); ++it){
        if(it->second == NULL)
            continue;
        it->second->invalidateRenderState(invalidateMatrixCache);
    }
}
