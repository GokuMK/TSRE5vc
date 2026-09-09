#ifndef TSRE_NETWORK_TOKEN_H
#define TSRE_NETWORK_TOKEN_H

#include <tsre/fileFunctions/FileBuffer.h>
#include <QDataStream>

namespace NetworkToken {

inline bool known(TS::TokenId id) {
    switch (id) {
    case TS::TSRE_Requested_Terrain_tFile:
    case TS::TSRE_Requested_Terrain_RawFile:
    case TS::TSRE_Requested_Terrain_FtFile:
    case TS::TSRE_Terrain_tFile:
    case TS::TSRE_Terrain_RawFile:
    case TS::TSRE_Terrain_FtFile:
    case TS::TSRE_Requested_TD_File:
    case TS::TSRE_Requested_TD_Lo_File: return true;
    default: return false;
    }
}

inline void write(QDataStream& stream, TS::TokenId id) {
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint8('B') << quint32(id);
}

// Same-version peers only. Keep the old envelope/payload; do not translate IDs
// inside transmitted files. The historical size DWORD is still reserved zero.
inline bool read(FileBuffer* data, TS::TokenId& id, QString& error) {
    if (data->off < 0 || data->length - data->off < 5) {
        error = QStringLiteral("Truncated binary network token");
        return false;
    }
    if (data->get() != 'B') {
        error = QStringLiteral("Invalid binary network marker");
        return false;
    }
    id = data->getToken();
    if (id >= 100001 && id <= 100008) {
        error = QStringLiteral("Incompatible legacy token protocol: upgrade client and server together");
        return false;
    }
    if (!known(id)) {
        error = QStringLiteral("Unsupported network token: ") + TS::describe(id);
        return false;
    }
    if (data->length - data->off < 13) {
        error = QStringLiteral("Truncated binary network envelope");
        return false;
    }
    return true;
}

} // namespace NetworkToken
#endif
