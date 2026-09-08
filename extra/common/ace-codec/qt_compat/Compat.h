#pragma once
// Private subset used by AceDocument/DxtCodec. Never mix with real Qt in one binary.
// Value copies are eager; fromRawData also copies to keep ownership simple.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
using quint16 = std::uint16_t;
using quint32 = std::uint32_t;
using quint64 = std::uint64_t;
using qint64 = std::int64_t;
using qsizetype = std::ptrdiff_t;
namespace Qt { enum Initialization { Uninitialized }; }

class QByteArray {
    std::string bytes_;
    static std::size_t count(qsizetype n) {
        if (n < 0) throw std::length_error("Negative byte-array size");
        return static_cast<std::size_t>(n);
    }
public:
    QByteArray() = default;
    QByteArray(const char* s) : bytes_(s ? s : "") {}
    QByteArray(const char* s, qsizetype n) {
        if (n < 0) bytes_ = s ? s : "";
        else if (n) {
            if (!s) throw std::invalid_argument("Null byte-array input");
            bytes_.assign(s, count(n));
        }
    }
    QByteArray(qsizetype n, char fill) : bytes_(count(n), fill) {}
    QByteArray(qsizetype n, Qt::Initialization) : QByteArray(n, '\0') {}
    qsizetype size() const { return static_cast<qsizetype>(bytes_.size()); }
    bool isEmpty() const { return bytes_.empty(); }
    void clear() { bytes_.clear(); }
    char* data() { return bytes_.data(); }
    const char* data() const { return bytes_.data(); }
    const char* constData() const { return bytes_.data(); }
    void resize(qsizetype n) { bytes_.resize(count(n)); }
    void reserve(qsizetype n) { bytes_.reserve(count(n)); }
    QByteArray left(qsizetype n) const { return mid(0, n); }
    QByteArray mid(qsizetype pos, qsizetype n = -1) const {
        // Match Qt's clipping, including a negative starting position.
        if (pos < 0) {
            if (n >= 0) { if (n <= -pos) return {}; n += pos; }
            pos = 0;
        }
        if (pos > size()) return {};
        n = n < 0 ? size() - pos : std::min(n, size() - pos);
        return QByteArray(constData() + pos, n);
    }
    bool startsWith(const char* prefix) const { return bytes_.compare(0, std::strlen(prefix), prefix) == 0; }
    static QByteArray fromRawData(const char* p, qsizetype n) { return QByteArray(p, n); }
    void append(const char* p, qsizetype n) {
        if (n) { QByteArray copy(p, n); bytes_.append(copy.bytes_); }
    }
    void append(const QByteArray& b) { bytes_.append(b.bytes_); }
    char& operator[](qsizetype i) { return bytes_.at(count(i)); }
    char operator[](qsizetype i) const { return bytes_.at(count(i)); }
    QByteArray& operator+=(const QByteArray& b) { append(b); return *this; }
    friend QByteArray operator+(QByteArray a, const QByteArray& b) { a += b; return a; }
    friend bool operator==(const QByteArray& a, const QByteArray& b) { return a.bytes_ == b.bytes_; }
    friend bool operator!=(const QByteArray& a, const QByteArray& b) { return !(a == b); }
};
class QString {
    std::string text_;
public:
    QString() = default;
    QString(const char* s) : text_(s ? s : "") {}
    void clear() { text_.clear(); }
    const std::string& toStdString() const { return text_; }
};
template<class T> class QVector {
    std::vector<T> values_;
public:
    QVector() = default;
    QVector(std::initializer_list<T> values) : values_(values) {}
    qsizetype size() const { return static_cast<qsizetype>(values_.size()); }
    bool isEmpty() const { return values_.empty(); }
    T& operator[](qsizetype i) { return values_.at(static_cast<std::size_t>(i)); }
    const T& operator[](qsizetype i) const { return values_.at(static_cast<std::size_t>(i)); }
    void push_back(const T& value) { values_.push_back(value); }
    void push_back(T&& value) { values_.push_back(std::move(value)); }
    auto begin() { return values_.begin(); }
    auto end() { return values_.end(); }
    auto begin() const { return values_.begin(); }
    auto end() const { return values_.end(); }
};
class QStringList : public QVector<QString> {
public:
    QStringList& operator<<(const QString& s) { push_back(s); return *this; }
};
template<class K, class V> class QHash {
    std::unordered_map<K, V> values_;
public:
    struct const_iterator {
        typename std::unordered_map<K, V>::const_iterator it;
        const V& value() const { return it->second; }
        bool operator==(const const_iterator& other) const { return it == other.it; }
        bool operator!=(const const_iterator& other) const { return it != other.it; }
    };
    const_iterator constFind(const K& k) const { return {values_.find(k)}; }
    const_iterator cend() const { return {values_.cend()}; }
    qsizetype size() const { return static_cast<qsizetype>(values_.size()); }
    void insert(const K& k, const V& v) { values_.insert_or_assign(k, v); }
};
struct QIODevice { enum OpenMode { ReadOnly, WriteOnly }; };
class QFile {
public:
    explicit QFile(const QString&) {}
    bool open(QIODevice::OpenMode) { return false; }
    qint64 size() const { return -1; }
    QByteArray readAll() { return {}; }
    QString errorString() const { return "File I/O is unsupported in the Qt-free codec; use byte buffers"; }
};
class QSaveFile : public QFile {
public:
    using QFile::QFile;
    qint64 write(const QByteArray&) { return -1; }
    bool commit() { return false; }
};
template<class T> T qFromLittleEndian(const void* input) {
    const auto* p = static_cast<const unsigned char*>(input);
    T result = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) result |= T(p[i]) << (8 * i);
    return result;
}
template<class T> void qToLittleEndian(T value, void* output) {
    auto* p = static_cast<unsigned char*>(output);
    for (std::size_t i = 0; i < sizeof(T); ++i) p[i] = static_cast<unsigned char>(value >> (8 * i));
}
QByteArray qCompress(const QByteArray& data, int level = -1);
