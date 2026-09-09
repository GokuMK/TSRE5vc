/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "SFileC.h"
#include <tsre/shape/SFile.h>
#include <QDebug>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <tsre/ogl/GLUU.h>
#include <tsre/fileFunctions/TS.h>
#include <tsre/fileFunctions/SimisReader.h>
#include <vector>


void SFileC::odczytajshaders(FileBuffer* data, SFile* shape) {
    data->skipLabel();
    shape->ishaders = Simis::count(data);
    shape->shader = new SFile::fshader[shape->ishaders];
    for (int i = 0; i < shape->ishaders; ++i) {
        Simis::Block item(data, TS::named_shader);
        shape->shader[i].name = data->readString().toLower();
        shape->shader[i].alpha = shape->shader[i].name == "texdiff" ? 1 : 0;
    }
}

void SFileC::odczytajpunktyc(FileBuffer* data, SFile* shape) {
    data->skipLabel();
    shape->tpoints.ipoints = Simis::count(data);
    shape->tpoints.points = new SFile::fpoint[shape->tpoints.ipoints + 1];
    for (int i = 0; i < shape->tpoints.ipoints; ++i) {
        Simis::Block item(data, TS::point);
        shape->tpoints.points[i].x = data->getFloat();
        shape->tpoints.points[i].y = data->getFloat();
        shape->tpoints.points[i].z = data->getFloat();
    }
}

void SFileC::odczytajuvpunktyc(FileBuffer* data, SFile* shape) {
    data->skipLabel();
    shape->tpoints.iuv_points = Simis::count(data);
    shape->tpoints.uv_points = new SFile::fpoint[shape->tpoints.iuv_points + 1];
    for (int i = 0; i < shape->tpoints.iuv_points; ++i) {
        Simis::Block item(data, TS::uv_point);
        shape->tpoints.uv_points[i].x = data->getFloat();
        shape->tpoints.uv_points[i].y = data->getFloat();
    }
}

void SFileC::odczytajnormalnec(FileBuffer* data, SFile* shape) {
    data->skipLabel();
    shape->tpoints.inormals = Simis::count(data);
    shape->tpoints.normals = new SFile::fpoint[shape->tpoints.inormals + 1];
    for (int i = 0; i < shape->tpoints.inormals; ++i) {
        Simis::Block item(data, TS::vector);
        shape->tpoints.normals[i].x = data->getFloat();
        shape->tpoints.normals[i].y = data->getFloat();
        shape->tpoints.normals[i].z = data->getFloat();
    }
}

void SFileC::odczytajmatricesc(FileBuffer* data, SFile* shape) {
    data->skipLabel();
    shape->iloscm = Simis::count(data);
    shape->macierz = new SFile::matrt[shape->iloscm + 1];
    for (int i = 0; i < shape->iloscm; ++i) {
        Simis::Block item(data, TS::matrix);
        shape->macierz[i].name = item.label();
        for (int j = 0; j < 16; ++j)
            shape->macierz[i].param[j] = j == 15 ? 1
                    : (j == 3 || j == 7 || j == 11) ? 0 : data->getFloat();
    }
}

void SFileC::odczytajimagesc(FileBuffer* data, SFile* shape) {
    data->skipLabel();
    shape->ilosci = Simis::count(data);
    shape->image = new SFile::imgs[shape->ilosci + 1];
    for (int i = 0; i < shape->ilosci; ++i) {
        Simis::Block item(data, TS::image);
        shape->image[i].name = data->readString();
        shape->image[i].tex = -1;
    }
}

void SFileC::odczytajtexturesc(FileBuffer* data, SFile* shape) {
    data->skipLabel();
    shape->ilosct = Simis::count(data);
    shape->texture = new SFile::text[shape->ilosct];
    for (int i = 0; i < shape->ilosct; ++i) {
        Simis::Block item(data, TS::texture);
        shape->texture[i].image = data->getInt();
        shape->texture[i].arg1 = data->getInt();
        shape->texture[i].arg2 = data->getInt();
        shape->texture[i].arg3 = data->getInt();
    }
}

void SFileC::odczytajvtx_statesc(FileBuffer* data, SFile* shape) {
    data->skipLabel();
    shape->iloscv = Simis::count(data);
    shape->vtxstate = new SFile::vtxs[shape->iloscv];
    for (int i = 0; i < shape->iloscv; ++i) {
        Simis::Block item(data, TS::vtx_state);
        shape->vtxstate[i].arg1 = data->getInt();
        shape->vtxstate[i].matrix = data->getInt();
        shape->vtxstate[i].arg2 = data->getInt();
        shape->vtxstate[i].arg3 = data->getInt();
        shape->vtxstate[i].arg4 = data->getInt();
    }
}

void SFileC::odczytajprim_statesc(FileBuffer* data, SFile* shape) {
    data->skipLabel();
    shape->iloscps = Simis::count(data);
    shape->primstate = new SFile::primst[shape->iloscps];
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

void SFileC::odczytajloddc(FileBuffer* bufor, SFile* pliks) {
    if (!QOpenGLContext::currentContext())
        throw FileBuffer::ParseError("Shape mesh upload requires an OpenGL context");
    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    GLUU* gluu = GLUU::get();
    int w, n, p, txt;
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
    pliks->distancelevel = new SFile::dist[pliks->iloscd];

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
        pliks->distancelevel[j].subobiekty = new SFile::sub[pliks->distancelevel[j].iloscs + 1];
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
                pliks->distancelevel[j].subobiekty[ii].czesci = new SFile::czes[count + 1];
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
                /////////////////////////
                int iloscv = 0;
                int offset = 0;
                for (int jj = 0; jj < pliks->distancelevel[j].subobiekty[ii].iloscc; jj++) {
                    iloscv += pliks->distancelevel[j].subobiekty[ii].czesci[jj].iloscv;
                }

                pliks->distancelevel[j].subobiekty[ii].VAO.create();
                QOpenGLVertexArrayObject::Binder vaoBinder(&pliks->distancelevel[j].subobiekty[ii].VAO);
                        
                pliks->distancelevel[j].subobiekty[ii].VBO.create();
                pliks->distancelevel[j].subobiekty[ii].VBO.bind();
                pliks->distancelevel[j].subobiekty[ii].VBO.allocate(iloscv * 9 * sizeof(GLfloat));
                f->glEnableVertexAttribArray(0);
                f->glEnableVertexAttribArray(1);
                f->glEnableVertexAttribArray(2);
                f->glEnableVertexAttribArray(3);
                f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), 0);
                f->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), reinterpret_cast<void *>(3 * sizeof(GLfloat)));
                f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), reinterpret_cast<void *>(6 * sizeof(GLfloat)));
                f->glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), reinterpret_cast<void *>(8 * sizeof(GLfloat)));

                for (int jj = 0; jj < pliks->distancelevel[j].subobiekty[ii].iloscc; jj++) {
                    float *wierzcholki = new float[pliks->distancelevel[j].subobiekty[ii].czesci[jj].iloscv*9];

                    for (int iii = pliks->distancelevel[j].subobiekty[ii].czesci[jj].iloscv - 1; iii >= 0; iii--) {
                            //pliks->distancelevel[j].subobiekty[ii].czesci[czilosc].wierzcholki[iii] = new SFile::wie();

                        w = pliks->distancelevel[j].subobiekty[ii].czesci[jj].idx[iii];
                        int prim_state = pliks->distancelevel[j].subobiekty[ii].czesci[jj].prim_state_idx;
                        if (w < 0 || w >= int(vert.size()) || prim_state < 0 || prim_state >= pliks->iloscps)
                            throw FileBuffer::ParseError("Invalid shape vertex or primitive-state index");
                        float alpha = 0;
                        float alphaTest = 0;
                        if(pliks->primstate[prim_state].arg2 >= 0 && pliks->primstate[prim_state].arg2 < pliks->ishaders)
                            alpha = pliks->shader[pliks->primstate[prim_state].arg2].alpha;
                        else 
                            alpha = 0;
                        if(pliks->primstate[prim_state].arg6 == 1)
                            alphaTest = -0.51f;
                        else 
                            alphaTest = -gluu->alphaTest;
                        if(alpha == 1)
                            alphaTest = 1.0;
                            //System.out.println("----v "+w);
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
                    delete[] wierzcholki;
                    pliks->distancelevel[j].subobiekty[ii].czesci[jj].offset = offset;
                    offset += pliks->distancelevel[j].subobiekty[ii].czesci[jj].iloscv;
                    delete[] pliks->distancelevel[j].subobiekty[ii].czesci[jj].idx;
                }
                pliks->distancelevel[j].subobiekty[ii].VBO.release();
            }
        }
        delete[] pliks->tpoints.normals;
        delete[] pliks->tpoints.points;
        delete[] pliks->tpoints.uv_points;
        return;
    }
