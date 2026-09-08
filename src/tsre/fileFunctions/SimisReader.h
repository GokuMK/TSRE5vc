#ifndef TSRE_SIMIS_READER_H
#define TSRE_SIMIS_READER_H

#include <tsre/fileFunctions/FileBuffer.h>
#include <memory>

namespace Simis {

// Counted/positional containers still need their schema. This helper only
// handles framing; unknown siblings are skipped by their complete block size.
class Block {
public:
    Block(FileBuffer* data, TS::TokenId expected)
        : data(data), header(find(data, expected)), limit(*data, header.end) {
        data->off = header.payload;
    }
    ~Block() { data->off = header.end; }
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    QString label() const {
        std::unique_ptr<QString> value(data->getString(header.body + 1, header.payload));
        return *value;
    }
private:
    static FileBuffer::Block find(FileBuffer* data, TS::TokenId expected) {
        data->findToken(expected);
        data->off -= 4;
        return data->readBlock();
    }
    FileBuffer* data;
    FileBuffer::Block header;
    FileBuffer::ScopedLimit limit;
};

inline int count(FileBuffer* data, int minimumItemBytes = 9) {
    const int value = data->getInt();
    if (value < 0 || minimumItemBytes <= 0
            || value > (data->readEnd() - data->off) / minimumItemBytes)
        throw FileBuffer::ParseError("Invalid SIMIS array count");
    return value;
}

} // namespace Simis
#endif
