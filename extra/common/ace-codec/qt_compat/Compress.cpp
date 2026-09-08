#include "Compat.h"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <mzip/miniz/miniz.h>
#include <limits>

QByteArray qCompress(const QByteArray& data, int level) {
    if (data.size() > std::numeric_limits<int>::max())
        throw std::length_error("Compression input exceeds codec limits");
    if (level < -1 || level > 9) level = -1;
    const auto size = static_cast<mz_ulong>(data.size());
    mz_ulong capacity = mz_compressBound(size);
    QByteArray result(static_cast<qsizetype>(capacity) + 4, Qt::Uninitialized);
    for (int i = 0; i < 4; ++i) result[i] = static_cast<char>(size >> (24 - 8 * i));
    // Qt represents empty input by the four-byte zero length alone.
    if (!size) { result.resize(4); return result; }
    if (mz_compress2(reinterpret_cast<unsigned char*>(result.data() + 4), &capacity,
                     reinterpret_cast<const unsigned char*>(data.constData()), size, level) != MZ_OK)
        throw std::runtime_error("ACE compression failed");
    result.resize(static_cast<qsizetype>(capacity) + 4);
    return result;
}
