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
#include <tsre/shape/SFileComplex.h>
#include <tsre/shape/SFileLegacy.h>

//int ShapeLib::jestshape;

ShapeLib::ShapeLib(MstsBackend backend) {
    if (backend == MstsBackend::ConfiguredDefault) {
        mstsBackend = qEnvironmentVariable("TSRE_MSTS_SHAPE_BACKEND");
        firstMstsLodOnly = qEnvironmentVariableIntValue("TSRE_MSTS_FIRST_LOD_ONLY") == 1;
        return;
    }
    switch (backend) {
    case MstsBackend::Legacy: mstsBackend = "legacy"; break;
    case MstsBackend::Complex: mstsBackend = "complex"; break;
    case MstsBackend::ComplexCompact: mstsBackend = "complex-compact"; break;
    case MstsBackend::ConfiguredDefault: break;
    }
}

ShapeLib::ShapeLib(const ShapeLib& orig)
    : mstsBackend(orig.mstsBackend), firstMstsLodOnly(orig.firstMstsLodOnly) {}

ShapeLib::~ShapeLib() {
}

void ShapeLib::reset() {
    jestshape = 0;
    shape.clear();
    contexts.clear();
    pathKeys.clear();
    byKey.clear();
    keys.clear();
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

const QString &ShapeLib::cachedKey(const QString &path) {
    auto found = keys.find(path);
    if (found == keys.end())
        found = keys.insert(path, ContentPath::key(path));
    return found.value();
}

int ShapeLib::addShape(QString path){
    return addShape(path, Game::root+"/ROUTES/"+Game::route+"/TEXTURES");
}       

int ShapeLib::addShape(QString path, QString texPath) {
    return addShape(path, texPath, ShapeLoadOptions());
}

int ShapeLib::addShape(QString path, QString texPath, const ShapeLoadOptions &options) {
    QString pathid = path;//(path + "/" + name);
    pathid.replace("\\", "/");
    pathid = ContentPath::normalize(pathid);
    texPath = ContentPath::normalize(texPath);
    QString context = cachedKey(texPath) + "\n" + Game::season;
    for (auto it = options.begin(); it != options.end(); ++it)
        context += "\n" + it.key() + "=" + it.value().toString();
    const QString pathKey = cachedKey(pathid);
    // One that failed to load is made again.
    const QString lookup = pathKey + QLatin1Char('\n') + context;
    const auto known = byKey.constFind(lookup);
    if (known != byKey.cend()) {
        const auto found = shape.find(known.value());
        if (found != shape.end() && found->second && !found->second->hasLoadFailed())
            return found->first;
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
        else
            asset = new SFileLegacy(pathid, path.split("/").last(), texPath);
    }
    ShapeLoadOptions loadOptions = options;
    loadOptions.insert(QString::fromLatin1(ShapeLoadOption::FirstLodOnly), firstMstsLodOnly);
    loadOptions.insert(QString::fromLatin1(ShapeLoadOption::Compact), mstsBackend == "complex-compact");
    asset->setLoadOptions(loadOptions);
    shape[jestshape] = asset;
    contexts[jestshape] = context;
    pathKeys[jestshape] = pathKey;
    byKey.insert(lookup, jestshape);

    return jestshape++;
}

