/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <shapeViewer/ContentHierarchyInfo.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Eng.h>
#include <tsre/shape/SFile.h>

ContentHierarchyInfo::ContentHierarchyInfo() {
}

ContentHierarchyInfo::ContentHierarchyInfo(const ContentHierarchyInfo& orig) {
    name = orig.name;
    parent = orig.parent;
    distanceLevelId = orig.distanceLevelId;
    type = orig.type;
    con = orig.con;
    eng = orig.eng;
    sfile = orig.sfile;
}

ContentHierarchyInfo::~ContentHierarchyInfo() {
}

