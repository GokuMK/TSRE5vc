#include <tsre/geo/ElevationTiffCodec.h>
#include <tsre/geo/ElevationRaster.h>

#define MINIZ_HEADER_FILE_ONLY
#include <mzip/miniz/miniz.h>

#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Elevation {
namespace {

bool lzw(const QByteArray &input, qsizetype expected, QByteArray &output) {
    if (expected <= 0 || expected > 128*1024*1024) return false;
    output.clear();
    output.reserve(expected);

    quint16 prefix[4096]{};
    quint8 suffix[4096]{};
    quint8 stack[4097]{};
    int bit = 0, width = 9, next = 258, previous = -1;

    const auto code = [&](int bits, int &value) {
        if (bit + bits > input.size()*8) return false;
        quint32 accumulator = 0;
        for (int i=0; i<bits; ++i) {
            const int at = bit+i;
            accumulator = (accumulator<<1)
                | ((quint8(input[at/8]) >> (7-at%8)) & 1);
        }
        bit += bits;
        value = int(accumulator);
        return true;
    };
    const auto expand = [&](int value, int &count, quint8 &first) {
        count = 0;
        while (value >= 258) {
            if (value >= next || count >= 4096) return false;
            stack[count++] = suffix[value];
            value = prefix[value];
        }
        if (value < 0 || value > 255 || count >= 4097) return false;
        first = quint8(value);
        stack[count++] = first;
        std::reverse(stack,stack+count);
        return true;
    };

    bool ended = false;
    while (bit < input.size()*8) {
        int current;
        if (!code(width,current)) break;
        if (current == 256) {
            width = 9; next = 258; previous = -1;
            continue;
        }
        if (current == 257) { ended = true; break; }
        if (current < 0 || current > next || current >= 4096) return false;

        int count = 0;
        quint8 first = 0;
        if (current == next) {
            if (previous < 0 || !expand(previous,count,first) || count >= 4097)
                return false;
            stack[count++] = first;
        } else if (!expand(current,count,first)) return false;

        if (output.size() > expected-count) return false;
        output.append(reinterpret_cast<const char*>(stack),count);

        if (previous >= 0 && next < 4096) {
            prefix[next] = quint16(previous);
            suffix[next] = first;
            ++next;
            // TIFF LZW changes width one code earlier than GIF LZW.
            if (width < 12 && next == (1<<width)-1) ++width;
        }
        previous = current;
    }
    return ended && output.size() == expected;
}

bool undoFloatingPredictor(QByteArray &bytes, bool little, int sampleBytes,
                           int width, int height) {
    const qsizetype rowBytes = qsizetype(width)*sampleBytes;
    if (sampleBytes < 2 || rowBytes <= 0 || bytes.size() != rowBytes*height)
        return false;
    QByteArray row(rowBytes,Qt::Uninitialized);
    for (int y=0; y<height; ++y) {
        char *source = bytes.data()+y*rowBytes;
        for (qsizetype i=1; i<rowBytes; ++i)
            source[i] = char(quint8(source[i])+quint8(source[i-1]));
        std::memcpy(row.data(),source,size_t(rowBytes));
        for (int x=0; x<width; ++x) for (int b=0; b<sampleBytes; ++b) {
            const int plane = little ? sampleBytes-b-1 : b;
            source[x*sampleBytes+b] = row[plane*width+x];
        }
    }
    return true;
}

bool undoIntegerPredictor(QByteArray &bytes, bool little, int sampleBytes,
                          int width, int height) {
    const qsizetype rowBytes = qsizetype(width)*sampleBytes;
    if ((sampleBytes != 2 && sampleBytes != 4) || rowBytes <= 0
            || bytes.size() != rowBytes*height)
        return false;
    for (int y=0; y<height; ++y) {
        uchar *row = reinterpret_cast<uchar*>(bytes.data()+y*rowBytes);
        for (int x=1; x<width; ++x) {
            uchar *current = row+x*sampleBytes;
            const uchar *previous = current-sampleBytes;
            if (sampleBytes == 2) {
                const quint16 delta = little ? qFromLittleEndian<quint16>(current)
                                             : qFromBigEndian<quint16>(current);
                const quint16 before = little ? qFromLittleEndian<quint16>(previous)
                                              : qFromBigEndian<quint16>(previous);
                if (little) qToLittleEndian<quint16>(quint16(delta+before),current);
                else qToBigEndian<quint16>(quint16(delta+before),current);
            } else {
                const quint32 delta = little ? qFromLittleEndian<quint32>(current)
                                             : qFromBigEndian<quint32>(current);
                const quint32 before = little ? qFromLittleEndian<quint32>(previous)
                                              : qFromBigEndian<quint32>(previous);
                if (little) qToLittleEndian<quint32>(delta+before,current);
                else qToBigEndian<quint32>(delta+before,current);
            }
        }
    }
    return true;
}

}

bool decodeTiffBlock(const QByteArray &encoded, int compression, int predictor,
                     bool little, int bits, int sampleFormat,
                     int width, int height, QVector<float> &values, QString &error) {
    error.clear(); values.clear();
    if (width <= 0 || height <= 0 || qint64(width)*height > 32*1024*1024
            || (bits != 16 && bits != 32 && bits != 64)
            || (sampleFormat != 1 && sampleFormat != 2 && sampleFormat != 3)
            || (sampleFormat == 3 && bits != 32 && bits != 64)
            || (bits == 64 && sampleFormat != 3)) {
        error = QStringLiteral("Unsupported TIFF sample block"); return false;
    }
    const int sampleBytes = bits/8;
    const qsizetype expected = qsizetype(width)*height*sampleBytes;
    QByteArray raw;
    if (compression == 1) {
        if (encoded.size() != expected) {
            error = QStringLiteral("Inconsistent uncompressed TIFF block"); return false;
        }
        raw = encoded;
    } else if (compression == 5) {
        if (!lzw(encoded,expected,raw)) {
            error = QStringLiteral("Cannot decompress TIFF LZW block"); return false;
        }
    } else if (compression == 8) {
        raw.resize(expected);
        mz_ulong decoded = mz_ulong(expected);
        if (mz_uncompress(reinterpret_cast<unsigned char*>(raw.data()),&decoded,
                          reinterpret_cast<const unsigned char*>(encoded.constData()),
                          mz_ulong(encoded.size())) != MZ_OK
                || decoded != mz_ulong(expected)) {
            raw.clear();
            error = QStringLiteral("Cannot decompress TIFF Deflate block"); return false;
        }
    } else {
        error = QStringLiteral("Unsupported TIFF compression"); return false;
    }
    if (predictor == 2) {
        // Predictor 2 differences fixed-width sample words. Some valid COGs
        // use it for Float32 bit patterns rather than predictor 3 byte planes.
        if (!undoIntegerPredictor(raw,little,sampleBytes,width,height)) {
            error = QStringLiteral("Invalid TIFF horizontal predictor"); return false;
        }
    } else if (predictor == 3) {
        if (sampleFormat != 3 || !undoFloatingPredictor(raw,little,sampleBytes,width,height)) {
            error = QStringLiteral("Invalid TIFF floating-point predictor"); return false;
        }
    } else if (predictor != 1) {
        error = QStringLiteral("Unsupported TIFF predictor"); return false;
    }

    values.resize(qsizetype(width)*height);
    const uchar *data = reinterpret_cast<const uchar*>(raw.constData());
    for (qsizetype i=0; i<values.size(); ++i) {
        if (bits == 64) {
            const quint64 bitsValue = little ? qFromLittleEndian<quint64>(data+8*i)
                                             : qFromBigEndian<quint64>(data+8*i);
            double value;
            std::memcpy(&value,&bitsValue,8);
            values[i] = float(value);
        } else if (bits == 32) {
            const quint32 bitsValue = little ? qFromLittleEndian<quint32>(data+4*i)
                                             : qFromBigEndian<quint32>(data+4*i);
            if (sampleFormat == 3) std::memcpy(&values[i],&bitsValue,4);
            else if (sampleFormat == 2) {
                qint32 signedValue;
                std::memcpy(&signedValue,&bitsValue,4);
                values[i] = signedValue;
            }
            else values[i] = bitsValue;
        } else if (sampleFormat == 2) {
            const quint16 rawValue = little ? qFromLittleEndian<quint16>(data+2*i)
                                            : qFromBigEndian<quint16>(data+2*i);
            values[i] = qint16(rawValue);
        } else {
            values[i] = little ? qFromLittleEndian<quint16>(data+2*i)
                               : qFromBigEndian<quint16>(data+2*i);
        }
    }
    return true;
}

bool encodeGeoTiff(const Raster &raster, QByteArray &bytes, QString &error) {
    error.clear();bytes.clear();
    if(raster.width<=0||raster.height<=0
            ||qint64(raster.width)*raster.height!=raster.values.size()
            ||raster.values.size()>32*1024*1024||raster.epsg<=0||raster.epsg>65535
            ||raster.transform[1]<=0||raster.transform[5]>=0
            ||raster.transform[2]!=0||raster.transform[4]!=0) {
        error=QStringLiteral("Invalid raster for GeoTIFF cache");return false;
    }
    constexpr quint32 tileWidth=256,tileHeight=256;
    const quint32 columns=(quint32(raster.width)+tileWidth-1)/tileWidth;
    const quint32 rows=(quint32(raster.height)+tileHeight-1)/tileHeight;
    const quint32 tileCount=columns*rows;
    if(!tileCount||tileCount>65536){error=QStringLiteral("Too many GeoTIFF cache tiles");return false;}
    const float noData=std::isfinite(raster.noData)?raster.noData:-9999.0f;
    QVector<QByteArray> tiles;tiles.reserve(tileCount);
    QVector<quint32> byteCounts;byteCounts.reserve(tileCount);
    QByteArray raw(qsizetype(tileWidth)*tileHeight*4,Qt::Uninitialized);
    for(quint32 tile=0;tile<tileCount;++tile){
        const quint32 tx=(tile%columns)*tileWidth,ty=(tile/columns)*tileHeight;
        for(quint32 y=0;y<tileHeight;++y)for(quint32 x=0;x<tileWidth;++x){
            float value=noData;
            if(tx+x<quint32(raster.width)&&ty+y<quint32(raster.height)){
                value=raster.values[(ty+y)*raster.width+tx+x];
                if(!std::isfinite(value)||(raster.hasNoData&&value==raster.noData))value=noData;
            }
            quint32 bitsValue;std::memcpy(&bitsValue,&value,4);
            qToLittleEndian<quint32>(bitsValue,
                reinterpret_cast<uchar*>(raw.data())+4*(y*tileWidth+x));
        }
        QByteArray encoded(qsizetype(mz_compressBound(mz_ulong(raw.size()))),Qt::Uninitialized);
        mz_ulong encodedSize=mz_ulong(encoded.size());
        if(mz_compress2(reinterpret_cast<unsigned char*>(encoded.data()),&encodedSize,
                        reinterpret_cast<const unsigned char*>(raw.constData()),
                        mz_ulong(raw.size()),MZ_DEFAULT_COMPRESSION)!=MZ_OK){
            error=QStringLiteral("Cannot compress GeoTIFF cache tile");return false;
        }
        encoded.resize(qsizetype(encodedSize));byteCounts.push_back(quint32(encoded.size()));
        tiles.push_back(std::move(encoded));
    }
    const auto u16=[](quint16 value){QByteArray b(2,Qt::Uninitialized);qToLittleEndian<quint16>(value,reinterpret_cast<uchar*>(b.data()));return b;};
    const auto u32=[](quint32 value){QByteArray b(4,Qt::Uninitialized);qToLittleEndian<quint32>(value,reinterpret_cast<uchar*>(b.data()));return b;};
    const auto f64=[](double value){quint64 bitsValue;std::memcpy(&bitsValue,&value,8);QByteArray b(8,Qt::Uninitialized);qToLittleEndian<quint64>(bitsValue,reinterpret_cast<uchar*>(b.data()));return b;};
    const auto longs=[&](const QVector<quint32> &values){QByteArray b;b.reserve(values.size()*4);for(quint32 v:values)b+=u32(v);return b;};
    struct Field{quint16 tag,type;quint32 count;QByteArray data;};
    const QByteArray noDataText=QByteArray::number(noData,'g',9)+'\0';
    QVector<quint16> keys{1,1,0,4,1024,0,1,1,1025,0,1,1,
                          3072,0,1,quint16(raster.epsg),3076,0,1,9001};
    QByteArray keyBytes;for(quint16 key:keys)keyBytes+=u16(key);
    const QByteArray scale=f64(raster.transform[1])+f64(-raster.transform[5])+f64(0);
    const QByteArray tie=f64(0)+f64(0)+f64(0)+f64(raster.transform[0])
        +f64(raster.transform[3])+f64(0);
    QVector<quint32> tileOffsets(tileCount);
    QVector<Field> fields{
        {256,4,1,u32(quint32(raster.width))},{257,4,1,u32(quint32(raster.height))},
        {258,3,1,u16(32)},{259,3,1,u16(8)},{262,3,1,u16(1)},
        {274,3,1,u16(1)},{277,3,1,u16(1)},{284,3,1,u16(1)},
        {322,4,1,u32(tileWidth)},{323,4,1,u32(tileHeight)},
        {324,4,tileCount,longs(QVector<quint32>(tileCount))},{325,4,tileCount,longs(byteCounts)},
        {339,3,1,u16(3)},{33550,12,3,scale},{33922,12,6,tie},
        {34735,3,quint32(keys.size()),keyBytes},{42113,2,quint32(noDataText.size()),noDataText}
    };
    const quint32 ifdSize=2+quint32(fields.size())*12+4;
    quint32 extraEnd=8+ifdSize;
    for(const Field &field:fields)if(field.data.size()>4)extraEnd+=quint32(field.data.size());
    const quint32 dataStart=(extraEnd+3)&~quint32(3);
    quint32 dataOffset=dataStart;
    for(int i=0;i<tiles.size();++i){tileOffsets[i]=dataOffset;dataOffset+=quint32(tiles[i].size());}
    fields[10].data=longs(tileOffsets);
    bytes.reserve(dataOffset);bytes.append("II",2);bytes+=u16(42);bytes+=u32(8);
    bytes+=u16(quint16(fields.size()));
    quint32 valueOffset=8+ifdSize;
    for(const Field &field:fields){
        bytes+=u16(field.tag);bytes+=u16(field.type);bytes+=u32(field.count);
        if(field.data.size()<=4){bytes+=field.data;for(int n=field.data.size();n<4;++n)bytes+='\0';}
        else{bytes+=u32(valueOffset);valueOffset+=quint32(field.data.size());}
    }
    bytes+=u32(0);
    for(const Field &field:fields)if(field.data.size()>4)bytes+=field.data;
    while(bytes.size()<int(dataStart))bytes+='\0';
    for(const QByteArray &tile:tiles)bytes+=tile;
    if(bytes.size()!=int(dataOffset)){bytes.clear();error=QStringLiteral("Cannot assemble GeoTIFF cache");return false;}
    return true;
}

}
