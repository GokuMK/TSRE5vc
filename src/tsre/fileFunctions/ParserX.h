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
#include <tsre/units/Quantity.h>

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
    // The next value inside a block as written (a number with its unit,
    // "-1mph"), up to a space or the block's end; empty at the end.
    static QString GetToken(FileBuffer* bufor);
    // A number as files write it, with the unit written after it, in SI
    // (task core si-units): the unit as `quantity` takes it, with Any as the
    // unit alone tells (Units::fromFile); a unit not known leaves the number
    // as written; a number without a unit is in `defaultUnit`, the
    // parameter's own (none: SI). A '+' after it adds the next number.
    static float GetNumber(FileBuffer* bufor, Units::Quantity quantity = Units::Quantity::Any,
                           QStringView defaultUnit = {});
    static float GetNumberInside(FileBuffer* bufor, bool *ok = NULL,
                                 Units::Quantity quantity = Units::Quantity::Any, QStringView defaultUnit = {});
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
    static float ReadNumber(FileBuffer* bufor, bool inside, bool *ok, Units::Quantity quantity,
                            QStringView defaultUnit);
};

#endif	/* PARSERX_H */

