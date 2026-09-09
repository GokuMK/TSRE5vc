/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "FileBuffer.h"
#include <tsre/fileFunctions/ReadFile.h>
#include <QFile>
#include <QDebug>
#include <string> 
#include <algorithm>
#include <QtEndian>
#include <cstring>

FileBuffer::FileBuffer() {
    this->off = 0;
}

FileBuffer::FileBuffer(unsigned char * data, int nLength) {
    this->data = data;
    this->length = nLength;
    this->off = 0;
}

FileBuffer::FileBuffer(const FileBuffer* orig) {
    length = orig->length;
    data = new unsigned char[length];
    std::copy ( orig->data, orig->data+length, data );
}

FileBuffer::~FileBuffer() {
    delete[] this->data;
}

int FileBuffer::readEnd() const {
    return limit < 0 ? length : limit;
}

bool FileBuffer::isBinarySimis() const {
    return data && length >= 32 && std::memcmp(data + 16, "JINX", 4) == 0
            && data[23] == 'b';
}

void FileBuffer::require(int bytes) const {
    if (!data || off < 0 || bytes < 0 || off > readEnd() || bytes > readEnd() - off)
        throw ParseError("Truncated SIMIS data or child outside its parent");
}

void FileBuffer::checkPayload(int bytes) const {
    if (limit >= 0)
        require(bytes);
}

FileBuffer::ScopedLimit::ScopedLimit(FileBuffer& buffer, int end)
    : buffer(buffer), previous(buffer.limit) {
    if (end < buffer.off || end > buffer.readEnd())
        throw ParseError("Invalid SIMIS parent boundary");
    buffer.limit = end;
}

FileBuffer::ScopedLimit::~ScopedLimit() {
    buffer.limit = previous;
}

int FileBuffer::getInt() {
    const quint32 bits = getUint();
    qint32 value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

TS::TokenId FileBuffer::getToken() {
    require(4);
    const auto id = qFromLittleEndian<quint32>(data + off);
    off += 4;
    return id;
}

unsigned int FileBuffer::getUint() {
    checkPayload(4);
    const auto value = qFromLittleEndian<quint32>(data + off);
    off += 4;
    return value;
}

unsigned short int FileBuffer::getShort() {
    checkPayload(2);
    const auto value = qFromLittleEndian<quint16>(data + off);
    off += 2;
    return value;
}

int FileBuffer::readBlockEnd() {
    require(4);
    const quint32 size = getUint();
    // Compare before adding: neither unsigned wrap nor signed overflow is allowed.
    if (size < 1 || size > quint32(readEnd() - off))
        throw ParseError("Invalid SIMIS block length");
    const int end = off + int(size);
    const int labelBytes = 1 + 2 * data[off];
    if (labelBytes > int(size))
        throw ParseError("Truncated SIMIS label");
    return end;
}

FileBuffer::Block FileBuffer::readBlock() {
    const TS::TokenId id = getToken();
    const int end = readBlockEnd();
    return {id, off, off + 1 + 2 * data[off], end};
}

void FileBuffer::skipLabel() {
    require(1);
    const int bytes = 1 + 2 * data[off];
    require(bytes);
    off += bytes;
}

QString FileBuffer::readString() {
    require(2);
    const int bytes = 2 * getShort();
    require(bytes);
    QString result;
    result.reserve(bytes / 2);
    for (int end = off + bytes; off < end; off += 2)
        result.append(QChar(qFromLittleEndian<quint16>(data + off)));
    return result;
}

void FileBuffer::skipBOM(){
    if (off < 0 || length - off < 2) return;
    if(this->getShort() == 65279)
        return;
    off -= 2;
    return;
}

bool FileBuffer::isBOM(){
    if (off < 0 || length - off < 2) return false;
    if(this->getShort() == 65279){
        off -= 2;
        return true;
    }
    off -= 2;
    return false;
}

short int FileBuffer::getSignedShort() {
    const quint16 bits = getShort();
    qint16 value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

float FileBuffer::getFloat() {
    const quint32 bits = getUint();
    float value;
    static_assert(sizeof(value) == sizeof(bits), "SIMIS requires 32-bit floats");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

unsigned char FileBuffer::get() {
    checkPayload(1);
    return data[off++];
}

QString* FileBuffer::getString(int start, int end) {
    if (limit >= 0 && (start < 0 || end < start || end > readEnd() || (end - start) % 2))
        throw ParseError("Invalid SIMIS string boundary");
    QString* s = new QString();

    for (int i = start; i < end; i += 2) {
        if (data[i] == 13) continue;
        *s += QChar(data[i], data[i + 1]);
    }
    return s;

}

void FileBuffer::findToken(TS::TokenId id) {
    while (off < readEnd()) {
        const int start = off;
        const Block block = readBlock();
        if (block.id == id) {
            off = start + 4; // historical caller convention: next read is length
            return;
        }
        off = block.end;
    }
    throw ParseError("Required SIMIS token not found");
}

void FileBuffer::toUtf16(){
    if(isBOM()) return;
    qDebug() << "converting to UTF16";
    // 
    unsigned char * newData = new unsigned char[length * 2];
    for(int i = 0; i < length; i++){
        newData[i*2] = data[i];
        newData[i*2+1] = 0;
    }
    length = length * 2;
    delete[] data;
    data = newData;
}

bool FileBuffer::insertFile(QString incPath, QString alternativePath, QString* loaded){
    int i;
    QString sh;
    incPath.replace("\\","/");
    incPath.replace("//","/");
    alternativePath.replace("\\","/");
    alternativePath.replace("//","/");
    QFile file(incPath);
    if (!file.open(QIODevice::ReadOnly)){
        if(alternativePath.length() > 0){
            incPath = alternativePath;
            file.setFileName(incPath);
            if (!file.open(QIODevice::ReadOnly)){
                qDebug() << incPath << "not exist";
                return false;
            }
        } else {
            qDebug() << incPath << "not exist";
            return false;
        }
    }
    qDebug() << incPath;
    if(loaded != NULL){
        *loaded = incPath;
    }
    FileBuffer* incData = ReadFile::readRAW(&file);
    incData->toUtf16();
    int remaining = length-off;
    unsigned char * newData = new unsigned char[incData->length + remaining ];
    memcpy(newData, incData->data, incData->length);
    memcpy(newData+incData->length, data+off, remaining);
    delete[] data;
    data = newData;
    length = incData->length + remaining;
    off = 0;
    skipBOM();
    return true;
}