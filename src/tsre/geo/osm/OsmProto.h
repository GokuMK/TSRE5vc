/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#pragma once

// Minimal protobuf wire format, enough for the OSM PBF messages.
// The reader never reads past its span; any malformed input sets a sticky error.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Osm::Proto {

enum class Wire : uint8_t { Varint = 0, Fixed64 = 1, Length = 2, Fixed32 = 5 };

struct Span {
    const uint8_t *data = nullptr;
    size_t size = 0;
    Span() = default;
    Span(const uint8_t *d, size_t n) : data(d), size(n) {}
    std::string_view view() const { return {reinterpret_cast<const char *>(data), size}; }
    bool empty() const { return size == 0; }
};

inline uint64_t zigzag(int64_t v) { return (uint64_t(v) << 1) ^ uint64_t(v >> 63); }
inline int64_t unzigzag(uint64_t v) { return int64_t(v >> 1) ^ -int64_t(v & 1); }

class Reader {
public:
    Reader() = default;
    Reader(const uint8_t *data, size_t size) : p_(data), end_(data + size) {}
    explicit Reader(Span s) : Reader(s.data, s.size) {}

    // Advances to the next field. Returns false at the end of the message or on error.
    bool next() {
        if (error_ || p_ >= end_) return false;
        const uint64_t key = readVarint();
        if (error_) return false;
        field_ = uint32_t(key >> 3);
        wire_ = Wire(key & 7);
        if (field_ == 0 || (wire_ != Wire::Varint && wire_ != Wire::Fixed64 && wire_ != Wire::Length && wire_ != Wire::Fixed32))
            return fail();
        return true;
    }
    uint32_t field() const { return field_; }
    Wire wire() const { return wire_; }
    bool ok() const { return !error_; }

    uint64_t varint() { return expect(Wire::Varint) ? readVarint() : 0; }
    int64_t int64() { return int64_t(varint()); }
    int64_t svarint() { return unzigzag(varint()); }
    Span bytes() {
        if (!expect(Wire::Length)) return {};
        const uint64_t n = readVarint();
        if (error_ || n > uint64_t(end_ - p_)) { fail(); return {}; }
        Span s(p_, size_t(n)); p_ += n;
        return s;
    }
    void skip() {
        switch (wire_) {
            case Wire::Varint: readVarint(); break;
            case Wire::Fixed64: advance(8); break;
            case Wire::Fixed32: advance(4); break;
            case Wire::Length: { const uint64_t n = readVarint(); if (!error_) advance(n); break; }
        }
    }

private:
    bool fail() { error_ = true; return false; }
    bool expect(Wire w) { if (error_ || wire_ != w) { fail(); return false; } return true; }
    void advance(uint64_t n) { if (n > uint64_t(end_ - p_)) fail(); else p_ += n; }
    uint64_t readVarint() {
        uint64_t r = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (p_ >= end_) { fail(); return 0; }
            const uint8_t c = *p_++;
            r |= uint64_t(c & 0x7f) << shift;
            if (c < 0x80) return r;
        }
        fail();
        return 0;
    }

    const uint8_t *p_ = nullptr, *end_ = nullptr;
    uint32_t field_ = 0;
    Wire wire_ = Wire::Varint;
    bool error_ = false;
};

// Packed repeated scalars. Each returns false if the span is malformed.
template <class F> bool forEachVarint(Span s, F &&f) {
    const uint8_t *p = s.data, *end = s.data + s.size;
    while (p < end) {
        uint64_t r = 0; int shift = 0;
        for (;;) {
            if (p >= end || shift >= 64) return false;
            const uint8_t c = *p++;
            r |= uint64_t(c & 0x7f) << shift;
            if (c < 0x80) break;
            shift += 7;
        }
        f(r);
    }
    return true;
}
template <class F> bool forEachSVarint(Span s, F &&f) {
    return forEachVarint(s, [&](uint64_t v) { f(unzigzag(v)); });
}

class Writer {
public:
    explicit Writer(std::string &out) : out_(out) {}

    static void putVarint(std::string &o, uint64_t v) {
        while (v >= 0x80) { o.push_back(char(v | 0x80)); v >>= 7; }
        o.push_back(char(v));
    }
    void key(uint32_t field, Wire w) { putVarint(out_, uint64_t(field) << 3 | uint64_t(w)); }
    void varint(uint32_t field, uint64_t v) { key(field, Wire::Varint); putVarint(out_, v); }
    void svarint(uint32_t field, int64_t v) { varint(field, zigzag(v)); }
    void bytes(uint32_t field, const void *data, size_t n) {
        key(field, Wire::Length); putVarint(out_, n);
        out_.append(static_cast<const char *>(data), n);
    }
    void bytes(uint32_t field, std::string_view s) { bytes(field, s.data(), s.size()); }
    template <class It> void packedVarint(uint32_t field, It begin, It end) {
        if (begin == end) return;
        scratch_.clear();
        for (It i = begin; i != end; ++i) putVarint(scratch_, uint64_t(*i));
        bytes(field, scratch_);
    }
    template <class It> void packedSVarint(uint32_t field, It begin, It end) {
        if (begin == end) return;
        scratch_.clear();
        for (It i = begin; i != end; ++i) putVarint(scratch_, zigzag(int64_t(*i)));
        bytes(field, scratch_);
    }
    // Delta-encodes then zigzags, as PBF stores ids, refs and dense coordinates.
    template <class It> void packedDelta(uint32_t field, It begin, It end) {
        if (begin == end) return;
        scratch_.clear();
        int64_t prev = 0;
        for (It i = begin; i != end; ++i) {
            // Wrapping difference; decoding sums the deltas back with the same wrap.
            putVarint(scratch_, zigzag(int64_t(uint64_t(int64_t(*i)) - uint64_t(prev))));
            prev = int64_t(*i);
        }
        bytes(field, scratch_);
    }

private:
    std::string &out_;
    std::string scratch_;
};

}
