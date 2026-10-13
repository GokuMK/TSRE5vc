/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef PARSERX_H
#define	PARSERX_H

#include <tsre/fileFunctions/FileBuffer.h>
#include <QString>
#include <QStringList>

class QTextStream;

class ParserX {
public:
    ParserX();
    ParserX(const ParserX& orig);
    virtual ~ParserX();
    
    static int NextLine(FileBuffer* bufor);
    static int FindTokenDomIgnore(QString sh, FileBuffer* bufor);
    static QString NextTokenDomIgnore(FileBuffer* bufor);
    static QString NextTokenInside(FileBuffer* bufor);
    static QString GetAlternativeTokenName(FileBuffer* bufor);
    static QString GetString(FileBuffer* bufor);
    static QString GetStringInside(FileBuffer* bufor);
    static float GetNumber(FileBuffer* bufor);
    static float GetNumberInside(FileBuffer* bufor, bool *ok = NULL);
    static unsigned int GetUInt(FileBuffer* bufor);
    static unsigned int GetHex(FileBuffer* bufor);
    static int SkipToken(FileBuffer* bufor);
    // The block whose token NextTokenInside has just returned, as it is in
    // the file: from the token to its closing bracket, with brackets inside
    // quoted strings. The buffer is left after the block, as SkipToken
    // leaves it. For blocks a reader does not model, to be written back.
    static QString KeepBlock(FileBuffer* bufor, const QString &token);
    // Kept blocks, each on its own line after the indent.
    static void WriteKept(QTextStream* out, const QStringList &kept, const QString &indent);
    static QString AddComIfReq(QString n);
    static QString MakeFlagsString(unsigned int val);
    static QString SplitToMultiline(QString n, QString woff = "");

private:
    static float NumberUnit(float x, unsigned short int &b, FileBuffer* bufor);
};

#endif	/* PARSERX_H */

