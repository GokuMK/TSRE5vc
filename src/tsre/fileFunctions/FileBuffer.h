/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef FILEBUFFER_H
#define	FILEBUFFER_H

#include <QString>
#include <tsre/fileFunctions/TS.h>
#include <stdexcept>

class FileBuffer {
public:
    FileBuffer();
    FileBuffer(unsigned char * data, int nLength);
    FileBuffer(const FileBuffer* orig);
    virtual ~FileBuffer();
    
    int getInt();
    unsigned short int getShort();
    short int getSignedShort();
    unsigned int getUint();
    float getFloat();
    QString* getString(int start, int end);
    unsigned char get();
    // SIMIS IDs are complete little-endian DWORDs, independent of file type.
    TS::TokenId getToken();
    void findToken(TS::TokenId id);
    class ParseError : public std::runtime_error {
    public:
        explicit ParseError(const char* message) : std::runtime_error(message) {}
    };
    struct Block {
        TS::TokenId id;
        int body;
        int payload;
        int end;
    };
    // A scope bounds legacy positional payload reads without changing text parsers.
    class ScopedLimit {
    public:
        ScopedLimit(FileBuffer& buffer, int end);
        ~ScopedLimit();
        ScopedLimit(const ScopedLimit&) = delete;
        ScopedLimit& operator=(const ScopedLimit&) = delete;
    private:
        FileBuffer& buffer;
        int previous;
    };
    int readEnd() const;
    bool isBinarySimis() const;
    Block readBlock(); // leaves off at body (including label framing)
    int readBlockEnd(); // ID already consumed; leaves off at body
    void skipLabel();
    QString readString(); // uint16 character count + UTF-16LE
    void require(int bytes) const;
    bool insertFile(QString incPath, QString alternativePath = "", QString* loaded = NULL);
    bool isBOM();
    void skipBOM();
    void toUtf16();
    
    int off = 0;
    int length = 0;
    unsigned char * data = NULL;
private:
    int limit = -1;
    void checkPayload(int bytes) const;
};

#endif	/* FILEBUFFER_H */

