/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/ObjFile.h>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <tsre/math3d/Vector3f.h>
#include <tsre/math3d/Vector2f.h>
#include <algorithm>
#include <cmath>


ObjFile::ObjFile(QString path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)){
        error = "Unable to open OBJ";
        return;
    }

    QTextStream in(&file);
    QString line;
    QVector<Vector3f> v;
    QVector<Vector2f> vt;
    QVector<Vector3f> vn;

    auto parseFloat = [](const QString &text, float &value) {
        bool ok = false;
        value = text.toFloat(&ok);
        return ok && std::isfinite(value);
    };
    int lineNumber = 0;
    while (!in.atEnd()) {
        lineNumber++;
        line = in.readLine();
        const int comment = line.indexOf('#');
        if(comment >= 0)
            line.truncate(comment);
        line = line.trimmed();
        if(line.isEmpty())
            continue;
        const QStringList args = line.split(
                QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        const QString directive = args.first();
        if((directive == "v" || directive == "vn") && args.size() != 4){
            error = QString("Malformed %1 at line %2")
                    .arg(directive).arg(lineNumber);
            points.clear();
            count = 0;
            return;
        }
        if(directive == "vt" && args.size() != 3){
            error = QString("Malformed vt at line %1").arg(lineNumber);
            points.clear();
            count = 0;
            return;
        }
        if(directive == "f" && args.size() != 4){
            error = QString("Only triangular OBJ faces are supported (line %1)")
                    .arg(lineNumber);
            points.clear();
            count = 0;
            return;
        }
        if(directive == "v" || directive == "vn"){
            float values[3];
            if(!parseFloat(args[1], values[0])
                    || !parseFloat(args[2], values[1])
                    || !parseFloat(args[3], values[2])){
                error = QString("Invalid numeric value at line %1")
                        .arg(lineNumber);
                points.clear();
                count = 0;
                return;
            }
            if(directive == "v")
                v.push_back(Vector3f(values[0], values[1], values[2]));
            else
                vn.push_back(Vector3f(values[0], values[1], values[2]));
        } else if(directive == "vt"){
            float values[2];
            if(!parseFloat(args[1], values[0])
                    || !parseFloat(args[2], values[1])){
                error = QString("Invalid texture coordinate at line %1")
                        .arg(lineNumber);
                points.clear();
                count = 0;
                return;
            }
            texYmin = std::min(texYmin, values[1]);
            texYmax = std::max(texYmax, values[1]);
            vt.push_back(Vector2f(values[0], values[1]));
        } else if(directive == "f"){
            for(int i = 1; i < 4; i++ ){
                const QStringList fargs = args[i].split('/');
                bool positionOk = false;
                bool textureOk = false;
                bool normalOk = false;
                const int positionIndex = fargs.size() == 3
                        ? fargs[0].toInt(&positionOk) : 0;
                const int textureIndex = fargs.size() == 3
                        ? fargs[1].toInt(&textureOk) : 0;
                const int normalIndex = fargs.size() == 3
                        ? fargs[2].toInt(&normalOk) : 0;
                if(!positionOk || !textureOk || !normalOk
                        || positionIndex <= 0 || textureIndex <= 0
                        || normalIndex <= 0 || positionIndex > v.size()
                        || textureIndex > vt.size() || normalIndex > vn.size()){
                    error = QString("Invalid face index at line %1")
                            .arg(lineNumber);
                    points.clear();
                    count = 0;
                    return;
                }
                const Vector3f &position = v[positionIndex - 1];
                const Vector3f &normal = vn[normalIndex - 1];
                const Vector2f &texture = vt[textureIndex - 1];
                points.push_back(position.x);
                points.push_back(position.y);
                points.push_back(position.z);
                points.push_back(normal.x);
                points.push_back(normal.y);
                points.push_back(normal.z);
                points.push_back(texture.x);
                points.push_back(texture.y);
                count++;
            }
        }
    }
    if(count == 0){
        error = "OBJ contains no supported triangles";
        return;
    }
    valid = true;
}

