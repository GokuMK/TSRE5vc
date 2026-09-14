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
#include <tsre/procedural/ShapeTemplates.h>
#include <QString>
#include <QFile>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>

ShapeTemplates::ShapeTemplates(const QString &sourcePath) {
    QString path = sourcePath;
    QString sh;
    path = ContentPath::normalize(path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << path << " shape definition file not exist";
        return;
    }

    FileBuffer* data = ReadFile::read(&file);
    file.close();
    data->toUtf16();
    data->skipBOM();

    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        if (sh == ("templates")) {
            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                if (sh == ("template")) {
                    QString name = ParserX::GetString(data);
                    templates[name] = new ShapeTemplate();
                    templates[name]->name = name;
                    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                        if (sh == ("type")) {
                            const QString type =
                                    ParserX::GetString(data).trimmed().toLower();
                            if(type == "track")
                                templates[name]->type = ShapeTemplate::TRACK;
                            else if(type == "road")
                                templates[name]->type = ShapeTemplate::ROAD;
                            else if(type == "ruler")
                                templates[name]->type = ShapeTemplate::RULER;
                            else if(type == "default")
                                templates[name]->type = ShapeTemplate::DEFAULT;
                            else
                                templates[name]->type = ShapeTemplate::NONE;
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("ballast")) {
                            ShapeTemplateElement *e = new ShapeTemplateElement();
                            e->type = e->BALLAST;
                            e->load(data);
                            templates[name]->elements[e->name] = e;
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("stretch")) {
                            ShapeTemplateElement *e = new ShapeTemplateElement();
                            e->type = e->STRETCH;
                            e->load(data);
                            templates[name]->elements[e->name] = e;
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("point")) {
                            ShapeTemplateElement *e = new ShapeTemplateElement();
                            e->type = e->POINT;
                            e->load(data);
                            templates[name]->elements[e->name] = e;
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("rail")) {
                            ShapeTemplateElement *e = new ShapeTemplateElement();
                            e->type = e->RAIL;
                            e->load(data);
                            templates[name]->elements[e->name] = e;
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("tie")) {
                            ShapeTemplateElement *e = new ShapeTemplateElement();
                            e->type = e->TIE;
                            e->load(data);
                            templates[name]->elements[e->name] = e;
                            ParserX::SkipToken(data);
                            continue;
                        }
                        qDebug() << "#Template - undefined token: " << sh;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    ParserX::SkipToken(data);
                    continue;
                }
                qDebug() << "#Templates - undefined token: " << sh;
                ParserX::SkipToken(data);
                continue;
            }
            ParserX::SkipToken(data);
            continue;
            
            continue;
        }
        qDebug() << "#ShapeTemplates - undefined token: " << sh;
        ParserX::SkipToken(data);
        continue;
    }
    delete data;
}

void ShapeTemplateElement::load(FileBuffer* data){
    name = ParserX::GetString(data);
    id = DEFAULT;
    ParserX::GetString(data);
    QStringList dist = ParserX::GetString(data).split("-");
    minDistance = dist[0].toFloat();
    maxDistance = dist[1].toFloat();
    //qDebug() << minDistance << "-"<<maxDistance;
    int shapeCount = ParserX::GetNumber(data);
    for(int i = 0; i < shapeCount; i++)
        shape.push_back(ParserX::GetString(data));
    texture = ParserX::GetString(data);
    yOffset = ParserX::GetNumber(data);
    if(type == RAIL){
        xOffset = ParserX::GetNumber(data);
    }
}

ShapeTemplates::~ShapeTemplates() {
    for(ShapeTemplate *shapeTemplate : templates){
        if(shapeTemplate == NULL)
            continue;
        for(ShapeTemplateElement *element : shapeTemplate->elements)
            delete element;
        delete shapeTemplate;
    }
    templates.clear();
}

