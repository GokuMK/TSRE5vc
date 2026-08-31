/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tdb/SpeedPostDAT.h>
#include <QDebug>
#include <tsre/Game.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/fileFunctions/MstsTextFileValidation.h>
#include <tsre/tdb/SpeedPost.h>
#include <tsre/ErrorMessage.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/Game.h>

SpeedPostDAT::SpeedPostDAT() {
    QString sh;
    QString path = Game::root + "/routes/" + Game::route + "/speedpost.dat";
    path.replace("//", "/");
    qDebug() << path;
    QFile file(path);
    sourceFileExists = file.exists();
    if(!sourceFileExists) {
        qDebug() << "speedpost.dat does not exist; using an empty speed-post configuration";
        loaded = true;
        return;
    }
    auto reportLoadFailure = [&](const QString &reason) {
        qWarning() << "Failed to load existing speedpost.dat" << path << reason;
        ErrorMessagesLib::PushErrorMessage(new ErrorMessage(
            ErrorMessage::Type_Error,
            ErrorMessage::Source_TDB,
            QString("Failed to load existing speed-post configuration: %1").arg(path),
            reason
        ));
    };
    if(!file.open(QIODevice::ReadOnly)) {
        reportLoadFailure(file.errorString());
        return;
    }
    if(file.size() < 34) {
        reportLoadFailure("File is too short to be a valid MSTS speed-post configuration.");
        file.close();
        return;
    }
    FileBuffer* bufor = ReadFile::read(&file);
    file.close();
    bufor->toUtf16();
    QString validationError;
    if(!MstsTextFileValidation::validate(bufor, validationError)) {
        reportLoadFailure(validationError);
        delete bufor;
        return;
    }
    bufor->off += 46+16;

    qDebug() << "speedpost.dat!";
    while (!((sh = ParserX::NextTokenInside(bufor).toLower()) == "")) {
        if (sh == "speed_warning_sign_shape") {
            this->speed_Warning_Sign_Shape = ParserX::GetString(bufor);
            ParserX::SkipToken(bufor);
            continue;
        }
        if (sh == "restricted_shape") {
            this->restricted_Shape = ParserX::GetString(bufor);
            ParserX::SkipToken(bufor);
            continue;
        }
        if (sh == "end_restricted_shape") {
            this->end_Restricted_Shape = ParserX::GetString(bufor);
            ParserX::SkipToken(bufor);
            continue;
        }
        if (sh == "speedpost_set") {
            SpeedPost* spost = new SpeedPost();
            this->speedPost.push_back(spost);
            while (!((sh = ParserX::NextTokenInside(bufor).toLower()) == "")) {
                spost->set(sh, bufor);
                ParserX::SkipToken(bufor);
                continue;
            }
            ParserX::SkipToken(bufor);
            continue;
        }
        qDebug() <<"#speedpost.dat - undefined token: "<< sh;
        ParserX::SkipToken(bufor);
        continue;
    }
    loaded = true;
    delete bufor;
}

SpeedPostDAT::~SpeedPostDAT() {
}

