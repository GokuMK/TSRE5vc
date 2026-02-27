/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/ShapeLib.h>
#include <tsre/Game.h>
#include <QDebug>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/SFile.h>

//int ShapeLib::jestshape;
//std::unordered_map<int, SFile*> ShapeLib::shape;

ShapeLib::ShapeLib() {
}

ShapeLib::ShapeLib(const ShapeLib& orig) {
}

ShapeLib::~ShapeLib() {
}

void ShapeLib::reset() {
    jestshape = 0;
    shape.clear();
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
    return addShape(path, Game::root+"/routes/"+Game::route+"/textures");
}       

int ShapeLib::addShape(QString path, QString texPath) {
    QString pathid = path;//(path + "/" + name).toLower();
    if(Game::caseInsensitiveFS)
        pathid = pathid.toLower(); 
    pathid.replace("\\", "/");
    pathid.replace("//", "/");
    //console.log(pathid);
    for ( auto it = shape.begin(); it != shape.end(); ++it ){
        if(it->second == NULL) continue;
        const QString& existingPath = it->second->getPathId();
        if (existingPath.length() == pathid.length())
            if (existingPath == pathid)
                return (int)it->first;
    }
    qDebug() << "Nowy " << jestshape << " shape: " << pathid;

    QString ext = pathid.toLower().split(".").last();
    if(ext == "gltf" || ext == "glb"){
        qDebug() << "ShapeLib: glTF/GLB not implemented yet:" << pathid;
        return -1;
    }
    if(ext != "s"){
        qDebug() << "ShapeLib: unknown extension, treating as MSTS shape:" << ext << "path:" << pathid;
    }
    shape[jestshape] = new SFile(pathid, path.split("/").last(), texPath);

    return jestshape++;
}

void ShapeLib::invalidateRendererCaches(bool invalidateMatrixCache){
    for (auto it = shape.begin(); it != shape.end(); ++it){
        if(it->second == NULL)
            continue;
        it->second->invalidateRenderState(invalidateMatrixCache);
    }
}
