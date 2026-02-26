/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/texture/TexLib.h>
#include <tsre/texture/AceLib.h>
#include <tsre/texture/DdsLib.h>
#include <tsre/texture/ImageLib.h>
#include <tsre/texture/PaintTexLib.h>
#include <tsre/texture/MapLib.h>
#include <tsre/texture/Texture.h>
#include <QDebug>
#include <QFile>
#include <tsre/Game.h>

int TexLib::jesttextur = 0;
std::unordered_map<int, Texture*> TexLib::mtex;
QHash<int, int> TexLib::disabledTextures;

void TexLib::reset() {
    jesttextur = 0;
    mtex.clear();
}

void TexLib::dumpStats(QString label) {
    qint64 cpuPixelsBytes = 0;
    qint64 cpuEncodedBytes = 0;
    qint64 estimatedVramBytes = 0;
    int totalTextures = 0;
    int loadedTextures = 0;
    int glLoadedTextures = 0;
    int glCompressedTextures = 0;
    int editableTextures = 0;
    int missingTextures = 0;
    int errorTextures = 0;

    for (auto it = mtex.begin(); it != mtex.end(); ++it) {
        Texture* t = it->second;
        if (t == nullptr) {
            continue;
        }

        totalTextures++;
        if (t->loaded) loadedTextures++;
        if (t->glLoaded) glLoadedTextures++;
        if (t->editable) editableTextures++;
        if (t->missing) missingTextures++;
        if (t->error) errorTextures++;

        if (t->imageData != nullptr) {
            if (t->imageSize > 0) {
                cpuPixelsBytes += t->imageSize;
            } else if (t->width > 0 && t->height > 0 && t->bytesPerPixel > 0) {
                cpuPixelsBytes += qint64(t->width) * qint64(t->height) * qint64(t->bytesPerPixel);
            }
        }

        cpuEncodedBytes += t->compressedData.size();

        if (t->glLoaded) {
            estimatedVramBytes += t->estimatedVramBytes();
            if (t->gpuIsCompressed()) {
                glCompressedTextures++;
            }
        }
    }

    const double cpuPixelsMB = double(cpuPixelsBytes) / (1024.0 * 1024.0);
    const double cpuEncodedMB = double(cpuEncodedBytes) / (1024.0 * 1024.0);
    const double vramMB = double(estimatedVramBytes) / (1024.0 * 1024.0);

    if (!label.isEmpty()) {
        qDebug() << "TexLib stats (" << label << "):"
                 << "total:" << totalTextures
                 << "loaded:" << loadedTextures
                 << "gl:" << glLoadedTextures
                 << "glCompressed:" << glCompressedTextures
                 << "editable:" << editableTextures
                 << "missing:" << missingTextures
                 << "error:" << errorTextures
                 << "cpuPixelsMB:" << cpuPixelsMB
                 << "cpuEncodedMB:" << cpuEncodedMB
                 << "vramMB(est):" << vramMB;
    } else {
        qDebug() << "TexLib stats:"
                 << "total:" << totalTextures
                 << "loaded:" << loadedTextures
                 << "gl:" << glLoadedTextures
                 << "glCompressed:" << glCompressedTextures
                 << "editable:" << editableTextures
                 << "missing:" << missingTextures
                 << "error:" << errorTextures
                 << "cpuPixelsMB:" << cpuPixelsMB
                 << "cpuEncodedMB:" << cpuEncodedMB
                 << "vramMB(est):" << vramMB;
    }
}

void TexLib::enableTexture(int id){
    Texture* tex = mtex[id];
    if(tex != NULL)
        disabledTextures[tex->tex[0]] = 0;
}

void TexLib::disableTexture(int id){
    Texture* tex = mtex[id];
    if(tex != NULL)
        disabledTextures[tex->tex[0]] = 1;
}

void TexLib::delRef(int texx) {
    try {
        Texture* t = mtex.at(texx);
        t->ref--;
        if (t->ref <= 0) {
            //System.out.println("--refs: "+mtex.get(texx).ref);
            if (t->glLoaded) {
                t->delVBO();
                mtex.erase(texx);
            }
        }
    } catch (const std::out_of_range& oor) {
            
    }
}

void TexLib::addRef(int texx) {
    try {
        Texture* t = mtex.at(texx);
        t->ref++;
    } catch (const std::out_of_range& oor) {
            
    }    
}

int TexLib::addTex(QString path, QString name, bool reload) {
    QString pathid = (path+"/"+name);
    if(Game::caseInsensitiveFS)
    pathid = pathid.toLower();
    pathid.replace("\\", "/");
    pathid.replace("//", "/");
    return addTex(pathid, reload);
}

int TexLib::getTex(QString pathid) {
    for ( auto it = mtex.begin(); it != mtex.end(); ++it ){
        if(it->second == NULL) continue;
        for(int i = 0; i < ((Texture*) it->second)->hashid.size(); i++)
            if (((Texture*) it->second)->hashid[i].length() == pathid.length()) 
                if (((Texture*) it->second)->hashid[i] == pathid) {
                    ((Texture*) it->second)->ref++;
                    return (int)it->first;
                }
    }
    return -1;
}

int TexLib::addTex(Texture* texture, bool reload) {
    if (texture == nullptr) {
        return -1;
    }
    if (texture->hashid.isEmpty() && !texture->pathid.isEmpty()) {
        texture->hashid.push_back(texture->pathid);
    }

    for ( auto it = mtex.begin(); it != mtex.end(); ++it ){
        Texture* existing = it->second;
        if(existing == nullptr) continue;

        for(int i = 0; i < existing->hashid.size(); i++){
            for(int j = 0; j < texture->hashid.size(); j++){
                if(existing->hashid[i].length() != texture->hashid[j].length())
                    continue;
                if(existing->hashid[i] != texture->hashid[j])
                    continue;

                if(!reload){
                    existing->ref++;
                    if(texture->imageData != nullptr){
                        delete[] texture->imageData;
                        texture->imageData = nullptr;
                    }
                    if(texture->tex != nullptr){
                        delete[] texture->tex;
                        texture->tex = nullptr;
                    }
                    delete texture;
                    return (int)it->first;
                }

                existing->delVBO();
                if(existing->imageData != nullptr){
                    delete[] existing->imageData;
                    existing->imageData = nullptr;
                }
                existing->compressedData.clear();
                existing->compressedGLFormat = 0;

                existing->bpp = texture->bpp;
                existing->imageSize = texture->imageSize;
                existing->bytesPerPixel = texture->bytesPerPixel;
                existing->compressed = texture->compressed;
                existing->width = texture->width;
                existing->height = texture->height;
                existing->type = texture->type;
                existing->typk = texture->typk;

                existing->compressedData = texture->compressedData;
                existing->compressedGLFormat = texture->compressedGLFormat;

                existing->imageData = texture->imageData;
                texture->imageData = nullptr;

                existing->loaded = texture->loaded;
                existing->editable = texture->editable;
                existing->missing = texture->missing;
                existing->error = texture->error;

                // Merge identities (keep existing stable ids, add new ones).
                for(int k = 0; k < texture->hashid.size(); k++){
                    bool found = false;
                    for(int u = 0; u < existing->hashid.size(); u++){
                        if(existing->hashid[u].length() == texture->hashid[k].length())
                            if(existing->hashid[u] == texture->hashid[k]){
                                found = true;
                                break;
                            }
                    }
                    if(!found)
                        existing->hashid.push_back(texture->hashid[k]);
                }

                if(texture->tex != nullptr){
                    delete[] texture->tex;
                    texture->tex = nullptr;
                }
                delete texture;

                existing->ref++;
                return (int)it->first;
            }
        }
    }

    texture->ref++;
    mtex[jesttextur] = texture;
    return jesttextur++;
}

int TexLib::addTex(QString pathid, bool reload) {
    
    Texture* newFile = NULL;
    for ( auto it = mtex.begin(); it != mtex.end(); ++it ){
        if(it->second == NULL) continue;
        for(int i = 0; i < ((Texture*) it->second)->hashid.size(); i++)
            if (((Texture*) it->second)->hashid[i].length() == pathid.length()) 
                if (((Texture*) it->second)->hashid[i] == pathid) {
                    if(!reload){
                        ((Texture*) it->second)->ref++;
                        return (int)it->first;
                    } else {
                        newFile = ((Texture*) it->second);
                        break;
                    }
                }
    }
    //qDebug() << "Nowa " << jesttextur << " textura: " << pathid;
    
    QString tType = pathid.toLower().split(".").last();
    
    // Openrails uses .dds textures instead of .ace
    if(tType == "ace"){
        QFile file(pathid);
        if (!file.exists()){
            tType = "dds";
            pathid = pathid.left(pathid.length() - 3)+"dds";
        }
        //qDebug() << "Using DDS";
    }
    
    int texId = 0;
    if(newFile == NULL){
        newFile = new Texture(pathid);
        newFile->ref++;
        mtex[jesttextur] = newFile;
        texId = jesttextur;
        jesttextur++;
    } else {
        newFile->delVBO();
    }
    //qDebug() << pathid.toLower();
    //qDebug() << tType;
        
    if(tType == "ace"){
        AceLib* t = new AceLib();
        t->texture = newFile;
        if(AceLib::IsThread && !reload)
            t->start();
        else
            t->run();
    } else if(tType == "dds"){
        DdsLib* t = new DdsLib();
        t->texture = newFile;
        if(DdsLib::IsThread && !reload)
            t->start();
        else
            t->run();
    } else if(tType == "png"||tType == "bmp"||tType == "jpg"/*||tType == "dds"*/||tType == "tga"){
        ImageLib* t = new ImageLib();
        t->texture = newFile;
        if(ImageLib::IsThread && !reload)
            t->start();
        else
            t->run();
    } else if(tType == ":painttex"){
        PaintTexLib* t = new PaintTexLib();
        t->texture = newFile;
        //t->start();
        t->run();
    } else if(tType == ":maptex"){
        MapLib* t = new MapLib();
        t->texture = newFile;
        t->start();
    }
    //AceLib::LoadACE(newFile);
    //tConcurrent::run();
    return texId;
}

int TexLib::cloneTex(int id) {
    Texture* t = mtex[id];
    if(t == NULL) {
        qDebug() << "null texture " << id;
        return -2;
    }
    Texture* newFile = new Texture(t);
    newFile->ref++;
    mtex[jesttextur] = newFile;
 
    return jesttextur++;
}

void TexLib::save(QString type, QString path, int id){
    Texture* t = mtex.at(id);
    if(t == NULL) 
        return;
    if(!t->editable)
        t->setEditable();
    AceLib::save(path, t);
}
