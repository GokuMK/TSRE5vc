/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ParserX.h"
#include <QDebug>
#include <QTextStream>
#include <QtEndian>
#include <math.h>
#include <charconv>
#include <cmath>
#include <tsre/units/UnitConversion.h>

QString ParserX::AddComIfReq(QString n){
    if(n.length() == 0)
        return "\""+n+"\"";
    if(n.contains(" "))
        return "\""+n+"\"";
    for(int i = 0; i < n.length(); i++){
        if(n[i].row() != 0)
            return "\""+n+"\"";
    }
    return n;
}

QString ParserX::MakeFlagsString(unsigned int val){
    QString flags;
    flags = QString::number(val, 16);
    int l = flags.length();
    for(int i=0; i<8-l; i++)
        flags = "0"+flags;
    return flags;
}

QString ParserX::SplitToMultiline(QString n, QString woff){
    if(n.length() == 0)
        return "\""+n+"\"";
    if(!n.contains(" ") && !n.contains("\\"))
        return n;
    if(n.contains(" ") && !n.contains("\\"))
        return "\""+n+"\"";
    
    QStringList list = n.split("\\n");
    n = "";
    int lastIndex = 0;
    // An empty part joins the one before it, with the line break it ended
    // with; the empty part after a last line break ends nothing.
    for(int i = 0; i < list.length(); i++){
        if(i > 0 && list[i].length() == 0){
            if(i < list.length()-1)
                list[lastIndex] += "\\n";
            list.removeAt(i);
            i--;
            continue;
        } 
        if(i < list.length()-1) 
            list[i] += "\\n";
        lastIndex = i;
    }
    for(int i = 0; i < list.length(); i++){
        if(i > 0) n += woff;
        n += "\""+list[i]+"\"";
        if(i < list.length()-1) 
            n += "+\n";
    }
    return n;
}
//-----------------------------------
// Next line
//-----------------------------------
int ParserX::NextLine(FileBuffer* bufor){
    unsigned short int b;
    while (bufor->length >= bufor->off + 2) {
        b = bufor->getShort();
        if(b == '\n'){
            return 1;
        }
    }
    return 0;
}
//-----------------------------------
// Szukanie sekcji
// DEPRECATED - DO NOT USE
//-----------------------------------
int ParserX::FindTokenDomIgnore(QString sh, FileBuffer* bufor){
    unsigned short int b;
    int i = 0, czytam = 0;
    int poziom = 0;
    QString sekcja = "";

    while (bufor->length >= bufor->off + 2) {
        b = bufor->getShort();
        //qDebug() << b;
        //bufor->off++;
        if (b == 40 && czytam == 0) {
            poziom++;
        }
        if (b == 41) {
            poziom--;
        }
        if (poziom > 0) continue;
        if ((b > 64) || (b>47 && b<58 && czytam == 1)) {
            czytam = 1;
            sekcja += b;
            i++;
        } else {
            if (czytam == 1) {
                if (b == 40) bufor->off -= 2;
                i = 0;
                //qDebug() << "mam " << sekcja << " " << sh;
                if (sekcja.toLower() == sh.toLower()) {
                    //if (b == '(') ibufor--;
                    for (;;) {
                        if(bufor->length <= bufor->off + 2)
                            return 0;
                        //qDebug() << "fail ";
                        //qDebug() << b;
                        b = bufor->getShort();
                        //bufor->off++;
                        if (b == 40) {
                            return 1;
                        }
                    }
                }
                sekcja = "";
                czytam = 0;
            }
        }
    }
    //console.log("fail");
    return 0;
}
//-----------------------------------
// Szukanie sekcji
// DEPRECATED - DO NOT USE
//-----------------------------------
QString ParserX::NextTokenDomIgnore(FileBuffer* bufor){
    unsigned short int b;
    int i = 0, czytam = 0;
    int poziom = 0;
    QString sekcja = "";

    while (bufor->length >= bufor->off + 2) {
        b = bufor->getShort();
        //bufor->off++;
        if (b == 40 && czytam == 0) {
            poziom++;
        }
        if (b == 41) {
            poziom--;
        }
        if (poziom > 0) continue;

        if ((b > 64) || (b>47 && b<58 && czytam == 1)) {
            czytam = 1;
            sekcja += b;
            i++;
        } else {
            if (czytam == 1) {
                if (b == 40) bufor->off -= 2;
                i = 0;

                for (;;) {
                    //if(bufor->length <= bufor->off + 2)
                    //    return "";
                    b = bufor->getShort();
                    //bufor->off++;
                    if (b == 40) {
                        //qDebug() << sekcja;
                        return sekcja;
                    }
                }
            }
        }
    }
    return "";
}
//-----------------------------------
// Szukanie sekcji
//-----------------------------------
QString ParserX::NextTokenInside(FileBuffer* bufor){
    unsigned short int b;
    int i = 0, czytam = 0;
    int poziom = 0;
    //bool koniec = false;
    QString sekcja = "";

    while (bufor->length >= bufor->off + 2) {
        b = bufor->getShort();
        //bufor->off++;
        if (b == 40 && czytam == 0) {
            poziom++;
        }

        if (b == 41 && czytam == 0) {
            poziom--;
        }
        if (poziom > 0) continue;
        if (poziom < 0) {
            bufor->off -= 2;
            return "";
        }
        //console.log("p "+String.fromCharCode(b)+" "+poziom);
        if ((b > 63) || (b>47 && b<58 && czytam == 1) ) {
            czytam = 1;
            sekcja += b;
            i++;
        } else {
            if (czytam == 1) {
                if (b == 40) bufor->off -= 2;
                i = 0;

                for (;;) {
                    if(bufor->length <= bufor->off + 2)
                        return "";
                    b = bufor->getShort();
                    //bufor->off++;
                    if (b > 63) {
                        //qDebug() << "PARSER FAIL ===================== " << sekcja;
                        bufor->off-=2;
                        return sekcja;
                    }
                    if (b == 40) {
                        //qDebug() << sekcja;
                        return sekcja;
                    }
                }
            }
        }
    }
    //console.log("sekcja");
    bufor->off -= 2;
    return "";
}
//-----------------------------------
// Parsowanie stringa
//-----------------------------------
QString ParserX::GetString(FileBuffer* bufor){
    QString sciezka = "";
    unsigned short int b = 0;
    while ((b < 46) && (b != 34) && (b!=33)&&(b!=35)&&(b!=36)&&(b!=37)&&(b!=38)) {
        b = bufor->getShort();
    }
    //bufor.position(bufor.position()-2); 
    if (b == 34) {
        bool specialChar = false;
        while ((b = bufor->getShort()) != 34 || specialChar) {
            //bufor->off++;
            if(!specialChar){
                if(b == '\\') specialChar = true;
            } else {
                specialChar = false;
            }
            sciezka += QChar(b);
        }
    } else {
        bufor->off -= 2;
        while (((b = bufor->getShort()) > 32) && (b != 41)) {
            //bufor->off++;
            sciezka += QChar(b);
        }
    }
    if(b == 41)
        bufor->off-=2;
    //bufor->off++;
    //qDebug() << sciezka;
    return sciezka;
}
//-----------------------------------
// Parsowanie stringa
//-----------------------------------
QString ParserX::GetToken(FileBuffer* bufor){
    QString token;
    unsigned short int b = ' ';
    while (b <= 32 && bufor->off + 2 <= bufor->length)
        b = bufor->getShort();
    if (b <= 32 || b == ')') {
        if (b == ')')
            bufor->off -= 2;
        return token;
    }
    while (b > 32 && b != ')') {
        token += QChar(b);
        if (bufor->off + 2 > bufor->length)
            return token;
        b = bufor->getShort();
    }
    if (b == ')')
        bufor->off -= 2;
    return token;
}

QString ParserX::GetStringInside(FileBuffer* bufor){
    QString sciezka = "";
    unsigned short int b = 0;
    while ((b < 46) && (b != 34) && (b!=33)&&(b!=35)&&(b!=36)&&(b!=37)&&(b!=38)) {
        b = bufor->getShort();
        if (b == 41){
            bufor->off -= 2;
            return "";
        }
    }
    //bufor.position(bufor.position()-2); 
    if (b == 34) {
        bool specialChar = false;
        while ((b = bufor->getShort()) != 34 || specialChar) {
            //bufor->off++;
            if(!specialChar){
                if(b == '\\') specialChar = true;
            } else {
                specialChar = false;
            }
            sciezka += QChar(b);
        }
        //bufor->off++;
        b = bufor->getShort();
        //bufor->off++;
        if(b == '+'){
            sciezka += ParserX::GetStringInside(bufor);
        } else {
            bufor->off-=2;
        }
        return sciezka;
    } else {
        bufor->off -= 2;
        while (((b = bufor->getShort()) > 32) && (b != 41)) {
            //bufor->off++;
            sciezka += QChar(b);
        }
        if(b == 41)
            bufor->off-=2;
        //bufor->off++;
        //qDebug() << sciezka;
        return sciezka;
    }
    return "";
}
//-----------------------------------
// Parsowanie stringa
//-----------------------------------
QString ParserX::GetAlternativeTokenName(FileBuffer* bufor){
    QString sciezka = "";
    if(bufor->data[bufor->off - 2] == 40)
        return sciezka;
    unsigned short int b = 0;
    while ((b < 46) && (b != 34) && (b!=33)&&(b!=35)&&(b!=36)&&(b!=37)&&(b!=38)) {
        b = bufor->getShort();
        if (b == 40){
            return "";
        }
    }

    if (b == 34) {
        bool specialChar = false;
        while ((b = bufor->getShort()) != 34 || specialChar) {
            if(!specialChar){
                if(b == '\\') specialChar = true;
            } else {
                specialChar = false;
            }
            sciezka += QChar(b);
        }
        b = bufor->getShort();
        if(b == '+'){
            sciezka += ParserX::GetStringInside(bufor);
            return sciezka;
        } else {
            bufor->off-=2;
        }
        for (;;) {
            if(bufor->length <= bufor->off + 2)
                return "";
            b = bufor->getShort();
            if (b == 40)
                return sciezka;
        }
        return sciezka;
    } else {
        bufor->off -= 2;
        while (((b = bufor->getShort()) > 32) && (b != 40)) {
            sciezka += QChar(b);
        }
        bufor->off -= 2;
        for (;;) {
            if(bufor->length <= bufor->off + 2)
                return "";
            b = bufor->getShort();
            if (b == 40)
                return sciezka;
        }
        return sciezka;
    }
    return "";
}
//-----------------------------------
// Parsowanie liczby rzeczywistej
//-----------------------------------
float ParserX::GetNumber(FileBuffer* bufor, Units::Quantity quantity, QStringView defaultUnit){
    return ReadNumber(bufor, false, NULL, quantity, defaultUnit);
}

float ParserX::GetNumberInside(FileBuffer* bufor, bool *ok, Units::Quantity quantity, QStringView defaultUnit){
    return ReadNumber(bufor, true, ok, quantity, defaultUnit);
}

// Skips to the number (GetNumber: "nan" is NaN; GetNumberInside: none
// before the block ends), reads its digits exactly and the unit written
// after it, and gives it in SI.
float ParserX::ReadNumber(FileBuffer* bufor, bool inside, bool *ok, Units::Quantity quantity,
                          QStringView defaultUnit){
    unsigned short int b = 0;
    while (b < 45 || (b > 46 && b < 48) || b > 57) {
        b = bufor->getShort();
        if (inside) {
            if (b == 41) {
                bufor->off = bufor->off - 2;
                if (ok != NULL) *ok = false;
                return 0;
            }
        } else if (b == 110) {
            b = bufor->getShort();
            if (b == 97) {
                b = bufor->getShort();
                if (b == 110)
                    return NAN;
            }
        }
    }
    // The digits, kept for an exact reading. Up to 19 significant digits
    // also make an integer with a decimal exponent: scaled by an exact power
    // of ten when that is exact too (Clinger's fast path), correctly
    // rounded as std::from_chars would give it.
    char digits[96];
    int n = 0;
    unsigned long long mantissa = 0;
    int significant = 0, scale = 0;
    bool fits = true;
    auto keep = [&](unsigned short int c, bool fraction) {
        if (n < int(sizeof(digits)))
            digits[n++] = char(c);
        if (fraction)
            --scale;
        if (mantissa == 0 && c == '0')
            return;
        if (significant < 19) {
            mantissa = mantissa * 10 + (c - '0');
            ++significant;
        } else {
            fits = false;
        }
    };
    bool negative = false;
    if (b == 45) {
        negative = true;
        if (n < int(sizeof(digits)))
            digits[n++] = '-';
        b = bufor->getShort();
    }
    if (b != 46) {
        while (b > 47 && b < 58) {
            keep(b, false);
            b = bufor->getShort();
        }
    }
    // A comma for the point, as MSTS reads it.
    if (b == 46 || b == 44) {
        if (n < int(sizeof(digits)))
            digits[n++] = '.';
        b = bufor->getShort();
        while (b > 47 && b < 58) {
            keep(b, true);
            b = bufor->getShort();
        }
    }
    int exponent = 0;
    if (b == 69 || b == 101) {
        bool below = false;
        if (n < int(sizeof(digits)))
            digits[n++] = 'e';
        b = bufor->getShort();
        if (b == 45) {
            below = true;
            if (n < int(sizeof(digits)))
                digits[n++] = '-';
            b = bufor->getShort();
        } else if (b == '+') {
            b = bufor->getShort();
        }
        while (b > 47 && b < 58) {
            if (n < int(sizeof(digits)))
                digits[n++] = char(b);
            if (exponent < 10000)
                exponent = exponent * 10 + (b - '0');
            b = bufor->getShort();
        }
        if (below)
            exponent = -exponent;
    }
    static const double Powers[23] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                      1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    const int power = scale + exponent;
    double value = 0.0;
    if (fits && mantissa <= (1ULL << 53) && power >= -22 && power <= 22) {
        value = power >= 0 ? double(mantissa) * Powers[power] : double(mantissa) / Powers[-power];
        if (negative)
            value = -value;
    } else {
        // As far as the digits make a number ("1e", "-" alone: what is before).
        std::from_chars(digits, digits + n, value);
    }
    // The unit written after it: "t", "km/h", "t-uk", "in/2", "*(ft^3)",
    // "lbf/mph^2"; not the block's closing bracket.
    char16_t unit[32];
    int u = 0, depth = 0;
    bool overlong = false;
    for (;;) {
        const bool letter = (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z');
        const bool inUnit = u > 0
                && ((b >= '0' && b <= '9') || b == '/' || b == '^' || b == '-' || b == '*' || b == '('
                    || (b == ')' && depth > 0));
        if (!letter && !inUnit && !(u == 0 && b == '*'))
            break;
        if (b == '(')
            ++depth;
        else if (b == ')')
            --depth;
        if (u < 32)
            unit[u++] = char16_t(b);
        else
            overlong = true;
        b = bufor->getShort();
    }
    double si = value;
    if ((u > 0 || !defaultUnit.isEmpty()) && !overlong
            && !Units::fromFile(value, QStringView(unit, u), quantity, si, defaultUnit))
        si = value;
    float x = float(si);
    if (b == '+')
        x += GetNumberInside(bufor, NULL, quantity, defaultUnit);
    else
        bufor->off -= 2;
    if (inside && ok != NULL)
        *ok = true;
    return x;
}
//-----------------------------------
// Parsowanie liczby uint
//-----------------------------------
unsigned int ParserX::GetUInt(FileBuffer* bufor){
    unsigned short int b = 0;
    unsigned int x;

    while (b < 45 || (b > 45 && b < 48) || b > 57) {
        b = bufor->getShort();
        //bufor->off++;
    }
    x = 0;
    while (b > 47 && b < 58) {
        x = x * 10 + b - 48;
        b = bufor->getShort();
        //bufor->off++;
    }
    bufor->off -= 2;
    return x;
}
//-----------------------------------
// Parsowanie liczby szesnastkowej
//-----------------------------------
unsigned int ParserX::GetHex(FileBuffer* bufor){
    unsigned short int b = 0, sb = 0;
    unsigned int x;

    while (((b < 48) || (b > 57 && b < 65) || (b > 70 && b < 97) || (b > 102)) || (sb != 32 && sb != 40 && sb != 0 )) {
        sb = b;
        b = bufor->getShort();
        //bufor->off++;
    }
    x = 0;
    while ((b > 47 && b < 58) || (b > 64 && b < 71) || (b > 96 && b < 103)) {
        if (b > 96) b = (b - 87 + 48);
        else {
            if (b > 64) b = (b - 55 + 48);
        }
        x = x * 16 + b - 48;
        b = bufor->getShort();
        //bufor->off++;
    }
    bufor->off -= 2;
    return x;
}
//-----------------------------------
// Pominiecie sekcji
//-----------------------------------
int ParserX::SkipToken(FileBuffer* bufor){
    unsigned short int b;
    int poziom = 0;

    for (int tt = 0; tt < 20000000; tt++){
        if(bufor->length <= bufor->off + 2)
            return 0;
        b = bufor->getShort();

        if (b == 40) {
            poziom++;
        }
        if (b == 41) {
            poziom--;
        }
        if (poziom < 0) return 0;
    }
    return 0;
}

QString ParserX::KeepBlock(FileBuffer* bufor, const QString &token){
    const auto charAt = [bufor](int at) -> unsigned short int {
        if(at < 0 || at + 2 > bufor->length)
            return 0;
        return qFromLittleEndian<quint16>(bufor->data + at);
    };
    const auto text = [&](int from, int to){
        QString value;
        for(int at = from; at + 2 <= to; at += 2)
            value += QChar(charAt(at));
        return value.replace("\r\n", "\n");
    };
    // NextTokenInside stops after the block's bracket. A word without one
    // is kept as it is.
    const int open = bufor->off - 2;
    if(charAt(open) != 40)
        return token;
    // The token ends before the spaces in front of the bracket.
    int end = open;
    while(end - 2 >= 0 && charAt(end - 2) <= 32)
        end -= 2;
    const int start = end - 2*token.length();
    // To the matching bracket; a quoted string ends at an unescaped quote.
    int depth = 1;
    bool quoted = false;
    int at = bufor->off;
    while(depth > 0 && at + 2 <= bufor->length){
        const unsigned short int b = charAt(at);
        at += 2;
        if(quoted){
            if(b == '\\')
                at += 2;
            else if(b == 34)
                quoted = false;
        } else if(b == 34){
            quoted = true;
        } else if(b == 40){
            depth++;
        } else if(b == 41){
            depth--;
        }
    }
    bufor->off = at;
    if(start >= 0 && text(start, end).compare(token, Qt::CaseInsensitive) == 0)
        return text(start, at);
    return token + " " + text(open, at);
}

void ParserX::WriteKept(QTextStream* out, const QStringList &kept, const QString &indent){
    for(const QString &block : kept)
        *out << indent << block << "\n";
}
